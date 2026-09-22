#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <gymj/server/gateway/ws_session.hpp>
#include <nlohmann/json.hpp>

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace net = boost::asio;
namespace beast = boost::beast;
using tcp = net::ip::tcp;
using Json = nlohmann::json;
using WsSession = gymj::server::gateway::WsSession;

namespace {

void send(const WsSession::Ptr& session, Json message) {
    session->send_text(message.dump());
}

void error(const WsSession::Ptr& session, const char* code) {
    send(session, {{"type", "error"}, {"code", code}});
}

void on_message(WsSession::Ptr session, std::string text) {
    Json message;
    try {
        message = Json::parse(text, [](int depth, Json::parse_event_t, Json&) {
            if (depth > 32) throw std::invalid_argument("json_too_deep");
            return true;
        });
    } catch (const Json::exception&) {
        return error(session, "invalid_json");
    } catch (const std::invalid_argument&) {
        return error(session, "json_too_deep");
    }
    if (!message.is_object()) return error(session, "invalid_json");
    const auto type = message.find("type");
    if (type == message.end() || !type->is_string()) return error(session, "invalid_type");

    if (*type == "ping") {
        send(session, {{"type", "pong"}});
    } else if (*type == "echo") {
        const auto payload = message.find("payload");
        if (payload == message.end() || !payload->is_string()) return error(session, "invalid_payload");
        send(session, {{"type", "echo"}, {"payload", *payload}});
    } else {
        error(session, "unknown_type");
    }
}

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
            std::make_shared<WsSession>(std::move(socket), on_message,
                [](WsSession::Ptr, beast::error_code) {})->run();
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
