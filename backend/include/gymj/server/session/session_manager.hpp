#ifndef GYMJ_SERVER_SESSION_MANAGER_HPP
#define GYMJ_SERVER_SESSION_MANAGER_HPP

#include <gymj/server/gateway/protocol.hpp>
#include <gymj/server/gateway/ws_session.hpp>

#include <chrono>
#include <cstddef>
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
    // 最近一次认证或断线的时间，只用于回收未入座的大厅身份。
    common::TableClock::time_point last_seen = common::TableClock::now();
};

class SessionManager {
public:
    using Session = std::shared_ptr<PlayerSession>;
    using Connection = gateway::WsSession::Ptr;
    using Clock = common::TableClock;

    struct Options {
        // 未入座且已断线的身份保留时长：足够覆盖页面刷新，又不无限累积。
        std::chrono::seconds lobby_ttl{120};
        // 进程内临时身份与已认证连接的数量上限，超限返回 rate_limited。
        std::size_t max_sessions = 4096;
        std::size_t max_connections = 2048;
    };

    struct Authentication {
        Session session;
        Connection previous_connection;
        bool resumed = false;
        std::optional<gateway::protocol::ErrorCode> error;
    };

    SessionManager() = default;
    explicit SessionManager(Options options);

    // 输入已由协议层校验；这里只负责身份与连接的绑定，不分配牌桌座位。
    Authentication authenticate(const Connection& connection,
                                const gateway::protocol::HelloRequest& request,
                                Clock::time_point now = Clock::now());
    Session current(const Connection& connection) const;
    bool superseded(const Connection& connection) const;
    // 仅当前连接断开时返回身份，旧连接的迟到关闭回调不得把新连接踢下线。
    Session disconnect(const Connection& connection, Clock::time_point now = Clock::now());
    void expire(const Session& session); // 撤销令牌及该身份的全部连接、座位绑定。
    // 回收未入座、已断线且超过大厅 TTL 的身份，返回本次回收数量。
    std::size_t sweep(Clock::time_point now = Clock::now());

    std::size_t session_count() const noexcept { return tokens_.size(); }
    std::size_t connection_count() const noexcept { return connections_.size(); }
    const Options& options() const noexcept { return options_; }

private:
    // v1 身份仅保存在进程内，重启失效；仍有人在桌时保留身份与座位，全员离开后失效。
    Options options_{};
    std::unordered_map<std::string, Session> tokens_;
    std::unordered_map<const gateway::WsSession*, Session> connections_;
};

} // namespace gymj::server
#endif
