#include <gymj/server/session/session_manager.hpp>

#include <array>
#include <fstream>
#include <stdexcept>

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
} // namespace

SessionManager::Authentication SessionManager::authenticate(
    const Connection& connection, const gateway::protocol::HelloRequest& request) {
    using gateway::protocol::ErrorCode;
    Authentication result;
    if (connections_.contains(connection.get())) {
        result.error = ErrorCode::AlreadyAuthenticated;
        return result;
    }
    if (request.session_token) {
        const auto found = tokens_.find(*request.session_token);
        if (found == tokens_.end()) {
            result.error = ErrorCode::SessionExpired;
            return result;
        }
        result.session = found->second;
        result.resumed = true;
        result.previous_connection = result.session->connection.lock();
    } else {
        auto session = std::make_shared<PlayerSession>();
        session->nickname = request.nickname.value();
        do { session->token = make_token(); } while (tokens_.contains(session->token));
        tokens_.emplace(session->token, session);
        result.session = std::move(session);
    }
    connections_.emplace(connection.get(), result.session);
    // 先切换当前连接，再由 Router 关闭旧连接，避免旧回调清空新绑定。
    result.session->connection = connection;
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

SessionManager::Session SessionManager::disconnect(const Connection& connection) {
    auto session = current(connection);
    connections_.erase(connection.get());
    if (session) session->connection.reset();
    return session;
}
void SessionManager::expire(const Session& session) {
    tokens_.erase(session->token);
    for (auto it = connections_.begin(); it != connections_.end();) {
        if (it->second == session) it = connections_.erase(it);
        else ++it;
    }
    session->connection.reset();
    session->room_id.reset();
    session->player.reset();
}
} // namespace gymj::server
