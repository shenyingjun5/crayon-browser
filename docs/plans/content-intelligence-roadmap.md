# CNT 网页内容与 AI Roadmap

> 状态：规划完成，尚未开工
> 目标：在不扩大网页信任、投屏协议和隐私边界的前提下，交付“当前页 -> 结构化快照 -> Markdown/阅读卡片 -> 可选模型”的内容闭环。

## 边界与交付顺序

- `CNT` 拥有页面内容 schema、确定性提取、Markdown、阅读卡片和模型调用数据边界；不拥有 CEF/ArkWeb 生命周期、Profile 删除、Cast-SDK 协议或 Agent 权限。
- Renderer 只采集有界语义快照；Browser process 校验来源和 generation；纯内容转换进入 `crayon-content`，不得反向依赖 CEF。
- 确定性 Markdown 先于模型交付。模型失败、离线或用户拒绝发送时，本机提取仍完整可用。
- 大屏阅读卡片先做本机预览；是否能原生投屏必须由 `CNT-10` 验证 Cast-SDK/接收端正式 capability。缺口在 Cast-SDK 建独立 Roadmap，本仓库不拼协议。
- P0 不做站点级批量抓取、内容库、任意 iframe 抓取、隐藏 DOM、网页视频剧集推断或广告/正片编排。

## 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 单一交付 | 验收与测试 | 最低证据 |
|---|---|---|---|---|---|---|
| CNT-01 | TODO | FND-08,CEF-06,PRV-08 | `crayon-ipc-schema`、`crayon-domain/content`、contract tests | 冻结 `PageSnapshot` v1、脱敏 `SourceRef`、字段分类、大小/节点/深度上限、navigation/generation 与截断语义 | CT-001、CT-002；前一 schema golden；query/fragment/secret 字段不可表达 | S2 |
| CNT-02 | TODO | CEF-03,CNT-01 | `browser/cef-shell/src/renderer/content_snapshot`、独立资源/测试 | 顶层可见语义内容采集；标题/段落/列表/链接/图片引用/表格/代码块 | CT-001～CT-003；跨源 iframe/隐藏表单/脚本排除；超限有标记 | S2 |
| CNT-03 | TODO | CEF-05,CNT-02 | `browser/cef-shell/src/browser/content_gateway`、contract tests | Browser process 来源、前台标签、Profile、generation、消息大小和字段白名单校验 | CT-002、CT-003；迟到/伪造/超大/取消/关闭全部失败关闭 | S2 |
| CNT-04 | TODO | CNT-03 | `crates/crayon-content/src/extract`、独立 tests | 确定性主内容选择、阅读顺序与降级结果；不调用模型 | CT-004；空页、导航页、长文、多栏、重复节点、本地 fixture | S1 |
| CNT-05 | TODO | CNT-04 | `crates/crayon-content/src/markdown`、golden fixtures | Markdown v1 序列化、引用元数据和稳定转义 | CT-005；Unicode、代码 fence、表格、链接、图片引用 golden | S1 |
| CNT-06 | TODO | CNT-04,CNT-05 | `crates/crayon-content/src/structured`、fixtures | 表格/链接/图片/代码块结构化视图和 CSV 数据模型；全部有界 | CT-005、CT-006；危险 scheme、超长单元格、重复资源、空输入 | S1 |
| CNT-07 | TODO | CNT-05,CNT-06,CEF-05 | `crayon-app-runtime/content_export`、`browser/shared-ui/features/content`、平台文件选择接口 | 复制/保存 Markdown、CSV 与引用 receipt；路径由用户选择且原子写入 | CT-007；取消/覆盖/非法文件名/部分失败；无静默保存 | S3 |
| CNT-08 | TODO | CEF-08,CNT-05 | `browser/shared-ui/features/reader`、locales、UI tests | 当前页提取预览与本机阅读模式；显示截断、来源和未包含项 | CT-008；键盘/无障碍/长文虚拟化/导航失效 | S3 |
| CNT-09 | TODO | CNT-08 | `crates/crayon-content/src/cards`、`browser/shared-ui/features/reader`、fixtures | 大屏阅读卡片分页/主题/字号/图片占位与本机预览，不接设备协议 | CT-009；16:9/4K/长表格/代码块/分页稳定、取消释放 | S2 |
| CNT-10 | TODO | CNT-09,SDK-08 | `docs/current`、Cast-SDK capability contract tests | 接收端 document/card 能力 gap analysis 与 GO/NO-GO；缺口只生成外部 SDK Roadmap/API 申请 | CT-010；记录 SDK/接收端版本、输入/输出/许可/隐私；不写临时协议 | S0+S2 |
| CNT-11 | TODO | CNT-05,PRV-08 | `crates/crayon-content/src/model`、`test-support/model-provider` | 最小 `ModelProvider` 接口、Fake、预算/超时/取消/流式错误和 provider-neutral DTO | CT-011、CT-012；无真实网络/Key；输出不参与安全决策 | S2 |
| CNT-12 | TODO | CNT-11,PRV-05,PRV-09 | `integrations/ai-providers`、`crayon-profile`、`shared-ui/features/ai-consent` | 注册 provider 配置、安全存储、发送前预览、逐跳网络门禁、删除/轮换与逐次 consent | CT-011、CT-012、CT-014、PV-007、PV-010；预览等于实际 payload，Key 不跨 origin | S4 |
| CNT-13 | TODO | CNT-12 | `crayon-app-runtime/content_ai`、`shared-ui/features/content`、tests | 单页摘要/要点/大纲/问答，输出绑定 snapshot/hash 和引用 | CT-012；取消、导航、无引用、provider 错误、不自动换 provider | S3 |
| CNT-14 | TODO | CNT-07,CNT-13 | `crates/crayon-content/src/bundle`、UI、tests | 用户显式选择的多标签资料包；逐标签来源/失败/去重，不后台遍历历史 | CT-013；标签关闭/跨 Profile/部分失败/容量上限/取消 | S3 |
| CNT-15 | TODO | CNT-07,CNT-09,CNT-13 | `tests/e2e/content`、`tests/security/content`、bench | 内容 E2E、泄漏扫描、导航竞态、provider 网络门禁、超限和 100 KB P95 基线 | CT-001～09、CT-011～14；无公网；Release 不含 fixture/provider secret | S3 |
| CNT-16 | TODO | CNT-14,CNT-15 | Review、`docs/current` | 内容/模型主线 Review、数据流、性能、已知限制和 P0/P1 修复 | CT-001～09、CT-011～14；P0/P1=0；未支持项不进入营销 | S3 |

## 垂直切片

1. `C1 确定性内容 Alpha`：`CNT-01..09`。交付当前页预览、Markdown/CSV、本机阅读模式和阅读卡片预览，不依赖模型与接收端新协议。
2. `C2 AI 内容 Beta`：`CNT-11..13`。依赖安全存储、诊断数据分级和发送前预览；先 Fake/本地契约，后接批准 provider。
3. `C3 研究资料包`：`CNT-14`。只处理用户显式选择的已打开标签，不扩展为站点爬虫。
4. `C4 大屏能力决策`：`CNT-10`。GO 后另建原子实现任务；NO-GO 时保留标签页镜像/本机预览，不影响内容 Alpha。

## Review 专项

- 页面输入、模型输出和图片元数据均视为不可信；检查提示注入、HTML/Markdown 注入、危险 URL 与超限 DoS。
- 快照、模型输入、导出文件和 receipt 分别有唯一所有者、TTL 与清理触发器。
- 不在 Renderer 执行模型/文件 IO；不在 Browser 锁内做提取、序列化、模型请求或外部回调。
- 不把“正文算法命中率”表述为完整网页抓取；动态/跨源/截断内容必须显式标记。
