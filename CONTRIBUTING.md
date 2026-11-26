# 参与 Orbite 开发

1. 克隆仓库，在项目根目录运行 `npm ci`，用 `npm start` 启动桌面版。
2. 从 `main` 创建主题分支；一次 Pull Request 聚焦一个改动。
3. 提交前运行 `npm test`。如有 UI 改动，运行 `node build-preview.cjs`，并在 PR 附截图或短录屏，注明测试设备及系统版本。
4. 请在 PR 说明改了什么、如何复现、哪些路径尚未验证。桌面输入、录屏或 Google 登录测试需要本机权限；未执行时请如实注明。

`core.cjs` 和 `transport.cjs` 处理 ACP，`main.cjs` / `preload.cjs` 是 Electron 桥接，`ui.js` / `motion.js` / `style.css` 是界面，`test/` 是可离线运行的测试。电脑控制以用户授权为前提，不在测试里请求真实桌面权限。

不要提交账户信息、令牌、私有项目路径、附带敏感数据的日志、附件副本或生成的 HTML 预览。截图和录屏也请先脱敏。
