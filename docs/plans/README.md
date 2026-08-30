# 活跃 Roadmap 索引

本目录只保存当前可执行的模块 Roadmap。领取任务前必须先读仓库根 `AGENTS.md`、`docs/current/README.md`、总 Roadmap 和所属模块 Roadmap。一次只领取一个满足依赖的原子任务。

## 1. 当前产品范围

- 产品是面向 AI Agent 定制的浏览器；Windows/macOS CEF 浏览器与局域网 Direct/Relay 投屏优先。
- 无视频推送路由时只交接给独立蜡笔投屏客户端；浏览器不做 WebRTC、采集或编码。
- 浏览器和投屏主链路完成后建设当前页数据面与确定性 Markdown；CAAP 协议/权限内核可在浏览器后半段先行。
- HarmonyOS 只规划鸿蒙电脑 PC 形态技术预览。
- CAAP、CLI/入站 MCP、高性能读页、语义地图和授权操作是核心范围；Workflow/Challenge 与 Capability Hub 按独立门禁后续交付。真实模型/provider 与文档/视频总结在第二阶段。Linux 没有当前活跃 Roadmap。
- 出站 Partner API/MCP 与入站 MCP 是不同安全边界；Partner/TV Cast Manifest 属于 Cast-SDK/接收端协议，不在浏览器内复制实现。

## 2. 权威入口

- [总 Roadmap](../crayon-private-cast-browser-roadmap.md)：阶段、依赖、总数与当前领取顺序。
- [当前契约索引](../current/README.md)：PRD、架构、测试和 Code Review 契约。

## 3. 模块索引

| 模块 | Roadmap | 当前目标 | 关键起点 |
|---|---|---|---|
| BRD | [brand-assets-roadmap.md](brand-assets-roadmap.md) | 品牌图标母版、跨平台确定性资产与接入门禁 | `BRD-01..04 DONE` |
| FND | [foundation-migration-roadmap.md](foundation-migration-roadmap.md) | Workspace、契约、质量入口与仓库基线 | 19 个原子任务 `DONE` |
| MED | [media-policy-relay-roadmap.md](media-policy-relay-roadmap.md) | 媒体观察、策略、LAN Relay、外部客户端交接迁移 | `MED-01..19 DONE` |
| CEF | [desktop-cef-browser-roadmap.md](desktop-cef-browser-roadmap.md) | Windows/macOS CEF 壳、共享 UI、媒体观察和 IPC | `CEF-01..15 全部完成`（`CEF-06..14` 模型层 VERIFIED，实机接线归后续装配/切片任务）；Windows 总 Review 证据已补齐 |
| BUX | [browser-product-experience-roadmap.md](browser-product-experience-roadmap.md) | Chrome-inspired 蜡笔桌面浏览器 UI 与日用基础功能 | `BUX-01..18 DONE`（BUX-17/18 2026-08-26） |
| MDV | [markdown-viewer-roadmap.md](markdown-viewer-roadmap.md) | 本地 Markdown Runtime：查看/编辑/保存、图标工具栏、图片与 Mermaid Full 离线扩展 | 基线 `MDV-01..14 DONE`（`MDV-14` Mermaid Full 供应链 2026-08-29 冻结，104 文件/3.5MB 闭包双次生成 hash 一致）；工具栏 `MDV-21..23 DONE`、`MDV-24 VERIFIED`（macOS arm64 与 Windows x64 主矩阵已闭合，Windows Debug/Release 65/65；Narrator、中文 IME 组合态、原生 OS 200% DPI 与原生 macOS x64 待补）；Mermaid `MDV-15..19 DONE`；`MDV-20 VERIFIED`（macOS arm64 Debug/Release 七类图、50-block、签名、NOTICE/SPDX、零公网与零残留已闭合，等待 Windows x64 发布回归后转 `DONE`） |
| MRT | [markdown-runtime-roadmap.md](markdown-runtime-roadmap.md) | Markdown Runtime Extension Framework：闭合扩展 API、Highlight/KaTeX 与后续图表/演示门禁 | `MRT-01..08 DONE`（MRT-06 Windows blocker 修复与真机复验已收口，2026-08-29 合并）；`MRT-09..19` 分波次推进或仅做 gap analysis |
| SDK | [cast-sdk-integration-roadmap.md](cast-sdk-integration-roadmap.md) | 固定源码 Cast-SDK facade、发现、连接和控制；后续 Partner Cast facade | `SDK-01..14 DONE`；`SDK-15/16` 等 HUB/外部已批准 API |
| PLT | [desktop-platform-adapters-roadmap.md](desktop-platform-adapters-roadmap.md) | Windows/macOS 存储、网络、生命周期、更新和客户端交接 | `PLT-01/02/W04/M04 DONE`；`PLT-M05 IN_PROGRESS`，`PLT-W05 TODO` |
| PRV | [privacy-security-roadmap.md](privacy-security-roadmap.md) | Profile、隐私、安全、日志和删除语义 | `PRV-01..12` 已完成或 VERIFIED；`PRV-13` 待总数据流 Review |
| CNT | [content-intelligence-roadmap.md](content-intelligence-roadmap.md) | 页面数据/Markdown 与第二阶段模型总结 | `CNT-01..07 DONE`、`CNT-08 VERIFIED`；`CNT-09 READY` |
| AGT | [agent-access-roadmap.md](agent-access-roadmap.md) | CAAP、tool registry、CLI/MCP、高性能读页和授权操作 | A0 完成；`AGT-07 READY`，`AGT-12C/13/14` 按装配依赖后续推进，`AGT-15 VERIFIED` |
| ACT | [semantic-action-roadmap.md](semantic-action-roadmap.md) | Page/Action/Form/Media/Risk Map、action_id、前置条件和效果验证 | `ACT-01..12 全部完成`（2026-08-30，ACT-12 总 Review GO）；实机接线归后续装配切片 |
| WFL | [workflow-learning-roadmap.md](workflow-learning-roadmap.md) | Challenge 接管、Workflow Learning、个人 Site Skill、健康与受控修复 | `WFL-01 VERIFIED`；`WFL-02/04/06 READY` |
| HUB | [capability-hub-roadmap.md](capability-hub-roadmap.md) | Capability Registry/Router、入站发现与出站 Partner connector | `HUB-01..06 DONE`；`HUB-07+ 待依赖` |
| HM | [harmony-browser-roadmap.md](harmony-browser-roadmap.md) | 鸿蒙电脑 PC 形态 ArkUI/ArkWeb 技术预览 | `HM-01`，后续启动 |
| QAR | [quality-release-roadmap.md](quality-release-roadmap.md) | Windows/macOS 构建、真实设备、性能、长稳和发布门禁 | `QAR-01` |
| RNM | [naming-migration-roadmap.md](naming-migration-roadmap.md) | `get-video` → `crayon-browser` 仓库、包、README、GitHub 与本地路径迁移 | `RNM-01..08 DONE` |

当前共 271 个活跃任务，212 个唯一当前测试用例。MDV 的 `MDV-14..20` 专注 Mermaid Full 依赖闭包、adapter、安全渲染、交互与跨平台收口，`MDV-21..24` 专注工具栏契约/glyph、编辑变换、平台快捷键与双平台无障碍；MRT 的 `MRT-01..19` 统一承接扩展节点/registry/loader、Highlight、KaTeX、TOC/Search、ECharts、Graphviz、Presentation，并将 TV/Cast 与 AI Source Producer 拆成两个独立 gap analysis；新增 `MD-011..013` 与 `MR-001..013`。任务数同时按真实模块 ID 纳入 `RNM-01..08`、`CEF-01A..01E/02W/02M` 与 `BUX-04A/04B` 拆分；Linux、浏览器 WebRTC/采集/编码等仍不计入活跃范围。

## 4. 当前领取队列

### 可直接领取

| 顺序 | 任务 | 状态 | 说明 |
|---:|---|---|---|
| 1 | `AGT-07` | READY | `CNT-08 VERIFIED` 后依赖已满足；交付 R1 target/标题/选区/结构化页面/Markdown 工具 |
| 2 | `CNT-09` | READY | C1 正确性、安全、导航竞争、超大页面、资源释放与 E2E 总矩阵 |
| 3 | `WFL-02` | READY | Challenge Detector；只检测证据，不解题、不绕过 |
| 4 | `WFL-04` | READY | 短期最小 checkpoint store；依赖的隐私/平台契约已满足 |
| 5 | `WFL-06` | READY | 仅记录已授权步骤与 verified effect 的有界 trace |

### 平台收口与待拆装配

| 任务 | 状态 | 说明 |
|---|---|---|
| `MDV-20` | VERIFIED | 仅缺 Windows x64 Mermaid Full 发布回归；不得改写 macOS 已有证据 |
| `MDV-24` | VERIFIED | 主矩阵已闭合；Narrator、中文 IME、原生 200% DPI、原生 macOS x64 仍待补 |
| `PLT-W05` | TODO | Windows 产品装配与 Direct/Relay/外部客户端交接验收；领取前补齐原子范围与设备条件 |
| `AGT-12C` | TODO | CEF 产品 accept loop、stop、session/grant/tool dispatch；先拆成可审查装配切片 |

### 依赖阻塞

- `AGT-13/14` 等 `AGT-07 + AGT-12`；`AGT-16` 再等 CLI/MCP 与 `AGT-15 VERIFIED`。
- `HUB-07` 等 `WFL-12`，`HUB-08` 等 `AGT-14`；Partner connector `HUB-09+` 仍按独立信任/OAuth/网络门禁推进。
- `CNT-11` 必须等 `CNT-10`、`AGT-16`、`PRV-13` 与 provider ADR；第一阶段不得提前接真实模型。
- `SDK-15/16` 等 `HUB-16` 及外部 Cast-SDK/receiver 正式 API，不在浏览器内临时拼协议。

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
