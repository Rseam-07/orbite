# Orbite 0.8.0-beta.1

首个 Qt 预发布版。Orbite 使用 C++ 和 Qt 6 Widgets，账号、模型和执行由官方 Antigravity CLI 提供。

## 下载与安装

从 GitHub Release 下载 macOS 通用 DMG，将 Orbite 拖到「应用程序」。通用包包含 Apple Silicon 与 Intel 两种架构，部署目标为 macOS 12 起。安装 [官方 Antigravity CLI](https://antigravity.google/docs/cli-install) 后，用 `agy --version` 确认 CLI 可用。首次连接使用官方 Google 登录流程，或复用已登录的 CLI 会话。独立工作树和 Git 审阅需要 Git。

预发布包使用 ad-hoc 签名。Apple Developer ID 签名、公证和自动更新仍待配置。安装包附带 SHA-256 文件和 Qt 许可说明。

## 这一版有什么

- 工作台按项目和最近会话组织任务。新会话有居中输入区，支持暖纸、夜蓝和跟随系统。
- 会话支持并行执行、审批、停止、运行中补充、队列和 CLI 历史导入。本地保存会话及草稿。恢复已有正文的会话时优先使用 ACP 的免重放续接。上轮中断会显示结果未确认。
- 支持拖入或选取文件和文件夹、粘贴截图。准备附件时可取消并清理未完成的副本。待用截图随草稿保存在本机。
- 审阅支持按文件名和路径筛选。工作树可导出含新文件与二进制改动的补丁，确认后可应用到原项目。应用前检查受影响的本地文件。
- Computer Use 为 macOS 实验功能。驱动检测、自测和 Antigravity 会话联调分别验证。兼容驱动可填入只读检查指令并由用户发送。

## Dot 开发进度对照

| 进度 | Qt 预发布版的处理 |
|---|---|
| 已合入 [#1–#9](https://github.com/Rseam-07/orbite/pulls?q=is%3Apr+is%3Amerged) | 保留截图粘贴、附件复制与取消、从会话续接、断线后控件同步、斜杠命令键盘选择、工作树与初次提交前审阅、刷新保留文件选择。长会话先渲染近期消息，更早的内容按需加载。 |
| [#10](https://github.com/Rseam-07/orbite/pull/10) | 读取 MCP 工具声明以判断是否可使用只读权限检查。扩展面板区分本地自测与会话联调。 |
| [#11](https://github.com/Rseam-07/orbite/pull/11)、[#12](https://github.com/Rseam-07/orbite/pull/12)、[#15](https://github.com/Rseam-07/orbite/pull/15) | Qt 工作台有项目与最近会话层级。草稿按会话隔离，切换项目或重启可恢复，未确认的发送保留在原对话中。 |
| [#16](https://github.com/Rseam-07/orbite/pull/16) | 工作区与会话变更各自保留筛选文字和文件选择，筛选无结果时清空旧补丁。 |
| [#13](https://github.com/Rseam-07/orbite/pull/13)、[#14](https://github.com/Rseam-07/orbite/pull/14) | 0.7 候选提交 `f9ea06e` 已完成四个平台候选构建与启动检查，复跑 41 项旧版测试通过。此次公开资产为 Qt 的 macOS 通用包。 |

[Dot 的 0.7 候选构建](https://github.com/Rseam-07/orbite/actions/runs/45940522812)

## 验收范围

应用自动测试覆盖 ACP 协议、附件、SQLite、Git 与 Qt 窗口，包括审批、新文件审阅和补丁交付。真实 Antigravity CLI 用于消息、历史加载和免重放续接联调。安装包在 macOS 15.8.1 上检查两种架构，Intel 路径使用 Rosetta。Computer Use 的实机桌面动作待验收。

0.7 的 Electron 资料可通过 JSON 导入。步骤见[使用说明](README.md#迁移-07-的资料)。
