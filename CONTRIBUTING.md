# 贡献指南

用 [README](README.md#构建与打包) 的命令构建并运行测试。改动 ACP 或工作树时，优先通过真实子进程和临时 Git 仓库验证。

- `src/rpc.*` 负责 stdio JSON-RPC、帧大小、请求超时与断线处理。
- `src/acp.*` 负责 Antigravity ACP 会话、模型选择、审批、流式事件与历史导入。
- `src/controller.*` 负责界面与引擎间的会话生命周期、草稿队列、目录互斥和后台作业。
- `src/profile.*` 负责本机 SQLite 资料库。Qt 版可用 `--profile <绝对路径>` 单独验证。
- `src/workspace.*` 负责 Git 工作树、审阅、补丁及应用。打包入口在 `scripts/package-macos.sh`。
- `src/window.*` 和 `src/review_panel.*` 是 Qt Widgets 界面。UI 测试通过这些正常入口操作窗口。

提交 Issue 时请注明系统、架构、Qt 或发行版本、`agy --version` 和复现步骤。会话记录、附件和本地凭据留在本机。
