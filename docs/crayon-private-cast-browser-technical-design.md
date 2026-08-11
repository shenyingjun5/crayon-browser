# 蜡笔 AI Agent 投屏浏览器技术方案

- 版本：v0.7
- 日期：2026-08-11
- 权威边界：`docs/crayon-private-cast-browser-prd.md`、`docs/current/architecture.md`

## 1. 技术目标与交付顺序

在 Windows/macOS CEF 和后续 HarmonyOS 电脑 ArkWeb 上建设 Agent-native 浏览器。交付顺序为：浏览器基本能力 -> LAN Cast-SDK Direct/Relay -> 当前页数据/Markdown -> CAAP/CLI/入站 MCP -> 语义地图与可验证动作 -> Workflow/Challenge -> Capability Hub/合作方 -> 第二阶段模型。后续模块的 feature NO-GO 不阻塞已满足范围的浏览器/LAN 投屏核心版本。

## 2. 分层与进程边界

```text
CLI / Inbound MCP / Product UI
              |
     CAAP + agent-gateway
              |
 task / grant / confirmation / receipt
              |
          app-runtime
   /        /       |          \
browser  semantic workflow   capability hub
engine   action     /challenge  -> outbound connectors
   \        |       /          /
 page-data/content       cast SDK/relay
              |
 Windows/macOS/Harmony platform adapters
```

Browser process 拥有可信 gateway、generation、页面事实缓存、guard 与 app-runtime。Renderer 只做有界事实采集。CLI/入站 MCP worker 只解析、限流和写回。出站 connector worker 只持最小网络/OAuth 能力。第二阶段 model worker 不持有浏览器授权。

## 3. CAAP v1、CLI 与入站 MCP

逻辑 envelope 至少包含：

```text
protocol_version, message_id, client_session_id, message_kind,
target_ref?, task_id?, deadline_ms?, idempotency_key?, payload
```

- ID 为 opaque 高熵/强类型值；字符串、数组、递归、消息、chunk 和总结果有硬限制。
- `TargetRef` 绑定 Profile/tab/navigation/generation。
- registry 声明稳定 tool ID/version、risk、schema、target、确认、流式、预算与 app-runtime use case；CLI help、MCP tools/list、确认 UI 和 release scan 使用同一来源。
- handshake 在创建业务 task 前验证 CAAP 版本、当前 OS 用户、短期 secret、client 类型、Profile 与 feature。
- Windows CLI 使用 named pipe，macOS 使用 Unix domain socket；机器可读结果走 stdout，脱敏诊断走 stderr。
- 入站 MCP 默认关闭、loopback only，把 initialize/list/call/cancel 映射 CAAP，不自行实现工具或权限。

通用文件上传不在 v1 schema。未来受限上传必须使用用户逐次选择、origin/用途/文件/TTL 绑定 grant，且另立 Roadmap。

## 4. 页面事实、Markdown 与语义地图

### 4.1 采集与缓存

- Renderer collector 从 DOM 和适用 accessibility facts 生成最小、版本化事实块，过滤 script/style、隐藏敏感值、密码/支付/file 值、跨源 iframe 正文和危险 URL。
- Browser gateway 验证 renderer/frame/origin/navigation/generation 后合并到有界 cache。
- 缓存按 Profile/tab/navigation 所有，含正文/结构索引和交互事实；导航、关闭、销毁、撤销、TTL 或内存压力清除。
- mutation 生成 dirty revision 和有界 `ChangeSet`，不承诺每次 DOM mutation 都全量重建。
- Markdown、R1 读取、语义地图和 Workflow 共享一次验证后的事实。

### 4.2 公共 DTO

```text
PageSnapshot {
  schema_version, target_ref, snapshot_id, revision, title,
  blocks[], links[], tables[], code_blocks[], provenance, truncation
}

SemanticMaps {
  action_map[], form_map[], media_map[], risk_map[], change_set?
}

ActionDescriptor {
  action_id, role, accessible_name, allowed_actions[],
  visible_state, risk_flags[], preconditions[], ttl
}
```

`compact` 返回任务相关摘要，`standard` 返回有界完整公共结构。`full` 仅为内部诊断/验证/受控修复 profile，仍经过字段 allowlist 与预算，禁止外发原始 DOM、HTML、CDP、对象指针或长期 selector。

`FormMap` 只包含字段语义、required/format/error/filled 状态，不包含值。`MediaMap` 只包含可见媒体事实和经产品策略计算的能力。`RiskMap` 由确定性规则生成，页面或模型不能降低风险。

### 4.3 输出与性能

- 小结果一次返回；大结果使用 sequence/cursor chunk。服务端每 task 的未确认 chunk 固定上限，cancel/deadline 传播到采集与清洗。
- provenance 标识 frame 范围、可见性、截断、revision 和 hash。
- cache 命中元数据/R1 标题本机 P95 目标不高于 50ms；100KB 清洗正文结构/Markdown P95 目标不高于 500ms，最终以 `AGT-15` benchmark 固化。
- benchmark 记录 first chunk、complete、CPU、RSS、UI event-loop delay、序列化字节与增量复用；不得在无对照证据时宣称全面快于其他浏览器。
- 常规读页不使用 screenshot/OCR；视觉只允许内部有界 fallback，并记录原因和风险。

## 5. action_id 与可验证动作

### 5.1 创建和绑定

`action_id` 由 Browser 根据 verified facts 签发，绑定 Profile、tab、navigation、generation、语义摘要、允许动作、风险、TTL 与随机 nonce，不包含 DOM 指针。内部 `LocatorEvidence` 可保存 role/name/text/结构邻近/可见性/几何等有界特征，但绝不作为外部稳定 API。

### 5.2 执行协议

```text
Resolve target/generation
 -> re-locate unique visible semantic target
 -> validate preconditions + monotonic risk
 -> validate grant/confirmation/parameter hash/idempotency
 -> app-runtime action use case
 -> wait bounded declared effect
 -> Verified | Failed | Indeterminate | AwaitingHuman
```

- 目标不唯一、被遮蔽、跨源、隐藏、过期或风险上升时 fail closed。
- password/payment/file 不产生可执行 action_id；任意 JS、selector 直通、自动滚动点击挑战均拒绝。
- effect 可为导航、ChangeSet、字段状态、页面语义或投屏状态。只发送输入事件不算成功。
- `Indeterminate` 副作用不自动重试；幂等 key 只防重复，并不证明业务成功。
- 高风险动作始终重新确认；低风险动作也必须在 target/generation 改变后重新读取。

## 6. grant、确认与不可信内容

grant 绑定 client/session/Profile/tool/risk/target/route/到期，支持单次、单任务及明确选择的 R0/R1 App 会话；重启失效，R1 不可升级 R2-R4。confirmation 展示 client、route、页面/脱敏 origin、目标语义、关键参数、影响、数据外发和到期，其 nonce 绑定参数 hash、generation 和 provider。

页面、模型、Recipe、合作方 tool description/response 使用 `UntrustedContent` 或等价 provenance。它们不能被解释为 CAAP envelope、grant、confirmation、route policy 或下一次工具调用。

## 7. Challenge 与 checkpoint

`ChallengeSignal` 只能描述 challenge_type、证据类别、页面区域、confidence band 和时间，不能包含解题内容。确定性检测命中后：

1. 取消尚未执行的自动步骤并切换 `AwaitingHuman`。
2. UI 显示原因、目标页面和继续/取消；不隐藏挑战或调用代解服务。
3. 写入有界、加密、短期 `Checkpoint`：task/recipe/version/target、最后 verified step、下一意图、idempotency 状态、到期；不含 secret、字段值或正文。
4. 用户完成后重新 snapshot/risk/action/grant/precondition。挑战仍在、页面不匹配、到期或结果不确定则终止。

## 8. Workflow Learning 与个人 Site Skill

### 8.1 数据结构

```text
WorkflowTrace { trace_id, owner_scope, task_intent, verified_steps[], outcome, provenance, ttl }
Recipe { recipe_id, version, origin_matcher, parameters[], steps[], expected_effects[], risk_summary }
SiteSkill { skill_id, owner_profile, recipe_version, health, validation, source, enabled, rollback_ref }
```

- trace 只记录稳定意图、action semantic、参数 placeholder、结果和 hash；redactor 在写盘前移除输入值、secret、正文、完整 URL query 和账户标识。
- 仅 `outcome=verified_success` 可生成 candidate；失败、取消、challenge 未完成或 effect unknown 直接丢弃学习候选。
- 保存是显式用户动作；预览必须显示名称、origin matcher、参数、步骤、权限、风险、数据外发和有效期。
- store 按 OS user/Profile 加密隔离；schema/version 迁移失败时禁用技能，不进行猜测性升级。

### 8.2 验证、健康和修复

- 保存后先在本地 fixture/沙箱验证 matcher、参数、步骤和预期效果；生产站点健康使用有界失败窗口，不做后台批量巡检。
- runner 每次重新路由并获取 grant；步骤只引用当前 action_id，不复用记录时 handle。
- 失败按 drift、challenge、permission、network、effect unknown 分类；版本更新先创建 candidate，可回滚到最近健康版本。
- controlled healer 只可在低风险、唯一多信号匹配、effect 可验证且无新数据外发时替换 locator evidence；高风险、跨源、低置信度或步骤语义变化只能提出用户审阅的修复候选。

## 9. Capability Registry 与 Router

### 9.1 Registry

```text
CapabilityDescriptor {
  id, version, source, trust_state, lifecycle_state,
  input_schema, output_schema, risk, data_scope,
  site_matchers, health, cost_hint, confirmations, provider_ref?
}
```

来源包括 built-in、personal Site Skill、approved partner。生命周期为 candidate/validated/enabled/degraded/disabled/revoked。相同 ID 不允许未签名覆盖；用户和 kill switch 可禁用。

### 9.2 Router

Router 输入 task intent、target、user preference、grant、risk、health、trust、数据范围和可用 provider，输出 selected route、ordered alternatives、`route_reason`、required confirmation 和 fallback policy。默认优先 approved Partner API/MCP、healthy Site Skill、Web Automation、Human Handoff、Reject。

fallback 必须重新校验：

- 新 route 的 schema、scope、provider、数据外发与风险。
- idempotency 是否可跨 route；若无法证明则停止。
- 是否需要新的 OAuth、grant、confirmation 或用户数据预览。
- 上一路径是否已有不确定副作用；有则不得继续。

## 10. 出站 Partner API/MCP Connector

出站连接器与入站 MCP 分属独立 crate、配置、token、网络 client、registry namespace 和审计事件。

- manifest/package：受信来源、固定版本、签名/兼容校验、撤销、禁用与 kill switch；动态 tool description 不可创建本地高权限工具。
- OAuth：state、nonce、PKCE（适用时）、redirect 精确匹配、最小 scope、provider/tenant/account 绑定；token 仅在 secure vault，通过 opaque handle 使用。
- 网络：endpoint allowlist、解析后 IP 检查、每次 redirect/DNS 重验，阻断 loopback/private/link-local/metadata；限制方法、header、body、response、时间和并发。
- runtime：rate limit、retry budget、exponential backoff、circuit breaker、health、quota 和 cancel；副作用调用默认不自动 retry。
- 输出：严格 schema/内容类型/大小验证；错误不回显 token/正文；审计仅记录 provider、tenant hash、capability、结果类别和延迟。

## 11. 投屏与 Partner Cast Manifest

- Direct/Relay 仅 LAN，浏览器只调用固定 Cast-SDK facade；Relay 使用高熵 session/resource ID、设备/route/TTL/upstream allow-set 绑定。
- Agent/Workflow 的 R3 投屏调用相同 `cast_usecase`，经过用户真实播放、DRM、广告、receiver capability 和确认。
- 无路由返回 `ExternalClientHandoff`，不创建浏览器 WebRTC/采集/编码会话。
- Partner/TV Cast Manifest 的签名、能力协商、字幕/队列/结果回报属于 Cast-SDK/receiver。浏览器先做 API 缺口分析；只有外部仓库经授权完成、固定版本发布后，adapter 才消费批准 facade，禁止临时拼协议或控制 URL。

## 12. 第二阶段模型

- 先完成 provider ADR：本地/云端/BYOK、地区、费用、保留、安全存储、endpoint 和错误语义；此前只有 Fake provider contract。
- 文档总结只接收用户确认的清洗 DTO；视频总结只接收合法可见字幕/转录或用户提供文本。
- 模型辅助 locator/Recipe 只能生成不可信 candidate；确定性 policy、risk 和用户确认决定是否采纳。
- 输出绑定 snapshot/hash/provenance；超时/取消/失败不影响本地 Markdown、动作或技能。

## 13. 测试、性能与供应链

- CAAP current/previous golden、握手、边界、取消、deadline、幂等、重放与 CLI/MCP 同义性。
- 页面 fixture 覆盖长文、表格、iframe、动态变化、隐藏/敏感表单、challenge 和 prompt injection。
- semantic/action 测试覆盖 ID 稳定窗口、precondition、风险单调、效果验证、旧 generation 和不确定副作用。
- Workflow 测试覆盖 redaction、verified-only learning、预览保存、Profile 隔离、健康、回滚、低风险修复与高风险禁止。
- Hub/connector 测试覆盖 route reason、fallback 重授权、签名/revoke、OAuth、SSRF/DNS rebinding、tool injection、限流/熔断和审计脱敏。
- performance harness 记录 first chunk、complete、UI delay、CPU/RSS、字节、重复任务步骤/时延和增量命中。
- release scan 禁止 remote bind、原始 CDP/WebDriver、任意 JS、Cookie/通用文件上传/通用网络工具、挑战绕过和浏览器自建 Cast 协议。
- Windows/macOS CEF、HarmonyOS 电脑 ArkWeb；Linux 不实现。新增依赖检查来源、许可证、维护、包体和跨平台影响。
