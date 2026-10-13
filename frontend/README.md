# Vue 牌桌原型

本地 Vue 3 + Vite 原型，使用示例数据，不建立 WebSocket 或请求后端。
保留原有通信调试页 `public/network-test.html`，与此原型独立。

## 启动

需要 Node.js 22.12+（或满足依赖要求的更新版本）。在 `frontend/` 下运行：

```powershell
npm install
npm run dev
```

打开终端显示的本地地址。构建使用 `npm run build`，产物在 `dist/`。

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

## 查看内容

- 填写昵称，进入原型，点击准备查看示例手牌。
- 点击手牌，再点击“打出选中的牌”，查看本地选牌和弃牌反馈。
- 使用“示例场景”切换碰/明杠/过、暗杠/加杠、自摸、荣和、等待及结算。
- 点击结算页“下一局准备”返回准备状态。
- “重置当前示例”恢复示例数据，“返回入口”回到昵称输入。

场景用于检查内容和按钮，手牌、操作和分数不保证构成符合自定义规则的完整牌局。
倒计时不运行，动作仅提供本地反馈；当前没有登录、网络、重连、真实计分或持久化。

## 文件

- `src/App.vue`：页面结构、入口和演示交互。
- `src/demo.js`：集中存放示例状态及牌面/操作中文名称。
- `src/components/`：座位信息、手牌选择、结算面板。
- `src/style.css`：基础可读布局。

后续接入服务端时，用 `table_update.view` 替换示例数据，并将按钮事件交给网络层。
