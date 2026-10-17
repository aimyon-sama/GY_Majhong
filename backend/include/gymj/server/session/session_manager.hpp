#ifndef GYMJ_SERVER_SESSION_MANAGER_HPP
#define GYMJ_SERVER_SESSION_MANAGER_HPP

#include <gymj/server/gateway/protocol.hpp>
#include <gymj/server/gateway/ws_session.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace gymj::server {

// 连接、登录身份和牌桌身份有不同生命周期：断开 socket 不能丢掉原座位。
struct PlayerSession {
    std::string token;
    std::string nickname;
    std::optional<std::string> room_id;
    std::optional<common::PlayerInfo> player;
    std::weak_ptr<gateway::WsSession> connection;
};

class SessionManager {
public:
    using Session = std::shared_ptr<PlayerSession>;
    using Connection = gateway::WsSession::Ptr;

    struct Authentication {
        Session session;
        Connection previous_connection;
        bool resumed = false;
        std::optional<gateway::protocol::ErrorCode> error;
    };

    // 输入已由协议层校验；这里只负责身份与连接的绑定，不分配牌桌座位。
    Authentication authenticate(const Connection& connection,
                                const gateway::protocol::HelloRequest& request);
    Session current(const Connection& connection) const;
    bool superseded(const Connection& connection) const;
    // 仅当前连接断开时返回身份，旧连接的迟到关闭回调不得把新连接踢下线。
    Session disconnect(const Connection& connection);

private:
    // v1 身份仅保存在进程内，重启失效；主动离桌保留身份，会话过期策略另行实现。
    std::unordered_map<std::string, Session> tokens_;
    std::unordered_map<const gateway::WsSession*, Session> connections_;
};

} // namespace gymj::server
#endif
