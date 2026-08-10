# 活跃 Roadmap 索引

本目录只保存当前可执行的模块 Roadmap。领取任务前必须先读仓库根 `AGENTS.md`、`docs/current/README.md`、总 Roadmap 和所属模块 Roadmap。一次只领取一个满足依赖的原子任务。

## 1. 当前产品范围

- Windows/macOS CEF 浏览器与局域网 Direct/Relay 投屏优先。
- 无视频推送路由时只交接给独立蜡笔投屏客户端；浏览器不做 WebRTC、采集或编码。
- 浏览器和投屏主链路完成后再做当前网页的确定性 Markdown 提取。
- HarmonyOS 只规划鸿蒙电脑 PC 形态技术预览。
- Linux、AI/模型、Agent、CLI、MCP 没有当前活跃 Roadmap。

## 2. 权威入口

- [总 Roadmap](../crayon-private-cast-browser-roadmap.md)：阶段、依赖、总数与当前领取顺序。
- [当前契约索引](../current/README.md)：PRD、架构、测试和 Code Review 契约。

## 3. 模块索引

| 模块 | Roadmap | 当前目标 | 关键起点 |
|---|---|---|---|
| FND | [foundation-migration-roadmap.md](foundation-migration-roadmap.md) | Workspace、契约、质量入口与仓库基线 | 19 个原子任务 `DONE` |
| MED | [media-policy-relay-roadmap.md](media-policy-relay-roadmap.md) | 媒体观察、策略、LAN Relay、外部客户端交接迁移 | `MED-01..19 DONE` |
| CEF | [desktop-cef-browser-roadmap.md](desktop-cef-browser-roadmap.md) | Windows/macOS CEF 壳、共享 UI、媒体观察和 IPC | `CEF-01B READY` |
| SDK | [cast-sdk-integration-roadmap.md](cast-sdk-integration-roadmap.md) | 固定源码 Cast-SDK facade、发现、连接和控制 | `SDK-03 READY` |
| PLT | [desktop-platform-adapters-roadmap.md](desktop-platform-adapters-roadmap.md) | Windows/macOS 存储、网络、生命周期、更新和客户端交接 | `PLT-01` |
| PRV | [privacy-security-roadmap.md](privacy-security-roadmap.md) | Profile、隐私、安全、日志和删除语义 | `PRV-01` |
| CNT | [content-intelligence-roadmap.md](content-intelligence-roadmap.md) | 当前页确定性提取与 Markdown 预览/导出 | `CNT-01`，等待主链路 |
| HM | [harmony-browser-roadmap.md](harmony-browser-roadmap.md) | 鸿蒙电脑 PC 形态 ArkUI/ArkWeb 技术预览 | `HM-01`，后续启动 |
| QAR | [quality-release-roadmap.md](quality-release-roadmap.md) | Windows/macOS 构建、真实设备、性能、长稳和发布门禁 | `QAR-01` |

当前共 128 个活跃任务，95 个当前测试用例。历史归档、已删除范围和未来设想不计入活跃总数。

## 4. 当前领取队列

| 顺序 | 任务 | 状态 | 说明 |
|---:|---|---|---|
| 1 | `MED-19` | DONE | `Mirror`/WebRTC schema 与 runtime 语义已迁移为 `ExternalClientHandoff`，保留 `mirror` 兼容读取窗口 |
| 2 | `CEF-01B` | READY | Windows CEF toolchain/bootstrap；不得扩张旧 Mirror 语义 |
| 3 | `SDK-02` | DONE | 固定版本 Cast-SDK facade 依赖接入完成；未实现 WebRTC 或外部客户端协议 |
| 4 | `SDK-03` | READY | 产品侧 `CastFacade` trait、强类型 DTO/error 契约；不暴露 SDK 内部类型 |
| 5 | 后续任务 | TODO | 严格按模块依赖和总阶段领取 |

`CNT-01` 即使接口设计已明确，也必须等 `CEF-15`、`SDK-14`、`MED-19`、`PRV-08` 完成后才能进入 `READY`。

## 5. 当前代码事实

- Foundation 19 个原子任务、`MED-01..19`、`CEF-01A`、`SDK-01`、`SDK-02` 已完成，共 41 项。
- Cast-SDK 固定源码 revision 为 `44c3a99871aa1e68cbda71eacefbb41d23a747a8`，由 `third_party/cast-sdk` gitlink 与 `config/cast-sdk-source.toml` 约束；后续以 `SDK-01` 最终 Review 记录为准。
- `CastPolicyDecision::Mirror` / `DeliveryPlan::Mirror` 已由 `MED-19` 迁移为 `ExternalClientHandoff`（纯建议 DTO + 稳定 reason + 用户确认要求）；旧 `mirror` wire 值仅作兼容读取，新代码不得再引用 Mirror 语义。
- Roadmap 表示目标和完成证据，不等于所有目标都已由代码实现；领取前必须读取真实代码、测试和 Git 状态。

## 6. 状态与完成规则

- 状态仅使用 `TODO`、`READY`、`IN_PROGRESS`、`BLOCKED`、`IMPLEMENTED`、`VERIFIED`、`DONE`。
- `IMPLEMENTED` 不等于完成；必须记录实际 Format、Lint、Unit、Integration、Build 与适用 Harness 结果。
- 平台/设备任务没有真实平台或指定 Harness 证据时不得标 `DONE`。
- 每个原子任务完成后按 `docs/current/code-review-standard.md` 独立 Review；P0/P1 未关闭不得合并。
- 外部发布、推送、Tag、部署、凭证使用和 Cast-SDK 外部仓库修改仍需明确授权。
