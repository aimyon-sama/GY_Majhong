#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <gymj/server/gateway/listener.hpp>
#include <gymj/server/gateway/router.hpp>
#include <gymj/server/gateway/ws_session.hpp>

#include <charconv>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace net = boost::asio;
namespace beast = boost::beast;
using Listener = gymj::server::gateway::Listener;
using Router = gymj::server::gateway::Router;
using WsSession = gymj::server::gateway::WsSession;

namespace {

std::uint16_t parse_port(std::string_view text) {
    unsigned int port = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || port > 65535) {
        throw std::invalid_argument("Port must be an integer between 0 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

template <typename Integer>
Integer parse_number(std::string_view text, const char* message) {
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument(message);
    }
    return value;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        std::uint16_t port = 8080;
        gymj::common::TableOptions options;
        int index = 1;
        if (index < argc && !std::string_view(argv[index]).starts_with("--")) port = parse_port(argv[index++]);
        for (; index < argc; ++index) {
            const std::string_view flag = argv[index];
            if (index + 1 >= argc) throw std::invalid_argument("Missing option value");
            const std::string_view value = argv[++index];
            if (flag == "--action-timeout-ms") {
                options.action_timeout_ms = parse_number<int>(value, "Invalid action timeout");
                if (options.action_timeout_ms <= 0) throw std::invalid_argument("Action timeout must be positive");
            } else if (flag == "--seed") {
                options.seed = parse_number<std::uint64_t>(value, "Invalid seed");
            } else {
                throw std::invalid_argument("Usage: gymj_server [port] [--action-timeout-ms N] [--seed N]");
            }
        }
        net::io_context ioc{1};
        auto router = std::make_shared<Router>(ioc, gymj::common::RuleConfig{}, options);
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc, router](beast::error_code ec, int) {
            if (!ec) {
                router->stop();
                ioc.stop();
            }
        });
        // main 只装配网络和业务组件；所有牌桌操作在这个单线程事件循环内串行执行。
        auto listener = std::make_shared<Listener>(ioc, port,
            [router](WsSession::Ptr connection, std::string text) {
                router->on_message(std::move(connection), std::move(text));
            },
            [router, &ioc](WsSession::Ptr connection, beast::error_code) {
                // 队列溢出可能在 send_text 内同步触发关闭。推迟断线处理，
                // 避免重入 Table 并把较新的断线快照插到本次出牌快照之前。
                net::post(ioc, [router, connection = std::move(connection)] {
                    router->on_close(connection);
                });
            });
        listener->run();
        router->start();
        std::cout << "WebSocket listening on ws://127.0.0.1:" << listener->port() << std::endl;
        ioc.run();
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
