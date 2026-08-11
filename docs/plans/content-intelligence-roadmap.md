# CNT 页面数据、Markdown 与第二阶段模型 Roadmap

- 状态：规划中
- 任务数：16
- C1 开始门禁：`CEF-15`、`SDK-14`、`MED-19`、`PRV-08`
- M2 开始门禁：`CNT-10`、`AGT-16`、`PRV-13`

## 1. 范围

- C1：确定性当前页快照、结构化内容、Markdown、预览/复制/保存，为用户和 Agent R1 共用。
- `CNT` 只拥有 verified `PageSnapshot`、正文/Markdown 和基础 revision；Action/Form/Media/Risk Map、action_id、前置条件、effect 和面向动作的 ChangeSet 由 `ACT` Roadmap 拥有并复用该数据面。
- M2：模型/provider 决策后，提供用户确认的文档总结与基于合法文本来源的视频总结。
- 非目标：批量爬取、后台站点遍历、隐藏字幕接口、媒体下载、未授权 ASR、模型参与权限/DRM/投屏安全决策。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| CNT-01 | TODO | CEF-15,SDK-14,MED-19,PRV-08 | `crayon-content-contract/**`,`crayon-page-data/**` | 定义 `PageSnapshot`、结构块、provenance、revision、截断与资源上限 | `CT-001`,`CT-002`; schema/golden | C1 |
| CNT-02 | TODO | CNT-01,CEF-01B | `browser/engine-api/**`,`apps/desktop-cef/**`,`crayon-browser-gateway/**` | 在跨引擎接口增加有界 snapshot stream/cancel，并实现 Renderer 分块采集与 Browser 来源/navigation验证 | `CT-001`,`CT-002`,`CT-007`; interface contract/integration | C1 |
| CNT-03 | TODO | CNT-02 | `crayon-page-data/**`,`crayon-app-runtime/**` | snapshot owner、generation 缓存、取消、分页和旧结果丢弃 | `CT-002`,`CT-007`; integration | C1 |
| CNT-04 | TODO | CNT-03 | `crayon-content-extract/**` | 确定性主正文、阅读顺序和结构块识别 | `CT-003`,`CT-008`; fixture/unit | C1 |
| CNT-05 | TODO | CNT-04 | `crayon-content-markdown/**` | 标准 Markdown 转换与稳定转义 | `CT-003`,`CT-004`; golden | C1 |
| CNT-06 | TODO | CNT-05 | `crayon-content-markdown/**` | 列表、引用、代码、表格、链接和图片引用规范化 | `CT-004`,`CT-005`; golden/security | C1 |
| CNT-07 | TODO | CNT-03,CNT-06 | `crayon-page-data/**`,`tests/perf/content/**` | 字段索引、增量 revision、流式/背压和 C1 性能基线 | `CT-006`,`CT-008`; benchmark/soak | C1 |
| CNT-08 | TODO | CNT-06,CNT-07,PRV-08 | `apps/desktop-cef/**`,`crayon-platform-api/**` | 本地预览、复制、保存、取消、覆盖和失败反馈 | `CT-005`,`CT-006`; UI integration | C1 |
| CNT-09 | TODO | CNT-08 | `tests/**`,`test-support/**` | 正确性、安全、导航竞争、超大页面、资源释放与 E2E | `CT-001..008`; E2E/security/perf | C1 |
| CNT-10 | TODO | CNT-09 | `docs/current/**`,`docs/plans/**` | C1 独立 Review 与 Agent data-plane 接口冻结 | CT-001..008；P0/P1=0 | C1 |
| CNT-11 | TODO | CNT-10,AGT-16,PRV-13 | ADR,`crayon-model-contract/**`,`docs/current/**` | 决定本地/云端/BYOK/provider、地区、费用、保留、密钥和数据发送契约 | `CT-009`; ADR/contract；未决策不开网络 | M2 |
| CNT-12 | TODO | CNT-11 | `crayon-model-adapter/**`,`crayon-profile/**` | provider registry、安全存储、origin/redirect、发送前 payload preview 和 Fake provider | `CT-009..011`; security/integration | M2 |
| CNT-13 | TODO | CNT-12 | `crayon-content-ai/document/**`,`crayon-app-runtime/**` | 当前文档摘要、要点、大纲/问答，绑定 snapshot/hash 与引用 | `CT-010..013`; Fake provider | M2 |
| CNT-14 | TODO | CNT-12,MED-07 | `crayon-content-ai/video/**`,`crayon-app-runtime/**` | 基于用户可见字幕/转录或用户文本的视频总结输入契约；无文本时明确拒绝 | `CT-010`,`CT-014`; 无媒体下载/隐藏接口 | M2 |
| CNT-15 | TODO | CNT-13,CNT-14 | `apps/desktop-cef/**`,locales,tests | AI UI、provider/字段预览、引用、取消、错误和本地 Markdown 降级 | `CT-011..014`; UI/E2E | M2 |
| CNT-16 | TODO | CNT-15 | threat model,Review,`docs/current/**` | 模型数据流、成本/隐私/安全/性能 Review 和 feature Go/NoGo | CT-009..014；P0/P1=0 | M2 |

## 3. 数据不变量

- page snapshot 与 Markdown 是无模型也可用的基础能力。
- 页面脚本不能触发文件写入、Agent grant 或模型网络请求。
- Agent R1 grant 不等于模型发送授权；每次 provider payload 单独预览/确认。
- 模型不接收 Cookie、Authorization、浏览历史、完整敏感 query、隐藏 DOM、跨源正文或其他标签。
- 视频总结首期不下载媒体/音轨、不绕 DRM、不调用隐藏字幕 API；无合法文本来源即明确不支持。
- 模型结果为 untrusted，不能触发 CAAP 工具、改变投屏策略或写回页面。
