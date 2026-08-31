# 当前权威契约索引

读取顺序：根 `AGENTS.md` → 本页 → 当前 PRD/架构/测试/Review → `docs/plans/README.md` 与任务所属 Roadmap → 真实代码和测试。

## 1. 产品与工程契约

| 文档 | 作用 |
|---|---|
| [当前 PRD](../crayon-private-cast-browser-prd.md) | v0.8 产品事实源：定位、范围、阶段、语义动作、Workflow/Hub、隐私与验收 |
| [当前架构](architecture.md) | v0.8 CAAP、语义动作、Markdown Runtime、Workflow/Challenge、Hub/connector、投屏、模型与平台边界 |
| [技术方案](../crayon-private-cast-browser-technical-design.md) | 当前实现方案与构建/供应链考虑 |
| [测试标准](testing-standard.md) | 分层、设施、平台矩阵和证据规则 |
| [测试用例](test-cases.md) | 212 个唯一当前权威测试 ID |
| [Code Review 标准](code-review-standard.md) | 审查顺序、门禁和交付格式 |
| [Content Data Plane v1](content-data-plane.md) | CNT C1 snapshot/Markdown/owner/delta/Agent R1 冻结接口、预算与 GO 结论 |
| [品牌图标契约](brand-assets.md) | `app-icon-v1` 参考源、母版、平台组合与禁用规则 |
| [桌面浏览器体验契约](browser-ux.md) | `browser-design-v1` 顶部信息架构、共享 token、标题栏/功能 icon、键盘与无障碍规则 |
| [本地 Markdown 查看器契约](markdown-viewer.md) | `crayon://mdv` scheme/CSP、入口手势门禁、图标工具栏与 Mermaid Full 离线扩展契约（v1.4） |
| [Markdown Runtime v1 契约](markdown-runtime.md) | `markdown-runtime-v1` ExtensionNode/manifest/registry、能力、预算、generation、错误与 current/previous golden |
| [Code Highlight 供应链契约](code-highlight.md) | `code-highlight-assets-v1` 选型、固定离线 grammar/别名/dependency、hash/许可/包体与安全输出边界 |
| [KaTeX 数学语法与供应链契约](math-katex.md) | `math-katex-assets-v1` 的 `$`/`$$` 定界、固定 option/宏禁令、ESM/CSS/WOFF2 离线闭包与 MRT-08 输出门禁 |
| [总 Roadmap](../crayon-private-cast-browser-roadmap.md) | 285 项活跃任务、第一期/第二期阶段和当前领取顺序 |
| [第一期发布 Roadmap](../plans/release-v1-roadmap.md) | 网页 Markdown、LAN 投屏、本地 Markdown 编辑三大闭环、平台顺序与关闭 feature |
| [第一期生产装配审计](release-v1-assembly.md) | REL-02 的真实 CEF source/link 调用图、断点任务映射与一期/二期默认开关 |
| [模块 Roadmap 索引](../plans/README.md) | 每个模块的原子任务、依赖与状态 |

## 2. 专项当前契约

| 文档 | 作用 |
|---|---|
| [CEF distribution](cef-distribution.md) | CEF 固定版本、已完成 hash/缓存/许可基线；Linux hash 仅是历史 `CEF-01A` 证据，不代表当前支持 |
| [FND migration review](fnd-migration-review.md) | Foundation 迁移和 Review 证据 |
| [威胁模型](threat-model.md) | PRV-10 交付的资产/信任边界/威胁/缓解/残余风险与安全用例映射 |
| [MED security review](med-security-review.md) | `MED-01..18` 历史策略/Relay 安全证据；Mirror 语义已由 `MED-19` 迁移为 `ExternalClientHandoff` |
| [CAAP v1 契约](caap-v1.md) | `AGT-01` 冻结的 Agent 协议 envelope/握手/能力/错误码与 golden 兼容窗口 |
| [Agent-native PRD 补充稿](../AI投屏浏览器_PRD更新稿_Agent-Native-Browser.md) | v0.7 的重要输入材料；不是独立权威契约，冲突处以当前 PRD/架构/Roadmap 为准 |

Cast-SDK source lock 的当前事实位于 `config/cast-sdk-source.toml`、`.gitmodules`、`SDK-01` Roadmap 证据和真实 gitlink；source decision 仍无独立文档，不得引用不存在的文件作为完成证据；威胁模型自 PRV-10 起位于 [threat-model.md](threat-model.md)。

## 3. 已确认产品范围

- 当前产品名/定位：蜡笔 AI Agent 投屏浏览器；从协议、页面数据面和授权边界为 AI Agent 定制。
- 桌面 UI 采用 Chrome/Chromium 用户熟悉的信息架构与快捷键心智，但使用蜡笔品牌和自有本地页面；`BUX-01..18` 覆盖起始页、omnibox、标签、书签、历史、下载、设置、Profile/无痕与日用基础功能。
- 浏览器内建本地 Markdown Runtime：用户经受控入口打开本地 `.md`，支持源码/渲染预览切换、分栏编辑实时预览、原子保存、图标化编辑工具栏与标准 Mermaid fence；`MDV-01..25` 承接查看器、工具栏、Mermaid 与生产 fixture 清理，`MRT-01..19` 承接闭合扩展框架、Highlight/KaTeX 及后续扩展/跨域门禁，均属用户能力，不作为 Agent 能力暴露。普通 Markdown 继续使用 vendored md4c 0.5.3/MIT；第三方 runtime 均为应用内固定闭包、按需加载，Mermaid tiny 选型已撤销。
- Windows/macOS CEF 浏览器与 LAN Direct/Relay 投屏优先。
- 无 Direct/Relay 路由时只交接给独立蜡笔投屏客户端；浏览器不做 WebRTC、屏幕/标签页/系统音频采集或编码。
- 浏览器与投屏主链路完成后建设当前页数据/Markdown；CAAP、CLI/MCP、高性能读页与授权操作是核心分阶段能力。
- HarmonyOS 只面向鸿蒙电脑 PC 形态技术预览。
- 真实模型/provider、视频/文档总结进入第二阶段；模型选型后续决定。Linux 不在当前范围。
- 页面理解以语义地图和短期 action_id 为核心；Workflow 仅从 verified success 生成候选并由用户预览保存；Challenge 只检测/暂停/接管。
- Capability Hub 区分入站 CAAP/MCP 与出站 Partner API/MCP；Partner/TV Cast Manifest 由 Cast-SDK/接收端拥有。

## 4. 真实现状

- 已收口的主干包括 `BRD-01..04`、Foundation、`MED-01..19`、`BUX-01..18`、`SDK-01..14`、`RNM-01..08`、`ACT-01..12` 与 `MRT-01..08`；CEF 为 `CEF-01..05/15 DONE`、`CEF-06..14 VERIFIED`，其他模块的 VERIFIED/DONE 差异仍以专项 Roadmap 的真实门禁为准。
- CEF 固定基线为 `150.0.10+g8042e43+chromium-150.0.7871.101` Standard。历史四平台 hash 已锁定，Windows x64 archive 已校验；后续产品构建只推进 Windows/macOS。
- Cast-SDK source revision 已由 `SDK-01` 固定并通过 `RG-008`；`SDK-01..14 DONE`，包括真实接收端 Harness 与总 Review。`SDK-15/16` 只承接后续 Partner/TV Cast gap 与正式外部 facade。
- `MED-19` 已完成：投屏决策集合为 `Direct/Relay/ExternalClientHandoff/Reject`，旧 `mirror` wire 值保留兼容读取窗口且不再发出；`tab_video`/`system_audio` 仅作为 `crayon-domain` 遗留字段存在，策略与 runtime 代码不再引用，不得继续扩张。
- 当前开发前沿：`CNT-17..19 DONE` 已在 macOS arm64 产品 CEF 壳闭合 Browser-issued request → Renderer DOM adapter → Browser gateway → 真实 Core owner/extract → 确定性 Markdown，并通过真实原生菜单完成预览/编辑、当前缓冲区复制、Save As、取消和明确覆盖；CNT-19 真 UI/自动化最终 Review P0/P1/P2=`0/0/0`，`CNT-20 READY` 承接双平台 security/perf/Debug/Release E2E。`PLT-M05b1/M05b2 DONE` 已接通真实 CEF media/resource/input/navigation 观察、Browser proof gate、唯一 Rust planning owner、双端 MHV1、有界 macOS helper/process/adapter，并在 Debug/Release 真 CEF fixture 闭合 MP4/HLS/DASH/credential/EME/blob/host crash 与 opaque candidate/decision；`PLT-M05b3 READY` 下一步接 Cast UI、Cast-SDK facade 与 session event pump。测试与产品启动固定 `use-mock-keychain`，真实 SecureStore/Keychain 验证放到最后，不得作为一期启动障碍。Direct/Relay 可使用 ADB 在线手机的正式接收端取证；Windows x64 同一期回归。Agent/CLI/MCP、Workflow、Hub、Partner、模型与 HarmonyOS 统一为第二期且默认关闭。

## 5. 权威与历史

当前 PRD、架构、协议/安全契约优先于模块 Roadmap；Roadmap 优先于归档。已完成任务的历史证据可以保留旧术语，但必须有显式 superseded 说明，不能用历史实现覆盖 2026-08-11 的产品决策。

归档文档只用于背景，不作为新任务验收来源。真实代码、测试与 Git 状态用于确认“已实现什么”，不能仅凭 Roadmap 推断。
