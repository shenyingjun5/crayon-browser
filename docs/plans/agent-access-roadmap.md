# AGT CAAP / CLI / MCP Agent 访问 Roadmap

- 状态：规划完成，尚未开工
- 任务数：16
- 目标：用自有 CAAP、入站 tool registry 和 capability guard，为 AI Agent 提供高性能读页，并把受控操作接到 `ACT` 的可验证语义动作运行时
- 非目标：远程控制、原始 CDP/WebDriver、任意 JavaScript、Cookie/凭证、密码/支付、文件上传、任意文件/网络工具

## 1. 架构边界

- `crayon-agent-gateway` 只编排协议、工具、授权、确认、任务代际和 receipt；实际行为调用 app-runtime 正常用例。
- CLI 使用当前用户可访问的本机 IPC；MCP 是 loopback CAAP adapter；二者共享 registry、错误、取消、幂等和安全门禁。
- 页面、模型、工具结果和 client 输入都不可信，不能生成/扩大 grant 或触发第二工具。
- R0/R1 先交付；R2/R3 后交付；R4 只有专项安全 Review GO 才进入 Preview。
- Agent 页面读取复用正式 page data/Markdown，不建立基于截图/OCR或外部自动化的第二数据面。
- 本 Roadmap 只负责 Agent 入站访问；Partner API/MCP 的出站连接由 `HUB` Roadmap 负责，两类 MCP 不共享 session、token、registry 或权限。
- Action/Form/Media/Risk Map、action_id、多信号 locator、前置条件和效果验证由 `ACT` Roadmap 拥有，AGT 只把它们以 CAAP 工具接入。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 单一交付 | 验收与测试 | 阶段 |
|---|---|---|---|---|---|---|
| AGT-01 | TODO | FND-08,PRV-08 | `crayon-domain/agent/**`,`crayon-ipc-schema/**`,`docs/current/**` | 冻结 CAAP v1 envelope、握手、版本/能力、target、stream、cancel、deadline、错误和 previous/current golden | `AG-001`; schema/compat/fuzz；无 OS/CEF/SDK 类型 | A0 |
| AGT-02 | TODO | AGT-01 | `crayon-domain/agent/**`,`crayon-agent-gateway/registry/**` | Tool/capability/risk R0～R4 registry 与永久禁止清单 | `AG-001`,`AG-015`; registry snapshot | A0 |
| AGT-03 | TODO | AGT-01,FND-09 | `crayon-agent-gateway/session/**` | client/task/session/target/generation、取消、超时、幂等和有界队列状态机 | `AG-002`; unit/property | A0 |
| AGT-04 | TODO | AGT-02,AGT-03,PRV-08 | `crayon-agent-gateway/grant/**` | 单次/任务/App 会话 grant、Profile 隔离、撤销和目标变化失效 | `AG-003`,`AG-005`; default deny | A0 |
| AGT-05 | TODO | AGT-04,CEF-08 | `apps/desktop-cef/**/agent-confirm/**`,locales | 确认 UI：client、工具、route、目标、参数摘要、数据披露、到期和无障碍 | `AG-004`; UI integration | A0 |
| AGT-06 | TODO | CNT-03,AGT-03 | `crayon-page-data/**`,`crayon-agent-gateway/page_stream/**` | generation-scoped 快照缓存、分页/流式/增量、索引、背压和性能 instrumentation | `AG-006`,`AG-015`; benchmark/soak | A1 |
| AGT-07 | TODO | AGT-04,AGT-06,CNT-08 | `crayon-agent-gateway/tools/content/**`,`crayon-app-runtime/**` | R1 target/标题/选区/结构化页面/Markdown 读取工具 | `AG-006`; 跨 Profile/后台/过期/超量拒绝 | A1 |
| AGT-08 | TODO | AGT-04,SDK-08 | `crayon-agent-gateway/tools/cast_read/**` | R0/R1 接收端能力和投屏状态读取，不返回 IP/URL/token | `AG-007`; adapter tests | A1 |
| AGT-09 | TODO | AGT-05,CEF-07,ACT-07,ACT-11 | `crayon-agent-gateway/tools/navigation/**`,`crayon-app-runtime/**` | R2 打开/切换/关闭标签、导航、后退、刷新、滚动及人工接管结果 | `AG-008`; scheme/redirect/download/popup/cancel | A2 |
| AGT-10 | TODO | AGT-05,SDK-12,MED-19 | `crayon-agent-gateway/tools/cast_control/**` | R3 选择设备、开始/暂停/seek/停止；沿用正常投屏门禁 | `AG-009`; 目标变化重确认；不控制外部镜像客户端 | A2 |
| AGT-11 | TODO | AGT-03,AGT-04 | `crayon-agent-gateway/receipt/**`,diagnostics | 有界脱敏 action receipt、TTL、用户预览/清除 | `AG-011`,`PV-010`; 无正文/query/secret | A0 |
| AGT-12 | TODO | AGT-04,AGT-11,PRV-10,PLT-01 | `apps/desktop-cef/agent-transport/**`,`crayon-platform-api/**` | Windows named pipe/macOS UDS CAAP transport；当前用户 ACL、限流、单客户端、stop | `AG-012`; 恶意本机 client/replay/oversize | A1 |
| AGT-13 | TODO | AGT-05,AGT-07,AGT-08,AGT-12 | `apps/agent-cli/**`,docs/tests | R0/R1 CLI Developer Preview；机器可读结果、版本、cancel | `AG-013`; 无交互不绕确认 | A1 |
| AGT-14 | TODO | AGT-05,AGT-07,AGT-08,AGT-12 | `apps/mcp/**`,MCP contracts | 只读 MCP Developer Preview，将 initialize/list/call/cancel 映射到 CAAP | `AG-014`; schema 同源、loopback only | A1 |
| AGT-15 | TODO | AGT-06,AGT-09,AGT-10,ACT-12 | `crayon-agent-gateway/tools/semantic/**`,`tests/security/agent/**`,`tests/perf/agent/**` | 把 R4 Action Map/action_id/effect 接入 CAAP，并完成提示注入/fuzz/恶意 client/性能专项 | `AG-005`,`AG-010`,`AG-015`; 不复制 locator/runtime；永久禁止 surface 零命中 | A2 |
| AGT-16 | TODO | AGT-09,AGT-10,AGT-13,AGT-14,AGT-15,ACT-12 | threat model,Review,`docs/current/**` | CAAP/CLI/入站 MCP 总 Review、数据流、benchmark、默认开关与 GO/NO-GO | 全 AG、适用 AC；P0/P1=0；独立发布决策 | A2 |

## 3. 垂直切片

1. `A0 权限内核`：`AGT-01..05,11`，不开 transport。
2. `A1 只读 Preview`：`AGT-06..08,12..14`，提供高性能 R0/R1。
3. `A2 受控操作 Preview`：`AGT-09,10,15,16`，R2～R4 与专项安全/性能门禁。

## 4. 性能门禁

- 已有缓存的标题/元数据 R1 本机 P95 目标 ≤50ms。
- 100KB 清洗内容的结构化快照/Markdown P95 目标 ≤500ms。
- 记录 first chunk、complete、CPU、RSS、序列化字节、UI event-loop delay 和增量复用率。
- benchmark 使用本地固定 fixture 和固定硬件记录；未完成对照实验前不得宣传“快于某浏览器”。
- 队列、snapshot cache、未确认 chunk、并发 task 和 receipt 全部有界；取消后资源可验证释放。

## 5. Review 专项

- MCP/CLI client 不能替代服务端确认；服务端始终 fail closed。
- 检查 confused deputy、跨 Profile、target 替换、TOCTOU、重放、间接提示注入和旧结果副作用。
- Release 扫描 remote bind、CDP/WebDriver、任意脚本、Cookie API、密码/支付、文件上传和通用文件/网络工具。
- Agent 功能可以单独 NO-GO，不阻塞浏览器/投屏核心；但不能以 debug 后门代替正式 Preview。
