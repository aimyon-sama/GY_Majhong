#include <gymj/server/gateway/ws_session.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace gymj::server::gateway {
namespace {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;

constexpr std::size_t max_message_bytes = 64 * 1024;
constexpr std::size_t max_queued_messages = 128;
constexpr std::size_t max_queued_bytes = 1024 * 1024;
} // namespace

WsSession::WsSession(net::ip::tcp::socket socket,
                                   MessageHandler on_message, CloseHandler on_close)
    : ws_(std::move(socket)),
      on_message_(std::move(on_message)),
      on_close_(std::move(on_close)) {
    if (!on_message_ || !on_close_) {
        throw std::invalid_argument("WsSessionExample requires both callbacks");
    }
}

void WsSession::run() {
    if (started_ || stopped_) return;
    started_ = true;
    ws_.read_message_max(max_message_bytes);
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.async_accept([self = shared_from_this()](beast::error_code error) {
        if (error) return self->stop("handshake", error);
        if (self->stopped_) return;
        self->accepted_ = true;
        if (!self->outbox_.empty()) self->write();
        self->read();
    });
}

void WsSession::read() {
    if (closing_ || stopped_) return;
    ws_.async_read(input_, [self = shared_from_this()](beast::error_code error, std::size_t) {
        if (error) {
            if (self->closing_ && error == net::error::operation_aborted) return;
            return self->stop("read", error);
        }

        const bool is_text = self->ws_.got_text();
        auto message = beast::buffers_to_string(self->input_.data());
        self->input_.consume(self->input_.size());

        if (is_text) {
            try {
                self->on_message_(self, std::move(message));
            } catch (const std::exception& e) {
                std::cerr << "message handler: " << e.what() << '\n';
                return self->stop("message handler", {});
            } catch (...) {
                std::cerr << "message handler: unknown exception\n";
                return self->stop("message handler", {});
            }
        } else {
            // Preserve the current test endpoint's response to binary messages.
            self->send_text(R"({"type":"error","code":"text_only"})");
        }

        if (!self->closing_ && !self->stopped_) self->read();
    });
}

void WsSession::send_text(std::string message) {
    if (closing_ || stopped_) return;
    if (outbox_.size() >= max_queued_messages || message.size() > max_queued_bytes
        || queued_bytes_ > max_queued_bytes - message.size()) {
        return stop("send queue limit", net::error::no_buffer_space);
    }

    queued_bytes_ += message.size();
    outbox_.push_back(std::move(message));
    if (accepted_ && !writing_) write();
}

void WsSession::write() {
    if (closing_ || stopped_ || writing_ || outbox_.empty()) return;
    writing_ = true;
    ws_.text(true);
    // Keep the front string alive until the completion handler runs.
    ws_.async_write(net::buffer(outbox_.front()),
        [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (error) {
                if (self->closing_ && error == net::error::operation_aborted) return;
                return self->stop("write", error);
            }
            self->queued_bytes_ -= self->outbox_.front().size();
            self->outbox_.pop_front();
            self->writing_ = false;
            if (!self->closing_ && !self->stopped_ && !self->outbox_.empty()) self->write();
        });
}

void WsSession::close() {
    if (closing_ || stopped_) return;
    closing_ = true;
    if (!accepted_) return stop("close", {});

    ws_.async_close(websocket::close_code::normal,
        [self = shared_from_this()](beast::error_code error) {
            self->stop("close", error);
        });
}

void WsSession::stop(const char* operation, beast::error_code error) {
    if (stopped_) return;
    stopped_ = true;
    if (error && error != websocket::error::closed
        && error != net::error::operation_aborted) {
        std::cerr << operation << ": " << error.message() << '\n';
    }

    beast::error_code ignored;
    beast::get_lowest_layer(ws_).socket().close(ignored);
    // An in-flight async_write may still refer to the front of outbox_.
    try {
        on_close_(shared_from_this(), error);
    } catch (const std::exception& e) {
        std::cerr << "close handler: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "close handler: unknown exception\n";
    }
}

} // namespace gymj::server::gateway
