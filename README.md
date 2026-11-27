# <img src="assets/orbit-logo.svg" alt="" width="48" height="48" align="center"> Orbite

Orbite 是 Google Antigravity 的独立 macOS 客户端。它用 C++ 和 Qt 6 Widgets 提供项目、会话、审批和 Git 审阅。模型和工具执行由官方 Antigravity CLI 提供。

## 运行

[下载预发布版](https://github.com/Rseam-07/orbite/releases) · [发布说明](RELEASE.md)

- 本地构建的磁盘映像可将 **Orbite.app** 拖到「应用程序」。开发包使用临时签名。正式分发需要 Developer ID 签名和公证。
- 安装[官方 Antigravity CLI](https://antigravity.google/docs/cli-install)。Orbite 会在常见位置查找 `agy`，也可以在「设置」指定路径。
- 打开一个项目并发送消息。引擎通过 `agy acp --sandbox` 运行。需要登录时按应用提示进入官方 Google 登录流程，也可以先在终端运行 `agy` 登录。
- `⌘N` 新建会话，`⌘K` 搜索项目和会话，`⌘↩` 发送或补充，`⇧⌘R` 切换审阅。

## 使用

工作台以项目磁贴和最近会话为入口。新会话使用居中的输入区，工作时可展开右侧审阅。外观支持暖纸、夜蓝与跟随系统。引擎公布的思考强度会显示为可调整的档位。

**管理会话。** 侧栏按项目组织会话，支持搜索正文、重命名、置顶和归档。恢复已有本地正文的会话时，可用的 ACP 免重放续接会跳过重复历史。可从「项目」导入属于当前目录的 Antigravity CLI 会话，连接后读取服务端提供的历史。SQLite 保存对话、草稿、附件选择和队列。退出时会关闭任务进程，下次打开可以恢复会话。中断的回合会标明结果未确认。

**并行与排队。** 独立目录可以同时跑任务。重叠目录同一时间只运行一个回合。运行时可以补充纯文字，也可以把下一条文字及附件放入会话队列。本轮正常完成时会运行显式排队的消息。停止、断线或退出后，未送出的排队消息可以从界面手动运行或取回。

**附件。** 可拖入文件和文件夹，通过按钮选取，或粘贴剪贴板中的截图。发送前在输入框查看并移除。附件会复制到任务目录下的 `.orbit-attachments`，每次最多 16 项、64 MiB 和 1,000 个条目，跳过符号链接、版本库和常见凭据文件。该目录应加入项目的 Git 忽略规则。支持图像输入的引擎会直接接收大小合适的 PNG、JPEG 和 WebP。

**工作树与审阅。** 建连前勾选「独立工作树」可从 Git 项目的当前提交创建会话分支。审阅可查看 Git 工作区状态、会话工具报告的文件变化、计划和操作状态。可按文件名或路径筛选变更。工作区差异支持引用文件行、导出补丁或确认后应用到原项目。补丁包含未忽略的新文件和二进制变更。应用前会检查补丁以及受影响文件的本地改动，结果保存在目标工作区，由用户审阅和提交。工作树可以在文件管理器中打开。

**Computer Use。** 在「扩展」检测已安装的 [Cua Driver](https://cua.ai/docs/cua-driver/quickstart)，分别确认 MCP 工具、macOS 权限和驱动官方自测结果。需要在会话菜单单独启用，再恢复或连接会话。兼容驱动可在检测后填入只读联调指令。

## 迁移 0.7 的资料

Qt 版使用独立的 SQLite 资料库。原 0.7 客户端仍可通过 Git 历史构建。可在 0.7 的开发者工具控制台运行 `copy(localStorage.getItem('orbit02-local'))`，将复制的内容保存为 JSON，再通过「项目 → 导入 Orbite 0.7 资料」导入。导入后的项目目录首次运行前需要重新选择。原工作树记录经项目校验后可接续使用。

## 构建与打包

需要 CMake 3.21.1+、C++20 编译器和 Qt 6.8+（Widgets、Concurrent、Sql 与 SQLite 驱动，测试还需要 Qt Test）。

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.8.3/macos" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open build/Orbite.app
scripts/package-macos.sh "/path/to/Qt/6.8.3/macos" build dist
```

打包脚本用 `macdeployqt` 收集运行库，检查 Cocoa 和 SQLite 插件，输出带架构名的 DMG 与 SHA-256 文件。提供 `ORBITE_SIGN_IDENTITY` 和 `ORBITE_NOTARY_PROFILE` 可走 Developer ID 签名与公证流程。GitHub Actions 分别配置了 Apple Silicon 和 Intel 构建。配置 CMake 时传入 `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` 可构建通用包。

开发测试用独立目录和 ACP 子进程，覆盖协议、存储、Git 和 Qt 窗口交互。功能边界见[对照与后续](对照与后续.md)。开发说明见[贡献指南](CONTRIBUTING.md)。

Orbite 使用 [MIT 许可证](LICENSE)。打包附带 Qt Base 许可文本与对应源码链接。
