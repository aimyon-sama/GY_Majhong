#include <gymj/server/gateway/listener.hpp>

#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/beast/core/error.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace gymj::server::gateway {
namespace {
namespace net = boost::asio;
namespace beast = boost::beast;
using tcp = net::ip::tcp;
} // namespace

Listener::Listener(net::io_context& ioc, std::uint16_t port,
                   WsSession::MessageHandler on_message,
                   WsSession::CloseHandler on_close)
    : acceptor_(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port}),
      retry_(ioc),
      on_message_(std::move(on_message)),
      on_close_(std::move(on_close)) {
    if (!on_message_) {
        throw std::invalid_argument("Listener requires a message handler");
    }
    if (!on_close_) {
        on_close_ = [](WsSession::Ptr, beast::error_code) {};
    }
}

std::uint16_t Listener::port() const {
    return acceptor_.local_endpoint().port();
}

void Listener::run() {
    accept();
}

void Listener::accept() {
    acceptor_.async_accept([self = shared_from_this()](beast::error_code error,
                                                       tcp::socket socket) {
        if (error == net::error::operation_aborted) return;
        if (error) {
            std::cerr << "accept: " << error.message() << '\n';
            self->retry_.expires_after(std::chrono::milliseconds(250));
            self->retry_.async_wait([self](beast::error_code retry_error) {
                if (!retry_error) self->accept();
            });
            return;
        }

        std::make_shared<WsSession>(std::move(socket), self->on_message_, self->on_close_)->run();
        self->accept();
    });
}

} // namespace gymj::server::gateway
