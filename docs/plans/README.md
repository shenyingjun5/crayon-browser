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
| CEF | [desktop-cef-browser-roadmap.md](desktop-cef-browser-roadmap.md) | Windows/macOS CEF 壳、共享 UI、媒体观察和 IPC | `CEF-02W DONE`，`CEF-03 IN_PROGRESS`（macOS 开发机恢复，状态单测已接入）；`CEF-01E VERIFIED`（macOS x64/arm64 构建证据已补，待 arm64 真机） |
| BUX | [browser-product-experience-roadmap.md](browser-product-experience-roadmap.md) | Chrome-inspired 蜡笔桌面浏览器 UI 与日用基础功能 | Windows 优先；`BUX-01 DONE`，`BUX-02 READY` |
| SDK | [cast-sdk-integration-roadmap.md](cast-sdk-integration-roadmap.md) | 固定源码 Cast-SDK facade、发现、连接和控制；后续 Partner Cast facade | `SDK-13 BLOCKED`（需真实接收端 Harness） |
| PLT | [desktop-platform-adapters-roadmap.md](desktop-platform-adapters-roadmap.md) | Windows/macOS 存储、网络、生命周期、更新和客户端交接 | `PLT-01` |
| PRV | [privacy-security-roadmap.md](privacy-security-roadmap.md) | Profile、隐私、安全、日志和删除语义 | `PRV-01` |
| CNT | [content-intelligence-roadmap.md](content-intelligence-roadmap.md) | 页面数据/Markdown 与第二阶段模型总结 | `CNT-01` 等待主链路；`CNT-11` 等待模型门禁 |
| AGT | [agent-access-roadmap.md](agent-access-roadmap.md) | CAAP、tool registry、CLI/MCP、高性能读页和授权操作 | `AGT-01`，依赖满足后开始 |
| ACT | [semantic-action-roadmap.md](semantic-action-roadmap.md) | Page/Action/Form/Media/Risk Map、action_id、前置条件和效果验证 | `ACT-01` 等待 `CNT-03/AGT-01` |
| WFL | [workflow-learning-roadmap.md](workflow-learning-roadmap.md) | Challenge 接管、Workflow Learning、个人 Site Skill、健康与受控修复 | `WFL-01` 等待 `ACT-12/AGT-03` |
| HUB | [capability-hub-roadmap.md](capability-hub-roadmap.md) | Capability Registry/Router、入站发现与出站 Partner connector | `HUB-01` 等待 `AGT-02/PRV-08` |
| HM | [harmony-browser-roadmap.md](harmony-browser-roadmap.md) | 鸿蒙电脑 PC 形态 ArkUI/ArkWeb 技术预览 | `HM-01`，后续启动 |
| QAR | [quality-release-roadmap.md](quality-release-roadmap.md) | Windows/macOS 构建、真实设备、性能、长稳和发布门禁 | `QAR-01` |
| RNM | [naming-migration-roadmap.md](naming-migration-roadmap.md) | `get-video` → `crayon-browser` 仓库、包、README、GitHub 与本地路径迁移 | `RNM-01..08 DONE` |

当前共 227 个活跃任务，186 个唯一当前测试用例。新增 `RNM-01..08` 命名迁移任务；`CEF-02` 已按 Windows 首发/macOS 后置拆为 `CEF-02W/02M`；Linux、浏览器 WebRTC/采集/编码等已删除范围不计入活跃总数。

## 4. 当前领取队列

| 顺序 | 任务 | 状态 | 说明 |
|---:|---|---|---|
| 1 | `SDK-13` | BLOCKED | 真接收端 Harness（CS-010、E2E-001/002）；当前环境无真机，Harness 就绪后领取 |
| 2 | `CEF-03` | IN_PROGRESS | RNM 已完成并验证新路径；macOS 开发机已恢复，状态单测和 CEF adapter 已接入，Windows 实机验收后置 |
| 3 | `BUX-02` | READY | `CEF-02W DONE`；与 CEF-03 按单任务规则串行推进 |
| 4 | `CEF-01E`/`CEF-02M` | VERIFIED/TODO | macOS bootstrap 已实现但平台证据后置；不得阻塞 Windows 主线，也不得用 Windows 证据完成 macOS |
| 5 | `AGT-01` | TODO | 依赖满足后冻结 CAAP v1；不提前开放 CLI/MCP transport |
| 6 | `CNT-01` | TODO | 等浏览器/投屏/隐私门禁后开始正式 page-data/Markdown |
| 7 | `ACT-01` | TODO | 等 `CNT-03/AGT-01`；不提前发明第二页面数据面 |
| 8 | 后续任务 | TODO | WFL/HUB 严格按语义动作、权限和隐私依赖领取 |

`CNT-01` 必须等 `CEF-15`、`BUX-18`、`SDK-14`、`MED-19`、`PRV-08` 完成后才能进入 `READY`。`CNT-11` 必须等 `CNT-10`、`AGT-16`、`PRV-13` 完成且模型/provider ADR 获批；不得提前接真实 provider。

## 5. 当前代码事实

- 品牌资产 `BRD-01..04`、Foundation 19 个原子任务、`MED-01..19`、`CEF-01A..01D`、`CEF-02W`、`BUX-01`、`SDK-01..12` 与 `RNM-01..08` 已完成，共 68 项。
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
