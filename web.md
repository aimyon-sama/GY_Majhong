# C++ 麻将 Web 网络部分：分步骤实现教程

本文默认你熟悉 TCP、HTTP、WebSocket 和网络服务设计，重点讲解 Boost.Asio、Boost.Beast、nlohmann/json 的 C++ 用法，以及它们与本仓库麻将牌桌的接入方式。

这是一份实现教程，不代表网络模块已经完成。第 1～4 步给出可运行的最小程序；第 5 步起逐渐拆分模块、接入现有接口，并标明需要自己实现的函数。建议每完成一步就验收、提交一次，再继续下一步。

验证范围：C++ 收发示例此前通过 MSVC 19.42、Boost 1.90 编译和 WebSocket 收发测试。本次调整了依赖下载与 CMake 接入步骤，不沿用旧版的依赖路径；文中的下载命令需要你在实现时执行，不能将此前测试视为新版依赖安装流程已完成端到端验证。后续麻将网络接入和 200 人压测仍按各步骤验收。

## 0. 先确定范围和路线

本项目采用以下路线：

- 浏览器：HTML + JavaScript，使用浏览器内置 `WebSocket`。
- C++：Boost.Asio 处理异步连接和定时器，Boost.Beast 处理 WebSocket 协议。
- 消息：JSON，使用重新下载的 `nlohmann/json`，网络和牌谱模块共用同一份头文件。
- 第三方库：固定版本，新下载后统一存放在 `backend/include/third_party/`，不使用系统安装或旧构建缓存里的库。
- 第一版运行模型：一个进程、一个运行 `io_context.run()` 的线程，所有牌桌操作串行执行。
- 玩家和房间：内存保存；牌谱和小分：文件导出。

200 人约为 50 桌，可以从这个模型开始验证。实际容量要通过活跃牌局压测确定，不能仅凭连接数判断。当前 `Table` 写牌谱会同步访问磁盘，这可能成为事件循环的延迟来源。

```text
浏览器页面
   |  JSON / WebSocket
   v
Listener：接受 TCP 连接
   v
WsSession：握手、读消息、排队写消息
   v
Router：检查消息结构和会话身份
   v
RoomManager：找到这个玩家所属的牌桌
   v
gymj::room::Table：判定动作、推进牌局、记录牌谱
   |
   +--> 请求回执：只发给发起人
   +--> Delivery：按 recipient_id 发给对应玩家
```

`WsSession` 只管理连接；独立的 `PlayerSession` 保存可跨连接恢复的身份与座位绑定。

### 本仓库已经有的接口

先阅读这些文件，不需要重新实现规则和牌桌状态机：

| 文件 | 网络层要使用的内容 |
| --- | --- |
| [table.hpp](backend/include/gymj/core/room/table.hpp) | `join / ready / start / submit / disconnect / tick / snapshot` |
| [table_state.hpp](backend/include/gymj/common/schema/table_state.hpp) | `Command / PlayerView / Delivery / TableUpdate` |
| [player_info.hpp](backend/include/gymj/common/player/player_info.hpp) | 服务端身份 `PlayerInfo` |
| [player_action.hpp](backend/include/gymj/common/player/player_action.hpp) | 当前支持的动作枚举 |
| [replay.hpp](backend/include/gymj/storage/replay/replay.hpp) | `Replay::path()` 和存储错误 |

特别注意现有行为：

1. 首次入座向 `Table::join` 传 `id = 0`，成功后从 `assigned_player` 取得服务端分配的身份。
2. `ready()` 只设置准备状态，不会自动 `start()`。
3. `submit()` 已处理同一小局内请求去重；不要在网络层再执行一次相同动作。
4. `disconnect()` 保留座位；目前没有 `leave()` 或换座接口。
5. `TableUpdate.deliveries` 已经按玩家过滤手牌和事件，不能改成广播原始 `RoundState`。
6. 一次调用可以产生多个 `Delivery`，同一个接收人可能出现多次；应按返回顺序发送。

## 1. 下载依赖并配置构建

**本步目标：网络示例能与现有后端使用同一套 CMake 构建。**

编译工具使用 CMake 3.24 或更高版本、支持 C++20 的编译器。下面给出 Windows PowerShell、VS 2022 和系统 `tar.exe` 的命令；编译器及 Windows SDK 可以使用已安装的版本，第三方库则按本节重新下载。

### 1.1 固定版本与目录

采用 Boost 1.90.0 和 nlohmann/json 3.12.0。Boost.Asio、Boost.Beast 及其 Boost 依赖一起来自完整 Boost 发行包，不单独下载 standalone Asio，也不只复制 `boost/asio` 子目录。

本教程使用回调式 API、由反向代理终止 TLS，因此这些 Boost 组件按头文件方式接入即可，不需要编译整个 Boost 或安装 OpenSSL。Windows SDK 的 `ws2_32`、`mswsock` 是系统库，正常链接。参见 [Beast 的依赖说明](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)。

下载完成后的目录如下。`third_party` 位于你指定的 `backend/include` 内，版本目录将外部代码与 `gymj` 头文件分开。

```text
backend/include/
  gymj/                         现有项目头文件
  third_party/
    CMakeLists.txt              下一节创建
    boost_1_90_0/
      boost/
        asio.hpp
        beast/
        ...                    保留完整 boost 头文件目录
      LICENSE_1_0.txt
    nlohmann_json_3_12_0/
      include/nlohmann/json.hpp
      LICENSE.MIT
```

### 1.2 重新下载并校验

从仓库根目录运行下面的 PowerShell。每次创建新的下载暂存目录，不读取本机现有 Boost、JSON、包管理器目录或旧构建产物。若目标版本目录已经存在，命令会停止，避免覆盖；确认是否需要替换后再自行处理已有目录。

Boost 压缩包约 211 MB，需要预留下载和解压空间。这里只解出完整 `boost/` 头文件树与许可证，不解出文档和示例。SHA-256 来自 [Boost 1.90.0 官方校验信息](https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.gz.json) 与 [JSON 3.12.0 发布说明](https://github.com/nlohmann/json/releases/tag/v3.12.0)。

```powershell
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath './backend/CMakeLists.txt')) {
    throw 'Run this from the repository root.'
}
Get-Command tar.exe -ErrorAction Stop | Out-Null
$repoRoot = (Get-Location).Path
$depsRoot = Join-Path $repoRoot 'backend/include/third_party'
$boostDir = Join-Path $depsRoot 'boost_1_90_0'
$jsonDir = Join-Path $depsRoot 'nlohmann_json_3_12_0'
if ((Test-Path -LiteralPath $boostDir) -or (Test-Path -LiteralPath $jsonDir)) {
    throw 'Dependency directory already exists; review it before replacing it.'
}
$downloadDir = Join-Path $depsRoot ('.download-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $downloadDir -Force | Out-Null

$boostArchive = Join-Path $downloadDir 'boost_1_90_0.tar.gz'
Invoke-WebRequest -UseBasicParsing `
    -Uri 'https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.gz' `
    -OutFile $boostArchive
$boostHash = (Get-FileHash -LiteralPath $boostArchive -Algorithm SHA256).Hash
if ($boostHash.ToLowerInvariant() -ne '5e93d582aff26868d581a52ae78c7d8edf3f3064742c6e77901a1f18a437eea9') {
    throw 'Boost SHA256 mismatch.'
}

$jsonStage = Join-Path $downloadDir 'nlohmann_json_3_12_0'
$jsonHeaders = Join-Path $jsonStage 'include/nlohmann'
New-Item -ItemType Directory -Path $jsonHeaders -Force | Out-Null
$jsonHeader = Join-Path $jsonHeaders 'json.hpp'
Invoke-WebRequest -UseBasicParsing `
    -Uri 'https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp' `
    -OutFile $jsonHeader
$jsonHash = (Get-FileHash -LiteralPath $jsonHeader -Algorithm SHA256).Hash
if ($jsonHash.ToLowerInvariant() -ne 'aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63') {
    throw 'JSON SHA256 mismatch.'
}
Invoke-WebRequest -UseBasicParsing `
    -Uri 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT' `
    -OutFile (Join-Path $jsonStage 'LICENSE.MIT')

tar.exe -xzf $boostArchive -C $downloadDir `
    'boost_1_90_0/boost' 'boost_1_90_0/LICENSE_1_0.txt'
if ($LASTEXITCODE -ne 0) { throw 'Boost extraction failed.' }
$boostStage = Join-Path $downloadDir 'boost_1_90_0'
if (-not (Test-Path -LiteralPath (Join-Path $boostStage 'boost/beast/websocket.hpp'))) {
    throw 'Boost headers missing after extraction.'
}
Copy-Item -LiteralPath $boostStage -Destination $boostDir -Recurse
Copy-Item -LiteralPath $jsonStage -Destination $jsonDir -Recurse
Write-Host "Dependencies installed in $depsRoot"
```

完成后，`.download-*` 是本次下载的暂存目录，可手动清理；不要将它加入版本控制。固定版本目录中的头文件和许可证需要保留。下载失败时重新下载，校验失败时停止，不回退到本机旧库。

### 1.3 为项目内依赖建立 CMake 目标

创建 `backend/include/third_party/CMakeLists.txt`：

```cmake
set(GYMJ_VENDOR_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(GYMJ_VENDOR_BOOST "${GYMJ_VENDOR_ROOT}/boost_1_90_0")
set(GYMJ_VENDOR_JSON "${GYMJ_VENDOR_ROOT}/nlohmann_json_3_12_0/include")

foreach(required_header IN ITEMS
    "${GYMJ_VENDOR_BOOST}/boost/asio.hpp"
    "${GYMJ_VENDOR_BOOST}/boost/beast/websocket.hpp"
    "${GYMJ_VENDOR_JSON}/nlohmann/json.hpp"
)
    if(NOT EXISTS "${required_header}")
        message(FATAL_ERROR "Missing vendored dependency: ${required_header}. Follow web.md step 1.2.")
    endif()
endforeach()

find_package(Threads REQUIRED)
add_library(gymj_boost INTERFACE)
add_library(gymj::boost ALIAS gymj_boost)
target_include_directories(gymj_boost SYSTEM INTERFACE "${GYMJ_VENDOR_BOOST}")
target_link_libraries(gymj_boost INTERFACE Threads::Threads)
if(WIN32)
    target_compile_definitions(gymj_boost INTERFACE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
    )
    target_link_libraries(gymj_boost INTERFACE ws2_32 mswsock)
endif()
if(MSVC)
    target_compile_options(gymj_boost INTERFACE /bigobj)
endif()

add_library(gymj_nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS gymj_nlohmann_json)
target_include_directories(gymj_nlohmann_json SYSTEM INTERFACE "${GYMJ_VENDOR_JSON}")
```

`INTERFACE` 目标将头文件路径和链接要求传给使用者，不生成额外的库文件。源代码继续使用 `<boost/asio.hpp>`、`<boost/beast/websocket.hpp>` 和 `<nlohmann/json.hpp>`，不需要在 include 语句里写 `third_party` 或版本号。

这里保留项目原来使用的 `nlohmann_json::nlohmann_json` 目标名，但它现在明确指向新下载的项目内头文件。`Threads` 和 Windows SDK 库来自编译工具链，不属于需要下载到 include 的第三方源码。

### 1.4 让牌谱模块也使用这份 JSON

将 `backend/src/storage/replay/CMakeLists.txt` 顶部的 `find_package(nlohmann_json ...)` 和整段 `FetchContent` 回退逻辑删除，文件改为：

```cmake
add_library(gymj_storage_replay STATIC heplay.cpp)
add_library(gymj::storage_replay ALIAS gymj_storage_replay)
target_link_libraries(gymj_storage_replay
    PUBLIC gymj::common
    PRIVATE nlohmann_json::nlohmann_json
)
target_compile_features(gymj_storage_replay PUBLIC cxx_std_20)
```

这一步必须做，否则牌谱模块仍可能从系统安装或旧缓存引入另一份 JSON，并与新目标冲突。不要在其他目录重新声明同名目标，也不再调用 `find_package(Boost)`、`find_package(nlohmann_json)` 或为它们配置 FetchContent。

### 1.5 新建应用构建文件

创建 `backend/app/CMakeLists.txt`：

```cmake
add_executable(gymj_server web_main.cpp)
target_compile_features(gymj_server PRIVATE cxx_std_20)
target_link_libraries(gymj_server PRIVATE
    gymj::core_room
    gymj::boost
    nlohmann_json::nlohmann_json
)
if(MSVC)
    target_compile_options(gymj_server PRIVATE /utf-8)
endif()
```

在 `backend/CMakeLists.txt` 中，将现有的 `add_subdirectory(src)` 替换成下面三行；保留原有 `project()`、C++20 设置和后面的测试配置：

```cmake
add_subdirectory(include/third_party)
add_subdirectory(src)
add_subdirectory(app)
```

顺序固定为“依赖目标 -> 核心模块 -> 应用”，测试目录也必须在依赖目标创建之后添加。牌谱模块对 JSON 是 `PRIVATE` 链接，网络应用自己使用 JSON 时仍需明确链接它。

MSVC 的 `/utf-8` 指定源码编码，`/bigobj` 从 `gymj::boost` 传递给使用者，避免 Boost 模板在 Debug 构建中触发 C1128。

### 1.6 构建和运行

先完成第 3 步的 `backend/app/web_main.cpp`，再从仓库根目录执行：

```powershell
cmake -S backend -B build-web-vendored -G "Visual Studio 17 2022" -A x64
cmake --build build-web-vendored --config Debug --target gymj_server
.\build-web-vendored\app\Debug\gymj_server.exe
```

使用全新的 `build-web-vendored/`，不要传旧版的 Boost 路径、JSON 源码覆盖参数或包管理器 toolchain。依赖下载只在第 1.2 节显式执行；随后 CMake 配置不再下载第三方库，缺文件就报错。

**验收：**两个依赖版本目录都在 `backend/include/third_party/` 内；编译命令的第三方 include 路径只指向这两个目录。牌谱、测试和网络模块共同使用这份 JSON，旧的 `build/json`、系统 Boost 目录均不参与构建。

## 2. 掌握 Asio / Beast 的 C++ 使用约束

本节只列实现约束，不展开网络概念：

- `net::io_context ioc{1}` 中的 `1` 是并发提示，不会创建或强制限定线程；本例只在主线程调用一次 `run()`。
- `async_*` 回调捕获 `shared_from_this()`，因此连接对象必须通过 `std::make_shared` 创建；不能在构造函数里提前调用 `shared_from_this()`。
- `net::buffer(...)` 不拥有底层内存。传给 `async_write` 的字符串必须存活到完成回调，不能直接使用临时 `message.dump()` 的内容。
- 每条流只挂一个读操作和一个写操作；所有业务推送走同一发送队列，由写完成回调推进下一条。
- `beast::flat_buffer` 在处理完消息后调用 `consume()`；正常关闭对应 `websocket::error::closed`，主动取消可能对应 `net::error::operation_aborted`。
- 本例中的 `send()`、牌桌调用和定时器都由同一个 `io_context` 线程执行。增加工作线程后必须重新处理执行器与共享状态的串行化。

上述队列与缓冲区约束见 [Beast 异步操作说明](https://www.boost.org/latest/libs/beast/doc/html/beast/using_websocket/notes.html) 和 [async_write 的缓冲区要求](https://www.boost.org/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/async_write.html)。

## 3. 写一个能运行的 C++ WebSocket 服务

**本步目标：浏览器发 JSON，C++ 返回 JSON；多个连接可以同时使用。**

创建 `backend/app/web_main.cpp`。下面是一个完整的入门程序，只处理 `ping` 和 `echo`，暂不接入麻将。

示例只监听本机 `127.0.0.1:8080`，适用于本地练习。它使用单线程、64 KiB 入站消息上限，以及有上限的发送队列。发生传输错误或队列溢出时直接断开 TCP；正式服务的优雅关闭放在第 12 步处理。

```cpp
#include <utility>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <csignal>
#include <deque>
#include <iostream>
#include <memory>
#include <string>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;
using Json = nlohmann::json;

class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    explicit WsSession(tcp::socket socket) : ws_(std::move(socket)) {}

    void run() {
        ws_.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::server));
        ws_.read_message_max(64 * 1024);
        ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
            if (ec) return self->stop("handshake", ec);
            self->read();
        });
    }

    // Call only from the single io_context thread, after the handshake.
    void send(Json message) {
        if (stopped_) return;
        auto text = message.dump();
        if (outbox_.size() >= 128 || queued_bytes_ + text.size() > 1024 * 1024) {
            return stop("slow client", {});
        }
        const bool idle = outbox_.empty();
        queued_bytes_ += text.size();
        outbox_.push_back(std::move(text));
        if (idle) write();
    }

private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer input_;
    std::deque<std::string> outbox_;
    std::size_t queued_bytes_ = 0;
    bool stopped_ = false;

    void read() {
        if (stopped_) return;
        ws_.async_read(input_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) return self->stop("read", ec);
                const bool is_text = self->ws_.got_text();
                auto text = beast::buffers_to_string(self->input_.data());
                self->input_.consume(self->input_.size());
                if (is_text) self->on_message(text);
                else self->send({{"type", "error"}, {"code", "text_only"}});
                self->read();
            });
    }

    void on_message(const std::string& text) {
        // Syntax errors become a discarded JSON value rather than an exception.
        auto message = Json::parse(text, nullptr, false);
        if (message.is_discarded() || !message.is_object()) {
            send({{"type", "error"}, {"code", "invalid_json"}});
            return;
        }
        auto type = message.find("type");
        if (type == message.end() || !type->is_string()) {
            send({{"type", "error"}, {"code", "invalid_type"}});
            return;
        }
        if (*type == "ping") {
            send({{"type", "pong"}});
        } else if (*type == "echo") {
            auto payload = message.find("payload");
            if (payload == message.end() || !payload->is_string()) {
                send({{"type", "error"}, {"code", "invalid_payload"}});
                return;
            }
            send({{"type", "echo"}, {"payload", *payload}});
        } else {
            send({{"type", "error"}, {"code", "unknown_type"}});
        }
    }

    void write() {
        ws_.text(true);
        ws_.async_write(net::buffer(outbox_.front()),
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) return self->stop("write", ec);
                self->queued_bytes_ -= self->outbox_.front().size();
                self->outbox_.pop_front();
                if (!self->stopped_ && !self->outbox_.empty()) self->write();
            });
    }

    void stop(const char* where, beast::error_code ec) {
        if (stopped_) return;
        stopped_ = true;
        if (ec != websocket::error::closed && ec != net::error::operation_aborted) {
            std::cerr << where << ": " << (ec ? ec.message() : "queue limit") << '\n';
        }
        beast::error_code ignored;
        beast::get_lowest_layer(ws_).socket().close(ignored);
        // Keep outbox_ alive: an outstanding write may still reference its front.
    }
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint)
        : acceptor_(ioc, endpoint) {}

    void run() { accept(); }

private:
    tcp::acceptor acceptor_;

    void accept() {
        acceptor_.async_accept(
            [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
                if (ec) {
                    if (ec != net::error::operation_aborted) {
                        std::cerr << "accept: " << ec.message() << '\n';
                    }
                    return;
                }
                std::make_shared<WsSession>(std::move(socket))->run();
                self->accept();
            });
    }
};

int main() {
    try {
        net::io_context ioc{1};
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](beast::error_code ec, int) {
            if (!ec) ioc.stop();
        });
        auto listener = std::make_shared<Listener>(
            ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), 8080});
        listener->run();
        std::cout << "WebSocket listening on ws://127.0.0.1:8080\n";
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}
```

需要逐行理解的部分：

1. `async_accept` 完成后才进入 `read()`，此时 WebSocket 握手已成功。
2. 每次读取完成，先 `consume()` 清空已处理的缓冲区，再挂下一次读取。
3. `send()` 将字符串存入成员队列，不能直接对临时的 `message.dump()` 建立异步缓冲区。
4. 只有队列从空变为非空时才启动写；之后由写完成回调取出下一条。
5. `[self = shared_from_this()]` 让对象在等待期间继续存在，不能改成捕获一个可能失效的裸 `this`。
6. `stop()` 可以被读写回调重复调用，因此需要 `stopped_` 防止重复处理。
7. 当前 `main()` 的异常捕获是进程级兜底。后续路由中要把可预期的输入错误转为回执，不能让一条坏消息使整个服务退出。

`parse(..., false)` 只解决 JSON 语法失败；取字段的类型和范围仍需自己验证。参见 [nlohmann/json 的 parse 文档](https://json.nlohmann.me/api/basic_json/parse/)。

**验收：**按第 1 步构建，终端出现监听地址且不退出，然后使用第 4 步页面测试收发。

## 4. 用一个浏览器页面测试连接

**本步目标：确认浏览器到 C++ 的完整收发路径。**

创建 `frontend/public/network-test.html`，写入：

```html
<!doctype html>
<html lang="zh-CN">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mahjong Network Test</title>
<button id="connect">Connect</button>
<button id="send" disabled>Send ping</button>
<button id="close" disabled>Disconnect</button>
<pre id="log"></pre>
<script>
let socket = null;
const connect = document.querySelector("#connect");
const send = document.querySelector("#send");
const close = document.querySelector("#close");
const log = document.querySelector("#log");
const lines = [];
function print(text) {
  lines.push(text);
  if (lines.length > 100) lines.shift();
  log.textContent = lines.join("\n");
}
connect.onclick = () => {
  if (socket && socket.readyState !== WebSocket.CLOSED) return;
  const ws = new WebSocket("ws://127.0.0.1:8080");
  socket = ws;
  connect.disabled = true;
  ws.onopen = () => {
    print("connected");
    send.disabled = false;
    close.disabled = false;
  };
  ws.onmessage = event => print("< " + event.data);
  ws.onerror = () => print("connection error");
  ws.onclose = event => {
    if (socket !== ws) return;
    print("closed: " + event.code);
    connect.disabled = false;
    send.disabled = true;
    close.disabled = true;
  };
};
send.onclick = () => {
  if (!socket || socket.readyState !== WebSocket.OPEN) return;
  socket.send(JSON.stringify({type: "ping"}));
  print('> {"type":"ping"}');
};
close.onclick = () => socket?.close(1000, "test complete");
</script>
</html>
```

本地练习可直接用浏览器打开这个文件。若浏览器策略不允许从本地文件连接，则通过编辑器的静态服务器打开它；正式前端将通过 HTTP/HTTPS 提供。

**验收顺序：**

1. 启动 C++ 程序，打开页面，点击 `Connect`，显示 `connected`。
2. 点击 `Send ping`，显示 `{"type":"pong"}`。
3. 打开第二个页面，两边都能收发，互不影响。
4. 点击 `Disconnect` 后再连接，仍能正常使用。
5. 在开发者工具的 Network / WS 中查看 WebSocket 消息。
6. 在该页面的控制台执行 `socket.send('{broken')`，收到 `invalid_json`；随后再发 ping，连接仍可用。
7. 测试 `socket.send('{"type":123}')`、`socket.send('[]')`，都应返回错误而非使进程退出。

## 5. 拆分网络模块，并约定消息协议

**本步目标：把“收到一条 JSON”变成可维护的麻将请求入口。**

第 3 步通过后，把类拆到以下位置。头文件声明类，`.cpp` 放实现，`web_main.cpp` 保留组装与启动代码。

```text
backend/include/gymj/server/
  gateway/ws_session.hpp
  gateway/listener.hpp
  gateway/router.hpp
  gateway/protocol.hpp
  session/player_session.hpp
  lobby/room_manager.hpp
backend/src/server/
  CMakeLists.txt
  gateway/ws_session.cpp
  gateway/listener.cpp
  gateway/router.cpp
  gateway/protocol.cpp
  session/player_session.cpp
  lobby/room_manager.cpp
protocol/
  v1.md
  examples/
```

新建 `gymj_server_lib` 库，把这些 `.cpp` 加入其中。在 `backend/src/CMakeLists.txt` 的现有子目录之后加入 `add_subdirectory(server)`。该库链接第 1 步的 `gymj::boost` 和 `nlohmann_json::nlohmann_json`，应用再链接它；公共头文件暴露哪个依赖的类型，就将对应依赖设为 `PUBLIC`，否则用 `PRIVATE`。拆分时继续使用项目内依赖目标，不重新搜索系统库。

### 5.1 协议第一版

先写 `protocol/v1.md`，固定字段和错误码，再写路由。下面是**待实现的业务协议**，第 3 步程序还不能处理这些消息。

| 客户端 `type` | 作用 | 身份要求 |
| --- | --- | --- |
| `hello` | 创建临时身份，或用 token 恢复身份 | 握手后第一条业务消息 |
| `create_room` | 创建房间并入座 | 已完成 hello |
| `join_room` | 加入指定房间 | 已完成 hello |
| `ready` | 当前座位准备 | 已入座 |
| `action` | 提交出牌、碰、杠、胡、过 | 已入座 |
| `snapshot` | 请求自己的完整视角 | 已入座 |
| `ping` | 应用层存活检查 | 已连接 |

每条业务消息带 `v: 1`。路由第一层检查版本、`type`、消息大小和字段类型。未知版本或类型应有明确错误码。

首次连接：

```json
{"v":1,"type":"hello","nickname":"玩家甲"}
```

服务端返回：

```json
{"v":1,"type":"welcome","sessionToken":"由服务端生成的随机令牌","resumed":false}
```

出牌请求：

```json
{
  "v": 1,
  "type": "action",
  "roomId": "room-001",
  "roundId": "1",
  "requestId": "7",
  "promptId": "18",
  "action": {"type": "discard", "tile": "5m"}
}
```

请求回执：

```json
{
  "v": 1,
  "type": "ack",
  "requestId": "7",
  "accepted": true,
  "duplicate": false,
  "tableSeq": "31",
  "error": null
}
```

这些编号的职责不同：

| 字段 | 来源和用途 |
| --- | --- |
| `requestId` | 客户端为同一玩家、同一小局递增生成，供 `Table::submit` 去重 |
| `roundId` | 快照中的当前小局编号，拒绝上一局遗留请求 |
| `promptId` | 快照中的当前操作窗口，拒绝过期按钮；多人响应可以共用 |
| `tableSeq` | 服务端状态版本；不能代替客户端请求编号或操作窗口 |
| `requestTag` | 可选的普通字符串，用于关联创建房间等大厅请求，不传给 `Command` |

所有 `uint64_t` 在 JSON 中统一使用十进制字符串，避免 JavaScript `Number` 丢失大整数精度。前端需要比较或递增时用 `BigInt`，发送时用 `.toString()`；不能直接 `JSON.stringify(BigInt)`。累计 `int64_t` 小分也建议采用字符串。

`requestTag` 只是关联请求，并不自动防止重复创建房间。为有副作用的大厅操作保留一个有容量和过期时间限制的回执缓存，相同 tag、相同参数返回原结果，相同 tag、不同参数拒绝执行。

### 5.2 严格解析，而不是直接强制转换

在 `protocol.cpp` 编写辅助函数，例如下面这个完整函数。它拒绝负数、浮点数、空字符串、溢出和多余字符；是否允许零由具体字段再判断。

```cpp
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <nlohmann/json.hpp>

std::optional<std::uint64_t> parse_u64(const nlohmann::json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.size() > 20) return std::nullopt;
    if (text.size() > 1 && text.front() == '0') return std::nullopt;
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return result;
}
```

动作解析使用显式白名单：`discard / pon / self_kan / add_kan / open_kan / tsumo / ron / pass`，映射到现有 `PlayerActionType`。本仓库当前没有吃牌动作，不能照搬其他麻将协议中的 `chi`。

牌面只接受两字符的 `1m`～`9m`、`1s`～`9s`、`1p`～`9p`，映射到 `TileType` 和 rank。未知枚举、非法牌面直接拒绝；不要将客户端整数 `static_cast` 成枚举。

`pass` 统一使用 `action_tile = null_tile`。其他动作尽量按 `available_actions` 给出的牌面原样提交；动作和牌面最终仍由 `Table` 校验。先将 `PlayerAction.action_tile` 初始化为 `null_tile`，再填入解析结果，避免未初始化字段进入去重比较。

还要限制昵称长度、字符串长度、数组大小与 JSON 嵌套深度。64 KiB 限制不等于深度限制，可以通过 JSON parser callback 在超出深度时抛出自定义协议异常，并在消息入口捕获。第一版只接受已定义字段形状。

**验收：**针对解析函数测试缺字段、错误类型、超范围编号、`"5x"`、`"0m"`、未知动作。全部应返回协议错误，牌桌状态不变。

## 6. 实现临时身份与房间

**本步目标：四个浏览器连接能入座、准备和开局。**

先只做一个固定房间 `room-001`，验证流程后再把它放进房间 map。当前规模无需先实现匹配系统。

### 6.1 区分三种身份

| 数据 | 用途 | 生命周期 |
| --- | --- | --- |
| connection ID | 区分旧连接和新连接，处理连接替换 | 一条连接 |
| session token | 浏览器证明“我是原来的临时玩家” | 断线后仍保留一段时间 |
| `PlayerInfo.id` | `Table` 内识别座位 | 当前桌的入座关系 |

玩家记录可以按下面的形状设计。这是结构示意，需要在对应头文件中引入标准库和项目头文件：

```cpp
struct PlayerSession {
    std::string token;
    std::string nickname;
    std::string room_id;
    std::optional<gymj::common::PlayerInfo> seated_player;
    std::weak_ptr<WsSession> connection;
    std::uint64_t connection_generation = 0;
};
```

`SessionManager` 用 `token -> PlayerSession` 保存身份；`RoomManager` 用 `roomId -> Room` 保存房间；房间持有 `Table`，另有 `player.id -> PlayerSession` 索引用于投递。

token 至少使用 128 位、建议 256 位密码学安全随机数，编码为十六进制字符串，并检查是否碰撞。Windows 可用 [BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom) 并链接 SDK 的 `bcrypt`，Linux 可用系统 `getrandom`；这两种实现不增加第三方库。不要用递增 ID、昵称、时间戳或 `mt19937` 生成认证令牌。

`hello` 的 token 只通过已加密的正式连接传输，不写入日志；首次本地练习使用 `ws://` 即可。无数据库意味着服务重启后内存身份失效，前端应明确返回大厅重新加入；文件牌谱仍保留，但不会自动恢复进行中的牌局。

### 6.2 入座与开局顺序

1. `hello` 成功后，连接绑定 `PlayerSession`，但此时尚无 `PlayerInfo.id`。
2. `join_room` 检查房间存在，玩家尚未在另一桌入座。
3. 构造 `PlayerInfo{session.nickname, 0}`，调用 `table.join(...)`。
4. 若成功，把 `update.assigned_player` 保存到玩家记录，并建立 ID 到连接的索引。
5. **先完成绑定，再分发 `update.deliveries`**，否则新玩家收不到自己的首次快照。
6. `ready` 使用会话保存的 `PlayerInfo` 调用 `table.ready(...)`，并投递结果。
7. 从返回视角的公共座位信息判断是否四人已入座、在线并准备；满足时由服务端调用一次 `table.start()`，再投递开局结果。

`start()` 仍会复查条件。失败时发送单独的 `start_failed` 通知，不能把已经成功的准备操作描述为失败。尤其要检查牌谱目录是否可写，当前 `Table` 在牌谱初始化失败时会拒绝开局。

首次入座重试不能再次传 `id = 0`，应复用已经绑定的 `assigned_player`。昵称允许相同，不能通过昵称寻找原来的座位。

网络层暂定“一名临时玩家只在一桌”。当前 `Table` 没有离桌接口，所以关闭网页表示断线，不代表座位空出来。若要做离桌、踢人、换桌，应先明确它们在等待中和进行中的行为，再增加核心接口与测试。

**验收：**四个独立标签页使用不同临时身份加入同房间，第五个人收到“房间已满”；第四人准备后，四个页面各收到自己的开局状态。

## 7. 把麻将动作接到 Table

**本步目标：玩家能从浏览器提交一次合法操作。**

在 `Router` 中把消息解析与牌桌调用分开。可以先实现这些函数：

- `decode_command(json)`：检查字段，返回 `Command` 或协议错误。
- `encode_view(PlayerView, now)`：将玩家视角转为 JSON。
- `encode_events(vector<RoundEvent>)`：只转换已过滤的事件。
- `deliver(TableUpdate)`：按接收人发送快照和动画事件。

以下是**接入伪代码**，`room_manager`、`deliver`、`encode_ack` 等需要按前面步骤实现，不能当成独立可编译文件：

```cpp
void Router::on_action(WsSession& connection, const Json& message) {
    auto& session = require_current_session(connection);
    auto& room = room_manager.require_room(session.room_id);
    require_matching_room_id(message, session.room_id);
    const auto command = decode_command(message);

    // PlayerInfo comes from the authenticated binding, never from the JSON.
    const auto update = room.table.submit(*session.seated_player, command);
    connection.send(encode_ack(update));
    deliver(update);
    log_replay_error_if_any(update);
}
```

进入这段逻辑前，`require_current_session` 必须确认 hello 已完成、连接仍是该身份的当前连接、玩家已入座。`roomId` 只是路由提示，必须与会话绑定一致；客户端不能借此操作其他房间。

### 7.1 回执与快照分别处理

- `accepted == false`：请求被拒绝，返回错误；不能自己扣牌或修改分数。
- `duplicate == true`：返回原回执；当前核心不会再次提供同一动作的 deliveries。
- `replay_error`：记录服务端存储告警，不能因此把已接受的出牌改成失败并要求重试。
- `error`：当前核心也可能在动作已接受、后续自动推进失败时携带错误文本，应记录并显示异常状态，不能只看文本是否为空来反转 `accepted`。
- `deliveries`：依次发送，每条使用 `delivery.view.table_seq`，不能全都套用外层 `update.table_seq`，因为中间快照可能来自不同阶段。

同一个 `requestId` 重发时，`roundId`、`promptId`、动作和牌面必须完全相同。使用同一编号改动作会被拒绝。

部分被拒绝的动作也会消耗请求编号。因此前端新操作总是使用新编号；恢复连接时依据 `lastRequestId` 继续递增。不要在失败后把计数器减回去。

### 7.2 编码玩家视角

只序列化 `Delivery.view` 或 `table.snapshot(已认证玩家)` 的结果，不序列化整个 `Table`、`RoundState` 或 `RoundTransition`。

第一版使用完整快照，消息可以这样组织：

```text
{
  v: 1,
  type: "table_update",
  roomId: "room-001",
  view: encode_view(delivery.view, now),
  events: encode_events(delivery.events)
}
```

上面是结构示意，不是合法 JSON 样例。`encode_view` 至少包含以下映射：

| C++ 字段 | JSON 字段和处理 |
| --- | --- |
| `self_seat` | `selfSeat`，本人座位 |
| `table_stage / round_stage` | `tableStage / roundStage`，显式字符串枚举 |
| 各种 `*_id / *_seq` | 对应驼峰字段，转十进制字符串 |
| `last_request_id` | `lastRequestId`，重连恢复请求计数 |
| `seats` | 昵称、在线、准备、手牌数量、牌河、副露 |
| `own_tiles` | `ownTiles`，自己的手牌、摸牌缓冲等 |
| `available_actions` | `availableActions`，本人当前允许提交的动作 |
| `deadline` | `remainingMs`，无截止时间则 `null` |
| `acting_player / dealer_seat / tiles_remaining` | 当前行动座位、庄家、剩余牌数 |
| `one_sou / eight_pin / pending_action` | 当前规则相关的公开状态，空值为 `null` |
| `round_result / point_result` | 结束时的结局和小分，其他时候为 `null` |
| `total_points` | `totalPoints`，四人的累计分数字符串 |

`remainingMs = max(0, duration_cast<milliseconds>(deadline - now).count())`，其中 `now` 使用 `Table::Clock::now()`。`steady_clock::time_point` 没有可传给浏览器的现实时间戳；前端倒计时只作显示，最终超时由服务器判断。

隐藏牌的 `null_tile` 应编码为 JSON `null`，不能直接调用现有 `tile_to_string(null_tile)`；该函数不是网络层的隐藏牌编码器。其他枚举同样显式映射字符串，避免调整 C++ 枚举顺序后破坏协议。

前端收到快照就替换本地状态。`events` 用于动画，不能再拿同一批事件对已经更新的快照重复“摸一张、扣一张”。第一版可以先不播放动画。

**验收：**合法出牌只有一次状态变化；相同请求重发没有第二次扣牌；伪造其他玩家 ID 不生效；用旧 `promptId` 提交被拒绝。检查四个连接的原始消息，任何人都不能看到对手的未公开手牌或暗杠牌面。

## 8. 增加服务器定时器

**本步目标：即使玩家断线或不发消息，牌局也会按时推进。**

在持有所有房间的服务对象里加入 `boost::asio::steady_timer`。每隔 50～100 ms 遍历活跃牌桌，调用 `table.tick(now)` 并分发返回结果。

下面是**类成员实现片段**，假设你已定义 `timer_`、`rooms_`、`stopping_` 和 `deliver`，且该服务对象继承 `enable_shared_from_this`：

```cpp
void Server::arm_tick() {
    timer_.expires_after(std::chrono::milliseconds(50));
    timer_.async_wait([self = shared_from_this()](beast::error_code ec) {
        if (ec || self->stopping_) return;
        const auto now = gymj::room::Table::Clock::now();
        for (auto& [id, room] : self->rooms_) {
            self->deliver(room->table.tick(now));
        }
        self->arm_tick();
    });
}
```

这是近似 50 ms 的轮询，不是严格实时调度；实际延迟还包括本轮处理耗时。一次 tick 没有变化时通常没有 deliveries，不需要主动向所有人发送快照。

定时器必须与网络回调使用同一个单线程 `io_context`。不要新建一个线程直接遍历牌桌，否则它可能与 `submit()` 同时修改状态。清理房间应在本轮遍历结束后统一执行，避免迭代器失效。

**验收：**玩家不操作会自动超时出牌或过；关闭全部四个客户端后，进行中的小局仍会由服务器推进；定时器停止时不会重新挂下一次等待。

## 9. 实现断线重连

**本步目标：刷新页面后恢复原座位和当前状态。**

浏览器收到 token 后可暂存 `sessionStorage`，刷新同一标签页能够继续使用，同时方便开四个独立标签页测试。由现有标签页复制出的页面可能继承存储，测试时应检查是否意外复用了 token。

重连发：

```json
{"v":1,"type":"hello","sessionToken":"之前保存的令牌"}
```

服务端流程：

1. token 存在且未过期，则找到原 `PlayerSession`；无效时返回 `session_expired`，不要偷偷创建新身份占另一个座位。
2. 增加 `connection_generation`，将新连接绑定为当前连接，再关闭旧连接。
3. 若已入座，使用已保存的 `PlayerInfo` 调用 `table.join(player)`；它会恢复在线状态，保留座位和原截止时间。
4. 返回 `welcome`，发送自己的完整快照，并向其他玩家更新在线状态。
5. 若发送 `join()` 的 deliveries 已包含恢复快照，无需额外重复发送；需要单独同步时调用 `snapshot(player)`。

旧连接的读取和关闭回调可能在新连接绑定后才触发。每个回调都要核对它携带的 generation；旧连接既不能再提交动作，也不能调用 `table.disconnect()` 把新连接标成离线。

正常断线时调用一次 `table.disconnect(player)`，将结果投递给其他人，移除网络连接索引，但保留临时身份和座位关系。

前端重连采用 1、2、4、8 秒、最高约 15 秒的退避，并加少量随机抖动。用户主动退出时停止重连。连接恢复后先等快照再开放操作按钮，不重放全部旧鼠标点击。

如果某个动作回执丢失，可以在相同小局内原样重发该请求，或先用快照确定结果；新小局不能继续补发上一局动作。快照完整，所以不要求收到所有历史增量事件；版本落后时替换为最新快照即可，旧回执不能让状态回退。

会话过期策略必须考虑已有座位：进行中的座位不能因短暂离线被悄悄删除身份，否则玩家无法回来。可以先保留到当前房间结束，再按明确的空闲时长清理会话和房间。

**验收：**刷新仍是原座位；同 token 第二连接接管后旧连接不能操作；旧连接关闭不会踢掉新连接；重连不重置操作倒计时；服务重启后显示身份失效而非永久转圈。

## 10. 再做最小麻将前端

**本步目标：四个人能从页面打完一小局。**

连接和状态同步稳定后，再从调试页面升级。第一版可以继续使用普通 HTML/CSS/JavaScript，之后再按团队习惯引入前端框架。

页面只需要：

1. 大厅：昵称、房间编号、创建、加入。
2. 牌桌：四个座位、昵称、在线状态、牌河、副露、自己的手牌和摸牌。
3. 操作：准备、出牌，以及服务器 `availableActions` 对应的碰、杠、胡、过。
4. 结算：本局小分明细、累计小分、下一局准备、下载结果。

参考天凤时，先参考四家围桌、底部手牌、中央局况和清晰的操作按钮。使用自己制作或有授权的牌图资源。

前端数据流保持为：

```text
WebSocket 收到快照 -> 替换 state -> render(state)
用户点击 -> 从当前快照构造请求 -> 发送 -> 等待服务端状态
```

按钮点击后可以暂时禁用来防连点，但服务端去重仍必须存在。前端不能自行洗牌、决定是否能胡、扣分或延长倒计时。

昵称和错误消息使用 `textContent` 渲染，避免把用户输入拼到 `innerHTML`。连接断开时锁定动作区域，保留最后画面并显示连接状态。

`ownTiles.hand` 与 `drawBuffer` 分开展示，不能漏掉摸牌。自己的座位按 `selfSeat` 旋转到下方，服务器座位编号保持原样用于发命令。

**验收：**开四个页面，完成准备、摸打、至少一种响应动作、超时和结算。对照牌谱确认页面与服务端的最终小分一致。

## 11. 接上牌谱与小分下载

**本步目标：打完后获得文件，不需要玩家数据库。**

当前 `Table` 已经调用 `Replay` 写 JSONL。网络层不要再把 `Delivery.events` 当成完整牌谱写一遍，它们已按接收人过滤，缺少完整回放需要的信息。

先区分两个概念：`RoundFinished` 只表示一小局结束；当前 `Table` 可以在同一张桌上继续下一小局，同一个 JSONL 文件会继续追加。若产品还需要“整场结束”，应由房间层明确定义局数或房主结束流程。

建议分两阶段实现：

1. 本地版本先在结算时显示导出状态，管理员从 `replay/` 取得现有牌谱；增加单独的小分 JSON 或 CSV 导出。
2. 浏览器版本增加已授权的 HTTP 下载接口，玩家点击下载。不要将整个牌谱目录当成静态公开目录。

### 11.1 导出完成的小局

检测最后一个结算视角中 `point_result` 已存在、`table_stage == RoundFinished`，使用 `(roomId, roundId)` 作为导出幂等键。多个玩家各收到一次结算，不代表应写四份文件。

小分至少保存：格式版本、房间编号、小局编号、座位、昵称、`point_result.delta_result` 和 `total_points`。如果导出 CSV，要正确转义逗号、引号和换行，并处理昵称以 `= / + / - / @` 等字符开头时的表格公式解释问题；第一版 JSON 更容易保持准确。

使用服务端生成的文件名，先写临时文件，确认写入成功后发布为完成文件。磁盘错误应显示“导出失败，可重试”，不能重新结算一次来重试导出。

### 11.2 下载不要泄露下一局

完整牌谱包含未公开手牌。不能在下一局已经追加到同一文件后，直接提供整个实时 `Replay::path()` 给上一局的下载请求。

可选实现：

- 小局结束后记录已完成的字节边界，生成不可变的截止该局的牌谱副本；下载只读取这个已完成文件。
- 房间正式结束、确认不再追加后，再发布整桌文件。

只导出一小局时，需要保留回放格式要求的头部、配置和关联记录，不能按文本随意截几行。最初可以导出“截至某局结束的整桌牌谱”简化实现。

HTTP 接口例如 `GET /api/replays/{exportId}`，服务端通过导出索引找到文件并核验参与者身份。文件路径由服务器保存，不能接受客户端提供的任意路径；同时设置合理的大小、并发和保留期限。

### 11.3 HTTP 接入放在哪里

第 3 步只有 WebSocket。若需要同一端口同时支持 HTTP 下载和 `/ws`：

1. TCP 接入后先用 Beast HTTP parser 读取请求，并限制请求头/体大小及超时。
2. 确认是合法 WebSocket Upgrade、路径为 `/ws`、Origin 被允许，再用 `ws.async_accept(request, handler)`。
3. 普通 HTTP 请求进入健康检查或下载处理器。
4. 初版可限制每条 HTTP 连接一个请求，拒绝不支持的请求体和流水线，避免交接时丢失已预读数据；需要更完整行为时使用官方高级服务器示例的连接管理方式。

这样就不能再对同一个 socket 先调用无参数的 `ws.async_accept()` 再尝试读 HTTP，它已经消费了握手。静态前端也可以交给反向代理单独提供。可参考 [Beast 官方示例目录](https://www.boost.org/latest/libs/beast/doc/html/beast/examples.html)。

下载可让前端通过 `fetch` 的 `Authorization` 头发送临时 token，再将响应转换成 Blob 触发下载；不要把主会话 token 拼进 URL，避免进入访问日志。下载只授予该局参与者，未完成的文件不可下载。

**验收：**每小局只产生一次导出；四人下载结果一致；非参与者无法取得文件；下一局开始后，上一局的下载内容保持不变；磁盘写入失败不会导致重复计分。

## 12. 从本地练习走向约 200 人服务

**本步目标：补齐网络运行边界，再测量容量。**

### 12.1 连接、内存和关闭

- 握手超时、消息上限、JSON 深度限制、发送队列上限都要启用。
- 对连接数、未认证连接数、创建房间数和消息频率设上限；握手后规定时间内必须 hello。
- 使用 Beast 的 WebSocket 超时与 ping/pong，持续保持异步读操作来处理控制帧。不要同时自行向底层 TCP 流设置一套相冲突的读超时。
- 慢客户端发送队列达到上限就断开并允许重连恢复，不能无限占用内存。
- 正式关闭时先停止新发送，按当前读写状态安排 `async_close`，用超时兜底关闭底层连接。取消操作后仍要让完成回调运行以释放资源。
- 正式停服时停止接受连接和新开局，通知客户端、处理导出，再取消定时器和关闭连接；第 3 步直接 `ioc.stop()` 只是教学用的快速退出。
- `accept` 遇到临时资源错误时应退避重试并记录；不能不停立即重试形成空转，也不能悄悄永久停止接受连接。

应用层日志保留连接 ID、房间 ID、动作类型、requestId、拒绝原因和耗时；避免日志包含 token 和未公开手牌。

### 12.2 部署路径

建议部署链路：

```text
浏览器 HTTPS / WSS
    -> 反向代理：证书、静态网页、/ws 升级转发
    -> C++ 服务：监听本机或受限内网地址
    -> 非公开的 replay/export 目录
```

HTTPS 页面连接使用 `wss://`。反向代理配置必须支持 WebSocket Upgrade，其空闲超时应与心跳设置配合。上线时核对所选代理的官方文档，不要直接把本地 echo 服务暴露到公网。

正式握手检查 Origin 白名单和请求路径，认证仍通过会话 token 完成。Origin 不是身份凭据；普通 CORS 响应头也不能代替 WebSocket 的 Origin 校验。

### 12.3 压测实际牌局，而不只连 200 个 socket

先做 4 人，再做 20 人、200 人。测试机器人按自己快照的合法动作发送请求，并带少量思考延迟，让牌局正常进行；另外测试同时重连和消息突发。

记录：

- 动作到回执的 P50/P95/P99 延迟。
- 定时器计划执行时间与实际执行时间的差值。
- 活跃连接数、房间数、CPU、内存、每连接发送队列长度。
- 牌谱写入耗时、失败次数、导出队列积压。
- 慢连接、频繁刷新、坏消息是否影响其他桌。

先定位瓶颈，再增加线程。若实测同步牌谱 I/O 阻塞严重，应把存储改为按桌有序的后台队列，同时明确队列满、写入失败和落盘确认的语义；不能把同一个 `Table` 随意放到多个线程调用。

需要多线程时，对每条连接和每张桌使用 strand 或统一串行执行器。不同连接各自有 strand，并不自动保证它们访问同一张桌时串行。

**验收：**200 个客户端持续参与约 50 桌，完成多轮结算和重连；没有数据竞争、重复扣牌、队列无限增长或手牌泄露，延迟达到你选定的目标。不要把本教程当作已经完成该容量验证的证明。

## 13. 按这个清单推进

| 阶段 | 本次只做的事情 | 完成标志 |
| --- | --- | --- |
| A | 第 1～4 步，编译和 WebSocket JSON 收发 | 两个浏览器稳定 ping/pong |
| B | 第 5 步，协议和解析器 | 非法输入不会进入 Table |
| C | 第 6～7 步，单房间入座和动作 | 四人能准备、开局、出牌 |
| D | 第 8～9 步，超时和重连 | 断线不冻结牌局，刷新不丢座位 |
| E | 第 10～11 步，页面和文件下载 | 能打完、看分、取回完整牌谱 |
| F | 第 12 步，部署边界和压测 | 测得约 200 人活跃对局的表现 |

每接入一个阶段，运行原有核心测试，避免网络接入过程中误改游戏行为：

```powershell
cmake --build build-web-vendored --config Debug
ctest --test-dir build-web-vendored -C Debug --output-on-failure
```

新网络测试重点覆盖：帧内坏 JSON、字段类型错误、队列写入顺序、重复动作、不同桌的身份隔离、重连替换竞态、四家隐私视角和牌谱导出幂等。规则正确性继续由已有核心测试负责。

## 14. 常见错误速查

| 现象 | 优先检查 |
| --- | --- |
| 下载或 SHA-256 校验失败 | 确认固定版本官方 URL、下载是否完整；重新下载，不使用系统库兜底 |
| CMake 报 Missing vendored dependency | 第 1.2 节是否完成，版本目录是否与 CMake 中一致 |
| 找不到 Boost 头文件 | 完整 `boost/` 目录是否位于 `backend/include/third_party/boost_1_90_0/` 下，目标是否链接 `gymj::boost` |
| JSON 目标重复定义或仍从外部引入 | 是否删除牌谱 CMake 中原有 `find_package` / `FetchContent` 逻辑，并先创建项目内依赖目标 |
| CMake 提示 generator 不一致 | 使用新的 `build-web-vendored/`，不要复用旧生成器缓存 |
| Windows 网络符号链接失败 | 是否链接 `ws2_32`、`mswsock`，编译器和库架构是否一致 |
| MSVC 报 C1128，节数超过限制 | 使用 Boost 的目标是否加了 `/bigobj` |
| 端口被占用 | 换端口并同步修改浏览器地址，不停止不相关的进程 |
| 浏览器访问服务地址没有网页 | WebSocket 服务没有自动提供 HTML，先单独打开测试页 |
| HTTPS 页面连接失败 | 是否误用 `ws://`，证书和反向代理 Upgrade 是否正常 |
| 发几条消息就崩溃 | 是否同时启动两个写操作，或发送缓冲区已经失效 |
| 客户端连接后服务不再处理其他人 | 是否使用了阻塞读、sleep 或长时间磁盘操作 |
| 四人准备后没有开始 | 网络层是否在准备齐全后调用 `start()`，牌谱目录是否可写 |
| 非法动作后下个请求也失败 | 是否复用了已被消耗的 requestId，或使用旧 promptId |
| 同一动作动画播放两次 | 是否同时用完整快照和事件重复修改状态 |
| 新连接刚恢复就被标离线 | 旧连接的关闭回调是否核对 connection generation |
| 前端倒计时与服务端不同 | 是否发送了 steady_clock 原值，是否错误地在重连时重置期限 |
| 牌谱下载包含下一局手牌 | 是否下载了仍在追加的整桌文件，而不是完成时的不可变导出 |

建议第一次实现停在阶段 A：先亲手运行示例、观察消息和回调，再将 `on_message()` 中的分支替换成业务路由。这样遇到问题时，能够判断它来自连接、协议、会话还是牌桌。
