# 活跃 Roadmap 索引

本目录只保存当前可执行的模块 Roadmap。领取任务前必须先读仓库根 `AGENTS.md`、`docs/current/README.md`、总 Roadmap 和所属模块 Roadmap。一次只领取一个满足依赖的原子任务。

## 1. 当前产品范围

- 产品是面向 AI Agent 定制的浏览器；Windows/macOS CEF 浏览器与局域网 Direct/Relay 投屏优先。
- 无视频推送路由时只交接给独立蜡笔投屏客户端；浏览器不做 WebRTC、采集或编码。
- 浏览器和投屏主链路完成后建设当前页数据面与确定性 Markdown；CAAP 协议/权限内核可在浏览器后半段先行。
- HarmonyOS 只规划鸿蒙电脑 PC 形态技术预览。
- 第一期只发布网页 Markdown、LAN Direct/Relay 投屏、本地 Markdown 编辑三大闭环；CAAP、CLI/入站 MCP、高性能读页、语义动作、Workflow/Challenge、Capability Hub、Partner 与模型仍是产品方向，但统一进入第二期且默认关闭。Linux 没有当前活跃 Roadmap。
- 出站 Partner API/MCP 与入站 MCP 是不同安全边界；Partner/TV Cast Manifest 属于 Cast-SDK/接收端协议，不在浏览器内复制实现。

## 2. 权威入口

- [总 Roadmap](../crayon-private-cast-browser-roadmap.md)：阶段、依赖、总数与当前领取顺序。
- [当前契约索引](../current/README.md)：PRD、架构、测试和 Code Review 契约。

## 3. 模块索引

| 模块 | Roadmap | 当前目标 | 关键起点 |
|---|---|---|---|
| REL | [release-v1-roadmap.md](release-v1-roadmap.md) | 第一期网页 Markdown、LAN 投屏、本地 Markdown 编辑三大闭环与发布范围 | `REL-01/02 DONE`；真实装配图见 current 审计，macOS arm64 先行 |
| BRD | [brand-assets-roadmap.md](brand-assets-roadmap.md) | 品牌图标母版、跨平台确定性资产与接入门禁 | `BRD-01..04 DONE` |
| FND | [foundation-migration-roadmap.md](foundation-migration-roadmap.md) | Workspace、契约、质量入口与仓库基线 | 19 个原子任务 `DONE` |
| MED | [media-policy-relay-roadmap.md](media-policy-relay-roadmap.md) | 媒体观察、策略、LAN Relay、外部客户端交接迁移 | `MED-01..19 DONE` |
| CEF | [desktop-cef-browser-roadmap.md](desktop-cef-browser-roadmap.md) | Windows/macOS CEF 壳、共享 UI、媒体观察和 IPC | `CEF-01..15 全部完成`（`CEF-06..14` 模型层 VERIFIED，实机接线归后续装配/切片任务）；Windows 总 Review 证据已补齐 |
| BUX | [browser-product-experience-roadmap.md](browser-product-experience-roadmap.md) | Chrome-inspired 蜡笔桌面浏览器 UI 与日用基础功能 | `BUX-01..18 DONE`（BUX-17/18 2026-08-26） |
| MDV | [markdown-viewer-roadmap.md](markdown-viewer-roadmap.md) | 本地 Markdown Runtime：查看/编辑/保存、图标工具栏、图片与 Mermaid Full 离线扩展 | 基线、工具栏与 Mermaid 已生产可达；`MDV-20/24 VERIFIED`，`MDV-25 READY` 移除生产 fixture 初始化后进入 MRT/发布收口 |
| MRT | [markdown-runtime-roadmap.md](markdown-runtime-roadmap.md) | Markdown Runtime Extension Framework：闭合扩展 API、Highlight/KaTeX 与后续图表/演示门禁 | `MRT-01..08 DONE`（MRT-06 Windows blocker 修复与真机复验已收口，2026-08-29 合并）；`MRT-09..19` 分波次推进或仅做 gap analysis |
| SDK | [cast-sdk-integration-roadmap.md](cast-sdk-integration-roadmap.md) | 固定源码 Cast-SDK facade、发现、连接和控制；后续 Partner Cast facade | `SDK-01..14 DONE`；`SDK-15/16` 等 HUB/外部已批准 API |
| PLT | [desktop-platform-adapters-roadmap.md](desktop-platform-adapters-roadmap.md) | Windows/macOS 存储、网络、生命周期、更新和客户端交接 | `PLT-01/02/W04/M04 DONE`；`PLT-M05 IN_PROGRESS`，`PLT-W05 TODO` |
| PRV | [privacy-security-roadmap.md](privacy-security-roadmap.md) | Profile、隐私、安全、日志和删除语义 | `PRV-01..12` 已完成或 VERIFIED；一期核心 `PRV-13A`、第二期扩展 `PRV-13B` |
| CNT | [content-intelligence-roadmap.md](content-intelligence-roadmap.md) | 页面数据/Markdown 与第二阶段模型总结 | C1 数据面 `CNT-01..10 DONE/VERIFIED`；一期产品装配 `CNT-17..19 DONE`、`CNT-20 READY`、`CNT-21 TODO`；`CNT-11..16` 第二期 |
| AGT | [agent-access-roadmap.md](agent-access-roadmap.md) | CAAP、tool registry、CLI/MCP、高性能读页和授权操作 | A0 完成；`AGT-07/15 VERIFIED`，`AGT-12C/13/14` 按装配依赖后续推进 |
| ACT | [semantic-action-roadmap.md](semantic-action-roadmap.md) | Page/Action/Form/Media/Risk Map、action_id、前置条件和效果验证 | `ACT-01..12 全部完成`（2026-08-30，ACT-12 总 Review GO）；实机接线归后续装配切片 |
| WFL | [workflow-learning-roadmap.md](workflow-learning-roadmap.md) | Challenge 接管、Workflow Learning、个人 Site Skill、健康与受控修复 | `WFL-01/02/03/04/06/07 VERIFIED` |
| HUB | [capability-hub-roadmap.md](capability-hub-roadmap.md) | Capability Registry/Router、入站发现与出站 Partner connector | `HUB-01..06 DONE`；`HUB-07+ 待依赖` |
| HM | [harmony-browser-roadmap.md](harmony-browser-roadmap.md) | 鸿蒙电脑 PC 形态 ArkUI/ArkWeb 技术预览 | `HM-01`，后续启动 |
| QAR | [quality-release-roadmap.md](quality-release-roadmap.md) | Windows/macOS 构建、真实设备、性能、长稳和发布门禁 | 核心 `QAR-02A/05A/08A` 与第二期 feature `02B/05B/08B` 已拆分 |
| RNM | [naming-migration-roadmap.md](naming-migration-roadmap.md) | `get-video` → `crayon-browser` 仓库、包、README、GitHub 与本地路径迁移 | `RNM-01..08 DONE` |

当前共 285 个活跃任务，212 个唯一当前测试用例。新增任务来自 `REL-01..04`、`CNT-17..21`、`PRV-13A/B` 对原 PRV-13 的分拆、`QAR-02/05/08` 的 A/B 分拆，以及 REL-02 发现的 `MDV-25` 生产 fixture 清理；PLT-M05b1..b6 为 PLT-M05 内部原子切片，不重复计入模块顶层任务数。MDV 的 `MDV-14..20` 专注 Mermaid Full，`MDV-21..24` 专注编辑器工具栏，`MDV-25` 负责 Release 生产隔离；MRT-09 P0 Runtime 总 Review属于一期，MRT-10..19 属第二期。Linux、浏览器 WebRTC/采集/编码仍不计入活跃范围。

## 4. 当前领取队列

### 第一期可直接领取

| 顺序 | 任务 | 状态 | 说明 |
|---:|---|---|---|
| 1 | `PLT-M05b2b` | READY | `b2a VERIFIED`；实现版本化本机 media-host 协议，随后 b2c CEF 接线，不调用 SDK/UI |
| 2 | `MDV-25` | READY | 移除 `BuildFixtureSnapshot()` 生产初始化；同样触及 CEF App/CMake，串行领取 |
| 3 | `CNT-20` | READY | 网页→Markdown 双平台 E2E/security/perf；macOS 先行，Windows x64 最后回归 |

### 平台收口与待拆装配

| 任务 | 状态 | 说明 |
|---|---|---|
| `MDV-20` | VERIFIED | 仅缺 Windows x64 Mermaid Full 发布回归；不得改写 macOS 已有证据 |
| `MDV-24` | VERIFIED | 主矩阵已闭合；Narrator、中文 IME、原生 200% DPI、原生 macOS x64 仍待补 |
| `MRT-09` | TODO | 等 `MDV-25` 清除生产 fixture 后执行 P0 Runtime 总 Review |
| `PLT-M05b2..b6/M05c` | READY/TODO | `M05b1 DONE`；macOS 策略→SDK→Direct→Relay→拒绝/交接→资源稳定性，严格串行 |
| `PLT-W05` | TODO | macOS 闭环缺陷关闭后做 Windows 对称装配；领取前按切片补齐原子范围与设备条件 |

### 第二期与依赖阻塞

- `AGT-13/14` 等 AGT-12 产品装配；`AGT-16` 再等 CLI/MCP 与 `AGT-15 VERIFIED`。
- `AGT-12C` 与 `AGT-13/14/16` 均为第二期；先拆 CEF accept loop、stop、session/grant/tool dispatch 原子切片，不能夹入一期 CNT/Cast 装配。
- `HUB-07` 等 `WFL-12`，`HUB-08` 等 `AGT-14`；Partner connector `HUB-09+` 仍按独立信任/OAuth/网络门禁推进。
- `CNT-11` 必须等 `CNT-21`、`AGT-16`、`PRV-13B` 与 provider ADR；第一期不得提前接真实模型。
- `SDK-15/16` 等 `HUB-16` 及外部 Cast-SDK/receiver 正式 API，不在浏览器内临时拼协议。

第一期任务完成前不直接领取 WFL/HUB/M2/MRT-10+；已具备依赖的第二期模型或状态机任务也保持排队，不与 CEF 产品装配争抢工作区和真机矩阵。

## 5. 当前代码事实

- 品牌资产 `BRD-01..04`、Foundation 19 个原子任务、`MED-01..19`、`CEF-01A..01D`、`CEF-02W`、`CEF-03`、`BUX-01..03`、`SDK-01..12` 与 `RNM-01..08` 已完成，共 71 项。
- `browser/engine-api` 已冻结为不含 CEF/ArkWeb/OS/Cast/Relay 类型的 C++17 契约，并通过 GCC/MSVC 双编译器、公开头独立编译、生命周期 contract 和 production boundary scan；它还不是可运行浏览器。
- Cast-SDK 固定源码 revision 为 `44c3a99871aa1e68cbda71eacefbb41d23a747a8`，由 `third_party/cast-sdk` gitlink 与 `config/cast-sdk-source.toml` 约束；后续以 `SDK-01` 最终 Review 记录为准。
- `CastPolicyDecision::Mirror` / `DeliveryPlan::Mirror` 已由 `MED-19` 迁移为 `ExternalClientHandoff`（纯建议 DTO + 稳定 reason + 用户确认要求）；旧 `mirror` wire 值仅作兼容读取，新代码不得再引用 Mirror 语义。
- `CastFacade` 的确定性 Fake 在 `test-support::cast_facade::FakeCastFacade`（dev/test target only）；SDK-05+ 的真实 service 与 SDK-12 编排测试都应以它为行为基准。
- Roadmap 表示目标和完成证据，不等于所有目标都已由代码实现；领取前必须读取真实代码、测试和 Git 状态。

## 6. 状态与完成规则

- 状态仅使用 `TODO`、`READY`、`IN_PROGRESS`、`BLOCKED`、`IMPLEMENTED`、`VERIFIED`、`DONE`。
- `IMPLEMENTED` 不等于完成；必须记录实际 Format、Lint、Unit、Integration、Build 与适用 Harness 结果。
- 平台/设备任务没有真实平台或指定 Harness 证据时不得标 `DONE`。
- 每个原子任务完成后按 `docs/current/code-review-standard.md` 独立 Review；P0/P1 未关闭不得合并。
- 外部发布、推送、Tag、部署、凭证使用和 Cast-SDK 外部仓库修改仍需明确授权。
