# REL 第一期三大闭环发布 Roadmap

- 版本：`release-v1-scope`
- 日期：2026-09-02
- 状态：`REL-01/02/05 DONE`；`REL-03/04 TODO`
- 任务数：5
- 当前发布候选：Windows 10/11 x64 首发；已有 macOS arm64 共享实现/证据保留，签名、公证、Keychain、平台生命周期与打包等 macOS 特有门禁后续独立验证，不阻塞 Windows 候选
- 第一期开关：网页 Markdown、本地 Markdown Runtime、LAN Direct/Relay 与外部客户端交接开启；Agent/CLI/MCP、Workflow、Capability Hub、Partner Cast、模型与 HarmonyOS 关闭
- 第一期语言：同一候选包支持 `en-US/zh-CN/zh-TW`，按用户首选系统 UI 语言在完整重启时自动选择；Windows `LOC-07W` 是 `REL-03` 硬门禁

## 1. 一期完成口径

一期只在发布包内形成三个用户闭环：

1. 真实网页经 Browser 验证的 snapshot 管线生成确定性 Markdown，并可预览、复制和保存。
2. 真实网页中经用户输入与播放推进验证的媒体可选择局域网接收端，完成 Direct/Relay、控制与停止；不满足条件时显式拒绝或经用户确认交接外部客户端。
3. 用户可打开本地 `.md`，使用源码/预览/分栏、编辑工具栏与离线 P0 Runtime，并以原子写和外部修改冲突保护保存。

完成不以模型类、Fake 或独立 Harness 存在为准。三个闭环都必须从真实 Windows x64 CEF 产品入口进入，在同一个候选包的 `en-US/zh-CN/zh-TW` 系统 UI 语言下取得自动化、ADB 接收端实机、性能、安全、长稳、安装/升级/回滚证据，最终经 `QAR-16W` Go/NoGo。macOS 已有共享实现和 arm64 证据不得回退，但 macOS 特有门禁不作为本阶段 Windows 候选的依赖。

## 2. 一期明确边界

- 投屏只支持 LAN `Direct/Relay`；无路由只允许 `ExternalClientHandoff`，不实现 WebRTC、标签页/窗口/系统音频采集、编码或视频下载。
- DRM/EME、需要密钥的媒体、加密 HLS、凭证绑定且不可安全路由的来源不得产生 Direct/Relay。
- 网页 Markdown 不包含隐藏/跨源正文、Cookie、Authorization、DOM/HTML/CDP 句柄；页面脚本不能触发预览、复制或文件写入。
- 本地 MDV 仍是用户能力，不进入 CAAP/tool registry，不开放任意文件系统。
- 一期语言只跟随用户首选系统 UI 语言，不提供手动 override 或运行中热切换；系统语言变化在完整重启后生效，不在支持集合内的语言回退 `en-US`，完整系统偏好列表不得直接进入 `Accept-Language`。
- macOS 第一期支持 Apple Silicon。原生 Intel Mac/x64 长稳在取得硬件前标记 `NOT_IN_RELEASE`，不得把 Rosetta 证据写成原生支持；若产品决定宣称 Universal/Intel 支持，QAR-10/15/16 必须追加原生 x64 门禁。
- Windows 候选不得宣称 macOS 已发布；macOS 的签名/公证、Keychain、原生生命周期、安装/升级/回滚和最终 Go/NoGo 保持 `TODO/NOT_IN_RELEASE`，后续补证时不能改写 Windows 已有证据。

## 3. 第二期统一后移

- `AGT-12C/13/14/16` 与其他 CLI/入站 MCP、R2/R3/R4 Agent 能力。
- `WFL-01..16` Challenge、Workflow、Site Skill 与受控修复。
- `HUB-07..16` Capability Hub 后续能力与 Partner connector。
- `CNT-11..16` provider、文档总结和视频总结。
- `MRT-10..19` TOC/Search、ECharts、Graphviz、Presentation、TV/Cast 与 AI Source Producer；`MRT-09` P0 总 Review 留在一期。
- `SDK-15/16` Partner/TV Cast，`HM-01..12` HarmonyOS 技术预览。

这些能力保持默认关闭并在 QAR-15 记为 `NOT_IN_RELEASE`。其未完成不得阻塞已达标的三大核心闭环发布。

## 4. 原子任务

| ID | 状态 | 依赖 | 单一交付目标 | 验收 |
|---|---|---|---|---|
| REL-01 | DONE | 用户范围决策、current/模块 Roadmap | 冻结一期三大闭环、平台顺序、第二期边界，并拆除核心 QAR 对 Agent 的错误硬依赖 | Roadmap/索引一致；任务数一致；Review P0/P1=0 |
| REL-02 | DONE | REL-01 | 对 CEF 产品调用图做一次只读装配审计，列出只有模型/测试而无生产调用方的 CNT、Cast、MDV 与浏览器基础模块，并冻结一期 feature flag 默认值 | 入口→owner→adapter→平台调用图；无“DONE 即已装配”推断；无生产改动 |
| REL-03 | TODO | CNT-21W,PLT-W05,MDV-20W,MDV-25W,MRT-09W,LOC-07W,PRV-13AW | 聚合 Windows x64 三闭环与三语言真实产品证据和支持矩阵，关闭 Windows 可关闭的 VERIFIED 状态 | 三闭环真实 CEF；三语言系统跟随/资源闭包；ADB 正式接收端；P0/P1=0；macOS 特有门禁明确 NOT_IN_RELEASE |
| REL-04 | TODO | REL-03,PLT-19W,QAR-01W/02AW/03W/04W/05AW/06W/07W/08AW/09/11W/12W/14W/15W | 聚合 Windows x64 发布门禁与一期已知限制，向 `QAR-16W` 提交 Go/NoGo 输入并形成可发布候选 | Windows 证据可追踪；关闭功能默认 off；artifact/SBOM/回滚 Runbook |
| REL-05 | DONE | 用户 2026-08-31 平台顺序决策、当前 PRD §7、REL-02 | 将一期收口顺序改为 Windows x64 首发候选，拆出 Windows 原子装配/发布门禁并后置 macOS 特有验证 | 不改生产行为；Roadmap/索引/总数一致；Review P0/P1=0 |

## REL-01 完成记录（2026-08-30）

- 决策：第一期只交付网页 Markdown、网页媒体 Direct/Relay 投屏、本地 Markdown 编辑三大闭环；开发顺序 macOS arm64 → Windows x64，二者均属一期功能范围。
- Roadmap：CNT 新增真实 CEF/产品导出装配 `CNT-17..21`；PLT-M05b 拆为观察、策略、执行、Direct、Relay、拒绝/交接六个切片；PRV-13 拆为一期核心与二期扩展；QAR-02/05/08 拆为核心 A 与二期 Agent B。
- 状态纠正：总 Roadmap 的 `CNT-10 READY` 更新为实际 `DONE`；`PLT-M05a` 的既有完成记录只作为 new-tab/基础壳证据，不再被解释为 CNT/Cast 全产品装配完成。
- Code Review：按 v0.8 复核范围、依赖、状态真实性、平台证据和发布门禁；未改变生产代码、既有测试结论或历史完成记录，P0/P1/P2=0/0/0。
- 未覆盖：REL-02 尚未输出完整生产调用图；具体实现、真机和发布证据由各模块原子任务与 QAR 取得。

## REL-02 原子范围（CEF 生产调用图与一期 feature flag 审计）

- 状态：`DONE`；依赖 `REL-01 DONE`。
- 单一目标：以只读代码审计确认一期三大闭环从用户入口到 owner/domain/adapter/platform 的真实生产调用关系，列出仅被测试或自身模块引用、尚无产品调用方的节点，并冻结第一期/第二期 feature 默认开关；不实现或修复任何生产能力。
- 输入：`browser/cef-shell/**`、`browser/shared-ui/**`、`crates/crayon-{app-runtime,page-data,content-*,cast-adapter,relay,agent-*}/**`、CMake/Cargo 生产依赖图、当前 REL/CNT/PLT/MDV/MRT/AGT Roadmap。
- 输出与允许修改：`docs/current/**` 新增或更新一期生产装配图、`docs/plans/release-v1-roadmap.md` 的发现和后续任务映射、必要的模块 Roadmap 状态纠正；禁止修改生产代码、测试、schema/golden、依赖或构建配置。
- 审计方法：入口/handler 注册、构造与 owner、事件/IPC、domain use case、platform/SDK/文件 IO 五段逐段提供符号与调用方证据；类/函数存在、测试通过或 Roadmap DONE 不能替代生产 reachability。
- 验收：网页 Markdown、媒体投屏、MDV 各有一张最小调用图；每个断点映射唯一后续任务 ID；Agent/Workflow/Hub/Partner/model/Harmony feature 在 Release 装配中默认关闭且无远程监听/provider；`rg`/CMake/Cargo 证据命令和未覆盖项实际记录；文档链接/任务状态/计数校验通过，Review P0/P1=0。
- 明确不做：不新增临时 feature flag 实现、不接 CEF/SDK/文件 IO、不运行公网或真机、不因发现缺口顺手修复；缺陷进入对应原子任务。

## REL-02 完成记录（2026-08-30）

- 交付：新增 [第一期生产装配审计](../current/release-v1-assembly.md)，以 macOS `crayon_browser` target 的 sources/link allowlist 为根，分别给出网页 Markdown、LAN 投屏、本地 MDV 的入口→owner→adapter→平台/SDK/文件 IO 调用图。
- 真实状态：网页 Markdown 和 LAN 投屏均为 `NOT_REACHABLE`——collector/gateway、observer/gate、page-tools/cast-view 与 Rust app-runtime 只存在于独立库/测试，均未进入 `CrayonBrowser.app`；本地 MDV 为 `REACHABLE_WITH_GAP`，真实文件入口/编辑/保存链已接通，但生产 App 仍以 `BuildFixtureSnapshot()` 初始化。
- 任务映射：网页链唯一映射 `CNT-17..21`；投屏链唯一映射 `PLT-M05b1..b6/M05c`；生产 fixture 进入新原子任务 `MDV-25`，完成后才允许 `MRT-09` 总 Review。
- feature 默认值：当前 CEF source/link allowlist 为产品事实源；MDV 当前 ON，网页 Markdown/投屏在完成各自门禁前 OFF；Agent/CLI/MCP、Workflow、Hub/Partner、model/provider 与 HarmonyOS 在一期均 OFF/`NOT_IN_RELEASE`，没有远程 listener 或 provider 装配。
- 真机资源：经用户授权，后续 Direct/Relay 可使用任一 ADB 在线手机运行固定 Cast-SDK 正式接收端；`adb devices -l` 本次见 4 台 `device`。此结果只证明设备可达，未运行或宣称投屏通过。
- 验证：关键符号非测试调用方 `rg`、CEF/CMake source/link 审计、`cargo tree -p crayon-app-runtime -e normal`、任务计数/链接检查、`cargo run -p repo-guard -- scan --root .` 与 `git diff --check`；具体最终结果以本原子提交记录为准。
- Code Review：按 v0.8 复核需求/边界、状态真实性、架构依赖、安全/隐私、发布默认面和任务唯一映射；本任务不改生产代码，P0/P1/P2=0/0/0。
- 未覆盖：没有运行 CEF、Cast、MDV 真机、性能或发布验证；这些能力仍由上述实现任务、PRV-13A、MRT-09、QAR 与 REL-03/04 取得证据。

## REL-05 原子范围（Windows x64 首发候选顺序）

- 状态：`DONE`；依赖 `REL-02 DONE` 与用户 2026-08-31 明确决策。
- 单一目标：只重排一期平台依赖和原子切片，使 Windows x64 三闭环、质量与打包可以在不等待 macOS 特有门禁时形成独立候选；不改变三闭环行为、安全边界或既有平台证据。
- 允许修改：根 `AGENTS.md` 项目记忆、总/current/plan 索引、REL/CNT/PLT/MDV/MRT/PRV/QAR Roadmap；禁止修改生产/测试代码、schema、依赖、构建脚本和历史完成证据。
- 必须冻结：`CNT-20W1/W2 -> CNT-21W`；`PLT-W05a..f -> PLT-19W`；`MDV-20W/25W -> MRT-09W`；`PRV-13AW`；`QAR-01W..16W` 的 Windows 候选路径。macOS 特有验证保留原任务或 M slice，状态如实为 `TODO/NOT_IN_RELEASE`。
- 验收：Windows 候选不存在对 `PLT-M05b4..b6/M05c`、QAR-10 或 macOS 签名/公证/Keychain 的硬依赖；总任务数按新增 REL-05 增加 1；`repo-guard`、`scripts/check.ps1 fast`、`git diff --check` PASS；v0.9 Review P0/P1=0。
- 明确不做：不把 macOS 已有 Debug/Release/真 CEF 证据删除或改写，不宣称 macOS 发布；不运行构建、真机、性能、长稳或打包，不开始 `CNT-20W1` 生产实现。

### REL-05 完成记录（2026-08-31，Windows x64）

- 改动：冻结 `CNT-20W1/W2 -> CNT-21W`、`PLT-W05a..f -> PLT-19W`、`MDV-20W/24W/25W -> MRT-09W`、`PRV-13AW` 与 QAR Windows slices；Windows 候选不再依赖 macOS b4..b6/M05c、QAR-10、签名/公证、Keychain 或 macOS 生命周期门禁；总任务数 286→287。
- 验证：基于合并提交 `7b8fe22`，`cargo run --quiet -p repo-guard -- scan --root .` 退出码 0、3.4 秒、PASS（RG-001..009 适用项通过，RG-003/004 仅既有 warning）；`& .\scripts\check.ps1 fast` 退出码 0、217.5 秒、PASS；`git diff --check` 退出码 0、PASS。
- Code Review：按 v0.9 从需求/边界、状态真实性、依赖图、平台证据隔离、安全/隐私、测试证据与可维护性独立复核；关闭 QAR-09 旧 `QAR-01` 依赖和总 Roadmap CNT 过期摘要后，P0/P1/P2/P3=`0/0/0/0`，结论 `APPROVE`。
- 未覆盖与风险：本原子任务不改生产代码，未运行 CEF 构建、真机、性能、长稳、安装或打包；这些证据按 Windows 任务图继续取得。macOS 特有门禁如实后置，不能由 Windows 结果替代。
