#ifndef GYMJ_SERVER_GATEWAY_WS_SESSION_HPP
#define GYMJ_SERVER_GATEWAY_WS_SESSION_HPP

#include <utility>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace gymj::server::gateway {

// One connection. Create with std::make_shared, then call run().
// All public methods and callbacks must run on the same io_context thread.
class WsSession final : public std::enable_shared_from_this<WsSession> {
public:
    using Ptr = std::shared_ptr<WsSession>;
    using MessageHandler = std::function<void(Ptr, std::string)>;
    using CloseHandler = std::function<void(Ptr, boost::beast::error_code)>;

    WsSession(boost::asio::ip::tcp::socket socket,
                     MessageHandler on_message, CloseHandler on_close);

    void run();
    void send_text(std::string message);
    void close();

private:
    void read();
    void write();
    void stop(const char* operation, boost::beast::error_code error);

    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
    boost::beast::flat_buffer input_;
    std::deque<std::string> outbox_;
    MessageHandler on_message_;
    CloseHandler on_close_;
    std::size_t queued_bytes_ = 0;
    bool started_ = false;
    bool accepted_ = false;
    bool writing_ = false;
    bool closing_ = false;
    bool stopped_ = false;
};

} // namespace gymj::server::gateway

#endif
