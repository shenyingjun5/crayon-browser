# HUB：Capability Registry、Router 与合作方连接器 Roadmap

- 状态：规划完成，尚未开工
- 任务数：16
- 目标：为内建能力、个人 Site Skill、受控网页自动化、人工接管及已批准 Partner API/MCP 提供统一描述、可解释路由和隔离的出站执行边界
- 非目标：远程入站控制、动态插件任意代码、凭证暴露、开放代理、透明跨路径重复副作用、浏览器自行定义 Cast 协议

## 1. 边界

- 入站 MCP 是 CAAP adapter；出站 Partner MCP/API 是独立 connector。registry namespace、session、token、网络 client 和审计不得复用。
- Router 只选择能力，不执行页面/网络/投屏；实际调用回到 app-runtime 或受限 connector。
- fallback 是一次新的授权决策，不继承 provider、scope、confirmation 或幂等假设。
- Partner/TV Cast Manifest 只通过 Cast-SDK 正式 facade 接入。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 单一交付 | 验收与测试 |
|---|---|---|---|---|---|
| HUB-01 | TODO | AGT-02,PRV-08 | `crayon-domain/capability/**`,`crayon-capability-hub/registry/**` | Capability descriptor、source/trust/lifecycle/version schema | `HB-001`; golden/冲突/撤销 |
| HUB-02 | TODO | HUB-01 | `crayon-capability-hub/builtin/**` | 内建 browser/content/cast/handoff 能力从权威 registry 注册 | `HB-002`; 无重复 schema/隐藏强工具 |
| HUB-03 | TODO | HUB-01 | `crayon-capability-hub/router/**` | RouteInput/RouteDecision/candidate/route_reason 稳定契约 | `HB-003`; 确定性 snapshot |
| HUB-04 | TODO | HUB-02,HUB-03 | `crayon-capability-hub/policy/**` | partner -> skill -> web -> human -> reject 默认策略及覆盖规则 | `HB-004`; trust/risk/health/preference 矩阵 |
| HUB-05 | TODO | HUB-04,AGT-04,AGT-11 | `crayon-capability-hub/fallback/**` | fallback 重授权、重确认、幂等和未知副作用停止 | `HB-005`; 跨 route 不静默重放 |
| HUB-06 | TODO | HUB-04,AGT-05 | `apps/desktop-cef/**/capability-route/**`,locales | route 预览、理由、偏好和临时覆盖 UI | `HB-006`; 数据外发/成本/风险可见 |
| HUB-07 | TODO | HUB-02,WFL-12 | `crayon-capability-hub/adapters/site_skill/**` | 个人 Site Skill registry adapter | `HB-007`; owner/Profile/health/版本隔离 |
| HUB-08 | TODO | HUB-03,AGT-14 | `crayon-agent-gateway/tools/capability/**` | 入站 MCP/CLI 能力 search/describe/preview，经 CAAP 暴露 | `HB-008`; 不泄漏 token/endpoint/隐蔽工具 |
| HUB-09 | TODO | HUB-01,PRV-10 | `crayon-partner-connector/api/**` | 与入站 MCP 分离的出站 Partner connector interface | `HB-009`; crate/dependency/session 隔离 |
| HUB-10 | TODO | HUB-09 | `crayon-partner-connector/trust/**` | 来源、版本、签名、兼容、revoke、disable 和 kill switch | `HB-010`; 篡改/降级/撤销/离线 |
| HUB-11 | TODO | HUB-09,PRV-07 | `crayon-partner-connector/oauth/**`,`crayon-platform-api/**` | OAuth state/PKCE、最小 scope 和 provider/tenant token vault | `HB-011`; redirect/CSRF/scope/清除/串租户 |
| HUB-12 | TODO | HUB-09,PLT-02 | `crayon-partner-connector/network/**` | endpoint allowlist、DNS/重定向重验、SSRF 与消息预算 | `HB-012`; rebinding/private/metadata/oversize |
| HUB-13 | TODO | HUB-10,HUB-11,HUB-12 | `crayon-partner-connector/mcp/**` | 出站 Partner MCP namespace、tool/schema 过滤和不可信响应 | `HB-013`; description injection 不可扩权 |
| HUB-14 | TODO | HUB-09,HUB-12 | `crayon-partner-connector/runtime/**` | health、rate/quota、retry budget、熔断、取消 | `HB-014`; 副作用默认不 retry；资源有界 |
| HUB-15 | TODO | HUB-05,HUB-13,HUB-14,AGT-11 | `crayon-capability-hub/audit/**`,`diagnostics/**` | provider/tenant hash/capability/route/结果的脱敏审计指标 | `HB-015`; 无正文/token/完整参数 |
| HUB-16 | TODO | HUB-01..HUB-15 | threat model,Review,`docs/current/**` | Hub/Partner connector 安全、隐私、供应链与性能总 Review | 全 HB；P0/P1=0；partner feature 独立 GO/NO-GO |

## 3. 完成门禁

- connector 只能访问受审 endpoint 和声明 scope；任何 redirect/DNS 变化重新检查，禁止通用 proxy。
- 动态 tool description、schema 和响应均不可信，不能注册高于 manifest 的能力或改变本地 policy。
- package/manifest 未签名、版本不兼容、已撤销或 kill switch 命中时 fail closed。
- Partner 能力未达到门禁时 Hub 仍可只运行 built-in/Site Skill/Web/Human 路径，不阻塞核心浏览器发布。

### HUB-01 原子范围（Capability descriptor 与 registry schema）

- 状态：`IN_PROGRESS`；依赖 `AGT-02 DONE`、`PRV-08 DONE`。
- 单一目标：`crayon-domain` 新增 `capability.rs`（闭合 source/trust/lifecycle/version schema + serde）与新建 `crayon-capability-hub` crate 的 `registry` 模块：确定性注册、冲突拒绝、撤销立即生效、快照 golden。不含 router/policy/fallback/connector。
- 边界：
  - `CapabilitySource = Builtin/PersonalSkill/Partner`（优先级递减）；`TrustLevel = System/UserApproved/Untrusted`；`LifecycleState = Active/Disabled/Revoked`（Revoked 对该 id+version 终态）。
  - 注册规则：同 id 首次注册生效；覆盖仅允许"source 优先级 ≥ 既有且版本不同"，否则 `Conflict` 稳定拒绝——Builtin 不可被 Personal/Partner 覆盖（不可未签名覆盖）；Revoked 后同 id+version 拒绝重注册，新版本可注册。
  - trust 与 source 一致性校验（Partner 不得声明 System trust）；id 为闭合 token；描述字段有界。
  - snapshot 为确定性排序输出（golden 锁定）；撤销立即反映在 snapshot 与查询。
- 验收与测试：HB-001。矩阵：注册/幂等、覆盖优先级矩阵、冲突拒绝、撤销立即生效与终态、trust 冲突、golden 快照、风暴不变量。命令：`cargo test -p crayon-capability-hub`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：router/policy/fallback（HUB-03/04/05）、内建能力清单（HUB-02）、partner connector（HUB-09+）、网络/IO。
