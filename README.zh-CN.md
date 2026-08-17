# GY Mahjong

[English](README.md)

一个在线麻将游戏，采用客户端/服务端架构。第一阶段目标是使用单个 C++ 权威服务器支持约 200 名玩家同时在线，前端使用接近天凤的桌面风格，规则由项目自定义，并在不持久化玩家账号数据的前提下导出牌谱和小分。

## 目标

- 支持约 200 名玩家同时在线，约 50 张活跃牌桌。
- 所有游戏状态和合法操作由服务端权威判定。
- 将自定义麻将规则与网络、存储、UI 解耦。
- 第一版不引入玩家数据库。
- 每局结束后导出牌谱和最终小分。
- 前端使用浏览器客户端，通过 WebSocket 与服务端通信。

## 架构

```text
frontend
  |
  | WebSocket JSON 消息
  v
backend C++ server
  |
  | 持有全部权威状态
  v
core game engine
  |
  | 生成牌谱事件和小分结果
  v
file exports
```

后端应把每条客户端消息当作请求，而不是事实。前端只负责渲染服务端下发的快照、事件、提示和合法操作。

## 目录结构

```text
backend/
  app/                    服务端可执行程序入口。
  include/                后端模块间共享的公共 C++ 头文件。
  src/
    core/
      tile/               牌模型、牌解析、牌山生成、牌工具函数。
      state/              GameState、TableState、PlayerState、回合状态。
      rules/              自定义规则、合法操作发现、胡牌检查。
      score/              小分计算和结算。
    server/
      gateway/            WebSocket 接入、消息路由、心跳。
      lobby/              房间创建、加入、准备，后续可扩展匹配。
      session/            临时玩家身份、断线重连、连接绑定。
      table/              牌桌状态机和超时处理。
    storage/
      replay/             牌谱写入、小分导出、崩溃安全的文件刷新。
  tests/                  后端单元测试和集成测试。

frontend/
  public/                 前端开发/构建工具直接服务的静态文件。
  src/
    assets/               牌图、音效、字体和视觉资源。
    components/           可复用 UI 控件和牌桌组件。
    net/                  WebSocket 客户端、协议编解码。
    state/                由服务端快照派生出的客户端视图状态。
    views/                大厅、牌桌、牌谱回放、结果页。

protocol/                 版本化的客户端/服务端消息结构。
replay/                   本地开发期间导出的牌谱文件。
samples/                  示例牌谱、小分文件、协议测试样例。
docs/                     设计说明和规则文档。
deploy/                   部署配置。
scripts/                  构建、测试、导出和维护脚本。
```

## 后端分层

### Core

核心层不应依赖网络或文件系统代码。它应该是确定性的，并且可以通过命令行测试直接验证。

核心职责：

- 牌表示。
- 牌山生成和摸牌顺序。
- 基于局内状态的合法操作发现。
- 自定义胡牌检查和多人响应裁决。
- 小分结算，以及基于听牌状态的荒牌结算。
- 牌谱事件生成。

已实现的核心模块：

- `GameEngine`：检查碰、杠、自摸、荣和、听牌、多响配置等底层牌型规则。
- `PointEngine`：根据一局结果、鸡牌、杠分和听牌状态计算自定义小分变化。
- `RuleEngine`：结合当前 `RoundState`、游戏规则和计分规则，生成玩家合法操作、裁决多人响应，并计算最终小分结果。

### Server

服务层负责连接、房间、会话、牌桌生命周期和计时器。

核心职责：

- 接收 WebSocket 客户端。
- 创建和加入房间。
- 将临时玩家令牌绑定到座位。
- 将玩家命令路由到正确牌桌。
- 下发状态快照和事件广播。
- 处理断线、重连、心跳和超时行为。

### Storage

第一版保持基于文件的存储方式。

核心职责：

- 随游戏推进追加牌谱事件。
- 导出最终小分。
- 对完成的产物使用原子写入或临时文件重命名。
- 保留足够事件数据，用于回放或排查异常中断的牌局。

## 协议方向

第一版使用 JSON WebSocket 消息。这样在规则仍然变化时更容易调试。二进制协议可以等到有明确性能证据时再考虑。

客户端命令示例：

```json
{
  "type": "discard",
  "roomId": "room-001",
  "tableId": "table-001",
  "seq": 42,
  "tile": "5m"
}
```

服务端事件示例：

```json
{
  "type": "event",
  "tableId": "table-001",
  "seq": 43,
  "event": {
    "kind": "player_discarded",
    "seat": 2,
    "tile": "5m"
  }
}
```

服务端操作提示示例：

```json
{
  "type": "prompt",
  "tableId": "table-001",
  "seq": 44,
  "actions": [
    { "type": "pon", "tiles": ["5m", "5m"] },
    { "type": "win" },
    { "type": "pass" }
  ],
  "timeoutMs": 8000
}
```

## 牌谱格式方向

内部牌谱格式应使用追加式事件日志，并在最终写入元信息。

```json
{
  "version": 1,
  "rule": "custom-v1",
  "roomId": "room-001",
  "tableId": "table-001",
  "players": ["p0", "p1", "p2", "p3"],
  "initialWall": ["1m", "9p"],
  "events": [
    { "seq": 1, "type": "deal" },
    { "seq": 2, "type": "draw", "seat": 0, "tile": "5s" },
    { "seq": 3, "type": "discard", "seat": 0, "tile": "5s" }
  ],
  "finalScores": [31200, 21800, 25000, 22000]
}
```

内部牌谱格式应作为事实来源。如果后续需要接近天凤格式的导出，可以从内部牌谱转换生成。

## 初始里程碑

1. 实现合法操作、胡牌检查、响应优先级和计分等规则核心。当前自定义规则表面已完成。
2. 当规则细节变化时，持续扩展确定性测试。
3. 实现纯 C++ 的局/桌状态机，用于应用已验证操作并生成牌谱事件。
4. 添加 WebSocket 服务端以及房间、牌桌、会话生命周期。
5. 构建最小浏览器客户端，支持大厅、牌桌、操作提示和结果页。
6. 在牌局结束时导出牌谱 JSON 和小分文件。
7. 添加断线重连、超时托管和牌谱回放器。

## 后端测试

在仓库根目录配置并运行后端测试：

```powershell
cmake -S backend -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

当前测试可执行文件：

- `gymj_game_engine_tests`：底层牌型、胡牌和听牌检查。
- `gymj_rule_engine_tests`：从 `RoundState` 生成合法操作、摸牌缓冲处理、响应裁决和计分转交。
- `gymj_point_engine_tests`：自定义小分结算细节。

## 开发说明

- 从单进程服务端开始。200 人规模不需要分布式状态。
- 所有权威判定都放在服务端。
- 游戏状态转移使用事件驱动，并带序号。
- 在构建大量 UI 功能前，优先围绕规则引擎写确定性测试。
- 除非出现长期账号、排行榜、库存或统计分析等需求，否则暂不引入数据库。
