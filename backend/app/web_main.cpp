#include <utility>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;
using Json = nlohmann::json;

namespace {

// All sessions and their callbacks run on the single io_context thread.
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    explicit WsSession(tcp::socket socket) : ws_(std::move(socket)) {}

    void run() {
        ws_.read_message_max(64 * 1024);
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
            if (ec) return self->stop("handshake", ec);
            self->read();
        });
    }

private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer input_;
    std::deque<std::string> outbox_;
    std::size_t queued_bytes_ = 0;
    bool stopped_ = false;

    void read() {
        if (stopped_) return;
        ws_.async_read(input_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) return self->stop("read", ec);
            const bool text_message = self->ws_.got_text();
            auto text = beast::buffers_to_string(self->input_.data());
            self->input_.consume(self->input_.size());
            if (text_message) self->on_message(text);
            else self->error("text_only");
            self->read();
        });
    }

    void on_message(const std::string& text) {
        Json message;
        try {
            message = Json::parse(text, [](int depth, Json::parse_event_t, Json&) {
                if (depth > 32) throw std::invalid_argument("json_too_deep");
                return true;
            });
        } catch (const Json::exception&) {
            return error("invalid_json");
        } catch (const std::invalid_argument&) {
            return error("json_too_deep");
        }
        if (!message.is_object()) return error("invalid_json");
        const auto type = message.find("type");
        if (type == message.end() || !type->is_string()) return error("invalid_type");

        if (*type == "ping") {
            send({{"type", "pong"}});
        } else if (*type == "echo") {
            const auto payload = message.find("payload");
            if (payload == message.end() || !payload->is_string()) return error("invalid_payload");
            send({{"type", "echo"}, {"payload", *payload}});
        } else {
            error("unknown_type");
        }
    }

    void error(const char* code) { send({{"type", "error"}, {"code", code}}); }

    void send(Json message) {
        if (stopped_) return;
        auto text = message.dump();
        if (outbox_.size() >= 128 || queued_bytes_ + text.size() > 1024 * 1024) {
            std::cerr << "Disconnecting slow client: send queue limit\n";
            return stop("send", {});
        }
        const bool idle = outbox_.empty();
        queued_bytes_ += text.size();
        outbox_.push_back(std::move(text));
        if (idle) write();
    }

    void write() {
        ws_.text(true);
        // The queue owns the buffer until this write's completion handler runs.
        ws_.async_write(net::buffer(outbox_.front()),
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) return self->stop("write", ec);
                self->queued_bytes_ -= self->outbox_.front().size();
                self->outbox_.pop_front();
                if (!self->stopped_ && !self->outbox_.empty()) self->write();
            });
    }

    void stop(const char* operation, beast::error_code ec) {
        if (stopped_) return;
        stopped_ = true;
        if (ec && ec != websocket::error::closed && ec != net::error::operation_aborted) {
            std::cerr << operation << ": " << ec.message() << '\n';
        }
        beast::error_code ignored;
        beast::get_lowest_layer(ws_).socket().close(ignored);
        // Do not clear outbox_: an outstanding write may still reference it.
    }
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& ioc, std::uint16_t port)
        : acceptor_(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port}), retry_(ioc) {}

    std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    void run() { accept(); }

private:
    tcp::acceptor acceptor_;
    net::steady_timer retry_;

    void accept() {
        acceptor_.async_accept([self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
            if (ec == net::error::operation_aborted) return;
            if (ec) {
                std::cerr << "accept: " << ec.message() << '\n';
                self->retry_.expires_after(std::chrono::milliseconds(250));
                self->retry_.async_wait([self](beast::error_code retry_ec) {
                    if (!retry_ec) self->accept();
                });
                return;
            }
            std::make_shared<WsSession>(std::move(socket))->run();
            self->accept();
        });
    }
};

std::uint16_t parse_port(std::string_view text) {
    unsigned int port = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || port > 65535) {
        throw std::invalid_argument("Port must be an integer between 0 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 2) throw std::invalid_argument("Usage: gymj_server [port]");
        const auto port = argc == 2 ? parse_port(argv[1]) : std::uint16_t{8080};
        net::io_context ioc{1};
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](beast::error_code ec, int) {
            if (!ec) ioc.stop();
        });
        auto listener = std::make_shared<Listener>(ioc, port);
        listener->run();
        std::cout << "WebSocket listening on ws://127.0.0.1:" << listener->port() << std::endl;
        ioc.run();
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
