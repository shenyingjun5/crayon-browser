# HUB：Capability Registry、Router 与合作方连接器 Roadmap

- 状态：`HUB-01..04 DONE`（2026-08-24）；`HUB-05/HUB-06 READY`，其余 TODO
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
| HUB-02 | DONE | HUB-01 | `crayon-capability-hub/builtin/**` | 内建 browser/content/cast/handoff 能力从权威 registry 注册 | `HB-002`; 无重复 schema/隐藏强工具 |
| HUB-03 | DONE | HUB-01 | `crayon-capability-hub/router/**` | RouteInput/RouteDecision/candidate/route_reason 稳定契约 | `HB-003`; 确定性 snapshot |
| HUB-04 | DONE | HUB-02,HUB-03 | `crayon-capability-hub/policy/**` | partner -> skill -> web -> human -> reject 默认策略及覆盖规则 | `HB-004`; trust/risk/health/preference 矩阵 |
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

### HUB-02 原子范围（内建能力权威注册）

- 状态：`DONE`（2026-08-23）；依赖 `HUB-01 DONE`。
- 单一目标：`crayon-capability-hub` 新增 `builtin.rs`：编译期权威的内建能力目录（browser/content/cast/handoff 四域各一项，全部 `source=Builtin`、`trust=System`），经 `HUB-01` 正常注册路径写入 `CapabilityRegistry` 并提供快照 golden；本任务不定义路由、策略或新 schema。
- 输入：HB-002（schema 来自权威来源；无重复工具和隐藏强能力）、架构 §8（每个能力声明稳定 ID/version/来源/信任/数据范围/生命周期）、PRD §4.7、`HUB-01` 的 descriptor schema 与 registry 规则。
- 输出与允许修改：`crates/crayon-capability-hub/src/builtin.rs`、`builtin_tests.rs`、`lib.rs` 仅加模块声明、crate `tests/` 新增快照 golden、`Cargo.toml` 仅可加 `crayon-agent-gateway` dev-dependency（永久禁止清单交叉核对，测试图专用）、本 Roadmap。
- 禁止修改：`HUB-01` registry/descriptor 行为与其 golden、domain schema、其他 crate 生产代码；不得注册超出四域目录的能力，不得引入网络/IO 或 partner 包加载。
- 边界：
  - 目录冻结 4 项：`builtin.browser`（受控导航/标签操作，`local_only`）、`builtin.content`（有界当前页内容提取与确定性 Markdown，`page_content`）、`builtin.cast`（经正常投屏门禁的会话选择与播放控制，`cast_control`）、`builtin.handoff`（暂停并移交人工接管/建议外部客户端，`local_only`）；统一版本取自单一目录常量。
  - 全部描述符必须通过 schema 校验；id 以 `builtin.` 前缀且不命中 AGT 永久禁止词汇表（dev 测试交叉核对）；summary ≤256 字节且不含凭证形态内容。
  - 注册只走 `CapabilityRegistry::register` 公共路径，无旁路注入；重复调用稳定拒绝且注册表不变。
- 验收与测试：HB-002。矩阵：全量注册成功、schema/source/trust/data_scope 断言、id 集合精确锁定（防隐藏能力）、永久禁止清单零命中、built-in 不可被 personal/partner 覆盖（Conflict）、同版本重注册拒绝、golden 快照逐字节一致。命令：`cargo test -p crayon-capability-hub`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：router/policy/fallback（HUB-03/04/05）、Site Skill adapter（HUB-07）、partner connector（HUB-09+）、CAAP 能力发现暴露（HUB-08）。

### HUB-03 原子范围（Router 稳定契约与确定性解析）

- 状态：`DONE`（2026-08-24）；依赖 `HUB-01 DONE`。
- 单一目标：`crayon-capability-hub` 新增 `router.rs`：冻结 `RouteInput`/`RouteCandidate`/`RouteEvaluation`/`RouteOutcome`/`RouteKind`/`RouteReason`(以闭合 outcome 承载)/`RouteDecision` 契约与确定性 `resolve()`——把输入 id 对照 registry 解析为候选与逐项结论，输出确定性快照；本任务不实现默认策略选择、trust/health/preference 覆盖或 fallback 重授权（HUB-04/05）。
- 输入：HB-003（相同 RouteInput 重复求值稳定、理由完整、无 secret/内部 endpoint）、架构 §8（Router 输出选定 route、候选、route_reason、必要授权和 fallback 条件；默认顺序 partner→skill→web→human→reject 由 HUB-04 落地）、`HUB-01` registry 查询视图。
- 输出与允许修改：`crates/crayon-capability-hub/src/router.rs`、`router_tests.rs`、`lib.rs` 仅加模块声明、crate `tests/` 新增决策快照 golden、本 Roadmap。零第三方新增。
- 禁止修改：registry/descriptor 行为与既有 golden、builtin 目录、其他 crate；不得引入网络/IO/时钟；不得在契约中携带 endpoint/token/summary 自由文本。
- 边界：
  - `RouteKind` 闭合五类且声明序即默认优先级序（Partner/SiteSkill/WebAutomation/HumanHandoff/Reject）；由 `CapabilitySource` 派生前三类，HumanHandoff/Reject 只能由后续策略层显式构造、不可从注册派生。
  - `RouteOutcome` 闭合四类：resolved/unknown_id/disabled/revoked；逐输入 id 一条评估，输入顺序保持；候选只含 resolved 且按 (kind 序, id) 确定排序。
  - `RouteInput` 校验：闭合 token、数量 ≤16、拒绝重复 id；错误闭合枚举。
  - 快照只含闭合 token 与枚举 wire 名，排除 summary/endpoint/secret；同输入重复解析逐字节一致。
- 验收与测试：HB-003。矩阵：重复求值一致性、四种 outcome、候选确定排序与输入顺序无关、输入校验（非法/超量/重复）、快照无自由文本泄漏、golden 逐字节一致、LCG 不变量（同输入同输出、候选恒排序）。命令：`cargo test -p crayon-capability-hub`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：默认策略与选择逻辑、覆盖规则（HUB-04）、fallback 重授权（HUB-05）、CAAP 能力发现暴露（HUB-08）。

### HUB-04 原子范围（默认路由策略与覆盖规则）

- 状态：`DONE`（2026-08-24）；依赖 `HUB-02 DONE`、`HUB-03 DONE`。
- 单一目标：`crayon-capability-hub` 新增 `policy.rs`：在 HUB-03 解析出的候选之上落地冻结默认策略 `Partner -> SiteSkill -> WebAutomation -> HumanHandoff -> Reject` 与两类覆盖规则（用户偏好提前 kind、数据外发约束），trust 不足候选一律排除，产出独立 `PolicyDecision { selected, fallback, reason, exclusions }` 并提供组合确定性快照；本任务不含 fallback 执行/重授权（HUB-05）、UI（HUB-06）与健康度信号（数据源尚不存在）。
- 输入：HB-004（partner/skill/web/human 的 trust/health/risk/偏好组合；默认优先级与覆盖规则确定；不可用路径不被选择）、架构 §8 默认策略与覆盖因素、`HUB-03` 路由契约。
- 输出与允许修改：`crates/crayon-capability-hub/src/policy.rs`、`policy_tests.rs`、`router.rs`（仅追加 `RouteCandidate.data_scope` 字段及快照列，`RouteDecision` 形状不变）、router golden 因新增 data_scope 列同步重审更新、`lib.rs` 仅加模块声明、crate `tests/` 新增策略决策 golden、本 Roadmap。零第三方新增。
- 禁止修改：registry/builtin 行为与既有 registry/builtin golden、domain schema、其他 crate；不得实现 fallback 执行或任何网络/IO；健康度因子不得凭空建模（无数据源即不进策略）。
- 边界：
  - 默认序即 `RouteKind` 声明序；不可用路径（unknown/disabled/revoked）天然不在候选内，策略只对 resolved 候选裁决。
  - trust 门禁：`TrustLevel::Untrusted` 一律排除（approved partner/user-saved skill 语义）；数据外发约束关闭时 `DataScope::ExternalEndpoint` 候选排除；两项排除均记入闭合 `ExclusionReason` 且按 id 排序。
  - 用户偏好 `prefer_kind` 只能把该 kind 提到最前，不改变其余相对序；偏好为 `Reject` 视为非法输入稳定拒绝。
  - 无剩余候选 → selected=None、reason=all_candidates_excluded/no_candidates；fallback 为剩余 kind 升序去重并恒以 HumanHandoff、Reject 收尾——每个 fallback 步骤都是一次新的授权决策（语义写入文档注释，执行归 HUB-05）。
  - 快照新增 selected/fallback/exclusions 段，仍只含闭合 token 与 wire 名。
- 验收与测试：HB-004。矩阵：默认优先级全序锁定、untrusted 排除后次优接管、外发约束矩阵、偏好提前与非法偏好、不可用路径端到端不被选择、exclusions 记录与排序、fallback 链确定性与收尾项、golden 更新逐字节一致、LCG 不变量（选中者必过门禁且无更优先可用候选）。命令：`cargo test -p crayon-capability-hub`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：fallback 重授权/幂等（HUB-05）、route 预览 UI（HUB-06）、Site Skill 健康 adapter（HUB-07）、CAAP 能力发现（HUB-08）、partner connector（HUB-09+）。

### HUB-04 完成记录（2026-08-24）

- 实现：`crayon-capability-hub` 新增 `policy.rs`（约 290 行）：冻结默认策略按 `RouteKind` 声明序生效——approved Partner -> healthy Site Skill -> Web Automation -> HumanHandoff -> Reject；`PolicyPreferences { prefer_kind, allow_external_endpoint }` 两类覆盖：偏好 kind 提到最前且不改变其余相对序、外发约束关闭时排除 `DataScope::ExternalEndpoint`；trust 门禁 `Untrusted` 一律排除（approved partner / user-approved skill 语义），双门禁命中按先判 trust 记因；`apply()` 产出独立 `PolicyDecision { selected, fallback, reason, exclusions }`（`RouteDecision` 保持纯解析产物，所有权分离），`reason` 闭合五态（含 `SelectedByUserPreference` 仅在偏好实际改变胜者时给出）、exclusions 按 id 排序、fallback 为剩余可用 kind 升序去重并恒以 HumanHandoff+Reject 收尾，语义为"下一次全新授权决策"的参考顺序而非执行（HUB-05）；`prefer_kind=Reject` 稳定拒绝；`PolicyDecision::snapshot(&decision)` 组合快照仅含闭合 token 与 wire 名。`router.rs` 仅追加 `RouteCandidate.data_scope` 字段与快照列，router golden 同步更新。零第三方新增；全同步、无锁/IO/时钟。
- 设计说明：原范围草稿写"字段并入 RouteDecision"，实现改为兄弟类型 `PolicyDecision`——避免 router↔policy 模块互相引用，保持"解析/裁决"单一所有者；HUB-03 完成记录中 P2 所述的 RouteDecision 形状演进因此不再需要，两个 golden 中仅 router golden 因 data_scope 列变化并已重审。
- 验证：`cargo test -p crayon-capability-hub` 35/35 通过（policy 新增 9 项：默认序选中 approved partner 且 untrusted 双胞胎被记因排除、纯 untrusted 输入 Reject+AllCandidatesExcluded、外发约束矩阵与门禁先判顺序锁定、偏好提升 web 胜出且 fallback 保序、Reject 偏好非法、不可用路径端到端不可选（禁用/撤销 id 不产生候选）、空解析 NoCandidates、策略 golden 逐字节一致、LCG 3000 步不变量——选中者必过双门禁/有效排序下无更优可行候选/fallback 恒以 human_handoff+reject 收尾/exclusions 有序/重复求值字节一致）；clippy `-D warnings` 零告警；fmt 通过；workspace 全量无失败；`git diff --check` 通过。
- Code Review：按标准八维复核。P0 0、P1 0、P2 1——每候选只记录一个排除原因（先判 trust），同时命中双门禁时外发原因被遮蔽；属可接受的确定性取舍，已由测试注释锁定，若 HUB-06 UI 需要完整原因可在 Exclusion 上扩展闭合多原因。
- 未覆盖与风险：健康度因子无数据源未建模（待 WFL-12/HUB-07 提供 Site Skill health 后扩展）；fallback 执行/重授权/幂等归 HUB-05；route 预览 UI 归 HUB-06。`HUB-04` 转为 `DONE`。

### HUB-03 完成记录（2026-08-24）

- 实现：`crayon-capability-hub` 新增 `router.rs`（约 280 行）：`RouteKind` 闭合五类且声明序即冻结默认优先级序（Partner/SiteSkill/WebAutomation/HumanHandoff/Reject，`rank()` 与 `Ord` 同源），`route_kind_of_source()` 只从 `CapabilitySource` 派生前三类、HumanHandoff/Reject 不可由注册派生；`RouteInput::new` 校验闭合 token、≤16 个 id、拒绝重复（`RouterError` 闭合三态）——输入是 untrusted 提案，只能经 registry 解析；`resolve()` 对每个 id 产出一条 `RouteEvaluation`（`RouteOutcome` 闭合四类 resolved/unknown_id/disabled/revoked，Resolved 才携带候选），live 注册成为 `RouteCandidate { id, version, kind, trust }` 并按 `(kind rank, id)` 确定排序；`RouteDecision::snapshot()` 只输出闭合 token 与枚举 wire 名两段列表，排除 summary/endpoint/secret。零第三方新增；全同步、无锁/线程/IO/时钟。
- 验证：`cargo test -p crayon-capability-hub` 26/26 通过（router 新增 8 项：golden 决策快照逐字节一致、HB-003 核心属性——同输入重复求值含重建 registry 后值相等且字节一致、四 outcome 全可达且 Resolved 与候选一一对应、候选排序与输入顺序无关并锁定 (rank,id) 序、kind 派生闭合与优先级序锁定、输入校验矩阵含边界 16/17、快照无自由文本泄漏（注入 summary/endpoint/token 标记断言不出现）、LCG 3000 步不变量——同输入同输出/候选严格有序/候选数=resolved 数）；clippy `-D warnings` 零告警；fmt 通过；基线 core lib 3/3、legacy-dev lib 58/58、workspace 全量无失败；`git diff --check` 通过。
- Code Review：按标准八维复核。P0 0、P1 0、P2 1——`RouteDecision` 当前只含 evaluations+candidates 两字段，HUB-04 落地策略时将追加 selected/fallback 形状；该演进发生在任何外部 wire 消费者出现之前，届时两个 golden 文件需随 Roadmap 同步重审。
- 未覆盖与风险：选择逻辑/trust-risk-health-preference 覆盖矩阵（HUB-04）、fallback 重授权（HUB-05）、CAAP 能力发现暴露（HUB-08）未涉及；`rank()` 依赖枚举判别值（声明序），重排 RouteKind 属协议化变更需先修订契约。`HUB-03` 转为 `DONE`，解锁 `HUB-04`。

### HUB-02 完成记录（2026-08-23）

- 实现：`crayon-capability-hub` 新增 `builtin.rs`（约 100 行）：编译期权威目录冻结 4 项内建能力——`builtin.browser`（受控导航/标签，`local_only`）、`builtin.content`（有界当前页提取与确定性 Markdown，`page_content`）、`builtin.cast`（正常投屏门禁内的会话选择与播放控制，`cast_control`）、`builtin.handoff`（暂停移交人工/建议外部客户端，`local_only`）；统一 `BUILTIN_CATALOG_VERSION = "1.0.0"`，全部 `source=Builtin`、`trust=System`；`builtin_descriptors()` 按冻结 `BUILTIN_IDS` 序产出，`register_builtins()` 只走 `CapabilityRegistry::register` 公共路径（严格模式：任何拒绝即中止且注册表保持一致），`builtin_registry()` 提供预装注册表。`Cargo.toml` 新增 `crayon-agent-gateway` dev-dependency（仅测试图）用于永久禁止清单交叉核对；无生产依赖新增。
- 验证：`cargo test -p crayon-capability-hub` 18/18 通过（新增 7 项：全量注册与 active 态、schema/source/trust/summary 断言、data_scope 域映射锁定、id 集合精确等于冻结集、永久禁止清单零命中、personal/partner 任意版本覆盖均 Conflict 且原注册不变、golden 快照逐字节一致）；`cargo clippy -p crayon-capability-hub -p crayon-domain -p crayon-agent-gateway --all-targets -- -D warnings` 零告警；fmt 通过；基线 core lib 3/3、legacy-dev lib 58/58、workspace 全量无失败；`git diff --check` 通过。
- Code Review：按标准八维复核。P0 0、P1 0、P2 0——目录与 `BUILTIN_IDS` 双源一致性由 id 集合精确锁定测试保证；`register_builtins` 中途失败仅可能来自冻结目录自身缺陷（同优先级+不同版本必然可注册），预装入口以 expect 兜底为编译期契约错误口径（与 `with_v1_tools` 一致）。
- 未覆盖与风险：router/policy/fallback（HUB-03/04/05）、Site Skill adapter（HUB-07）、CAAP 能力发现（HUB-08）、partner connector（HUB-09+）未涉及；目录演进（新增第五域或版本升级）属协议化变更，需先修订本 Roadmap。`HUB-02` 转为 `DONE`，解锁 `HUB-04`（另需 `HUB-03`）与 `HUB-07` 的 builtin 依赖。

### HUB-01 完成记录（2026-08-23）

- 实现：`crayon-domain` 新增 `capability.rs`（约 230 行）+ `capability_tests.rs`：闭合 `CapabilitySource = Partner(0)/PersonalSkill(1)/Builtin(2)`（precedence 大者优先，serde/wire 名一致）、`TrustLevel`、`LifecycleState`、`DataScope` 四个闭合枚举与 `CapabilityDescriptor { id, version, source, trust, data_scope, summary }`；id/version 走闭合字符集 `[a-z0-9_.:-]`（≤64/≤32 字节），summary ≤256 字节仅限长度校验；Partner 声明 System trust 在 schema 层拒绝（TrustConflict）；`wire_tag()` 产出 `id@version:source:trust:scope` 确定性标签。新建 `crates/crayon-capability-hub` crate：`registry.rs`（约 250 行）单 current-per-id 注册表——首次注册生效；替换要求 source precedence `>=` 既有且版本不同，否则 `Conflict`（Builtin 不可被 Personal/Partner 覆盖）；同 id+version 重复注册稳定拒绝（`DuplicateRegistration`）；`Revoked` 对 id+version 终态——当前版本撤销立即生效且可幂等重复，被撤销版本归档（每 id 上界 `MAX_REVOKED_HISTORY_PER_ID=8`，满载后该 id 再注册 fail closed 返回 `RevocationHistoryFull`，永不静默丢弃 tombstone），新版本可在撤销后按优先级规则接替；`set_enabled` 绑定精确 version（stale 调用者失败而非作用于已替换记录），离开 Revoked 不可能（`LifecycleTerminal`）；容量 `MAX_REGISTRATIONS=64` 满载 `Capacity`（既有 id 的替换不受影响）；`snapshot()` 按 id 确定序输出 `id|version|source|trust|data_scope|state`，排除自由文本 summary；错误枚举闭合且稳定 Display。workspace members 注册新 crate；无新增第三方依赖；全同步、无锁/线程/IO/时钟。
- 修正（相对 WIP 初稿）：移除不可编译的 `From<CapabilitySchemaError> for CoreError`（`CoreError` 无 `InvalidInput` 变体且为 FND-08 冻结契约，域内各模块各自持有闭合错误，与 agent/config/diagnostics 一致）；`trust_wire_name()` 从 descriptor 私有方法改为 `TrustLevel::wire_name()` 公开常量方法，与其余枚举对齐。
- 验证：`cargo test -p crayon-capability-hub` 11/11 通过（golden 快照逐字节一致与重建确定性、3x3 替换优先级矩阵全格锁定、首注生效/同 pair 拒绝含字段篡改对照、撤销即时生效+幂等+终态+未知目标、撤销后新版本接替并归档可查、lifecycle 版本绑定与 stale 拒绝、schema 校验矩阵含 Partner+System、容量上界、撤销历史满载 fail closed、LCG 3000 步风暴不变量——容量上界/每 id 活跃 precedence 不降/已撤销 pair 永不复活/快照恒定 id 序）；`cargo test -p crayon-domain --lib` 含 capability 6 项（token 边界矩阵、validate 矩阵含 256/257 字节边界、trust 冲突、precedence 序、四枚举 serde wire 名 roundtrip、wire_tag golden）；`cargo clippy -p crayon-capability-hub -p crayon-domain --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；基线回归 core lib 3/3、legacy-dev lib 58/58、workspace 全量无失败；`git diff --check` 通过。
- Code Review：按需求/边界→正确性→架构/API→并发/生命周期→安全/隐私→性能→测试→可维护性复核。P0 0、P1 0、P2 2：(1) 同 id+version 已存在时，低优先级来源得到 `Conflict` 而足够优先级来源得到 `DuplicateRegistration`——两序皆可辩护，取"优先级先判"使越权覆盖尝试获得更具诊断性的错误；行为已被 golden/矩阵测试锁定。(2) Active 记录被新版本替换后旧版本即被遗忘，此后旧版本可再次注册（同优先级降级换版不受 HUB-01 约束）——Roadmap 未约束该情形，partner 包的降级防护明确归 `HUB-10`（签名/篡改/降级/撤销/kill switch），builtin 由编译期权威来源（`HUB-02`）保证。
- 未覆盖与风险：router/policy/fallback（HUB-03/04/05）、内建能力清单注册（HUB-02）、partner connector 与网络/IO（HUB-09+）均未涉及；registry 为进程内 v1 语义不持久化，重启即清（与 grant/receipt 同口径）。`HUB-01` 转为 `DONE`，解锁 `HUB-02`、`HUB-03`。
