# HUB：Capability Registry、Router 与合作方连接器 Roadmap

- 状态：`HUB-01 DONE`（2026-08-23）；`HUB-02/HUB-03 READY`，其余 TODO
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
| HUB-01 | DONE | AGT-02,PRV-08 | `crayon-domain/capability/**`,`crayon-capability-hub/registry/**` | Capability descriptor、source/trust/lifecycle/version schema | `HB-001`; golden/冲突/撤销 |
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

- 状态：`DONE`；依赖 `AGT-02 DONE`、`PRV-08 DONE`。
- 单一目标：`crayon-domain` 新增 `capability.rs`（闭合 source/trust/lifecycle/version schema + serde）与新建 `crayon-capability-hub` crate 的 `registry` 模块：确定性注册、冲突拒绝、撤销立即生效、快照 golden。不含 router/policy/fallback/connector。
- 边界：
  - `CapabilitySource = Builtin/PersonalSkill/Partner`（优先级递减）；`TrustLevel = System/UserApproved/Untrusted`；`LifecycleState = Active/Disabled/Revoked`（Revoked 对该 id+version 终态）。
  - 注册规则：同 id 首次注册生效；覆盖仅允许"source 优先级 ≥ 既有且版本不同"，否则 `Conflict` 稳定拒绝——Builtin 不可被 Personal/Partner 覆盖（不可未签名覆盖）；Revoked 后同 id+version 拒绝重注册，新版本可注册。
  - trust 与 source 一致性校验（Partner 不得声明 System trust）；id 为闭合 token；描述字段有界。
  - snapshot 为确定性排序输出（golden 锁定）；撤销立即反映在 snapshot 与查询。
- 验收与测试：HB-001。矩阵：注册/幂等、覆盖优先级矩阵、冲突拒绝、撤销立即生效与终态、trust 冲突、golden 快照、风暴不变量。命令：`cargo test -p crayon-capability-hub`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：router/policy/fallback（HUB-03/04/05）、内建能力清单（HUB-02）、partner connector（HUB-09+）、网络/IO。

### HUB-01 完成记录（2026-08-23）

- 实现：`crayon-domain` 新增 `capability.rs`（约 230 行）+ `capability_tests.rs`：闭合 `CapabilitySource = Partner(0)/PersonalSkill(1)/Builtin(2)`（precedence 大者优先，serde/wire 名一致）、`TrustLevel`、`LifecycleState`、`DataScope` 四个闭合枚举与 `CapabilityDescriptor { id, version, source, trust, data_scope, summary }`；id/version 走闭合字符集 `[a-z0-9_.:-]`（≤64/≤32 字节），summary ≤256 字节仅限长度校验；Partner 声明 System trust 在 schema 层拒绝（TrustConflict）；`wire_tag()` 产出 `id@version:source:trust:scope` 确定性标签。新建 `crates/crayon-capability-hub` crate：`registry.rs`（约 250 行）单 current-per-id 注册表——首次注册生效；替换要求 source precedence `>=` 既有且版本不同，否则 `Conflict`（Builtin 不可被 Personal/Partner 覆盖）；同 id+version 重复注册稳定拒绝（`DuplicateRegistration`）；`Revoked` 对 id+version 终态——当前版本撤销立即生效且可幂等重复，被撤销版本归档（每 id 上界 `MAX_REVOKED_HISTORY_PER_ID=8`，满载后该 id 再注册 fail closed 返回 `RevocationHistoryFull`，永不静默丢弃 tombstone），新版本可在撤销后按优先级规则接替；`set_enabled` 绑定精确 version（stale 调用者失败而非作用于已替换记录），离开 Revoked 不可能（`LifecycleTerminal`）；容量 `MAX_REGISTRATIONS=64` 满载 `Capacity`（既有 id 的替换不受影响）；`snapshot()` 按 id 确定序输出 `id|version|source|trust|data_scope|state`，排除自由文本 summary；错误枚举闭合且稳定 Display。workspace members 注册新 crate；无新增第三方依赖；全同步、无锁/线程/IO/时钟。
- 修正（相对 WIP 初稿）：移除不可编译的 `From<CapabilitySchemaError> for CoreError`（`CoreError` 无 `InvalidInput` 变体且为 FND-08 冻结契约，域内各模块各自持有闭合错误，与 agent/config/diagnostics 一致）；`trust_wire_name()` 从 descriptor 私有方法改为 `TrustLevel::wire_name()` 公开常量方法，与其余枚举对齐。
- 验证：`cargo test -p crayon-capability-hub` 11/11 通过（golden 快照逐字节一致与重建确定性、3x3 替换优先级矩阵全格锁定、首注生效/同 pair 拒绝含字段篡改对照、撤销即时生效+幂等+终态+未知目标、撤销后新版本接替并归档可查、lifecycle 版本绑定与 stale 拒绝、schema 校验矩阵含 Partner+System、容量上界、撤销历史满载 fail closed、LCG 3000 步风暴不变量——容量上界/每 id 活跃 precedence 不降/已撤销 pair 永不复活/快照恒定 id 序）；`cargo test -p crayon-domain --lib` 含 capability 6 项（token 边界矩阵、validate 矩阵含 256/257 字节边界、trust 冲突、precedence 序、四枚举 serde wire 名 roundtrip、wire_tag golden）；`cargo clippy -p crayon-capability-hub -p crayon-domain --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；基线回归 core lib 3/3、legacy-dev lib 58/58、workspace 全量无失败；`git diff --check` 通过。
- Code Review：按需求/边界→正确性→架构/API→并发/生命周期→安全/隐私→性能→测试→可维护性复核。P0 0、P1 0、P2 2：(1) 同 id+version 已存在时，低优先级来源得到 `Conflict` 而足够优先级来源得到 `DuplicateRegistration`——两序皆可辩护，取"优先级先判"使越权覆盖尝试获得更具诊断性的错误；行为已被 golden/矩阵测试锁定。(2) Active 记录被新版本替换后旧版本即被遗忘，此后旧版本可再次注册（同优先级降级换版不受 HUB-01 约束）——Roadmap 未约束该情形，partner 包的降级防护明确归 `HUB-10`（签名/篡改/降级/撤销/kill switch），builtin 由编译期权威来源（`HUB-02`）保证。
- 未覆盖与风险：router/policy/fallback（HUB-03/04/05）、内建能力清单注册（HUB-02）、partner connector 与网络/IO（HUB-09+）均未涉及；registry 为进程内 v1 语义不持久化，重启即清（与 grant/receipt 同口径）。`HUB-01` 转为 `DONE`，解锁 `HUB-02`、`HUB-03`。
