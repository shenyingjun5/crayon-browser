# MRT：Markdown Runtime Extension Framework Roadmap

状态：`MRT-01 DONE`，`MRT-02 READY`，`MRT-03..19 TODO`。本 Roadmap 吸收 `docs/reference/蜡笔投屏浏览器_Markdown_Runtime_Extension_Framework_V1.0.md` 第 34..59 章，但以当前 PRD、安全契约和真实 C++17/md4c/CEF 工程为准。目标是在不建立第二 Markdown parser、不扩大文件/Agent 权限、不让大型扩展进入浏览器 bootstrap 的前提下，为 MDV 提供统一、闭合、可审计的 Extension Framework。

## 1. 采纳结论

- 兼容层级固定为：Level A `CommonMark/GFM`，Level B 成熟生态扩展，Level C 蜡笔专有 Runtime。Level A 的普通 Markdown 输出优先级最高，任何扩展失败都必须降级为安全源码/代码块。
- 扩展节点类型预留 `inline/block/fence/container`，但按任务逐类启用；不因为 API 可表达就自动启用 `$...$`、`:::...` 或任意自定义 fence。
- 保留现有 `browser/shared-ui/markdown` 的 vendored md4c 0.5.3 作为唯一 parser。参考文档中的 TypeScript 目录和 `markdown-it` 只是概念输入，不能覆盖现有依赖方向。
- Extension Registry 是编译期闭合 registry，不是可下载插件市场；manifest 不能携带任意模块路径、脚本、网络 endpoint 或权限升级。未识别 fence 永远回退普通代码块。
- 大型运行时全部应用内固定版本、manifest/hash/许可锁定、按需加载、单扩展错误隔离、有界并发/cache/generation；文档、扩展输出与 AI 结果统一视为不可信。
- P0 = Framework + Mermaid Full + Code Highlight + KaTeX；P1 = TOC/Outline/Search；P2 = ECharts + Graphviz + 本地 Presentation；P3 的 PlantUML/Vega/AI 编辑只保留候选门禁，不提前建立生产实现。
- Presentation 只先做本地浏览器模式。TV/Cast 会改变接收端/Cast-SDK 协议与媒体类型，`MRT-18` 只做 gap analysis；没有受审 facade 前不得实现投屏。

## 2. 模块所有权与边界

| 组件 | 所有权 | 明确不拥有 |
|---|---|---|
| `browser/shared-ui/markdown` | md4c 标准解析、安全 HTML、扩展节点事实 | CEF、动态模块、网络、投屏 |
| `browser/shared-ui/markdown-runtime`（规划） | ExtensionNode/Manifest/Registry/Router、预算、cache key 与结果状态 | 本地文件 IO、CEF API、第三方算法 |
| `browser/shared-ui/mdv` | 文档会话、主题、视图、扩展占位/错误 UI、Presentation 本地状态 | Agent 工具、Cast 协议、任意插件下载 |
| `browser/cef-shell/src/browser/mdv` | 应用内资源路由、Renderer 生命周期与平台装配 | 扩展语义、公共 schema、任意文件路由 |
| `third_party/<extension>` | 固定版本浏览器运行时闭包、LICENSE/NOTICE/manifest | npm cache、开发依赖、在线更新 |

MRT 是用户侧 MDV 基础设施，不进入 `crayon-page-data`、CNT 的确定性网页 Markdown、CAAP/tool registry 或 Agent 文件能力。未来 AI 只能作为 Markdown Source Producer，经 CNT 模型数据门禁产生候选文本；不能直接注册扩展、授予权限或触发保存/投屏。

## 3. 原子任务

| ID | 状态 | 依赖 | 允许路径 | 单一交付 | 验收 |
|---|---|---|---|---|---|
| MRT-01 | DONE | MDV-13 | `docs/current`,`docs/plans` | 冻结 Runtime v1 契约：四类节点、三层兼容、manifest/schema、能力/资源策略、错误/预算/生命周期与永久禁止面 | MR-001；契约 Review |
| MRT-02 | READY | MRT-01 | `browser/shared-ui/markdown`,`browser/shared-ui/markdown-runtime` | md4c ExtensionNode adapter：保留标准 HTML并产出有界 inline/block/fence/container 事实；默认全部未启用 | MR-002；CommonMark/GFM golden 零回退 |
| MRT-03 | TODO | MRT-02 | `browser/shared-ui/markdown-runtime` | 编译期 Extension Registry/Router：按 node kind + 精确 info string 分发，冲突/未知/禁用稳定回退 | MR-001/002；registry contract |
| MRT-04 | TODO | MRT-03 | `browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | 通用 runtime loader/cache/lifecycle：manifest 资源、按需 import、预算、generation、错误隔离与资源清理 | MR-003；lazy/cache/风暴 |
| MRT-05 | TODO | MRT-04 | `third_party/highlight`,`tools`,`docs/current` | Code Highlight 依赖选型与离线 grammar 闭包冻结；比较 highlight.js/Prism/Shiki 后只固定一个 | MR-004；许可/hash/包体/语言矩阵 |
| MRT-06 | TODO | MRT-05 | `browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | Code Highlight fence extension：语言 allowlist、grammar 按需加载、未知语言纯文本回退 | MR-004；注入/主题/lazy |
| MRT-07 | TODO | MRT-04 | `third_party/katex`,`tools`,`docs/current` | KaTeX 语法与供应链契约：明确 inline/block 定界、转义、宏/URL/HTML 禁令、字体/CSS 本地闭包 | MR-005；许可/语法/安全矩阵 |
| MRT-08 | TODO | MRT-07 | `browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | KaTeX inline/block extension：按需加载、局部错误、主题/字体离线与编辑 generation | MR-005；公式 golden/注入/实机 |
| MRT-09 | TODO | MDV-20,MRT-06,MRT-08 | `tests/e2e/desktop`,`tools/repo-guard`,`docs/current`,`docs/plans` | P0 Runtime 收口：CommonMark/GFM + Highlight + Mermaid Full + KaTeX 的双平台/包体/性能/安全总 Review | MR-001..005,MR-008/012；P0/P1=0 |
| MRT-10 | TODO | MRT-09 | `browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv` | TOC/Outline：从解析事实生成有界标题树、稳定会话锚点与键盘/读屏导航 | MR-006；重复标题/超深/编辑更新 |
| MRT-11 | TODO | MRT-09 | `browser/shared-ui/mdv` | 当前文档本地 Search：只查内存源码/安全文本，结果/高亮有界，不持久化 query | MR-006；Unicode/大文档/取消 |
| MRT-12 | TODO | MRT-09 | `third_party/echarts`,`tools`,`docs/current` | ECharts 供应链与纯 JSON option schema：固定运行时闭包、series/component allowlist、禁止 function/eval/URL | MR-007；schema/许可/包体 |
| MRT-13 | TODO | MRT-12 | `browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | `echarts` fence extension：JSON parse/validate、Canvas/SVG 渲染、resize/主题、局部错误与释放 | MR-007/008；恶意 option/资源回落 |
| MRT-14 | TODO | MRT-09 | `third_party/graphviz`,`tools`,`docs/current` | Graphviz WASM 选型与 sandbox 契约：DOT 预算、WASM/worker 闭包、许可、内存/CPU/超时/取消 | MR-009；许可/资源/敌意 DOT |
| MRT-15 | TODO | MRT-14 | `browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | `dot/graphviz` fence extension：WASM lazy load、SVG policy、worker 终止与局部错误 | MR-008/009；超时/取消/资源回落 |
| MRT-16 | TODO | MRT-10,MRT-11 | `docs/current`,`browser/shared-ui/mdv` | 本地 Presentation v1 契约与状态机：分节规则、Normal/Presentation 切换、导航/焦点/退出；不含 TV/Cast | MR-010；契约/状态风暴 |
| MRT-17 | TODO | MRT-16,MRT-13,MRT-15 | `browser/shared-ui/mdv`,`browser/shared-ui/locales`,`tests/e2e/desktop` | Presentation UI：16:9/自适应布局、键盘翻页、图表重排、speaker-note 明确不做、双平台实机 | MR-010；a11y/主题/resize/退出 |
| MRT-18 | TODO | MRT-17,SDK-15 | `docs/current`,`docs/plans` | TV/Cast gap analysis：明确接收端/Cast-SDK facade、内容类型、会话、遥控器与失败语义；只产出外部独立 Roadmap 触发条件 | MR-011；无浏览器私有协议/媒体伪装 |
| MRT-19 | TODO | MRT-09,CNT-11 | `docs/current`,`docs/plans` | AI Source Producer gap analysis：冻结候选 Markdown、发送预览、provenance、取消与用户保存边界；只产出 CNT 后续任务触发条件 | MR-013；无 registry/文件/保存/投屏权限 |

## 4. 分波次领取

```text
Foundation: MRT-01 -> MRT-02 -> MRT-03 -> MRT-04
P0:         (MDV-14..20 Mermaid) + MRT-05 -> 06 + MRT-07 -> 08 -> MRT-09
P1:         MRT-10 / MRT-11
P2:         MRT-12 -> 13 / MRT-14 -> 15 / MRT-16 -> 17
Gate:       MRT-18 TV/Cast gap / MRT-19 AI source-producer gap only
```

`MDV-15` 改为 Mermaid adapter，依赖 `MRT-03`；`MDV-16` 的资源路由消费 `MRT-04` 的 manifest/loader 契约。MRT 不复制 Mermaid 任务，也不把 Mermaid 合并进巨大 renderer。

## 5. MRT-01 原子范围（Runtime v1 契约冻结）

- 状态：`DONE`；依赖 `MDV-13 VERIFIED`。单一目标是新增 `docs/current/markdown-runtime.md`，不写生产代码。
- 契约必须冻结：Level A/B/C；`ExtensionNodeKind` 四类闭合枚举；精确 matcher 与优先级/冲突；未知/禁用回退；manifest 字段与版本；输出类型（safe-html/svg/canvas/error）及各自 policy；network/script/file/export/interactive 能力默认 deny；source/document/extension generation；block/字节/深度/时间/并发/cache 上限语义；加载失败、渲染失败、取消、超时、满载、导航与销毁状态；locale/a11y；previous/current manifest golden 规则。
- 允许修改：新增 `docs/current/markdown-runtime.md`，更新 current/plans/总 Roadmap/test 索引。禁止修改生产代码、现有 md4c/MDV 行为、CAAP/CNT/Cast-SDK、第三方依赖。
- 安全边界：manifest 是编译期声明，不允许文档或 AI 提供 manifest、模块路径、版本、URL、capability 或 render options；`trusted` 不能由文档字段表达；所有可执行内容永久禁止。
- 验收：MR-001 的 schema/example/reject vectors 可直接驱动 MRT-02..04；与 `markdown-viewer.md`、架构、PRD、AGENT 文件禁令无冲突；`git diff --check`；按 Review 标准 P0/P1/P2=0。
- 明确不做：parser/AST、registry 代码、第三方选型、Presentation/Cast 实现。

### MRT-01 完成记录（2026-08-28）

- 实现：新增权威 `docs/current/markdown-runtime.md`（`markdown-runtime-v1`），冻结 Level A/B/C、`inline/block/fence/container` 四类 closed node、`render-plan/v1` 与 `manifest/v1` current schema、Level A-first/精确 matcher/冲突双方拒绝、编译期 registry 原子发布、四类类型化输出 policy、默认 deny capability、资源边界、命名预算、document/source/extension 三重 generation、闭合状态/错误、session-only cache、locale/a11y 与 current/previous 兼容窗口。提供 4 个接受、15 个拒绝/回退的 MR-001 向量。
- 安全：文档/网页/AI/MCP 不能提供 manifest、extension ID、模块/路径/URL/options/capability/`trusted`；network/file/dynamic code/external process/export 默认关闭，未审核 page-local interaction 关闭；未知字段、schema、matcher、policy 或 partial registry 均 fail closed；失败保持 Level A fallback 且零资产/网络/文件副作用。
- 验证：两个 JSON current 示例均可解析；render plan 的 UTF-8 `source_bytes` 与半开 range 不变量通过；19 个 MR-001 vector ID 唯一；4 份相关 current 文档的相对 Markdown 链接存在；`git diff --check` 通过。任务只改契约/索引，未运行生产构建与 C++ 测试（无生产代码变化）。
- Code Review：按 v0.8 复核需求/边界、架构/API、安全/隐私、生命周期、性能、测试和可维护性；P0/P1/P2 = 0/0/0。契约没有建立第二 parser、动态插件、通用执行器、Agent/Cast/文件/网络能力。
- 未覆盖：ExtensionNode/registry/loader 生产实现与可执行 contract 分别归 `MRT-02..04`；Mermaid 供应链归 `MDV-14`。`MRT-02` 转为 `READY`。

## 6. 各阶段共同门禁

- 每个第三方扩展拆成“供应链/契约”与“runtime 接入”两个原子任务；前者不过，后者不得领取。依赖必须固定版本、来源、integrity/hash、许可证、浏览器运行时 closure 与回滚版本。
- 不创建通用 JS/WASM 执行器。每个 extension 只有专用、闭合输入 schema；ECharts 禁止 function/eval，KaTeX 禁止危险宏/HTML/URL，Graphviz 限 CPU/内存/worker，输出 SVG 统一经过 policy gate。
- 大型 extension 无匹配节点时零加载；缓存只在会话内存，key 至少包含 extension ID/version/source hash/theme/options/policy version；Profile/文档/导航/Renderer 销毁时清空。
- 普通 Markdown 输出、启动时间和内存是硬回归门禁。Extension 错误、超时或资源满载不得影响源码查看、编辑、保存或其他 block。
- 所有用户文案进入 locale；所有 extension toolbar/menu 使用自有 glyph；不持久化本地路径、搜索词、公式、DSL、图表数据或渲染结果。

## 7. 延后候选与触发条件

- PlantUML：只有在确认无需本机 Java、无需远程 PlantUML server、无 Graphviz 重复运行时且许可证/包体可接受后，才追加独立任务。
- Vega/Vega-Lite：等待 ECharts/Graphviz P2 数据可视化边界稳定后再做选型，不预注册生产 fence。
- AI Generate/Modify：等待 `MRT-19`、CNT provider ADR、发送预览和权限门禁完成；AI 只生成候选 Markdown，用户编辑/保存路径不变，不获得 extension registry、文件、保存或投屏权限。
- TV/Cast：等待 `MRT-18` gap analysis、外部 Cast-SDK/receiver 独立 Roadmap 和正式 facade。浏览器不得把 Markdown HTML/SVG 伪装成媒体 URL、不得自建 receiver 协议或远程页面控制通道。

## 8. 完成口径

- P0 完成：`MRT-01..09` 与 `MDV-14..20` 达到规定状态，MR-001..005/008/012 与 MD-008..010 有自动化/实机证据；无图/无公式/无高亮文档零额外 runtime 加载。
- P1/P2 各自 feature GO/NO-GO，不阻塞已完成的基础 MDV/Mermaid；任何供应链、安全或性能 NO-GO 保持对应 extension 关闭。
- `MRT-18/19` 完成只表示各自跨域边界和外部依赖已明确，不表示 TV/Cast 或 AI 生成功能已实现。
