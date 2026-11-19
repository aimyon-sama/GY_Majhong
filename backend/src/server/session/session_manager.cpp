#include <gymj/server/session/session_manager.hpp>

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace gymj::server {
namespace {
std::string make_token() {
    // 使用操作系统的 256 位安全随机数；洗牌用的 mt19937 和递增玩家 ID
    // 都可预测，不能用作允许读取私有手牌的身份凭据。
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("secure random source unavailable");
    }
#else
    std::ifstream source("/dev/urandom", std::ios::binary);
    if (!source.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
        throw std::runtime_error("secure random source unavailable");
    }
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (const auto byte : bytes) {
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 15]);
    }
    return token;
}

// 未入座、已断线的大厅身份才有回收意义；在座身份由房间生命周期管理。
bool sweepable(const PlayerSession& session) {
    return !session.player && !session.room_id && session.connection.expired();
}
} // namespace

SessionManager::SessionManager(Options options) : options_(options) {
    if (options_.lobby_ttl <= std::chrono::seconds(0)) {
        throw std::invalid_argument("lobby ttl must be positive");
    }
    if (options_.max_sessions == 0 || options_.max_connections == 0) {
        throw std::invalid_argument("session limits must be positive");
    }
}

SessionManager::Authentication SessionManager::authenticate(
    const Connection& connection, const gateway::protocol::HelloRequest& request,
    Clock::time_point now) {
    using gateway::protocol::ErrorCode;
    Authentication result;
    if (connections_.contains(connection.get())) {
        result.error = ErrorCode::AlreadyAuthenticated;
        return result;
    }
    // 连接上限先于令牌查找：被接管的旧连接在关闭前仍占用一个映射项。
    if (connections_.size() >= options_.max_connections) {
        result.error = ErrorCode::RateLimited;
        return result;
    }
    if (request.session_token) {
        const auto found = tokens_.find(*request.session_token);
        if (found == tokens_.end()) {
            result.error = ErrorCode::SessionExpired;
            return result;
        }
        // 复制一份 shared_ptr：expire 会删除令牌表中的这个条目。
        const Session session = found->second;
        // 定期清理最多晚一个扫描周期；认证入口必须自己拒绝过期身份，
        // 否则过期令牌会在两次扫描之间重新生效，并顺带刷新 last_seen。
        if (sweepable(*session) && now - session->last_seen >= options_.lobby_ttl) {
            expire(session);
            result.error = ErrorCode::SessionExpired;
            return result;
        }
        result.session = session;
        result.resumed = true;
        result.previous_connection = result.session->connection.lock();
    } else {
        // 回收过期大厅身份后再判断上限，避免正常玩家被遗留身份挤掉。
        if (tokens_.size() >= options_.max_sessions) {
            sweep(now);
            if (tokens_.size() >= options_.max_sessions) {
                result.error = ErrorCode::RateLimited;
                return result;
            }
        }
        auto session = std::make_shared<PlayerSession>();
        session->nickname = request.nickname.value();
        do { session->token = make_token(); } while (tokens_.contains(session->token));
        tokens_.emplace(session->token, session);
        result.session = std::move(session);
    }
    connections_.emplace(connection.get(), result.session);
    // 先切换当前连接，再由 Router 关闭旧连接，避免旧回调清空新绑定。
    result.session->connection = connection;
    result.session->last_seen = now;
    return result;
}

SessionManager::Session SessionManager::current(const Connection& connection) const {
    const auto found = connections_.find(connection.get());
    if (found == connections_.end() || found->second->connection.lock() != connection) return {};
    return found->second;
}

bool SessionManager::superseded(const Connection& connection) const {
    return connections_.contains(connection.get()) && !current(connection);
}

SessionManager::Session SessionManager::disconnect(const Connection& connection,
                                                   Clock::time_point now) {
    auto session = current(connection);
    connections_.erase(connection.get());
    if (session) {
        session->connection.reset();
        session->last_seen = now;
    }
    return session;
}

void SessionManager::expire(const Session& session) {
    // 先复制令牌键：删除的正是持有该字符串的节点。
    const auto token = session->token;
    tokens_.erase(token);
    for (auto it = connections_.begin(); it != connections_.end();) {
        if (it->second == session) it = connections_.erase(it);
        else ++it;
    }
    session->connection.reset();
    session->room_id.reset();
    session->player.reset();
}

std::size_t SessionManager::sweep(Clock::time_point now) {
    // 先收集再删除：expire 会同时修改令牌表和连接表。
    std::vector<Session> expired;
    for (const auto& entry : tokens_) {
        const auto& session = entry.second;
        if (!sweepable(*session)) continue;
        if (now - session->last_seen < options_.lobby_ttl) continue;
        expired.push_back(session);
    }
    for (const auto& session : expired) expire(session);
    return expired.size();
}
} // namespace gymj::server
