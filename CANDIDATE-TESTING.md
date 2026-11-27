# Orbite 候选包怎么验收

这份清单给愿意帮忙试装的贡献者。**GitHub Actions 的候选包不是公开 Release。** 用候选 PR 指向的那次构建，不要混用不同构建的下载。0.7.0 第二批对应 [候选 PR #22](https://github.com/Rseam-07/orbite/pull/22) 和 [Artifacts 下载](https://github.com/Rseam-07/orbite/actions/runs/45963013543#artifacts)，包含两项附件修复，替代 #14 的测试包。下载需登录 GitHub。

## 选包、校验、启动

| 平台 | Artifacts 名称 | 如何确认能启动 |
| --- | --- | --- |
| Mac（Apple Silicon） | `Orbite-darwin-arm64` | 解压应用 ZIP，打开 Orbite.app |
| Mac（Intel） | `Orbite-darwin-x64` | 解压应用 ZIP，打开 Orbite.app |
| Windows x64 | `Orbite-win32-x64` | 完整解压应用 ZIP，再打开 Orbite.exe |
| Linux x64 | `Orbite-linux-x64` | 用系统安装工具安装 .deb，再从应用菜单打开 Orbite |

先解开 Actions 的外层下载 ZIP。对照其中 `.sha256` 文件，校验**内层应用 ZIP 或 .deb**，不是外层下载包。macOS 可用 `shasum -a 256`，Windows PowerShell 可用 `Get-FileHash -Algorithm SHA256`，Linux 可用 `sha256sum`，再加实际文件名。

候选没有 Apple 开发者签名／公证、Windows 发布者签名或自动更新。如果系统或组织策略阻止启动，记录提示，不要关闭系统安全保护。第二批的启动检查在 macOS 15（两种架构）、Windows Server 2025、Ubuntu 24.04（X11）完成。macOS 13 是最低系统要求，未在 13 实测；Windows 10/11 桌面、Debian 与原生 Wayland 未分别验证。

## 最小附件和登录验收

使用一个临时项目和自己生成的无敏感内容文件。不要拿工作仓库、真实截图、凭据或私人文件来试。安装官方 Antigravity CLI；要测试 Git 功能再准备一个本地 Git 仓库。连接如果需要 Google 登录，由官方 CLI 处理，只在自己愿意使用的测试环境完成，**不要把密码、验证码或令牌贴到 Orbite、Issue 或测试报告**。

1. 创建名为「Orbite 验收」的临时项目，再创建一个**位于项目外**的「附件样本」文件夹。放入一个 `note.txt`，内容只有 `Orbite folder smoke test`。另建一个独立文件 `extra.txt`，内容只有 `Orbite file smoke test`。
2. 打开项目，发送「只回复 A」建立会话 A，等回合结束。若此时需要登录，先完成官方登录，回到 Orbite 后核对草稿再发送。再回工作台。尝试将「附件样本」从系统文件管理器拖到工作台；应提示先打开目标会话。回到 A，附件栏应为空。
3. 在同一项目点「新建会话」，把它作为 B。把「附件样本」拖入页面，再从「＋ → 添加文件」选取 `extra.txt`。发送前应只在 B 看到两个附件，A 仍为空。系统拖放与「＋」选择是不同入口，结果分开记。
4. 在 B 发：`只读取我附上的两个示例文本，各原样返回一行，不要修改文件。` 如需登录，按官方 CLI 指引自行完成。登录后核对附件仍在 B，必要时再点一次发送；不要认为 OAuth 完成就代表消息已经发出。只有收到两行预期文字、回合正常结束、两个原件不变，才算这轮通过。
5. 可选：在新的空草稿一次选择 17 个生成的小文件，应显示整批未添加；移除一个后重新选 16 个，应都出现。还可用纯文本另开一轮测试停止，或者在临时 Git 仓库手工改一行，检查「审阅 → 工作区状态」。**Computer Use 仅为 macOS 实验功能，不属于本轮，不需开启系统权限。**

## 怎么反馈

写下候选 PR 或运行链接、系统和架构、`agy --version` 的版本号。将「应用启动」「真实 Google 登录或已有会话复用」「系统文件管理器拖放」「＋ 选文件」「一轮回复」「原件不变」分别标注为**通过／失败／未测**。模拟 ACP、浏览器演示和 CI 启动不能填成真实 Google 登录或系统文件管理器拖放。失败时只贴脱敏的提示文字和复现步骤，不上传原始日志、账号资料或本地路径。
