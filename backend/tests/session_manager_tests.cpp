#include <gymj/server/session/session_manager.hpp>

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using gymj::server::SessionManager;
using gymj::server::gateway::WsSession;
using gymj::server::gateway::protocol::ErrorCode;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
}
} // namespace

int main() {
    try {
        test_connection_ownership();
        std::cout << "session manager tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "session manager test failed: " << error.what() << '\n';
        return 1;
    }
}
