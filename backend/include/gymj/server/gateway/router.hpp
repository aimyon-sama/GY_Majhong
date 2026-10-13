#ifndef GYMJ_SERVER_GATEWAY_ROUTER_HPP
#define GYMJ_SERVER_GATEWAY_ROUTER_HPP

#include <gymj/server/lobby/room_manager.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>

namespace gymj::server::gateway {

// 与 WsSession、Table 一样，所有入口都在同一个 io_context 线程串行执行。
// 解析器检查消息形状，Router 检查身份和房间，Table 检查麻将规则。
class Router : public std::enable_shared_from_this<Router> {
public:
    explicit Router(boost::asio::io_context& ioc, common::RuleConfig rules = {},
                    common::TableOptions options = {});
    // 在 shared_ptr 建立后启动；停止后取消待处理的计时回调。
    void start();
    void stop();
    void on_message(WsSession::Ptr connection, std::string text);
    void on_close(WsSession::Ptr connection);

private:
    void schedule_tick();
    using Session = SessionManager::Session;
    void hello(const WsSession::Ptr& connection, const protocol::HelloRequest& request);
    void handle(const WsSession::Ptr&, const Session&, const protocol::JoinRoomRequest&);
    void handle(const WsSession::Ptr&, const Session&, const protocol::ReadyRequest&);
    void handle(const WsSession::Ptr&, const Session&, const protocol::SnapshotRequest&);
    void handle(const WsSession::Ptr&, const Session&, const protocol::ActionRequest&);
    Room* seated_room(const WsSession::Ptr&, const Session&, std::string_view room_id,
                      std::optional<std::string> tag, std::optional<std::uint64_t> request_id = {});
    void deliver(Room& room, const common::TableUpdate& update);
    void notice(Room& room, const protocol::ServerNotice& value);

    SessionManager sessions_;
    RoomManager rooms_;
    boost::asio::steady_timer timer_;
    bool running_ = false;
};
} // namespace gymj::server::gateway
#endif
