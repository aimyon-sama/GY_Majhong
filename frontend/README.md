# Vue 联机牌桌

Vue 3 + Vite 牌桌通过协议 v1 连接 C++ 服务端。手牌、牌河、合法操作及分数均来自
`table_update.view`，按钮只提交请求；收到服务端快照后才更新牌桌。
原有通信调试页 `public/network-test.html` 仍可独立使用。

## 启动

先在仓库根目录使用 Windows MSVC preset 构建并启动后端：

```powershell
Push-Location backend
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
Pop-Location
./backend/out/build/windows-msvc-debug/app/Debug/gymj_server.exe 8080 --action-timeout-ms 15000
```

另开终端，需要 Node.js 22.12+（或满足依赖要求的更新版本）。在 `frontend/` 下运行：

```powershell
npm install
npm run dev
```

打开终端显示的本地地址，服务地址填写 `ws://127.0.0.1:8080`，输入昵称后点击
“连接并进入”。构建使用 `npm run build`，产物在 `dist/`。

### Windows：`spawn cmd.exe EACCES`

这个错误发生在 npm 启动脚本命令解释器时，Vite 尚未启动。
在 `frontend/` 目录直接用 Node 启动 Vite，可以跳过 npm 对 `cmd.exe` 的调用：

```powershell
node ./node_modules/vite/bin/vite.js --host 127.0.0.1
```

如果已安装 PowerShell 7，也可以仅为这一次 npm 命令指定 PowerShell：

```powershell
npm --script-shell=pwsh run dev
```

这些命令不修改系统权限或全局 npm 配置。具体拒绝原因仍需结合运行终端的权限和系统策略判断。

## 四人联调与恢复身份

- 新建四个标签页，分别在地址栏输入前端地址。不要复制已入座标签页：浏览器可能复制
  `sessionStorage`，导致两个页面恢复同一身份。也可使用不同浏览器配置文件。
- 四人连接、入座并准备后，服务端自动开局；第五人会收到房间已满提示。
- 点击手牌后出牌，碰、杠、胡、过只在服务端允许时显示。等待确认和同步期间禁用操作。
- 每个标签页按服务地址在 `sessionStorage` 中保存临时凭据。刷新自动尝试恢复；断线后点击
  “重新连接”。恢复时等待身份确认和新快照，不重放此前点击。
- “断开连接”保留座位，由 AI 持续代管并自动准备下一局。同一身份被另一页面接管时，
  旧页面停止连接，不自动争抢身份。
- 任意阶段可点击“离开并交给 AI”。服务端确认后清空本地牌桌，进入“AI 代管中”；
  身份、原座位、累计分和连接保留，其他玩家不能补位。点击“返回原座位”即可接回控制。
- 原桌仍有真人时，离开后刷新或重连会恢复原房间原座位及最新牌局，可能已进入之后的小局。
  返回不重置操作期限，也不重放旧动作。
- AI 在等待准备和每局结算后自动准备，在线玩家仍需手动准备；四个座位都准备好就开局。
  至少一名真人留在桌上时才继续；最后一名真人离开或断线后立即关闭本局，停止 AI。
- 全员离开后，旧身份令牌失效，无法恢复原桌。仍打开的页面收到关局提示并清除凭据；
  已断线页面下次恢复会收到身份失效提示。重新连接可创建新身份开始新对局。
- 当前代打策略沿用超时摸切、碰后切末张和响应过牌；保留原操作期限。
  本阶段不增加限制玩家另开对局的身份规则。满房且从未入座的身份仍可重试加入。
- 服务重启使原身份失效；页面会清除失效凭据并提示重新进入。若浏览器禁止存储，当前页面
  仍能重连，但刷新后可能无法恢复。
- 倒计时仅用于显示，最终期限由服务端判断。小局结束后显示服务端结算，真人准备与 AI 自动准备共同开始下一局。

连接、认证或在途请求超过 10 秒无响应时停止等待，可手动重新连接同步状态。

## 测试

在 `frontend/` 运行 `npm test` 验证身份恢复、快照同步、重复点击保护、请求编号、超时和离桌。
也可直接运行 `node --test tests/*.test.js`，无需额外测试依赖。

在仓库根目录运行真实服务端联调（脚本自行启动并关闭独立服务器）：

```powershell
node scripts/test-frontend.mjs backend/out/build/windows-msvc-debug/app/Debug/gymj_server.exe
```

该脚本使用正式前端网络模块，验证四人开局、第五人拒绝、出牌、刷新恢复、流局结算、
主动交给 AI、第五人无法补位、离开后刷新恢复原座位，以及 AI 自动准备下一局。
它不操作浏览器 DOM；页面点击与布局仍需浏览器验收。

## 文件

- `src/App.vue`：连接表单、快照渲染、操作提交、座位视角和倒计时。
- `src/net/game-client.js`：WebSocket 生命周期、身份存储、协议请求与快照同步。
- `src/tile-labels.js`：牌面和操作中文名称。
- `src/demo.js`：保留的静态示例数据，联机页面不使用。
- `src/components/`：座位信息、手牌选择、结算面板。
- `src/style.css`：基础可读布局。

客户端使用十进制字符串传输编号和分数，编号比较与递增使用 `BigInt`；忽略旧快照。
动作回执后主动请求完整快照，避免拒绝或重复请求没有广播时一直锁住按钮。
