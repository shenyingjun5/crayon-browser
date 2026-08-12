# RNM：Crayon Browser 命名迁移 Roadmap

状态：`RNM-01..04 DONE`，`RNM-05 READY`。本 Roadmap 将历史仓库名 `get-video` 迁移为产品级名称 `crayon-browser`，并保持 Windows/macOS CEF、Rust workspace、文档、GitHub 与本地工作区命名一致。命名迁移优先于恢复 `CEF-03`，但不得改写历史执行证据或扩大产品能力。

## 冻结命名

| 层级 | 正式名称 | 说明 |
|---|---|---|
| 中文正式产品名 | 蜡笔 AI Agent 投屏浏览器 | 面向用户、PRD、安装界面和品牌文档 |
| 中文简称 | 蜡笔浏览器 | 空间受限的用户界面 |
| 英文产品名 | Crayon Browser | GitHub README、构建产物和英文品牌文案 |
| 英文定位 | AI Agent & Cast Browser | 描述，不进入 package/binary 名称 |
| GitHub 仓库 | `shenyingjun5/crayon-browser` | 从 `shenyingjun5/get-video` 原地改名 |
| 本地目录 | `D:\crayon-browser` | 最后一步执行，避免中途破坏构建缓存和工作区 |
| Rust 正式根 package | `crayon-browser-core` | 取代历史 `get-video` package |
| Rust 正式 library | `crayon_browser_core` | 取代历史 `get_video` crate path |
| 历史 CLI binary | `crayon-legacy-video-tool` | 仅 `legacy-dev`，不作为正式产品 CLI |
| 历史 Tauri package | `crayon-legacy-app` | 明确为迁移/兼容工具 |
| Windows 产物 | `CrayonBrowser.exe` / `CrayonBrowser.dll` | 已符合规范，保持稳定 |
| macOS 产物 | `CrayonBrowser.app` | 已符合规范，保持稳定 |
| macOS Bundle ID | `com.crayon.browser` | 已符合规范，保持稳定 |

## 兼容与历史规则

- 活动代码、构建脚本、根 README、当前契约和新命令统一使用新名称。
- 已完成 Roadmap、Review 和测试证据中的历史命令（例如 `cargo test -p get-video`）保持原文，并在本 Roadmap 说明其为改名前证据；不得伪造为新名称下重新运行。
- 不提供长期 `get-video`/`get_video` 正式别名。历史 Tauri/提取器只允许在明确的 legacy package/binary 中继续存在，完成迁移后活动代码不得依赖旧名称。
- GitHub 仓库原地改名后立即更新 `origin` 并回读；不依赖 GitHub 的旧 URL 重定向作为长期配置。
- 根 README 遵循 GitHub 仓库主页常见结构：项目定位、状态/边界、功能、架构、平台、快速开始、目录、文档、开发/测试、贡献、安全、许可证；不得写虚假完成度、虚假兼容或不存在的发布包。
- 当前没有仓库级 `LICENSE`，README 必须明确“尚未授予开源许可证”，不得因仓库公开就声称已经开源。许可证选择另立任务，不能在本迁移中擅自决定。

## 原子任务

| ID | 状态 | 依赖 | 允许修改 | 单一交付 | 验证/完成门禁 |
|---|---|---|---|---|---|
| RNM-01 | DONE | CEF-02W | 本 Roadmap、Roadmap/current 索引 | 冻结命名矩阵、兼容边界和执行顺序；释放暂停中的 CEF-03 | 文档一致性、`git diff --check`、Review |
| RNM-02 | DONE | RNM-01 | 根/app/demo Cargo manifests/locks、Rust imports、检查脚本、repo-guard 契约 | `get-video/get_video` 迁移为正式/legacy 新 package、lib、binary 名 | `cargo metadata`、新 package tests、legacy tests、repo-guard tests |
| RNM-03 | DONE | RNM-02 | 根 README、活动配置/UI 文案、current/活跃 Roadmap、AGENTS | 按 GitHub 规范重写 README，并清除活动产品文案中的旧名/旧进度 | 链接检查、locale/config contract、旧名 allowlist 扫描 |
| RNM-04 | DONE | RNM-03 | CI、CMake/Cargo 入口、文档命令 | 新名称下完成全仓库与 Windows CEF 回归 | format/lint/unit/integration/fast/security；CEF Debug/Release build+ctest |
| RNM-05 | READY | RNM-04 | RNM Roadmap/current 索引 | 独立 Code Review、记录证据并完成本地代码命名迁移 | P0/P1=0；P2 有 owner；工作区干净 |
| RNM-06 | TODO | RNM-05 | GitHub repo settings、local `origin` | GitHub 仓库原地改名为 `crayon-browser`，同步 description/topics 和远程地址 | GitHub API readback、`git ls-remote origin`、默认分支 main |
| RNM-07 | TODO | RNM-06 | GitHub `main` | 推送最终 README/代码并核对 GitHub 首页渲染源 | local/remote SHA 一致、README 为默认分支根文件 |
| RNM-08 | TODO | RNM-07 | 本地工作区父目录 | 将 `D:\get-video` 原子改名为 `D:\crayon-browser` 并重建含绝对路径的 CMake cache | 新路径 git status/remote、旧目录不存在、CEF configure smoke |

## 禁止项

- 不改 Cast-SDK 外部仓库名称、gitlink revision、公开 facade 或接收端协议。
- 不顺带实现 `CEF-03`、浏览器 UI、投屏、Agent、模型或新依赖。
- 不批量重写归档和历史证据中的旧命令；活动/历史判定必须可审查。
- 不删除旧目录后重建；本地目录只允许在所有提交与远程回读完成后进行单次、已验证的同卷重命名。
- 不创建 GitHub Release、Tag、PR 或安装包发布；本次 GitHub 写操作只含仓库改名、元数据、main 推送与 README 同步。

## RNM-01 完成记录（2026-08-12）

- 冻结上述中文/英文产品名、仓库/目录、Rust package/library/legacy binary 和既有 CEF 产物矩阵；明确历史证据不批量改写、无长期旧名别名、无许可证不得宣称开源。
- `CEF-03` checkpoint 保持在 Git 中并退回 `READY`，命名迁移完成后从状态模型单测恢复；没有改动 CEF、Cast-SDK、协议或产品行为。
- 验证：Roadmap/current 状态检索一致；`git diff --check` 通过。
- Code Review：需求/边界、架构、历史证据、GitHub/本地迁移顺序与安全性复核，P0/P1/P2/P3 均为 `0`。

## RNM-02 完成记录（2026-08-12）

- 根 package/library/legacy binary 已迁移为 `crayon-browser-core`、`crayon_browser_core`、`crayon-legacy-video-tool`；被排除的 Tauri app/demo 迁移为 `crayon-legacy-app`/`crayon-legacy-demo`。Rust imports、root/app lockfile、PowerShell/Bash 检查入口、legacy matrix 和 repo-guard 正式根/legacy owner 契约同步更新；旧 `GET_VIDEO_*` 调试环境变量改为 `CRAYON_LEGACY_*`。
- 验证：root/app/demo `cargo metadata --offline --no-deps` 均识别新 package 与依赖；`cargo test -p crayon-browser-core --lib` 3/3、legacy lib 58/58、`legacy_contract` 9/9、`cargo test -p repo-guard` 24/24 通过；`cargo check --offline --manifest-path app/Cargo.toml` 通过；`cargo fmt --all -- --check` 通过。
- 失败/未覆盖：首个组合命令在 120 秒超时，拆分后核心验证全部通过；demo 独立首次编译在 240 秒超时且 Cargo/rustc 仍运行，已只终止本次明确 PID，未取得 demo compile 通过证据。`demo` 为排除的历史工具且 app 同一依赖链已通过，不阻塞 package 命名契约，但 RNM-04 完整回归前应在缓存就绪后重跑。Tauri 生成的未跟踪 schema/demo lock 已清理，没有进入提交。
- Code Review：package/target 唯一性、feature 门禁、legacy adapter owner、lockfile、脚本路径、环境变量和机械 import 复核；P0/P1/P2/P3 均为 `0`。产品 UI 文案和活动文档中的旧名称明确留给 `RNM-03`，没有混入本任务。

## RNM-03 完成记录（2026-08-12）

- 根 README 已按 GitHub 项目首页结构重写，包含准确的开发状态、产品边界、能力状态、架构、平台、快速开始、目录、贡献、安全和许可证说明；没有虚构安装包、兼容性、完成度或开源授权。
- 正式 locale、产品默认配置和历史 Tauri app/demo 的活动标题统一为 Crayon 品牌；legacy 工具继续明确标注为历史开发入口。FND Review 中的旧 package/命令保留原始证据，并增加历史名称说明。
- 验证：README 的 9 个相对链接与品牌图标路径均存在；4 份 JSON 配置通过 `ConvertFrom-Json`；`cargo test -p crayon-app-runtime --test config_locales` 2/2、`cargo test -p crayon-domain --test v1_config` 10/10、`scripts/check.ps1 brand-assets` 通过（8 项资产检查、3 项安全单测）；`git diff --check` 通过。
- 旧名 allowlist：活动源码、配置、UI 和根 README 已无旧名；剩余命中只存在于本命名迁移说明、`docs/current/fnd-migration-review.md` 的带说明历史证据，以及旧 PRD/Roadmap/测试/设计文档的历史事实，未批量篡改。
- Code Review：产品事实、链接、配置 schema、locale parity、legacy 隔离、历史证据和许可证陈述复核；P0/P1/P2/P3 均为 `0`。

## RNM-04 完成记录（2026-08-12）

- `scripts/check.ps1 fast` 通过：repo-guard、format、品牌资源、正式 workspace 全测试和 legacy lib 58 项均成功；总耗时约 86 秒。repo-guard 保留既有文件/函数规模和 legacy 配置字面量 warning，没有 failure。
- `scripts/check.ps1 security` 通过：repo-guard、relay 单元 3 项、relay security corpus 7 项全部成功；总耗时约 24 秒。
- `cargo clippy --workspace --all-targets --offline -- -D warnings` 通过；`cargo check --offline --manifest-path app/Cargo.toml` 与 `demo/Cargo.toml` 均通过。RNM-02 中 demo 首编译超时的不确定性已关闭；Tauri 自动生成的未跟踪 schema/lock 未进入提交。
- Windows CEF：使用已校验的 CEF 150.0.10 固定包重新 configure；Debug、Release 均构建成功，`CrayonBrowser.dll` 与 contract targets 生成；Debug/Release `ctest` 各 8/8 通过。configure 仅有既存的“ATL is not supported”非阻塞 warning。
- 未覆盖：本任务没有 macOS 实机/双架构证据，也没有真实接收端设备门禁；两者分别保留给 `CEF-02M` 与 `SDK-13`，不属于 Windows 命名回归。
