# REL 第一期三大闭环发布 Roadmap

- 版本：`release-v1-scope`
- 日期：2026-08-30
- 状态：`REL-01 DONE`；`REL-02 READY`；`REL-03/04 TODO`
- 任务数：4
- 发布范围：Windows x64、macOS arm64；实现与验证顺序为 macOS arm64 先行、Windows x64 同一期回归
- 第一期开关：网页 Markdown、本地 Markdown Runtime、LAN Direct/Relay 与外部客户端交接开启；Agent/CLI/MCP、Workflow、Capability Hub、Partner Cast、模型与 HarmonyOS 关闭

## 1. 一期完成口径

一期只在发布包内形成三个用户闭环：

1. 真实网页经 Browser 验证的 snapshot 管线生成确定性 Markdown，并可预览、复制和保存。
2. 真实网页中经用户输入与播放推进验证的媒体可选择局域网接收端，完成 Direct/Relay、控制与停止；不满足条件时显式拒绝或经用户确认交接外部客户端。
3. 用户可打开本地 `.md`，使用源码/预览/分栏、编辑工具栏与离线 P0 Runtime，并以原子写和外部修改冲突保护保存。

完成不以模型类、Fake 或独立 Harness 存在为准。三个闭环都必须从真实 CEF 产品入口进入，在 macOS arm64 与 Windows x64 发布候选包取得自动化、实机、性能、安全、长稳、安装/升级/回滚证据，最终经 QAR-16 Go/NoGo。

## 2. 一期明确边界

- 投屏只支持 LAN `Direct/Relay`；无路由只允许 `ExternalClientHandoff`，不实现 WebRTC、标签页/窗口/系统音频采集、编码或视频下载。
- DRM/EME、需要密钥的媒体、加密 HLS、凭证绑定且不可安全路由的来源不得产生 Direct/Relay。
- 网页 Markdown 不包含隐藏/跨源正文、Cookie、Authorization、DOM/HTML/CDP 句柄；页面脚本不能触发预览、复制或文件写入。
- 本地 MDV 仍是用户能力，不进入 CAAP/tool registry，不开放任意文件系统。
- macOS 第一期支持 Apple Silicon。原生 Intel Mac/x64 长稳在取得硬件前标记 `NOT_IN_RELEASE`，不得把 Rosetta 证据写成原生支持；若产品决定宣称 Universal/Intel 支持，QAR-10/15/16 必须追加原生 x64 门禁。

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
| REL-02 | READY | REL-01 | 对 CEF 产品调用图做一次只读装配审计，列出只有模型/测试而无生产调用方的 CNT、Cast、MDV 与浏览器基础模块，并冻结一期 feature flag 默认值 | 入口→owner→adapter→平台调用图；无“DONE 即已装配”推断；无生产改动 |
| REL-03 | TODO | CNT-21,PLT-M05,MDV-20,MRT-09,PRV-13A | 聚合 macOS arm64 三闭环候选包证据与支持矩阵，关闭可关闭的 VERIFIED 状态 | 三闭环真实 CEF；签名包；P0/P1=0；Intel 明确 NOT_IN_RELEASE 或补证据 |
| REL-04 | TODO | REL-03,PLT-W05,PLT-19,QAR-01/02A/03/04/05A/06/07/08A/09..12/14/15 | 聚合 Windows x64 对称回归、发布门禁与一期已知限制，向 QAR-16 提交 Go/NoGo 输入 | Windows/macOS 证据可追踪；关闭功能默认 off；回滚 Runbook |

## REL-01 完成记录（2026-08-30）

- 决策：第一期只交付网页 Markdown、网页媒体 Direct/Relay 投屏、本地 Markdown 编辑三大闭环；开发顺序 macOS arm64 → Windows x64，二者均属一期功能范围。
- Roadmap：CNT 新增真实 CEF/产品导出装配 `CNT-17..21`；PLT-M05b 拆为观察、策略、执行、Direct、Relay、拒绝/交接六个切片；PRV-13 拆为一期核心与二期扩展；QAR-02/05/08 拆为核心 A 与二期 Agent B。
- 状态纠正：总 Roadmap 的 `CNT-10 READY` 更新为实际 `DONE`；`PLT-M05a` 的既有完成记录只作为 new-tab/基础壳证据，不再被解释为 CNT/Cast 全产品装配完成。
- Code Review：按 v0.8 复核范围、依赖、状态真实性、平台证据和发布门禁；未改变生产代码、既有测试结论或历史完成记录，P0/P1/P2=0/0/0。
- 未覆盖：REL-02 尚未输出完整生产调用图；具体实现、真机和发布证据由各模块原子任务与 QAR 取得。

## REL-02 原子范围（CEF 生产调用图与一期 feature flag 审计）

- 状态：`READY`；依赖 `REL-01 DONE`。
- 单一目标：以只读代码审计确认一期三大闭环从用户入口到 owner/domain/adapter/platform 的真实生产调用关系，列出仅被测试或自身模块引用、尚无产品调用方的节点，并冻结第一期/第二期 feature 默认开关；不实现或修复任何生产能力。
- 输入：`browser/cef-shell/**`、`browser/shared-ui/**`、`crates/crayon-{app-runtime,page-data,content-*,cast-adapter,relay,agent-*}/**`、CMake/Cargo 生产依赖图、当前 REL/CNT/PLT/MDV/MRT/AGT Roadmap。
- 输出与允许修改：`docs/current/**` 新增或更新一期生产装配图、`docs/plans/release-v1-roadmap.md` 的发现和后续任务映射、必要的模块 Roadmap 状态纠正；禁止修改生产代码、测试、schema/golden、依赖或构建配置。
- 审计方法：入口/handler 注册、构造与 owner、事件/IPC、domain use case、platform/SDK/文件 IO 五段逐段提供符号与调用方证据；类/函数存在、测试通过或 Roadmap DONE 不能替代生产 reachability。
- 验收：网页 Markdown、媒体投屏、MDV 各有一张最小调用图；每个断点映射唯一后续任务 ID；Agent/Workflow/Hub/Partner/model/Harmony feature 在 Release 装配中默认关闭且无远程监听/provider；`rg`/CMake/Cargo 证据命令和未覆盖项实际记录；文档链接/任务状态/计数校验通过，Review P0/P1=0。
- 明确不做：不新增临时 feature flag 实现、不接 CEF/SDK/文件 IO、不运行公网或真机、不因发现缺口顺手修复；缺陷进入对应原子任务。
