# Orbite 首发候选版（0.7.0）

**拯救反重力，让它像 Codex 一样好用。** Orbite 是独立的 Google Antigravity 桌面客户端，不是 Google 官方产品。用自己的 Google 账号通过官方 Antigravity CLI 登录；不需要 Codex 或 Claude Code 订阅。

| 平台 | 首发目标 | 运行方式 |
| --- | --- | --- |
| macOS | Apple Silicon / Intel，分开下载 | 解压 ZIP 后打开 Orbite.app |
| Windows | x64 | 完整解压 ZIP，打开 Orbite.exe；不要只提取 EXE |
| Linux | Debian / Ubuntu x64 | 用系统安装工具安装 .deb，从应用菜单启动 Orbite |

macOS 和 Windows 首发采用便携包，Linux 提供 .deb。这不是下载列表。只有实际生成、验收并上传到 GitHub Release 的文件才是发布资产。Apple 开发者签名和公证、Windows 发布者签名、自动更新均尚未配置；不要为了启动而关闭系统安全保护。如果候选包被组织策略拦截，请记录具体提示。每份包会生成单独的 SHA-256 文件。

## 内容与范围

- 工作台、流式会话、审批、并行任务，以及 Git 审阅和独立工作树。
- 文件和文件夹拖入或点「＋」选取；截图粘贴保留在本次草稿，发送时才处理。常见凭据、符号链接等会跳过，但不是完整隐私扫描。
- 安装 [官方 Antigravity CLI](https://antigravity.google/docs/getting-started?tab=cli) 后先用终端运行 `agy --version`；Windows 会查找 `agy.exe`，包括 Google 安装器默认目录。若未登录，按官方 CLI 提示登录。使用 Git 审阅或独立工作树需安装 Git。
- **Computer Use 仅为 macOS 实验功能**。Windows 和 Linux 不提供桌面控制。Cua Driver 不随包发布，独立自测不代表 Google 会话已接通。
- 0.7.0 并非 Codex 全功能替代；不自动发送草稿、批准审批或修改全局 MCP 配置。

## 维护者验收

1. 候选提交必须通过四个原生构建任务（macOS arm64/x64、Windows x64、Linux x64）。每个任务跑自动测试，从候选 ZIP 解压启动，或安装 Linux 的候选 .deb 后启动，验证正式窗口和 Electron 桥接就绪；它**不**验证 Google 登录、权限和真实项目操作。
2. 从每个平台对应下载包在干净系统上检查启动（双击 / 解压运行）、选择含空格或中文的项目、拖入文件和目录、粘贴截图、官方 Google 登录或复用会话、收一轮回复、停止和 Git 审阅。Mac 的 Computer Use 按 README 另行验收，未通过只能作为明确标注的实验功能。
3. 使用通过上述门槛的同一个提交，准备 GitHub 预发行版和所有平台资产及 SHA-256；核对文件名、签名状态、校验值与版本号，再公开。工作流本身没有发布权限，不会自动打标签或发布。
