#ifndef GYMJ_SERVER_GATEWAY_LISTENER_HPP
#define GYMJ_SERVER_GATEWAY_LISTENER_HPP

#include <gymj/server/gateway/ws_session.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cstdint>
#include <memory>

namespace gymj::server::gateway {

// Accepts TCP connections and starts one WebSocket session per connection.
// Create with std::make_shared, then call run().
class Listener final : public std::enable_shared_from_this<Listener> {
public:
    Listener(boost::asio::io_context& ioc, std::uint16_t port,
             WsSession::MessageHandler on_message,
             WsSession::CloseHandler on_close = {});

    std::uint16_t port() const;
    void run();

private:
    void accept();

    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::steady_timer retry_;
    WsSession::MessageHandler on_message_;
    WsSession::CloseHandler on_close_;
};

} // namespace gymj::server::gateway

#endif
