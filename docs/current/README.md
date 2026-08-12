# 当前权威契约索引

读取顺序：根 `AGENTS.md` → 本页 → 当前 PRD/架构/测试/Review → `docs/plans/README.md` 与任务所属 Roadmap → 真实代码和测试。

## 1. 产品与工程契约

| 文档 | 作用 |
|---|---|
| [当前 PRD](../crayon-private-cast-browser-prd.md) | v0.7 产品事实源：定位、范围、阶段、语义动作、Workflow/Hub、隐私与验收 |
| [当前架构](architecture.md) | v0.7 CAAP、语义动作、Workflow/Challenge、Hub/connector、投屏、模型与平台边界 |
| [技术方案](../crayon-private-cast-browser-technical-design.md) | 当前实现方案与构建/供应链考虑 |
| [测试标准](testing-standard.md) | 分层、设施、平台矩阵和证据规则 |
| [测试用例](test-cases.md) | 186 个唯一当前权威测试 ID |
| [Code Review 标准](code-review-standard.md) | 审查顺序、门禁和交付格式 |
| [品牌图标契约](brand-assets.md) | `app-icon-v1` 参考源、母版、平台组合与禁用规则 |
| [桌面浏览器体验契约](browser-ux.md) | `browser-design-v1` 顶部信息架构、共享 token、标题栏/功能 icon、键盘与无障碍规则 |
| [总 Roadmap](../crayon-private-cast-browser-roadmap.md) | 218 项活跃任务、阶段和当前领取顺序 |
| [模块 Roadmap 索引](../plans/README.md) | 每个模块的原子任务、依赖与状态 |

## 2. 专项当前契约

| 文档 | 作用 |
|---|---|
| [CEF distribution](cef-distribution.md) | CEF 固定版本、已完成 hash/缓存/许可基线；Linux hash 仅是历史 `CEF-01A` 证据，不代表当前支持 |
| [FND migration review](fnd-migration-review.md) | Foundation 迁移和 Review 证据 |
| [MED security review](med-security-review.md) | `MED-01..18` 历史策略/Relay 安全证据；Mirror 语义已由 `MED-19` 迁移为 `ExternalClientHandoff` |
| [Agent-native PRD 补充稿](../AI投屏浏览器_PRD更新稿_Agent-Native-Browser.md) | v0.7 的重要输入材料；不是独立权威契约，冲突处以当前 PRD/架构/Roadmap 为准 |

Cast-SDK source lock 的当前事实位于 `config/cast-sdk-source.toml`、`.gitmodules`、`SDK-01` Roadmap 证据和真实 gitlink；当前不存在独立的 source decision/threat-model 文档，后续任务不得引用不存在的文件作为完成证据。

## 3. 已确认产品范围

- 当前产品名/定位：蜡笔 AI Agent 投屏浏览器；从协议、页面数据面和授权边界为 AI Agent 定制。
- 桌面 UI 采用 Chrome/Chromium 用户熟悉的信息架构与快捷键心智，但使用蜡笔品牌和自有本地页面；`BUX-01..18` 覆盖起始页、omnibox、标签、书签、历史、下载、设置、Profile/无痕与日用基础功能。
- Windows/macOS CEF 浏览器与 LAN Direct/Relay 投屏优先。
- 无 Direct/Relay 路由时只交接给独立蜡笔投屏客户端；浏览器不做 WebRTC、屏幕/标签页/系统音频采集或编码。
- 浏览器与投屏主链路完成后建设当前页数据/Markdown；CAAP、CLI/MCP、高性能读页与授权操作是核心分阶段能力。
- HarmonyOS 只面向鸿蒙电脑 PC 形态技术预览。
- 真实模型/provider、视频/文档总结进入第二阶段；模型选型后续决定。Linux 不在当前范围。
- 页面理解以语义地图和短期 action_id 为核心；Workflow 仅从 verified success 生成候选并由用户预览保存；Challenge 只检测/暂停/接管。
- Capability Hub 区分入站 CAAP/MCP 与出站 Partner API/MCP；Partner/TV Cast Manifest 由 Cast-SDK/接收端拥有。

## 4. 真实现状

- 品牌资产 `BRD-01..04`、Foundation 19 个原子任务、`MED-01..19`、`CEF-01A..01D`、`BUX-01`、`SDK-01..12` 已完成，共 59 项。
- CEF 固定基线为 `150.0.10+g8042e43+chromium-150.0.7871.101` Standard。历史四平台 hash 已锁定，Windows x64 archive 已校验；后续产品构建只推进 Windows/macOS。
- Cast-SDK source revision 已由 `SDK-01` 固定并通过 `RG-008`；facade、Fake、真实 service 生命周期、能力缓存、投送执行、会话监督与 runtime 用例编排已完成；真机接收端闭环（`SDK-13`）尚未完成。
- `MED-19` 已完成：投屏决策集合为 `Direct/Relay/ExternalClientHandoff/Reject`，旧 `mirror` wire 值保留兼容读取窗口且不再发出；`tab_video`/`system_audio` 仅作为 `crayon-domain` 遗留字段存在，策略与 runtime 代码不再引用，不得继续扩张。
- `browser/engine-api` 的 C++17 跨 CEF/ArkWeb 契约已由 `CEF-01B` 冻结并通过 GCC/MSVC 双编译器与 contract；`CEF-01C` 已建立 Windows/macOS 共用的离线 CEF 构建图，`CEF-01D` 已交付可启动、可关闭且无残留进程的 Windows x64 最小 CEF 壳和受管标题栏品牌图标。当前阶段按用户决策优先把 Windows 浏览器全部基础功能开发并实机跑通：`CEF-02W DONE` 已交付官方 bootstrap EXE/client DLL 多进程结构、Debug/Release sandbox、品牌图标和无残留进程实机证据，下一任务为 `CEF-03 READY`，同时解锁 `CEF-06` 与 `BUX-02 READY`。`CEF-01E VERIFIED` 的 macOS App/Helper Bundle 与 CI 保持冻结，远程双架构/实机证据和正式 sandbox 由 `CEF-02M` 后置，不再阻塞 Windows，也不得用 Windows 证据完成 macOS。`BUX-01` 已冻结 Chrome-inspired 两层信息架构、共享 token、21 个自有功能 glyph 和 8 份规格 golden。`SDK-13 BLOCKED`（需真实接收端 Harness）；`CNT-01..10` 等待浏览器/投屏门禁；`AGT/ACT/WFL/HUB` 分别规划入站访问、语义动作、工作流/挑战和能力生态；`CNT-11..16` 是第二阶段模型能力。

## 5. 权威与历史

当前 PRD、架构、协议/安全契约优先于模块 Roadmap；Roadmap 优先于归档。已完成任务的历史证据可以保留旧术语，但必须有显式 superseded 说明，不能用历史实现覆盖 2026-08-11 的产品决策。

归档文档只用于背景，不作为新任务验收来源。真实代码、测试与 Git 状态用于确认“已实现什么”，不能仅凭 Roadmap 推断。
