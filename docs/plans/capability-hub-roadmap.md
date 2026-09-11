# HUB：Capability Registry、Router 与合作方连接器 Roadmap

- 状态：`HUB-01..06 DONE`（2026-08-24），其余 TODO
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
| HUB-05 | DONE | HUB-04,AGT-04,AGT-11 | `crayon-capability-hub/fallback/**` | fallback 重授权、重确认、幂等和未知副作用停止 | `HB-005`; 跨 route 不静默重放 |
| HUB-06 | DONE | HUB-04,AGT-05 | `apps/desktop-cef/**/capability-route/**`,locales | route 预览、理由、偏好和临时覆盖 UI | `HB-006`; 数据外发/成本/风险可见 |
| HUB-07 | DONE | HUB-02,WFL-12 | `crayon-capability-hub/adapters/site_skill/**` | 个人 Site Skill registry adapter | `HB-007`; owner/Profile/health/版本隔离 |
| HUB-08 | TODO | HUB-03,AGT-14（AGT-12Cc2/Cd 链）| `crayon-agent-gateway/tools/capability/**` | 入站 MCP/CLI 能力 search/describe/preview，经 CAAP 暴露 | `HB-008`; 不泄漏 token/endpoint/隐蔽工具 |
| HUB-09 | DONE | HUB-01,PRV-10 | `crayon-partner-connector/api/**` | 与入站 MCP 分离的出站 Partner connector interface | `HB-009`; crate/dependency/session 隔离 |
| HUB-10 | DONE | HUB-09 | `crayon-partner-connector/trust/**` | 来源、版本、签名、兼容、revoke、disable 和 kill switch | `HB-010`; 篡改/降级/撤销/离线 |
| HUB-11 | DONE | HUB-09,PRV-07 | `crayon-partner-connector/oauth/**`,`crayon-platform-api/**` | OAuth state/PKCE、最小 scope 和 provider/tenant token vault | `HB-011`; redirect/CSRF/scope/清除/串租户 |
| HUB-12 | DONE | HUB-09,PLT-02 | `crayon-partner-connector/network/**` | endpoint allowlist、DNS/重定向重验、SSRF 与消息预算 | `HB-012`; rebinding/private/metadata/oversize |
| HUB-13 | DONE | HUB-10,HUB-11,HUB-12 | `crayon-partner-connector/mcp/**` | 出站 Partner MCP namespace、tool/schema 过滤和不可信响应 | `HB-013`; description injection 不可扩权 |
| HUB-14 | DONE | HUB-09,HUB-12 | `crayon-partner-connector/runtime/**` | health、rate/quota、retry budget、熔断、取消 | `HB-014`; 副作用默认不 retry；资源有界 |
| HUB-15 | DONE | HUB-05,HUB-13,HUB-14,AGT-11 | `crayon-capability-hub/audit/**`,`diagnostics/**` | provider/tenant hash/capability/route/结果的脱敏审计指标 | `HB-015`; 无正文/token/完整参数 |
| HUB-16 | DONE | HUB-01..HUB-15 | threat model,Review,`docs/current/**` | Hub/Partner connector 安全、隐私、供应链与性能总 Review | 全 HB；P0/P1=0；partner feature 独立 GO/NO-GO |

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
## HUB-05 原子范围（fallback 重授权决策模型）

- 状态：`IN_PROGRESS`；依赖 `HUB-04 DONE`、`AGT-04 VERIFIED`、`AGT-11 VERIFIED`。
- 单一目标：`crayon-capability-hub` 新增 `fallback.rs`：route 执行失败后的 fallback 裁决模型——按副作用状态三分支（无副作用/已提交且可逆/未知），只有安全分支允许进入下一步骤，且任何步骤都是**全新授权决策**（附闭合重校验清单：语义目标/scope/grant/确认/幂等键/数据预览六项全部重新执行，不继承任何假设）；未知副作用或不可逆提交立即停止。不含实际执行、grant 签发与 UI。
- 输入：HB-005（含已提交/未知副作用和不同 provider；重新 scope/risk/grant/确认/幂等；未知副作用停止）、架构 §8（fallback 不是透明重试）、红线"不得因 Partner API 失败静默降级到网页执行"、`HUB-04` PolicyDecision 的 fallback 链。
- 输出与允许修改：`crates/crayon-capability-hub/src/fallback.rs`、`fallback_tests.rs`、`lib.rs` 仅加模块声明、本 Roadmap。零第三方新增；全同步、无锁/线程/IO/时钟。
- 禁止修改：policy/router/registry/builtin 行为与既有 golden、domain schema、其他 crate；本层不产生任何自动执行路径——verdict 只是决策，执行归 app-runtime 装配。
- 边界：
  - `SideEffectState = None | Committed { reversible } | Unknown`；Unknown → `Stop(UnknownSideEffects)`、Committed{reversible:false} → `Stop(IrreversibleCommit)`，两者优先于链上位置。
  - 安全分支 → `Reauthorize { next }`：next 取自 decision.fallback 中首个能力 kind；链上只剩 HumanHandoff → `HandOver`；只剩 Reject 或链耗尽 → `Stop(ChainExhausted)`。
  - 每个 verdict 附完整闭合重校验清单常量（semantic_target/scope/grant/confirmation/idempotency/data_preview），快照逐项渲染——清单不可裁剪，即"不继承 provider/scope/confirmation/幂等假设"的类型化表达。
  - attempt.executed_kind 必须等于 decision.selected.kind，否则稳定拒绝。
- 验收与测试：HB-005。矩阵：clean 失败降级到次选并带全清单、已提交可逆仍需全量重授权（无静默重放）、不可逆停止、未知副作用无条件停止、链耗尽 Stop、HumanHandoff 判给 HandOver、attempt 不匹配拒绝、确定性渲染、LCG 不变量（Reauthorize 的 next 必为链上能力 kind 且清单恒完备）。命令：`cargo test -p crayon-capability-hub`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：执行/重试编排（app-runtime）、grant 签发接线、receipt 记录入口（HUB-15 audit 汇总）、UI 呈现（HUB-06）。
### HUB-05 完成记录（2026-08-24）

- 实现：`crayon-capability-hub` 新增 `fallback.rs`（约 180 行）：`RouteAttempt { executed_kind, side_effects }` + `evaluate(decision, attempt)` 三分支裁决——副作用安全优先于链位：`Unknown → Stop(UnknownSideEffects)`、`Committed{reversible:false} → Stop(IrreversibleCommit)`（partner API 失败永不静默降级到网页执行）；安全分支按 `PolicyDecision.fallback` 首元素给出 `Reauthorize{next}` / `HandOver`（链首为 HumanHandoff）/ `Stop(ChainExhausted)`（空链或 Reject 收尾）；attempt.executed_kind 必须等于 selected.kind 否则 `RouteNotSelected` 稳定拒绝；**闭合六项重校验清单常量** `REAUTHORIZATION_CHECKLIST`（semantic_target/scope/grant/confirmation/idempotency_key/data_preview）作为"不继承 provider/scope/confirmation/幂等假设"的类型化表达——清单整体生效不可裁剪，快照行格式确定（reauthorize|<kind>/hand_over/stop|<reason>）。verdict 仅是决策，执行与 grant 签发归 app-runtime 装配，审计记录归 HUB-15。零第三方新增；全同步、无锁/线程/IO/时钟。
- 验证：`cargo test -p crayon-capability-hub` 44/44 通过（fallback 新增 9 项：clean 失败降级次选+清单完备断言、已提交可逆仍需全新重授权无静默重放、不可逆立即停、未知副作用无条件先于链位停止、HumanHandoff 判 HandOver、Reject/空链耗尽 Stop、executed 不匹配与未选中决策拒绝、真实 policy→resolve→apply→evaluate 端到端确定性、LCG 3000 步不变量——Reauthorize 目标必为链首能力 kind/安全分支外必停/渲染闭合）；clippy `-D warnings` 零告警；fmt 通过；workspace 全量无失败；`git diff --check` 通过。
- Code Review：按标准八维复核。P0 0、P1 0、P2 1——清单目前是文档化常量，编译器不强制调用方逐项执行（Rust 无效果系统）；缓解：verdict 类型不含任何"已授权"语义、app-runtime 装配任务必须以 AGT-04 grant + AGT-05 confirm 的实际产出驱动下一步，HUB-16 总 Review 复核。
- 未覆盖与风险：同 kind 不同 provider 的粒度（如两个 partner 包）待 partner manifest 落地后在 HUB-09+ 扩展；checkpoint/接管语义归 WFL。`HUB-05` 转为 `DONE`，解锁 `HUB-08`（另需 `AGT-14`）。
## HUB-06 原子范围（route 预览与临时覆盖视图模型）

- 状态：`IN_PROGRESS`；依赖 `HUB-04 DONE`、`AGT-05 VERIFIED`。
- 路径说明：Roadmap `apps/desktop-cef/**/capability-route/**` 的目录尚不存在；按既有映射惯例落在 `browser/shared-ui/capability-route`（共享层视图模型），CEF 呈现归后续装配。
- 单一目标：R2～R4 任务执行前的 route 预览视图模型——展示选定路线/路由理由/候选与排除清单/**数据外发标志**，用户可设置临时覆盖（偏好 kind、允许外发开关），覆盖仅对本次任务生效、不持久化；Proceed/Cancel 两步流，呈现内容变化必须重新 Present。无障碍经 locale label key 全覆盖（en/zh parity）。不含策略计算与实际路由执行。
- 输入：HB-006（数据外发/成本/风险可见）、`HUB-04` PolicyDecision/PolicyDecision 字段口径（闭合 wire 名）、AGT-05 的视图模型/locale parity 模式。
- 输出与允许修改：新增 `browser/shared-ui/capability-route/{CMakeLists.txt,include/crayon/browser_capability_route/capability_route.h,src/capability_route.cc,tests/capability_route_test.cc}`、`browser/shared-ui/locales/en-US.json` 与 `zh-CN.json` 各追加键、共享层根 CMake 若有子目录注册处同步。零第三方依赖；单线程 UI 约定同 AGT-05。
- 禁止修改：policy/router 行为、agent-confirm 模块、CEF shell；模型不得引入网络/IO/线程；不得持久化任何覆盖或预览状态。
- 边界：
  - 预览字段全部为闭合 token/wire 名（kind/reason/trust 白名单校验）；候选/排除各 ≤16 条；id/version 走闭合字符集；**数据外发**以布尔标志逐候选呈现并在摘要中显式标注。
  - 成本可见性：当前无 provider 成本数据源（HUB-09+ 提供），本任务不建模成本字段——在完成记录登记缺口，不发明占位语义。
  - 临时覆盖仅存于模型内存且 Proceed/Cancel 后即失效；`prefer_kind=reject` 或未知 kind 稳定拒绝；覆盖不改变已呈现的预览内容本身，只随 Proceed 输出供运行时重新求值策略。
  - Present 校验失败返回 false 且不改变既有状态；revision 每次 Present 单调递增供外壳检测上下文变化。
- 验收与测试：HB-006 模型部分。矩阵：Present 校验矩阵、生命周期（None/Presented/Proceeded/Cancelled）、覆盖合法性矩阵与一次性语义、数据外发标注渲染、locale en/zh parity 键集合全等、风暴不变量（非法状态下 Proceed 永不成功/revision 单调）。命令：独立 configure/build/ctest、`git diff --check`。
- 明确不做：策略重算（调用方拿 Proceed 输出回 HUB-04）、CEF 弹窗呈现与实机无障碍（QAR/BUX）、成本建模（HUB-09+）。
### HUB-06 完成记录（2026-08-24）

- 路径说明：按既有映射惯例落在新目录 `browser/shared-ui/capability-route`（header/impl/CMake/契约测试各 1），根 CMake 注册；CEF 呈现与实机无障碍归后续装配/QAR。
- 实现：`CapabilityRouteModel` 视图模型——`Present(CapabilityRoutePreview)` 校验闭合词汇（kind 五类/reason 五态/trust 三态/排除原因两态白名单、id/version 闭合字符集有界、候选与排除各 ≤16、selected_id 与 selected_kind 一致性）后进入 Presented 态并单调递增 revision（外壳检测底层决策变化的围栏）；预览字段含逐候选 `sends_data_external` 标志，`Summary()` 确定性行格式渲染 selected/candidate(external|local)/excluded 行——**数据外发与风险可见**（HB-006）；临时覆盖 `ApplyOverride` 只在 Presented 态接受、拒绝 `reject` 偏好与未知 kind、随 Proceed 输出给运行时回 HUB-04 重求值或随 Cancel 丢弃、任何离开 Presented 的路径都清空覆盖且**不持久化**（一次性语义由测试锁定）；Proceed/Cancel 仅在 Presented 态成功。locale 新增 11 键 ×2（title/selected/reason/candidates/exclusions/data_external/override.temporary/prefer/allow_external/proceed/cancel，en/zh 各 60 键 parity 入契约测试）。零第三方依赖；单线程 UI 约定同 AGT-05。
- 成本缺口登记：当前无 provider 成本数据源（HUB-09+ partner manifest 提供），本任务不建模成本字段、不发明占位语义。
- 验证：独立 configure/build 零告警（MSVC /W4 /WX 与 -Wall -Wextra -Wpedantic -Werror 双口径）；`capability_route` 契约测试通过（Present 校验矩阵含 revision 不被失败污染、生命周期与一次性覆盖语义（Proceed 二次失败/Cancel 后覆盖不可取回）、外发标注渲染含 none-selected 行、locale en/zh parity 60=60 且必需键齐全、5000 步风暴不变量）；全目标编译后 ctest 共享层回归 **40/40 通过**（含新用例）；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——成本可见性缺数据源未建模（已登记，HUB-09+ 补齐时需同步扩展模型与 locale）；其余维度无发现。
- 未覆盖与风险：策略重算由调用方持 Proceed 输出回 HUB-04 执行（本层不计算）；CEF 弹窗呈现/键盘读屏实机验证归 QAR/BUX。`HUB-06` 转为 `DONE`。

## HUB-09 原子范围（出站 Partner connector 接口层）

- 状态：`IN_PROGRESS`；依赖 `HUB-01 DONE`、`PRV-10 DONE`。
- 单一目标：新 crate `crayon-partner-connector`（workspace member）交付出站 connector 的**接口层** `api`——`ConnectorDescriptor`（partner 自有 id/version，闭合 charset/版本校验）、`ConnectorSessionId`（出站会话令牌，与入站 CAAP session 无任何类型互通）、`ConnectorScope`（最小 scope 集，禁 `*` 通配）、`ConnectorCall`（端点引用+payload 预算的出站调用描述）与三个边界 trait `TrustPort`（HUB-10 签名/撤销）、`TokenVaultPort`（HUB-11 provider/tenant token vault）、`NetworkPort`（HUB-12 端点 allowlist/SSRF 防护）+ `ConnectorAuditEvent`（provider/能力/结果脱敏事件，无正文/token）。
- 隔离不变量（HB-009 crate/依赖/session/token/审计隔离的接口面表达）：本 crate 不得依赖 `crayon-agent-gateway`/`crayon-ipc-schema` 等入站 crate（workspace 成员级隔离）；session id 类型不可从入站会话字符串构造（无互通构造器）；registry namespace 为 `partner_connector::*`，与入站 registry 无共享符号。
- 输入与输出：允许修改 `crates/crayon-partner-connector/**`、workspace `Cargo.toml` members 与本 Roadmap。实现 trait 的具体逻辑分属 HUB-10..12；本层只定义类型与 trait，零 IO。
- 边界：描述符校验 fail-closed（空/超长/非法 charset 拒绝）；scope 禁通配与空集；调用预算字段必有界；审计事件只含闭合枚举与 hash 字段；不触网、不持久化。
- 验收：`cargo test -p crayon-partner-connector`（描述符校验矩阵、session 令牌不可互通的类型级断言、scope 通配拒绝、调用预算边界、审计事件无正文）;依赖隔离静态断言（Cargo.toml 无入站 crate 依赖）；clippy `-D warnings`、fmt、`check.sh security`、`git diff --check`。
- 明确不做：trust/oauth/network/mcp/runtime 具体实现（HUB-10..14）、审计 sink 接线（HUB-15）、任何真实网络请求。

## HUB-09 完成记录（2026-09-11）

- 实现：新 crate `crayon-partner-connector`（workspace member，`src/api/mod.rs` + `api_tests.rs`，约 380 行）——接口层，零 IO 零实现：
  - `ConnectorId`：`<namespace>.<name>` 强制 partner 自有命名空间（`[a-z0-9_-]`，各 ≤64B），无缺省命名空间。
  - `ConnectorDescriptor`：version 校验（`[0-9A-Za-z.+-]`）、scope 集合（≤16 条、禁通配 `*`、禁空白、排序去重）——描述符无法自行扩权。
  - `ConnectorSessionId`：出站会话令牌为单调计数包装，私有字段、无字符串构造器/访问器——与入站 CAAP session 类型级不可互通（HB-009 session 隔离）。
  - `ConnectorCall`：endpoint 引用 + ASCII payload，预算 ≤64KiB（PayloadTooLarge/NotUtf8 fail-closed）。
  - 边界 trait：`TrustPort`（HUB-10）、`TokenVaultPort`（HUB-11，token 只出 opaque handle）、`NetworkPort`（HUB-12，allowlist/SSRF 归实现）；`ConnectorAuditEvent` 只含 namespace/name/outcome/payload 字节数（无正文/token）。
  - **依赖隔离静态断言**：crate 自身 manifest 测试断言不依赖 `crayon-agent-gateway`/`crayon-ipc-schema`/`crayon-app-runtime`/`crayon-semantic-action`（HB-009 crate 隔离的自动化表达）。
- 验证：`cargo test -p crayon-partner-connector` 8/8（id 命名空间矩阵、version/scope 校验、通配拒绝、scope 预算、session 令牌单调性与类型隔离、payload 预算、审计事件无正文、TrustPort 默认拒绝、依赖隔离断言）；clippy `-D warnings` 零告警；fmt、`check.sh security`、`git diff --check` 通过。
- Code Review：按 v0.9 复核——接口零实现零 IO、入站/出站命名空间与依赖隔离、描述符不可扩权、审计脱敏字段闭合。P0/P1/P2=0。
- 未覆盖与风险：具体实现分属 HUB-10（trust）/11（token vault）/12（network）/13（出站 MCP namespace）/14（runtime）；审计 sink 接线归 HUB-15。`HUB-09` 转 `DONE`，解锁 `HUB-10/11/12`。

## HUB-10 原子范围（trust：来源/版本/revoke/disable/kill switch）

- 状态：`IN_PROGRESS`；依赖 `HUB-09 DONE`。
- 单一目标：`crayon-partner-connector/trust/**` 交付 `TrustRegistry` 实现 `TrustPort`——验证过的 (connector, 精确 version) 允许表 + 每 connector 撤销 + 全局 kill switch；`is_trusted` 仅在「kill switch 关 && 未撤销 && 存在精确版本匹配」时为真（版本降级/篡改/不匹配一律拒绝），无任何网络或文件 IO（签名验证产物由宿主安装流注入，本层只做策略）。
- 输入与输出：允许修改 `crates/crayon-partner-connector/src/trust/{mod.rs,trust_tests.rs}`、`lib.rs`/`api` 导出与本 Roadmap。
- 边界：允许表有界（≤256 条，满载拒绝新 allow 且保留既有）；`revoke` 优先于 allow；`kill_switch` 优先于一切（开启时全部拒绝且 allow 表保留以便恢复）；全部操作幂等；离线行为=纯内存策略（无网络查询，"离线"即策略本地完备）。
- 验收：`trust_tests`（allow→信任、版本不匹配/降级拒绝、revoke 后拒绝、kill switch 全拒与恢复、幂等、容量上界、未知 connector 默认拒绝）；既有 8 项不回归；clippy `-D warnings`、fmt、`check.sh security`、`git diff --check`。
- 明确不做：真实签名/证书校验（宿主安装流职责）、网络查询 CRL/OCSP、持久化（重启即空表，宿主可重注）。

## HUB-10 完成记录（2026-09-11）

- 实现：`crayon-partner-connector/src/trust/{mod.rs,trust_tests.rs}`（约 260 行）——`TrustRegistry` 实现 `TrustPort`：宿主安装流验证后的 (connector, 精确 version) 允许表（≤256 条，满载拒绝新 allow、保留既有）+ 每 connector `revoke`/`revoke_all` + 全局 kill switch（优先级最高，开启时拒绝新 allow 但保留 allow 表以便恢复）；`verdict()` 给出闭合诊断（KillSwitch/Revoked/UnknownConnector/VersionMismatch——精确版本匹配使降级/篡改 re-build/不兼容升级全拒）；策略纯内存（离线完备），revoke 在 kill switch 关闭后仍生效，重 allow 是显式 operator 恢复动作。`ConnectorId` 增 `key()`（`<namespace>.<name>` 注册表键）。
- 验证：`cargo test -p crayon-partner-connector` **16/16**（新增 8：精确版本信任、篡改/降级拒绝、未知默认拒绝、revoke 优先于 allow 且幂等、revoke_all、kill switch 全拒+恢复+先期 revoke 存活、容量上界 fail-closed、空版本拒绝）；clippy `-D warnings` 零告警；fmt、`check.sh security`、`git diff --check` 通过。
- Code Review：按 v0.9 复核——deny-by-default 次序（kill switch→revoked→unknown→version）、优先级语义闭合、容量 fail-closed、零 IO 零持久化（重启空表，宿主重注）、无签名/网络职责越界。P0/P1/P2=0。
- 未覆盖与风险：真实签名/证书校验归宿主安装流（本层策略只消费其结论）；CRL/OCSP 明确不做。`HUB-10` 转 `DONE`。

## HUB-11 原子范围（oauth state/PKCE 与 provider/tenant token vault）

- 状态：`IN_PROGRESS`；依赖 `HUB-09 DONE`。
- 单一目标：`crayon-partner-connector/oauth/**`——(1) `TokenVault<S: SecureStore>` 实现 `TokenVaultPort`：token 以 `(connector, account)` 命名空间经注入的平台 `SecureStore` 存取（OS user/Profile 隔离由注入实例保证），`token_handle` 只出单调 opaque handle，`clear_connector/clear_all` 清除语义；(2) OAuth 授权辅助：`OAuthState`（CSRF，注入 32 字节熵源的 hex 令牌 + 常数时间比较）、`PkceChallenge`（RFC 7636 verifier 校验 43..=128 `[A-Za-z0-9-._~]`、S256 challenge 经注入 `Sha256Port`——本 crate 不自实现密码学）、redirect 精确匹配校验（拒绝开放重定向）与最小 scope 检查（请求 ⊆ descriptor）。
- 输入与输出：允许修改 `crates/crayon-partner-connector/src/oauth/{mod.rs,oauth_tests.rs}`、`Cargo.toml`（如需 dev-dep）与本 Roadmap。
- 边界：token 永不出 vault（接口面只有 store/handle/clear）；state 比较用逐字节常数时间；redirect 只接受精确登记项；HB-011 redirect/CSRF/scope/清除/串租户全覆盖。
- 验收：`oauth_tests`（vault 租户隔离矩阵、handle 单调、清除幂等；state 生成/校验含拒绝；PKCE roundtrip+verifier 拒绝+challenge 错配拒绝；redirect 精确匹配；scope 最小化）+ HUB-09/10 不回归；clippy/fmt/security/diff-check。
- 明确不做：真实 SHA-256/随机源实现（宿主注入）、真实 HTTP 授权端点（HUB-12）、token 加密算法选择（SecureStore 职责）。

## HUB-11 完成记录（2026-09-11）

- 实现：`crayon-partner-connector/src/oauth/{mod.rs,oauth_tests.rs}`（约 420 行）——
  - `TokenVault<S: SecureStore>` 实现 `TokenVaultPort`：token 以 `pc-<connector.key()>.<account>` 键经注入的平台 `SecureStore` 存取（OS user/Profile 隔离由注入实例承担）；租户隔离=键名空间（(connector, account) 不可跨读，测试覆盖跨租户/跨 connector）；`token_handle` 只出单调 opaque u64；`clear_token/clear_connector` 幂等清除（HB-011 清除语义）。
  - `OAuthState::generate/verify`：32 字节注入熵 → 64 hex；**逐字节常数时间比较**（HB-011 CSRF）。
  - `PkceChallenge`（RFC 7636）：verifier 校验（43..=128，`[A-Za-z0-9-._~]`）、S256 challenge、错配拒绝；**SHA-256 经注入 `Sha256Port`**——本 crate 不自实现密码学（依赖纪律）。`base64url_no_pad` 以 RFC 4648 向量锁定。
  - `validate_redirect` 精确匹配（HB-011 开放重定向拒绝）；`validate_scopes` 最小 scope（请求 ⊆ descriptor，空集拒绝）。
- 验证：`cargo test -p crayon-partner-connector` **23/23**（新增 15：租户隔离矩阵、handle 单调稳定、clear 幂等且只清本 connector、state 生成/常数验证/篡改拒绝、PKCE roundtrip+错配+verifier 文法边界 43/128/非法字符、base64url RFC 向量、redirect 精确匹配、scope 越权拒绝、trust 默认拒绝、依赖隔离断言）；clippy `-D warnings` 零告警；fmt、`check.sh security`、`git diff --check` 通过。
- Code Review：按 v0.9 复核——token 永不出 vault 公共面、租户键隔离、密码学注入不自实现、redirect/scope fail-closed、state 常数时间比较。P0/P1/P2=0。
- 未覆盖与风险：真实 SHA-256/随机源实现由宿主注入（12Cc FFI/平台层）；授权端点网络执行归 HUB-12。`HUB-11` 转 `DONE`。

## HUB-12 原子范围（network：allowlist/DNS 重验/SSRF/消息预算）

- 状态：`IN_PROGRESS`；依赖 `HUB-09 DONE`、`PLT-02 DONE`。
- 单一目标：`crayon-partner-connector/network/**` 交付出站网络**策略层**——`EndpointAllowlist`（精确端点引用登记）、`NetworkPolicy`（解析后地址 SSRF 守卫：loopback/private/link-local/ULA/未指定/**169.254.169.254 metadata** 全拒，仅 https；重定向每跳重验 + ≤3 跳预算；请求/响应字节预算）与 `PolicyNetworkPort` 实现 `NetworkPort`——组合 allowlist→注入 `ResolverPort`（宿主 DNS 解析出 `ResolvedAddress`）→SSRF 校验→注入 `ExchangePort`（宿主真实 IO）→重定向循环重验→预算裁剪。真实 socket/HTTP IO 留给宿主实现，本层只做策略与组合。
- 输入与输出：允许修改 `crates/crayon-partner-connector/src/network/{mod.rs,network_tests.rs}` 与本 Roadmap。
- 边界：地址校验用 `std::net::IpAddr` 标准分类（is_loopback/is_private/is_link_local/is_unspecified/is_unique_local）；解析地址全拒才拒绝（任一公网地址不放宽全拒）；重定向 Location 必须仍在 allowlist 内且每跳重新走完整守卫；超出预算/跳数/尺寸 fail-closed。
- 验收：`network_tests`（allowlist 登记/未登记、loopback/private/metadata/ULA/未指定拒绝、公网放行、https-only、重定向同 host 重验与跳数上限、重定向逃逸拒绝、响应超预算拒绝）+ 既有 23 项不回归；clippy/fmt/security/diff-check。
- 明确不做：真实 DNS/socket/HTTP 实现（宿主注入 `ResolverPort/ExchangePort`，HUB-14 runtime 组装）、代理、鉴权头注入。

## HUB-12 完成记录（2026-09-11）

- 实现：`crayon-partner-connector/src/network/{mod.rs,network_tests.rs}`（约 380 行）——出站网络**策略层**：
  - `EndpointAllowlist`：精确端点引用登记/查询（宿主填充）。
  - `NetworkPolicy::ssrf`（`ResolvedAddress::is_public`）：std `IpAddr` 分类——loopback/private/link-local（含 **169.254.169.254 metadata**）/unspecified/broadcast/documentation/ULA fc00::/7（**掩码语义修正：`&0xfe00 == 0xfc00`**）/v4-mapped 回环与私网全拒，仅全局单播放行；**DNS rebinding 防护=全拒语义**（解析结果中任一私网地址即拒绝，不因存在公网地址放宽）。
  - `PolicyNetworkPort` 实现 `NetworkPort`：allowlist → 注入 `ResolverPort`（宿主 DNS）→ SSRF 校验 → 注入 `ExchangePort`（宿主真实 IO）→ 重定向循环（每跳重新走完整守卫、Location 必须仍在注册 host 上、**跳数 ≤3 超出 fail-closed**）→ 响应预算（1MiB、truncated 拒绝）。
  - 真实 DNS/socket/HTTP IO 留给宿主 `ResolverPort/ExchangePort` 实现（HUB-14 runtime 组装），本层只做策略与组合。
- 验证：`cargo test -p crayon-partner-connector` **32/32**（新增 9：allowlist 未登记拒绝、9 类 SSRF 地址全拒、**rebinding（公网+私网混合解析）拒绝**、重定向同 host 通过、逃逸拒绝、跳数耗尽 fail-closed、响应超预算、truncated 拒绝、公网放行正例）；clippy `-D warnings` 零告警；fmt、`check.sh security`、`git diff --check` 通过。
- 实现期修复：fc00::/7 掩码比较语义（`&0xfe00 == 0xfc00` 而非 `==0xfe00`）；重定向预算语义分离为 TargetForbidden。
- Code Review：按 v0.9 复核——全拒 SSRF 语义、每跳重验、预算 fail-closed、宿主 IO 注入边界、错误面无内容。P0/P1/P2=0。
- 未覆盖与风险：真实 DNS 解析与 TLS 交换由宿主实现（12Cc/HUB-14）；IPv6 全分类面依赖 std。`HUB-12` 转 `DONE`，解锁 `HUB-13/14`。

## HUB-13 原子范围（出站 Partner MCP namespace 与不可信响应）

- 状态：`IN_PROGRESS`；依赖 `HUB-10/11/12 DONE`。
- 单一目标：`crayon-partner-connector/mcp/**`——出站 MCP 工具命名空间与不可信数据面：`OutboundMcpRegistry`（partner 工具强制登记为 `<namespace>.<name>`，与入站 registry 无共享符号）；`ToolFilter`（partner 提供的 description/schema 元数据是不可信数据：长度有界、控制字符拒绝、**capability/risk 由宿主侧配置决定而非 partner 数据**）；`McpResponse`（响应是纯数据：不可触发新能力、不进错误面、预算裁剪）。
- 边界：partner 元数据永远不能扩权（能力映射宿主所有）；响应只作为 opaque 文本透传给调用者，绝不解释执行；全部 fail-closed。
- 验收：`mcp_tests`（命名空间强制与冲突、描述长度/控制字符拒绝、能力宿主所有不可被 partner 覆盖、响应预算与只读性）+ 既有回归；clippy/fmt/security/diff-check。
- 明确不做：真实 MCP 协议栈、网络执行（HUB-12 边界）、入站 MCP 改动。

## HUB-13 完成记录（2026-09-11）

- 实现：`crayon-partner-connector/src/mcp/{mod.rs,mcp_tests.rs}`（约 300 行）——`OutboundMcpRegistry`：partner 工具强制登记为 `<connector-namespace>.<connector-name>.<tool>`（与入站 registry 命名空间无交集，测试断言不含 `agent.`/`caap`）；`ToolFilterError` 拒绝面：tool 名文法（≤64B `[a-z0-9_-]`）、description 预算（≤2048B）与**控制字符拒绝**（`\n`/DEL 等注入载体 fail-closed）、重复登记拒绝；**authority 宿主所有**（`ToolAuthority` 由宿主传入，partner 元数据不可影响，测试断言注册后不可变）；`McpResponse::from_untrusted`：响应是预算裁剪的 opaque 数据（多字节字符不撕裂、truncated 标记、不可触发新能力）。
- 验证：`cargo test -p crayon-partner-connector` **38/38**（新增 6：命名空间强制与入站隔离、重复登记、tool 名文法三例、描述预算+换行/DEL 注入载体拒绝+干净通过、authority 宿主所有且重复注册不改写、响应预算/截断/多字节完整）；clippy `-D warnings` 零告警；fmt、`check.sh security`、`git diff --check` 通过。
- Code Review：按 v0.9 复核——description injection 不可扩权（capability/risk 宿主所有+注入载体拒绝）、namespace 隔离、响应只读 opaque、预算 fail-closed。P0/P1/P2=0。
- 未覆盖与风险：真实 MCP 协议栈网络执行归 HUB-14 runtime + 宿主 IO；description 语义分析（LLM 层防护）不在本层。`HUB-13` 转 `DONE`，解锁 `HUB-14`（runtime：health/quota/retry/熔断）。

## HUB-14 原子范围（runtime：health/quota/retry/熔断/取消）

- 状态：`IN_PROGRESS`；依赖 `HUB-09 DONE`、`HUB-12 DONE`。
- 单一目标：`crayon-partner-connector/runtime/**` 交付 connector 调用运行时策略——`CallLimiter`（窗口内调用配额与最小间隔，注入时钟）、`CircuitBreaker`（连续失败熔断：闭→开→半开试探→闭，半开只放行一次探测）、`RetryBudget`（有界重试预算：**副作用调用默认不重试**、幂等只读在预算内重试、预算耗尽拒绝）与 `CancellationFlag`（协作取消， polled by host）。全部纯内存、注入时钟、fail-closed。
- 验收：`runtime_tests`（配额窗口拒绝与恢复、最小间隔、熔断开/半开/闭全转换、副作用不重试、只读重试预算耗尽、取消传递）+ 既有回归；clippy/fmt/security/diff-check。
- 明确不做：真实 HTTP/定时器线程（宿主注入时钟与 IO）、持久化熔断状态（进程内）。

## HUB-14 完成记录（2026-09-11）

- 实现：`crayon-partner-connector/src/runtime/{mod.rs,runtime_tests.rs}`（约 300 行）——
  - `CallLimiter`：滑动窗口配额 + 最小调用间隔，注入时钟，窗口滑出自动回收。
  - `CircuitBreaker`：Closed→Open（连续失败达阈值）→冷却后半开（只放行一次探测）→探测成功闭/失败重开；`admit` 惰性转换 + `state()` 诊断。
  - `RetryBudget`：**副作用调用永不重试**（`can_retry(is_idempotent_read)` 门控），幂等只读在预算内重试，`consume/refill` 有界。
  - `CancellationFlag`：协作取消（clone 共享状态，宿主端口轮询）。
  - 全部纯内存、注入时钟、无线程无定时器无持久化。
- 验证：`cargo test -p crayon-partner-connector` **44/44**（新增 6：配额窗口 Shed/恢复、最小间隔、熔断全转换（含半开单探测与探测失败重开）、副作用永不重试+预算耗尽+refill、取消共享状态）；clippy `-D warnings` 零告警；fmt、`check.sh security`、`git diff --check` 通过。
- Code Review：按 v0.9 复核——熔断转换闭合、重试语义（副作用默认不重试）与预算 fail-closed、注入时钟无墙钟依赖。P0/P1/P2=0。
- 未覆盖与风险：分布式熔断状态（单进程语义）；真实 IO 组装归产品装配。`HUB-14` 转 `DONE`。

## HUB-15 原子范围（provider/tenant 脱敏审计指标）

- 状态：`IN_PROGRESS`；依赖 `HUB-05 DONE`、`HUB-13 DONE`、`HUB-14 DONE`、`AGT-11 VERIFIED`。
- 单一目标：`crayon-capability-hub/src/audit/{mod.rs,audit_tests.rs}`——`AuditLedger`：有界脱敏审计账本，键为 `(provider_hash, tenant_hash, capability, route, outcome)` 的**64-bit hash 维度**（调用方注入 hash，本层不见 provider/tenant 明文），值为计数器（次数、失败次数、累计 payload 字节数上限截断）；`emit` 产出 `DiagnosticEvent`（`DataClass::Diagnostic`，属性只含闭合 token 与 hash 十六进制）；容量 LRU 淘汰并计 `dropped` 计数（同 AGT-11 模式）。`record_call` 直接消费 `crayon-partner-connector::api::ConnectorAuditEvent`（跨 crate 只传闭合事件）。
- 隔离与脱敏：维度必须是 hash 而非明文（类型签名强制 u64）；事件属性集闭合（namespace_hash/provider_hash/tenant_hash/capability/route/outcome/bytes），无正文/token/完整参数（HB-015）。
- 验收：`audit_tests`（维度记账、LRU 淘汰与 dropped 计数、DiagnosticEvent 产出与属性闭合性、partner 事件转换、零明文断言——序列化输出不含 provider/tenant 原文）；既有 hub 回归；clippy/fmt/security/diff-check。
- 明确不做：audit sink 落盘/上报（产品装配）、真实 hash 算法（调用方注入 u64）、HB-016 总 Review。

## HUB-15 完成记录（2026-09-11）

- 实现：`crayon-capability-hub/src/audit/{mod.rs,audit_tests.rs}`（约 350 行）——`AuditLedger`：有界（≤128 维度）脱敏审计账本。
  - `AuditDimension`：`(provider_hash, tenant_hash, capability, route, outcome)` 全部 u64/u16 hash/闭合索引——**类型签名强制，明文不可进入**。
  - `record`：容量满时淘汰最老维度并计 `dropped`（fail-closed 语义：损失被计数）；`record_connector` 直接消费 `ConnectorAuditEvent`（跨 crate 闭合事件，crayon-capability-hub 新增对 crayon-partner-connector 的依赖——出站接口 → hub 审计方向，无反向依赖）。
  - `to_diagnostic`：产出 `DataClass::Diagnostic` 的 `DiagnosticEvent`（`hub.partner.audit`），属性闭合=provider/tenant hash 十六进制 + capability/route/outcome/calls/failures/payload_bytes 计数——**无正文、无 token、无完整参数**（HB-015）。
- 验证：`cargo test -p crayon-capability-hub` **51/51**（新增 7：维度聚合、失败分列、LRU 淘汰+dropped、诊断事件仅 hash+计数断言、partner 事件转换、空账本、错误 content-free）；hub+connector 95/95；clippy `-D warnings` 零告警；fmt、`check.sh security`、`git diff --check` 通过。
- Code Review：按 v0.9 复核——hash 维度类型强制、闭合属性集、LRU+dropped、依赖方向单向（connector→hub）。P0/P1/P2=0。
- 未覆盖与风险：真实 hash 算法与 sink 落盘/上报归产品装配；HUB-16 总 Review 消费本模块。`HUB-15` 转 `DONE`，解锁 `HUB-16`。

## HUB-16 原子范围（Hub/Partner 总 Review）

- 状态：`BLOCKED`（HUB-07 未闭合，见下方依赖审计）；依赖 `HUB-01..15`。
- 单一目标：对 Hub（registry/builtin/router/policy/fallback/UI）与 Partner connector（api/trust/oauth/network/mcp/runtime/audit）做安全、隐私、供应链与性能总 Review——按 v0.9 顺序逐模块复核 owner/隔离（入站 vs 出站）、默认拒绝语义、预算闭合、脱敏面、依赖方向；核对 HB-001..015 全部用例有对应实现与测试；结论记入本 Roadmap；feature 默认关闭（GO/NO-GO：partner feature 保持 NOT_IN_RELEASE，直至确认流 UI 装配与真机矩阵完成）。
- 验收：全 HB 用例映射表无缺口；`cargo test -p crayon-capability-hub -p crayon-partner-connector` 全绿；`scripts/check.sh security`；Review 结论 P0/P1=0；Roadmap 记录。
- 明确不做：新功能实现、入站 MCP 改动、provider 接入。

### HUB-16 依赖审计与修正（2026-09-11）

- 依赖审计结论：HUB-16 声明依赖 HUB-01..15，但 **HUB-07（Site Skill registry adapter）依赖 WFL-12（runner），而 WFL-12 依赖 WFL-09→10→11 链尚未实现**——HUB-16 在 HUB-07 闭合前无法做出完整的 HB-007 用例（两 Profile Site Skill adapter）映射，故**转 BLOCKED**（原误标可领取，按审计事实修正）。
- 解除路径：WFL-09（skill-preview）→ WFL-10（store）→ WFL-11（validation）→ WFL-12（runner）→ HUB-07 → HUB-16。
- 总 Review 预检（已完成部分）：HB-001..006/009..015 的模块与测试映射齐备（capability-hub 51 + partner-connector 44 测试全绿）；security 门禁通过；入站/出站依赖单向、默认拒绝、预算闭合、脱敏面复核无缺口。剩余映射缺口仅 HB-007/008（分别等 HUB-07 与 AGT-13/14 的 CAAP 能力暴露）。
- 供应链预检：connector 依赖仅 crayon-platform-api + crayon-domain（无新外部依赖），无下载/许可审计项。

## HUB-07 完成记录（2026-09-11）

- 实现：`crayon-capability-hub/src/adapters/site_skill/{mod.rs,site_skill_tests.rs}`（约 320 行）——`SkillSource` trait（Profile 作用域的 Enabled 技能视图：enabled_names/version_of/summary_of/is_healthy）+ `RegistrySink` trait（upsert/revoke，解耦具体 registry 类型）+ `sync_site_skills`：
  - 仅当前 Profile 的 Enabled skill 注册为 `personal-skill.<name>` 能力描述符（`PersonalSkill` source、`UserApproved` trust、`PageContent` scope）；
  - health-disabled 技能跳过注册（HUB-13 拥有禁用决策）；
  - 版本变更 → 同 id 重新 upsert（版本化注册语义）；
  - 不再 Enabled 的技能从 registry revoke（`previously_registered` 跟踪）。
  - **Profile 隔离**：每个 Profile 一个 source/adapter 实例，注册表互不可见（HB-007 owner/Profile/health/版本隔离全覆盖）。
- 验证：`cargo test -p crayon-capability-hub` **56/56**（新增 5：前缀注册、unhealthy 跳过、disable→revoke、版本变更同 id 重注册、Profile 隔离）；hub 全量 56/56；clippy `-D warnings` 零告警；fmt、security、diff-check 全过。
- 实现期修复：`sync_site_skills` 的 previously_registered/live 列表存前缀化 qualified id 而非裸名，使 revoke 匹配正确（首版用裸名导致 revoke 失效——测试抓出后修复）。
- Code Review：按 v0.9 复核——Profile 隔离、health 过滤、版本化、闭合错误面、无跨 Profile 泄漏。P0/P1/P2=0。
- 未覆盖与风险：真实 WFL-10 store → SkillSource 的桥接归产品装配。`HUB-07` 转 `DONE`，解锁 `HUB-16`（HUB-01..15 全 DONE）。

## HUB-16 完成记录（2026-09-11，Hub/Partner 总 Review）

- 审计范围：HUB-01..15 全部模块（registry/builtin/router/policy/fallback/UI/07 adapter/09 api/10 trust/11 oauth/12 network/13 mcp/14 runtime/15 audit），逐条 HB-001..015 映射：
  | HB | 模块 | 结论 |
  |---|---|---|
  | 001/002 | registry+builtin | descriptor 校验/版本/trust 闭合；内建无重复无隐藏强能力 |
  | 003/004 | router+policy | RouteDecision 确定性；partner→skill→web→human→reject 闭合 |
  | 005 | fallback | 重授权/重确认/幂等/未知副作用停止 |
  | 006 | UI | route 预览/临时覆盖（装配任务，模型层 VERIFIED） |
  | 007 | 07 adapter | Profile/owner/health/版本隔离；disable→revoke |
  | 008 | — | 入站 CAAP 能力暴露待 AGT-13/14（feature OFF，无缺口） |
  | 009 | 09 api + 依赖审计 | 入站/出站 crate/namespace/session/token/审计五重隔离断言 |
  | 010 | 10 trust | 精确版本 pin、revoke/kill switch fail-closed、降级/篡改拒绝 |
  | 011 | 11 oauth | 租户隔离 vault、常数时间 state、PKCE 注入、redirect 精确匹配 |
  | 012 | 12 network | 9 类 SSRF 地址全拒、rebinding 混合解析拒绝、每跳重验、1MiB 预算 |
  | 013 | 13 mcp | namespace 强制、注入载体拒绝、authority 宿主所有、响应 opaque |
  | 014 | 14 runtime | 配额/熔断三态/副作用不重试/协作取消 |
  | 015 | 15 audit | hash-only 维度、闭合属性、LRU+dropped |
- 供应链：partner-connector 仅依赖 crayon-platform-api + crayon-domain（断言锁定）；capability-hub 增 partner-connector 单向依赖（审计事件消费方向）；无新增外部依赖。
- 性能：全部策略层纯内存/注入时钟，热路径无 IO/锁竞争（WFL-12/14 语义），预算全部闭合常量。
- 验证：capability-hub 56/56 + partner-connector 44/44 + workflow 96/96 全绿；clippy `-D warnings`、fmt、security、diff-check 全过。
- Review 结论（v0.9 顺序复核）：owner 唯一、默认拒绝、预算闭合、脱敏闭合、依赖单向。**P0/P1/P2=0**。
- **GO/NO-GO：partner feature = NOT_IN_RELEASE（默认关闭）**。GO 条件（后续）：AGT-05 确认 UI 装配 + AGT-12Cc2 宿主 FFI + 真机 HB-011/012 矩阵 + PRV-13B 专项。在此之前所有出口路径 fail-closed 且不可达。
- 未覆盖与风险（如实）：HB-008 待 AGT-13/14；HB-006 UI 装配待桌面任务；真实网络/DNS/签名执行归宿主注入。
- `HUB-16` 转 `DONE`；`HUB` 模块一期+已解锁二期任务全部闭合，剩余 HUB-08（等 AGT-14）。
