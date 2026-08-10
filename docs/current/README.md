# 当前权威契约索引

读取顺序：根 `AGENTS.md` → 本页 → 当前 PRD/架构/测试/Review → `docs/plans/README.md` 与任务所属 Roadmap → 真实代码和测试。

## 1. 产品与工程契约

| 文档 | 作用 |
|---|---|
| [当前 PRD](../crayon-private-cast-browser-prd.md) | 产品定位、范围、阶段、用户体验、隐私与验收 |
| [当前架构](architecture.md) | 依赖方向、模块所有权、投屏/交接/Markdown 与平台边界 |
| [技术方案](../crayon-private-cast-browser-technical-design.md) | 当前实现方案与构建/供应链考虑 |
| [测试标准](testing-standard.md) | 分层、设施、平台矩阵和证据规则 |
| [测试用例](test-cases.md) | 95 个当前权威测试 ID |
| [Code Review 标准](code-review-standard.md) | 审查顺序、门禁和交付格式 |
| [总 Roadmap](../crayon-private-cast-browser-roadmap.md) | 128 项活跃任务、阶段和当前领取顺序 |
| [模块 Roadmap 索引](../plans/README.md) | 每个模块的原子任务、依赖与状态 |

## 2. 专项当前契约

| 文档 | 作用 |
|---|---|
| [CEF distribution](cef-distribution.md) | CEF 固定版本、已完成 hash/缓存/许可基线；Linux hash 仅是历史 `CEF-01A` 证据，不代表当前支持 |
| [FND migration review](fnd-migration-review.md) | Foundation 迁移和 Review 证据 |
| [MED security review](med-security-review.md) | `MED-01..18` 历史策略/Relay 安全证据；Mirror 语义已由 `MED-19` 迁移为 `ExternalClientHandoff` |

Cast-SDK source lock 的当前事实位于 `config/cast-sdk-source.toml`、`.gitmodules`、`SDK-01` Roadmap 证据和真实 gitlink；当前不存在独立的 source decision/threat-model 文档，后续任务不得引用不存在的文件作为完成证据。

## 3. 已确认产品范围

- 当前产品名/定位：蜡笔隐私投屏浏览器。
- Windows/macOS CEF 浏览器与 LAN Direct/Relay 投屏优先。
- 无 Direct/Relay 路由时只交接给独立蜡笔投屏客户端；浏览器不做 WebRTC、屏幕/标签页/系统音频采集或编码。
- 浏览器与投屏主链路完成后，再建设当前网页的确定性 Markdown 预览/复制/保存。
- HarmonyOS 只面向鸿蒙电脑 PC 形态技术预览。
- Linux、AI/模型、Agent、CLI、MCP 均不在当前范围。

## 4. 真实现状

- Foundation 19 个原子任务、`MED-01..19`、`CEF-01A`、`SDK-01`、`SDK-02` 已完成，共 41 项。
- CEF 固定基线为 `150.0.10+g8042e43+chromium-150.0.7871.101` Standard。历史四平台 hash 已锁定，Windows x64 archive 已校验；后续产品构建只推进 Windows/macOS。
- Cast-SDK source revision 已由 `SDK-01` 固定并通过 `RG-008`；真实 facade/平台接线和接收端闭环尚未完成。
- `MED-19` 已完成：投屏决策集合为 `Direct/Relay/ExternalClientHandoff/Reject`，旧 `mirror` wire 值保留兼容读取窗口且不再发出；`tab_video`/`system_audio` 仅作为 `crayon-domain` 遗留字段存在，策略与 runtime 代码不再引用，不得继续扩张。
- `CEF-01B READY`、`SDK-03 READY`；`CNT-01..10` 全部等待浏览器和投屏主链路门禁。

## 5. 权威与历史

当前 PRD、架构、协议/安全契约优先于模块 Roadmap；Roadmap 优先于归档。已完成任务的历史证据可以保留旧术语，但必须有显式 superseded 说明，不能用历史实现覆盖 2026-08-11 的产品决策。

归档文档只用于背景，不作为新任务验收来源。真实代码、测试与 Git 状态用于确认“已实现什么”，不能仅凭 Roadmap 推断。
