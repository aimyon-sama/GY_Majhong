#include <gymj/server/session/session_manager.hpp>

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using gymj::server::SessionManager;
using gymj::server::gateway::WsSession;
using gymj::server::gateway::protocol::ErrorCode;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

WsSession::Ptr make_connection(boost::asio::io_context& ioc) {
    // 身份管理不需要真的连接网络；关闭事件由测试显式安排，复现迟到回调。
    return std::make_shared<WsSession>(boost::asio::ip::tcp::socket(ioc),
        [](WsSession::Ptr, std::string) {}, [](WsSession::Ptr, boost::beast::error_code) {});
}

void test_connection_ownership() {
    boost::asio::io_context ioc;
    auto connection = [&] {
        // 身份管理不需要真的连接网络；关闭事件由测试显式安排，复现迟到回调。
        return std::make_shared<WsSession>(boost::asio::ip::tcp::socket(ioc),
            [](WsSession::Ptr, std::string) {}, [](WsSession::Ptr, boost::beast::error_code) {});
    };
    SessionManager manager;
    auto first = connection();
    auto second = connection();
    auto replacement = connection();
    const auto a = manager.authenticate(first, {"same name", {}});
    const auto b = manager.authenticate(second, {"same name", {}});
    require(a.session && b.session && a.session != b.session, "names must not identify sessions");
    require(a.session->token != b.session->token, "sessions need independent credentials");
    require(a.session->token.size() == 64 && std::all_of(a.session->token.begin(),
        a.session->token.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); }),
        "token must encode 256 bits as lowercase hex");
    require(!a.session->player && !a.session->room_id, "hello must not allocate a seat");
    require(manager.authenticate(first, {"another", {}}).error == ErrorCode::AlreadyAuthenticated,
        "one connection cannot create multiple identities");

    const auto resumed = manager.authenticate(replacement, {{}, a.session->token});
    require(resumed.resumed && resumed.session == a.session && resumed.previous_connection == first,
        "token must restore original identity and identify replaced connection");
    require(manager.superseded(first) && !manager.current(first), "old connection cannot act");
    require(!manager.disconnect(first), "old close callback must not disconnect new owner");
    require(manager.current(replacement) == a.session, "replacement survives late close");
    require(manager.disconnect(replacement) == a.session && a.session->connection.expired(),
        "current close must detach transport but retain identity");
    auto offline_replacement = connection();
    require(manager.authenticate(offline_replacement, {{}, a.session->token}).session == a.session,
        "offline identity can be restored");
    auto unknown = connection();
    require(manager.authenticate(unknown, {{}, std::string(64, '0')}).error == ErrorCode::SessionExpired,
        "unknown token must not create an identity");
    require(!manager.current(unknown), "failed hello must leave connection unauthenticated");
    require(manager.current(second) == b.session, "other session stays isolated");
    a.session->room_id = "100001";
    a.session->player = gymj::common::PlayerInfo{"same name", 123};
    manager.expire(a.session);
    require(!a.session->room_id && !a.session->player && a.session->connection.expired(),
        "expiry clears room, seat and transport bindings");
    require(!manager.current(offline_replacement) && !manager.disconnect(offline_replacement),
        "expired transport cannot act and its late close is harmless");
    require(manager.authenticate(unknown, {{}, a.session->token}).error == ErrorCode::SessionExpired,
        "expired token cannot recover the old game");
    require(manager.current(second) == b.session, "expiring one identity leaves others usable");
    require(manager.authenticate(unknown, {"fresh", {}}).session != nullptr,
        "expiry still permits a fresh identity for a new game");

}

void test_lobby_sessions_expire_and_stay_bounded() {
    boost::asio::io_context ioc;
    const auto epoch = SessionManager::Clock::now();
    using std::chrono::seconds;
    using std::chrono::days;

    SessionManager::Options options;
    options.lobby_ttl = seconds(30);
    SessionManager manager{options};

    auto idle_connection = make_connection(ioc);
    const auto idle = manager.authenticate(idle_connection, {"idle", {}}, epoch);
    require(idle.session != nullptr && !idle.resumed, "hello should create a lobby identity");
    require(manager.session_count() == 1 && manager.connection_count() == 1,
            "new identity should occupy one session and one connection slot");
    require(manager.disconnect(idle_connection, epoch + seconds(10)) == idle.session,
            "disconnect must keep an unseated lobby identity for reconnect");
    require(manager.connection_count() == 0, "disconnect must release the connection slot");

    // 页面刷新窗口内仍可恢复原身份。
    auto refresh_connection = make_connection(ioc);
    const auto resumed = manager.authenticate(refresh_connection, {{}, idle.session->token}, epoch + seconds(20));
    require(resumed.resumed && resumed.session == idle.session, "identity should resume inside the lobby TTL");
    require(manager.disconnect(refresh_connection, epoch + seconds(20)) == idle.session,
            "refreshed identity stays resumable after closing again");

    require(manager.sweep(epoch + seconds(49)) == 0, "unexpired lobby identity must be kept");
    const auto expired = manager.sweep(epoch + seconds(50));
    require(expired == 1, "identity idle past the lobby TTL should be swept");
    require(manager.session_count() == 0, "sweep should release the token");
    require(manager.authenticate(make_connection(ioc), {{}, idle.session->token}, epoch + seconds(50)).error
                == ErrorCode::SessionExpired,
            "swept token must be reported as expired");

    // 已入座身份遵守房间恢复约定，不按大厅 TTL 回收。
    auto seated_connection = make_connection(ioc);
    const auto seated = manager.authenticate(seated_connection, {"seated", {}}, epoch);
    require(seated.session != nullptr, "seated fixture should authenticate");
    seated.session->player = gymj::common::PlayerInfo{"seated", 99};
    seated.session->room_id = "100001";
    manager.disconnect(seated_connection, epoch + seconds(1));
    require(manager.sweep(epoch + days(1)) == 0, "seated identities follow the room lifecycle, not the lobby TTL");
    require(manager.authenticate(make_connection(ioc), {{}, seated.session->token}).session == seated.session,
            "seated identity should still resume");

    // 在线但未入座的身份不会被回收。
    SessionManager online_manager{SessionManager::Options{seconds(1), 8, 8}};
    auto online_connection = make_connection(ioc);
    require(online_manager.authenticate(online_connection, {"online", {}}, epoch).session != nullptr,
            "online lobby identity should authenticate");
    require(online_manager.sweep(epoch + days(1)) == 0, "connected lobby identity must not be swept");

    // 反复创建并断开大厅连接后，会话数量会回落到 0。
    SessionManager bulk_manager{SessionManager::Options{seconds(30), 512, 64}};
    for (int i = 0; i < 64; ++i) {
        auto connection = make_connection(ioc);
        const auto session = bulk_manager.authenticate(connection, {"bulk", {}}, epoch);
        require(session.session != nullptr, "bulk lobby identity should be created");
        bulk_manager.disconnect(connection, epoch);
    }
    require(bulk_manager.session_count() == 64, "disconnected identities remain resumable inside the TTL");
    require(bulk_manager.sweep(epoch + seconds(30)) == 64, "all idle lobby identities should be swept");
    require(bulk_manager.session_count() == 0 && bulk_manager.connection_count() == 0,
            "sweeping idle lobby identities must release every entry");

    // 会话上限：达到上限后拒绝新身份，过期身份被回收后恢复可用。
    SessionManager::Options limited_options{seconds(30), 2, 8};
    SessionManager limited{limited_options};
    auto first_connection = make_connection(ioc);
    auto second_connection = make_connection(ioc);
    require(limited.authenticate(first_connection, {"a", {}}, epoch).session != nullptr,
            "first identity should fit the session cap");
    require(limited.authenticate(second_connection, {"b", {}}, epoch).session != nullptr,
            "second identity should fit the session cap");
    require(limited.authenticate(make_connection(ioc), {"c", {}}, epoch).error == ErrorCode::RateLimited,
            "session cap must reject new lobby identities");
    limited.disconnect(first_connection, epoch + seconds(1));
    require(limited.sweep(epoch + seconds(31)) == 1, "expired identity should free a session slot");
    require(limited.authenticate(make_connection(ioc), {"c", {}}, epoch + seconds(31)).session != nullptr,
            "session slot should be reusable after the sweep");

    // 连接上限：已认证连接达到上限后拒绝，关闭旧连接后恢复。
    SessionManager socket_limited{SessionManager::Options{seconds(30), 100, 2}};
    auto socket_a = make_connection(ioc);
    auto socket_b = make_connection(ioc);
    require(socket_limited.authenticate(socket_a, {"x", {}}, epoch).session != nullptr, "first socket should fit");
    require(socket_limited.authenticate(socket_b, {"y", {}}, epoch).session != nullptr, "second socket should fit");
    require(socket_limited.authenticate(make_connection(ioc), {"z", {}}, epoch).error == ErrorCode::RateLimited,
            "connection cap must reject extra authenticated sockets");
    socket_limited.disconnect(socket_a, epoch);
    require(socket_limited.authenticate(make_connection(ioc), {"z", {}}, epoch).session != nullptr,
            "closing a socket should release its connection slot");
}
void test_expired_token_is_rejected_without_waiting_for_sweep() {
    boost::asio::io_context ioc;
    using std::chrono::milliseconds;
    using std::chrono::seconds;
    const auto epoch = SessionManager::Clock::now();
    SessionManager::Options options;
    options.lobby_ttl = seconds(30);
    SessionManager manager{options};

    auto create_connection = make_connection(ioc);
    const auto created = manager.authenticate(create_connection, {"idle", {}}, epoch);
    require(created.session != nullptr, "fixture should create a lobby identity");
    const auto token = created.session->token;
    manager.disconnect(create_connection, epoch);

    // TTL 前：可以恢复，并从恢复时刻重新计时。
    const auto resume_at = epoch + seconds(30) - milliseconds(1);
    auto resume_connection = make_connection(ioc);
    const auto resumed = manager.authenticate(resume_connection, {{}, token}, resume_at);
    require(resumed.session == created.session, "identity should resume just before the lobby TTL");
    manager.disconnect(resume_connection, resume_at);

    // 恰好到期：认证入口直接撤销，不依赖任何 sweep。
    const auto exactly = resume_at + seconds(30);
    require(manager.authenticate(make_connection(ioc), {{}, token}, exactly).error == ErrorCode::SessionExpired,
            "token must expire exactly at the lobby TTL, before any sweep");
    require(manager.session_count() == 0, "failed resume must revoke the expired token");
    require(manager.connection_count() == 0, "failed resume must leave no connection binding");
    require(manager.authenticate(make_connection(ioc), {{}, token}, exactly + seconds(1)).error
                == ErrorCode::SessionExpired,
            "expired token must stay invalid afterwards");

    // 在线接管与已入座身份都不适用大厅 TTL。
    auto online_connection = make_connection(ioc);
    const auto online = manager.authenticate(online_connection, {"online", {}}, epoch);
    require(online.session != nullptr, "online fixture should authenticate");
    const auto takeover = manager.authenticate(make_connection(ioc), {{}, online.session->token}, epoch + seconds(120));
    require(takeover.session == online.session && takeover.resumed,
            "a live lobby takeover must not be gated by the lobby TTL");

    auto seated_connection = make_connection(ioc);
    const auto seated = manager.authenticate(seated_connection, {"seated", {}}, epoch);
    require(seated.session != nullptr, "seated fixture should authenticate");
    seated.session->player = gymj::common::PlayerInfo{"seated", 5};
    seated.session->room_id = "100001";
    manager.disconnect(seated_connection, epoch);
    const auto seated_resume =
        manager.authenticate(make_connection(ioc), {{}, seated.session->token}, epoch + seconds(3600));
    require(seated_resume.session == seated.session && seated_resume.resumed,
            "seated identity must not be gated by the lobby TTL");
}
} // namespace

int main() {
    try {
        test_connection_ownership();
        test_lobby_sessions_expire_and_stay_bounded();
        test_expired_token_is_rejected_without_waiting_for_sweep();
        std::cout << "session manager tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "session manager test failed: " << error.what() << '\n';
        return 1;
    }
}
