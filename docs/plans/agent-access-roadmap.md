# AGT Agent/CLI/MCP 安全访问 Roadmap

> 状态：规划完成，尚未开工
> 目标：用单一版本化 tool registry 和 capability guard，让 AI Agent 在用户可见、可撤销、可审计的边界内读取当前页并控制浏览/投屏；默认不提供通用浏览器自动化后门。

## 边界与上线策略

- `agent-gateway` 只编排版本化工具、授权、确认、任务代际和 receipt；实际浏览、内容与投屏行为必须调用 `app-runtime` 正常用例，不直接碰 CEF、Cast-SDK、relay 或平台 API。
- 页面内容、无障碍快照、模型输出和 MCP client 输入都不可信，不能授予 capability、改变确认策略或访问其他 Profile。
- CLI 与 MCP 共用同一 registry；CLI 不是绕过确认的内部后门。Developer Preview 默认关闭、只绑定 loopback、使用短期高熵 secret。
- R0/R1 读取先交付；R2/R3 副作用能力后交付；R4 页面写操作只在专项威胁模型与安全评审通过后进入 P2 Preview。
- 永不提供 Cookie/Authorization、密码/支付/文件上传、任意 JavaScript、原始 CDP/WebDriver、任意文件系统、远程监听或通用网络代理工具。

## 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 单一交付 | 验收与测试 | 最低证据 |
|---|---|---|---|---|---|---|
| AGT-01 | TODO | FND-08,PRV-08 | `crayon-domain/agent`、`crayon-ipc-schema`、contracts | Tool/capability/risk v1 schema，R0～R4 与永久禁止清单 | AG-001；前后版本 golden；工具声明不含 OS/CEF/SDK 类型 | S2 |
| AGT-02 | TODO | AGT-01,FND-09 | `crates/crayon-agent-gateway/src/session`、tests | task/session/tab/navigation/generation 状态机、取消、超时和幂等键 | AG-002；重复/旧结果/退出/满队列/资源释放 | S2 |
| AGT-03 | TODO | AGT-02,PRV-08 | `crates/crayon-agent-gateway/src/grant`、tests | 单次/单任务/App 会话 grant、Profile 隔离、撤销与目标变化失效 | AG-003；默认 deny；页面/模型不能扩大权限 | S2 |
| AGT-04 | TODO | AGT-03,CEF-08 | `browser/shared-ui/features/agent-confirm`、locales、UI tests | 确认 UI：工具、目标、关键参数、数据披露和过期原因 | AG-004；拒绝/超时/导航/设备变化/无障碍 | S3 |
| AGT-05 | TODO | AGT-01,CNT-03 | `crates/crayon-agent-gateway/src/trust`、security tests | 不可信内容标记、source provenance、参数复制边界与 indirect prompt injection 防护 | AG-005；页面指令不能授权、改目标或调用第二工具 | S2 |
| AGT-06 | TODO | AGT-03,AGT-05,CNT-07 | `crayon-agent-gateway/tools/content`、app-runtime adapters | R1 当前页/选区/Markdown/标签读取工具；按 Profile/tab/generation 限定 | AG-006；跨 Profile/后台标签/过期/超量全部拒绝 | S3 |
| AGT-07 | TODO | AGT-03,SDK-08 | `crayon-agent-gateway/tools/cast_read`、app-runtime adapters | R0/R1 设备 capability 与当前投屏状态读取，不返回 IP/URL/token | AG-007；旧 route、同名设备、无会话、脱敏 | S2 |
| AGT-08 | TODO | AGT-04,CEF-07 | `crayon-agent-gateway/tools/navigation`、app-runtime adapters | R2 打开/切换/关闭标签、导航、后退、刷新、滚动；有界且需确认 | AG-008；scheme/origin/重定向/下载/弹窗/取消 | S3 |
| AGT-09 | TODO | AGT-04,SDK-12 | `crayon-agent-gateway/tools/cast_control`、app-runtime adapters | R3 选择设备、开始/暂停/seek/停止；沿用播放门禁与 policy | AG-009；目标变化重确认；不能绕 DRM/广告/relay 门禁 | S3 |
| AGT-10 | TODO | AGT-05,AGT-08,CEF-05 | `browser/cef-shell/src/browser/semantic_handle`、agent tools、tests | P2 R4 可见语义元素 handle 与 click/type；无 selector/脚本透传 | AG-010；密码/支付/文件/隐藏元素/跨源 frame 永久拒绝 | S3 |
| AGT-11 | TODO | AGT-02,AGT-03 | `crates/crayon-agent-gateway/src/receipt`、diagnostics、tests | 本机有界脱敏 action receipt、TTL、用户预览/清除 | AG-011、PV-010；无正文、query、secret；诊断不参与正确性 | S2 |
| AGT-12 | TODO | AGT-03,AGT-11,PRV-10 | `apps/desktop/agent-transport`、config、security tests | 默认关闭的 loopback transport、短期 secret、单客户端/限流/stop | AG-012；非 loopback、重放、CSRF/DNS rebinding、超限、退出 | S3 |
| AGT-13 | TODO | AGT-04,AGT-06,AGT-07,AGT-12 | `apps/desktop/agent-cli`、tests、docs | R0/R1 CLI Developer Preview；机器可读错误与版本协商 | AG-013；无交互环境不绕确认；Release 开关/帮助脱敏 | S3 |
| AGT-14 | TODO | AGT-04,AGT-06,AGT-07,AGT-12 | `apps/desktop/mcp`、MCP contracts、docs | 只读 MCP Developer Preview，映射 registry schema，不复制工具实现 | AG-014；initialize/list/call/cancel/version/oversize；默认关闭 | S3 |
| AGT-15 | TODO | AGT-08,AGT-09,AGT-10,AGT-13,AGT-14 | MCP/CLI adapters、`tests/security/agent`、E2E | R2～R4 Preview 与 fuzz/提示注入/本机恶意 client/资源上限验证 | AG-015；所有副作用确认；永久禁止 surface 零命中 | S4 |
| AGT-16 | TODO | AGT-13,AGT-14,AGT-15 | threat model、Review、`docs/current` | Agent 控制面总 Review、数据流、默认开关与 GO/NO-GO | 全 AG；P0/P1=0；P2 有任务；独立发布决策 | S4 |

## 垂直切片

1. `A1 权限内核`：`AGT-01..05,11`。只建立 schema、任务状态、grant、确认和 receipt，不开放 transport。
2. `A2 只读 Developer Preview`：`AGT-06,07,12..14`。可读取当前页/内容和投屏状态；默认关闭、loopback only。
3. `A3 受控副作用 Preview`：`AGT-08,09`。导航和投屏控制逐次确认，不能绕过产品状态机。
4. `A4 页面写操作实验`：`AGT-10,15,16`。只有安全评审 GO 才发布；NO-GO 不影响内容与投屏主产品。

## Review 专项

- MCP 规范的用户确认是产品最低线，不是假定 client 会替产品实现；服务端仍必须 fail closed。
- 不把 origin allow-list、secret redaction 或 loopback 当作单一安全边界；组合检查 client、Profile、tab、generation、grant、目标与参数。
- 检查 confused deputy：已授权读取当前页不能被复用为导航、提交、投屏或读取其他 Profile。
- 检查 stop/revoke 与 callback/模型/tool completion 竞态；旧结果不得执行补偿性副作用。
- Release 扫描原始 CDP/WebDriver、任意脚本、remote bind、Cookie API、文件上传和测试控制面。
