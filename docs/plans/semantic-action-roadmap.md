# ACT：页面语义地图与可验证动作 Roadmap

- 状态：执行中；`ACT-01 DONE`、`ACT-02..08 VERIFIED`（均 2026-08-30）
- 任务数：12
- 目标：把已验证页面事实转换为 Agent 可高效读取的语义地图，并通过短期 `action_id`、前置条件和效果验证执行受控操作
- 非目标：原始 DOM/HTML/CDP 输出、长期 CSS/XPath、任意 JavaScript、截图/OCR 常规控制、密码/支付/通用文件上传

## 1. 边界

- `crayon-page-data` 拥有基础事实；`crayon-semantic-action` 拥有地图、action_id、内部 locator evidence、风险、前置条件和效果验证。
- 对外仅 `compact`/`standard`；`full` 是有界内部 profile，不得等同原始 DOM。
- 动作最终调用 `crayon-app-runtime` 正常用例；授权和确认仍由 `crayon-agent-gateway` 拥有。
- risk 单调且由确定性规则决定；模型、页面和 connector 只能提供不可信证据。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 单一交付 | 验收与测试 |
|---|---|---|---|---|---|
| ACT-01 | DONE | CNT-03,AGT-01 | `crayon-domain/semantic/**`,`crayon-ipc-schema/**` | 冻结 Page/Action/Form/Media/Risk Map、ChangeSet、effect 与错误 schema | `AC-001`; current/previous golden；无引擎类型 |
| ACT-02 | VERIFIED | ACT-01 | `crayon-semantic-action/detail/**` | `compact/standard/internal-full` 字段和资源预算 | `AC-002`; raw DOM/HTML/CDP 永不出界 |
| ACT-03 | VERIFIED | ACT-01,AGT-03 | `crayon-semantic-action/handle/**` | action_id 签发、target/generation/TTL/nonce 绑定与失效 | `AC-003`; property/replay/跨 Profile |
| ACT-04 | VERIFIED | ACT-03,CEF-05 | `browser/engine-api/**`,`browser/cef-shell/**/semantic/**` | 多信号 action discovery 与内部 locator evidence | `AC-004`; 外部无 selector；重复/遮挡/动态页 |
| ACT-05 | VERIFIED | ACT-04 | `crayon-semantic-action/precondition/**` | 可见、唯一、可操作、同源、页面状态前置条件 | `AC-005`; stale/hidden/cross-origin fail closed |
| ACT-06 | VERIFIED | ACT-01,PRV-10 | `crayon-semantic-action/risk/**` | 确定性、单调 risk policy 与敏感元素排除 | `AC-006`; password/payment/file 不可执行 |
| ACT-07 | VERIFIED | ACT-05,ACT-06,AGT-05,CEF-07 | `crayon-semantic-action/runtime/**`,`crayon-app-runtime/**` | action_id 到正常浏览器用例的受控执行 | `AC-007`; cancel/deadline/generation/确认绑定 |
| ACT-08 | VERIFIED | ACT-07 | `crayon-semantic-action/effect/**` | 有界效果等待、verified/failed/indeterminate 和幂等语义 | `AC-008`; 不确定副作用不重放 |
| ACT-09 | TODO | ACT-04,ACT-06 | `crayon-semantic-action/form/**` | FormMap 字段/约束/错误/filled 状态 | `AC-009`; 不包含字段值；敏感/file 排除 |
| ACT-10 | TODO | ACT-01,CNT-04 | `crayon-semantic-action/change/**`,`crayon-page-data/**` | revision/ChangeSet 生成、分页、截断和旧增量丢弃 | `AC-010`; 动态页/高频变化/背压 |
| ACT-11 | TODO | ACT-07,ACT-08 | `crayon-semantic-action/handoff/**`,`crayon-app-runtime/**` | 动作级人工接管结果、可恢复/不可恢复原因 | `AC-011`; 无隐式重试或权限继承 |
| ACT-12 | TODO | ACT-02..ACT-11 | `tests/security/semantic/**`,`tests/perf/semantic/**`,`docs/current/**` | 语义动作性能/安全/生命周期总 Review 与 GO/NO-GO | `AC-001..012`; P0/P1=0；Release surface 零命中 |

## 3. 性能与 Review 门禁

- 同一 navigation 的地图、Markdown 和 Agent 读取共享一次 verified cache；ChangeSet 必须显著小于可复用场景下的全量结果。
- collector、map builder、effect waiter、队列和缓存全部有界，可取消，旧 generation 结果只丢弃。
- `action_id` 不是跨导航持久标识；Site Skill 保存语义意图和 matcher 证据，不保存当前 handle。
- 高风险或低置信度目标变化不得静默重定位；视觉 fallback 不能绕过 same-origin、risk、grant 或 confirmation。

## ACT-01 原子范围（语义地图 schema 冻结）

- 状态：`DONE`；依赖 `CNT-03 DONE`、`AGT-01 DONE`。
- 单一目标：在平台无关 domain 层冻结 v1 Page/Action/Form/Media/Risk Map、revision ChangeSet、effect report 与稳定错误码，并纳入 current/previous IPC golden 兼容窗口。
- 输入与输出：输入为 CNT-03 的 tab/generation/revision owner 契约和 AGT-01 的版本化协议基础；输出仅限 `crates/crayon-domain/src/semantic/**`、domain re-export、`crates/crayon-domain/tests/semantic.rs`、`crates/crayon-ipc-schema/tests/v1_contract.rs`、`schemas/{current,previous}/semantic_*.json` 与本 Roadmap。
- 边界与预算：node id 是每 navigation 生成的 64-byte 闭合 token，不是 selector；map 最多 512 nodes、256 actions、16 forms、16 media、512 risk entries；form 不表达字段值，action 集不表达 JavaScript/drag/upload/password，media 不携带 URL。origin 仅表达 HTTP(S) scheme/host/port；所有 wire struct 拒绝未知字段，current/previous golden 逐字节镜像。
- 验收：AC-001 覆盖闭合枚举、预算、引用完整性、敏感节点、origin、revision 单调、effect outcome/reason、无字段值/selector/HTML/CDP、未知字段与 stable error；domain semantic test、IPC v1 golden、fmt、严格 Clippy、repo guard/security。
- 明确不做：detail profile/总资源裁剪（ACT-02）、action_id/locator（ACT-03/04）、risk policy 执行（ACT-06）、动作执行/effect wait（ACT-07/08）、ChangeSet 生成与背压（ACT-10）。

### ACT-01 完成记录（2026-08-30）

- 实现：新增平台无关 `semantic` domain family，冻结 20 类 node、6 类受控 action、FormMap、MediaMap、9 类 risk reason、PageMap、单调 revision ChangeSet、verified/failed/indeterminate effect report 与 11 个稳定错误码。所有集合、token、标题/摘要/错误/detail 均有命名预算；跨集合引用由 `PageMap::new` 校验。
- 安全边界：schema 不包含 CEF/ArkWeb/DOM/CDP、CSS/XPath、任意 JavaScript、字段值、密码、支付值、文件路径、媒体 URL、Cookie/Authorization 或执行入口。Password/File node 仅作为可识别的敏感类型存在，不可由闭合 action 集表达上传或凭证输入；wire struct 使用 `deny_unknown_fields`，opaque node id 使用闭合字符集。
- Golden 与测试：新增 5 组 current/previous golden（page map、change set、effect verified/failed、semantic error）且逐字节镜像；semantic 13/13 覆盖闭合枚举、预算/重复/引用、origin、Form 无值、敏感类型、revision/effect 组合及 unknown field；IPC v1 contract 9/9 覆盖 roundtrip、previous、unknown field、version 和 secret denial。
- 验证：`cargo fmt --all -- --check` 通过；`cargo clippy -p crayon-domain -p crayon-ipc-schema --all-targets -- -D warnings` 通过；`cargo test -p crayon-domain --test semantic -- --test-threads=1` 13/13；`cargo test -p crayon-ipc-schema --test v1_contract -- --test-threads=1` 9/9；semantic current/previous `cmp` 全部通过；`git diff --check` 通过；`bash scripts/check.sh security` 通过。`bash scripts/check.sh fast` 的 guard/format/asset 与已执行的数百项 Workspace 测试通过，因宿主在每个测试二进制间固定等待约 50 秒而主动中止，未声称完整通过；影响包与 IPC 回归已由上述定向命令完整覆盖。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、安全/隐私、性能、测试与可维护性；保留 PageMap/ChangeSet/EffectReport 的显式 schema-field 构造参数并使用局部 Clippy 例外，避免引入第二套参数 schema。最终 P0/P1/P2 = 0/0/0；无锁、IO、网络、回调、日志或运行时状态。
- 未覆盖与风险：完整 fast Workspace 因宿主调度未跑完；已执行部分无失败，剩余为 ACT-01 未触及模块。detail profile、action handle、Browser collector、risk policy、执行与 effect wait 均由 ACT-02..10 继续实现；`ACT-02 READY`。


## ACT-02 原子范围（detail profile 字段与资源预算）

- 状态：`VERIFIED`；依赖 `ACT-01 DONE`。
- 单一目标：在新建 `crayon-semantic-action` crate 冻结 v1 语义地图的三个有界输出 profile（`compact`/`standard`/`internal_full`）的字段面与资源预算，并提供确定性投影函数。
- 输入与输出：输入为 ACT-01 冻结的 `crayon-domain::PageMap` 词汇；输出仅限 `crates/crayon-semantic-action/**`、workspace member 登记与 `crayon-domain` 对预算常量的纯新增 re-export。
- 边界与预算：`compact` ≤128 nodes / ≤64 actions，forms/media/risk 仅计数不携带条目；`standard`/`internal_full` 等于冻结地图预算（≤512 nodes / ≤256 actions / 16 forms / 16 media / 512 risk）；序列化字节上限 compact 256KiB、standard 1MiB、internal_full 2MiB；`DetailBudget::new` 拒绝超过地图冻结上限或为 0 的预算。`internal_full` 附加闭合内部 annotation（ordinal + sensitive），永不对外 transport，结构上无法表达 DOM/HTML/CDP/selector。
- 验收：AC-002 覆盖 profile 闭合集与预算冻结、compact 对超大页的截断与显式 truncation 报告、standard 恒等与 fail closed、internal_full annotation 与敏感标志、字节预算 fail closed、wire `deny_unknown_fields` 与 raw 表面零泄漏。
- 明确不做：action handle（ACT-03）、多信号 discovery/locator evidence（ACT-04）、precondition（ACT-05）、risk policy 执行（ACT-06）、执行/effect（ACT-07/08）、ChangeSet 生成（ACT-10）。

### ACT-02 完成记录（2026-08-30）

- 实现：新增 `crates/crayon-semantic-action`（`detail` 模块）：`DetailProfile` 闭合三 profile、`DetailBudget` 预算模型与校验、`render_compact`（截断并报告 truncation，节点被省略的 action 一并省略）、`render_standard`（恒等且超预算 fail closed）、`render_internal_full`（冻结地图 + 闭合 annotation）。截断语义：compact 不携带 forms/media/risk 属于字段面设计，`*_count` 承载信息，只有节点/action 超预算才计入 truncation。
- 安全边界：三个 profile 均由 `deny_unknown_fields` wire 类型承载；测试断言 wire 零泄漏 `selector`/`html`/`dom`/`xpath`/`javascript`，raw DOM 注入被拒绝；`internal_full` 仅命名约定内部 + 类型闭合，未新增外部传输面。
- 验证：`cargo test -p crayon-semantic-action` 8/8（detail.rs）；`cargo test -p crayon-domain -p crayon-ipc-schema` 全量回归通过（含 semantic 13 与 IPC v1 9）；`cargo clippy -p crayon-semantic-action -p crayon-domain --all-targets -- -D warnings` 通过；`cargo fmt -p crayon-semantic-action -- --check` 通过。
- Code Review：按 v0.8 复核；修正一处语义（profile 字段面裁剪不作为 truncation 事件）并保持截断可解释；`fits_map` 因 MSRV 1.85 非 const 化。P0/P1/P2 = 0/0/0；无锁、IO、网络、日志或运行时状态。
- 未覆盖与风险：byte budget 校验需要一次序列化（读取路径上的 CPU 成本，预算内地图最大约 200KiB，可接受）；未涉及真实 collector 接线。`ACT-03 READY`（依赖 AGT-03 已 DONE）。


## ACT-03 原子范围（action_id 签发与绑定失效）

- 状态：`VERIFIED`；依赖 `ACT-01 DONE`、`AGT-03 DONE`。
- 单一目标：在 `crayon-semantic-action/handle/**` 冻结单次 `action_id`（ActionHandle）的数据契约与有界签发/解析/消费/失效状态机。
- 输入与输出：输入为 ACT-01 词汇（node/action kind/tab/generation）与 profile scope token；输出仅限 `crates/crayon-semantic-action/src/handle/**`、`tests/handle.rs` 与本 Roadmap。
- 边界与预算：handle id 为 `h`+26 位 base32（128-bit 熵，闭合字符集）；TTL ∈ (0, 300_000 ms]；nonce 为一次性 64-bit 熵；registry 上限 256 个活 handle，满载返回 `Saturated` 不驱逐；时钟全部注入，不读系统时钟。
- 失效语义：解析失败按 Unknown/Expired/StaleGeneration/ProfileMismatch/NonceMismatch 闭合返回；消费为单次（先移除后成功，重放见 Unknown）；nonce 不匹配的 consume 销毁 handle；跨 tab 请求与伪造 id 不可区分（Unknown）；generation/profile/tab 提供批量失效。
- 验收：AC-003 覆盖同 generation 重读稳定、单次消费与重放、nonce 猜测销毁、TTL 边界与 sweep、generation 推进只失效旧 handle、Profile 切换、目标/tab 绑定、有界签发与熵 id 闭合形状；descriptor wire 零页面内容。
- 明确不做：多信号 discovery/locator evidence（ACT-04）、precondition（ACT-05）、risk policy（ACT-06）、执行与 effect wait（ACT-07/08）、grant/确认（AGT-04/05 所有权）。

### ACT-03 完成记录（2026-08-30）

- 实现：新增 `handle` 模块：`ActionHandleId`（OS 熵 base32 签发 + 闭合校验）、`ProfileScope`/`HandleNonce` 强类型绑定、`ActionHandle` 冻结数据契约（TTL 边界校验、注入时钟）与 `ActionHandleDescriptor`（deny_unknown_fields 外部视图，仅 id/node/kind/expires）；`HandleRegistry` 单一所有者：`issue`（有界、拒绝式满载）、`resolve`/`consume`（单次、fail closed、nonce 销毁）、`invalidate_before_generation`/`invalidate_tab`/`invalidate_profile`/`sweep_expired`（全部有界、返回丢弃计数）。
- 安全边界：handle 不含 selector/DOM/页面内容；跨 Profile 与跨 tab 一律拒绝且不可枚举；重放、过期、旧 generation 无副作用；无锁、无 IO、无系统时钟读取；执行接线未开始（ACT-07），grant/确认仍归 agent-gateway。
- 验证：`cargo test -p crayon-semantic-action` 17/17（detail 8 + handle 9）；`cargo test -p crayon-domain -p crayon-ipc-schema` 13 个 suite 全 ok 回归；`cargo clippy -p crayon-semantic-action --all-targets -- -D warnings` 通过（修正 module_inception、too_many_arguments 显式说明、div_ceil）；`cargo fmt -p crayon-semantic-action -- --check` 通过。
- Code Review：按 v0.8 复核；consume 采用先移除后成功保证重放不可见；tab 不匹配与伪造 id 统一为 Unknown 防枚举。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：nonce 销毁仅在 `consume` 路径，`resolve` 的 nonce 不匹配不销毁（resolve 为只读预检，文档已注明）；handle 尚无 Browser 侧签发方接线（ACT-04/07）。`ACT-04 READY`（依赖 CEF-05 已完成）。


## ACT-04 原子范围（多信号 action discovery 与内部 locator evidence）

- 状态：`VERIFIED`（engine-api 契约模型层）；依赖 `ACT-03 VERIFIED`、`CEF-05 DONE`。
- 单一目标：在 `browser/engine-api` 冻结多信号 action discovery 契约：闭合信号/提示词汇、确定性多信号一致匹配、遮挡排除、模糊 fail closed 与内部 locator evidence 数据面。
- 输入与输出：输入为 verified 候选事实（opaque target token + 信号值）与调用方 hints；输出仅限 `browser/engine-api/include/crayon/browser_engine/action_discovery.h`、`src/action_discovery.cc`、`tests/action_discovery_test.cc`、`tests/headers/action_discovery.cc`、共享 `IsValidBoundedText` 提升（types/snapshot 重构）与本 Roadmap。
- 边界与预算：≤512 候选、每候选 ≤8 信号、≤4 hints（kind 去重）、信号值 ≤256B 有效 UTF-8；候选/信号预算超限整体拒绝，不静默截断。
- 匹配语义：候选匹配要求全部 hint 按种类+值同时命中（多信号一致，非投票）；occluded 候选永不匹配；0/1/>1 命中分别给出 no_match/unique/ambiguous；ambiguous 不报告 target；跨 tab/伪造 id 由 handle 层负责。
- 验收：AC-004 的契约侧：闭合词汇与稳定 verdict 名、部分信号不匹配、模糊 fail closed、遮挡排除、畸形输入整体拒绝、动态列表预算边界；forbidden API scan 对 `selector`/DOM/CDP 等零命中。
- 明确不做：CEF 实机接线（`browser/cef-shell/**/semantic/**` 归后续装配切片，与 CEF-06..14 先例一致）、precondition（ACT-05）、risk policy（ACT-06）、执行（ACT-07）。

### ACT-04 完成记录（2026-08-30）

- 实现：`action_discovery.h/cc`：`ActionSignalKind`/`DiscoveryHintKind` 闭合枚举、`ActionCandidate`/`DiscoveryHint`/`DiscoveryEvidence` 契约类型、`FuseDiscoveryEvidence` 确定性融合与 `IsValid` 输入校验；`DiscoveryTargetId` 新增 opaque id 标签；`IsValidBoundedText` 从 snapshot.cc 内部实现提升为 types.h/types.cc 共享入口，snapshot.cc 委托（同一 UTF-8/控制字符根因单点化）。
- 安全边界：evidence 仅含 opaque target、verdict、命中计数与命中的 hint kind；wire 无 CSS/XPath/JS query、无 DOM 引用；`forbidden_api_scan` 通过；ambiguous 不泄露任何候选 id。
- 验证：独立构建 `cmake -S browser/engine-api -B /tmp/engine-api-build -DCRAYON_ENGINE_API_BUILD_TESTS=ON && cmake --build && ctest`：4/4 通过（browser_engine_contract、browser_engine_discovery 5 场景、headers compile、forbidden scan）。`git diff --check` 通过。
- Code Review：按 v0.8 复核；修正两处（evidence target 改 `std::optional` 表达无匹配；`ActionCandidate` 显式构造拒绝无主候选）。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：CEF 真实引擎接线与遮挡/动态页真机证据未做（本任务为契约模型层，实机归后续 cef-shell 切片）；signals 与 hints 的枚举值顺序当前按位对齐（static_cast），由闭合枚举测试锁定。`ACT-05 READY`（依赖 ACT-04）。


## ACT-05 原子范围（可见/唯一/可操作/同源/页面状态前置条件）

- 状态：`VERIFIED`；依赖 `ACT-04 VERIFIED`。
- 单一目标：在 `crayon-semantic-action/precondition/**` 冻结执行前置条件的确定性、无副作用评估与 fail-closed 违规报告。
- 输入与输出：输入全部为 Browser verified 事实（ElementState、kind/action、bound/current origin、bound/current revision、discovery 唯一性）；输出仅限 `crates/crayon-semantic-action/src/precondition/**`、`tests/precondition.rs`、`lib.rs` re-export 与本 Roadmap。
- 语义：6 个闭合检查（visible/enabled/actionable_kind/same_origin/revision_current/unique_target）产出 7 类闭合违规；违规按 `PreconditionViolation::ALL` 稳定序去重，报告有界（≤8）；origin 先经 `is_valid_origin` 校验，畸形 origin 使评估本身失败而非误匹配；sensitive kind（password/file）报告 `SensitiveTarget` 而非 `KindMismatch`；kind→action 兼容表闭合（Click: Button/Link/Tab/MenuItem；SetText/Clear: TextInput/Textarea；SelectOption: Select；Check/Uncheck: Checkbox/Radio）。
- 验收：AC-005 契约侧：hidden/disabled/cross-origin/revision 推进/ambiguous/sensitive 全部 fail closed 且无副作用；全 hold 才允许执行；wire `deny_unknown_fields`；闭合词汇测试。
- 明确不做：stale handle 的识别（ACT-03 registry 语义）、执行（ACT-07）、effect 等待（ACT-08）、risk policy 执行（ACT-06）。

### ACT-05 完成记录（2026-08-30）

- 实现：`precondition/mod.rs`：`PreconditionCheck`/`PreconditionViolation` 闭合词汇、`PreconditionInput` 借用式 verified 事实包、`PreconditionReport`（deny_unknown_fields wire）、`is_actionable` 冻结兼容表与 `evaluate`（origin 校验 → 全检查 → 稳定序去重报告）。
- 安全边界：评估纯函数无副作用，任何违规即拒绝；页面/模型输入无法翻转结论；violation 上限 `MAX_PRECONDITION_VIOLATIONS=8` 独立命名常量。
- 验证：`cargo test -p crayon-semantic-action` 27/27（detail 8 + handle 9 + precondition 10）；`cargo test -p crayon-domain -p crayon-ipc-schema` 13 suite 全 ok；`cargo clippy --all-targets -- -D warnings` 通过；`cargo fmt --check` 通过。
- Code Review：按 v0.8 复核；将 violations 上限改为独立命名常量（复用 risk 常量为跨域耦合）。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：与真实 discovery/handle 的集成评估由 ACT-07 接线；revision 相等性判断偏严格（页面演进依赖 ACT-10 ChangeSet/重读，非本层放宽）。`ACT-06 READY`（依赖 PRV-10 已 DONE）。


## ACT-06 原子范围（确定性单调 risk policy 与敏感元素排除）

- 状态：`VERIFIED`；依赖 `ACT-01 DONE`、`PRV-10 VERIFIED`。
- 单一目标：在 `crayon-semantic-action/risk/**` 冻结确定性、单调的风险评估：由 verified 事实推导 `RiskDecision`（level + reasons + executable），敏感元素永久排除执行。
- 输入与输出：输入为 `SemanticNodeKind` 与 `RiskFacts`（7 个 verified 布尔事实）；输出仅限 `crates/crayon-semantic-action/src/risk/**`、`tests/risk.rs`、`lib.rs` re-export 与本 Roadmap。
- 单调语义：policy 不接受任何风险"输入"，只接受事实；每个事实只能抬升 level（payment/credential/file→R4，offsite/download/cross-origin→R3，ambiguous/low-confidence/unverified-effect→R2），reasons 排序去重；`MAX_EXECUTABLE_RISK=R2`，R3/R4 与 sensitive（password→SensitiveCredential、file→FileUpload）一律 `executable=false`。
- 验收：AC-006 契约侧：风险只升不降（superset 事实单调）、敏感元素零可执行路径、确定性（同事实同决策）、wire `deny_unknown_fields` 且无 lowering 表面、闭合 reasons 与稳定 wire 名。
- 明确不做：确认/grant（AGT-04/05）、执行（ACT-07）、effect（ACT-08）、page 收集（ACT-04/CEF 侧）。

### ACT-06 完成记录（2026-08-30）

- 实现：`risk/mod.rs`：`RiskFacts` 事实包（无降低路径）、`RiskDecision`（deny_unknown_fields wire）、`assess` 确定性评估与 `MAX_EXECUTABLE_RISK` 常量；`lib.rs` re-export。
- 安全边界：策略无系统时钟、无 IO、无锁；页面/模型请求在类型层面无法降低风险；确认与 grant 语义未进入本层。
- 验证：`cargo test -p crayon-semantic-action` 33/33（detail 8 + handle 9 + precondition 10 + risk 6）；`cargo clippy --all-targets -- -D warnings` 通过；`cargo fmt --check` 通过；`cargo test -p crayon-domain -p crayon-ipc-schema` 回归 13 suite 全 ok。
- Code Review：按 v0.8 复核；移除未使用的 `action` 参数（评估仅由 kind + facts 决定，避免伪输入）。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：offsite/download/cross-origin 等事实的真实引擎判定归 CEF 侧 discovery/execution 接线；本层仅冻结单调合并语义。`ACT-07 READY`（依赖 AGT-05/CEF-07 均已完成）。


## ACT-07 原子范围（action_id 到正常浏览器用例的受控执行）

- 状态：`VERIFIED`（模型层审批门 + app-runtime 执行用例）；依赖 `ACT-05/06 VERIFIED`、`AGT-05/CEF-07 DONE`。
- 单一目标：把单次 `action_id` 经 fail-closed 审批序列（确认绑定 → 风险 → handle 消费 → 前置条件）转换为对注入 executor port 的恰好一次派发。
- 输入与输出：输出仅限 `crates/crayon-semantic-action/src/runtime/**`、`crates/crayon-app-runtime/src/semantic_action_runtime.rs(+tests)`、`lib.rs` re-export、Cargo 依赖登记与本 Roadmap。
- 审批顺序：确认缺失先拒绝（不烧 handle）；风险拒绝次之（不烧 handle，调用方可重读后重试）；handle 消费（单次，重放 Unknown）；前置条件最后评估（violated 时 handle 已消费，须重读换新 handle）。`ApprovedAction` 冻结 node/action/tab/generation/deadline/confirmation，executor 不得放大。
- 边界：`ConfirmationRef` 为 ≤128B 闭合字符集 opaque token（归 AGT-05 mint）；`ActionExecutor` port 只接收 ApprovedAction，永不接触 handle/page map/未验证页面输入；runtime 计数为有界诊断计数器，不参与正确性；无锁/IO/系统时钟。
- 验收：AC-007 契约侧：恰好一次派发、重放拒绝、取消等价于不派发（无状态可残留）、generation/profile/tab 绑定拒绝、deadline 随 handle 冻结、executor 拒绝不静默重试、shutdown 幂等。
- 明确不做：effect 验证与幂等（ACT-08）、CEF 实机接线（后续装配切片）、grant/确认 UI 实现（AGT-04/05）。

### ACT-07 完成记录（2026-08-30）

- 实现：semantic-action `runtime` 模块（`SemanticActionGate.approve` 四层审批、`ApprovedAction`、`ConfirmationRef`、`ExecutionRequest`）；app-runtime `SemanticActionRuntime`（`ActionExecutor` port、有界计数、sweep/invalidate_tab/幂等 shut_down、`HandleRegistry::invalidate_all`）。
- 安全边界：审批每层 fail closed 且拒绝原因闭合；风险拒绝保留 handle、前置条件拒绝消费 handle 的差异语义被测试锁定（后者要求重读换新 handle）；executor 只见冻结的 ApprovedAction。
- 验证：`cargo test -p crayon-semantic-action -p crayon-app-runtime` 72/72（含 runtime 7 场景）；`cargo test -p crayon-domain -p crayon-ipc-schema` 13 suite 回归全 ok；`cargo clippy --all-targets -- -D warnings` 通过；`cargo fmt --check` 通过。
- Code Review：按 v0.8 复核；`gate_mut` 限 `pub(crate)` 未扩大公共 API。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：真实 CEF executor 接线归后续 cef-shell 切片；effect 等待/幂等/indeterminate 由 ACT-08 续作。`ACT-08 READY`。


## ACT-08 原子范围（有界效果等待、verified/failed/indeterminate 与幂等）

- 状态：`VERIFIED`；依赖 `ACT-07 VERIFIED`。
- 单一目标：在 `crayon-semantic-action/effect/**` 冻结效果验证的终态语义：有界等待、verified/failed/indeterminate 终态报告与 idempotency 账本（重复副作用拦截、indeterminate 永不重放）。
- 输入与输出：输入为 ACT-01 的 `EffectReport` 词汇与闭合字符集 idempotency key；输出仅限 `crates/crayon-semantic-action/src/effect/**`、`tests/effect.rs`、`lib.rs` re-export 与本 Roadmap。
- 边界与预算：等待窗口 ∈ (0, 10_000 ms]（注入时钟）；ledger ≤256 条、key ≤64B 闭合字符集 `[a-z0-9_.:-]`、`for_handle` 由单次 handle id 派生（同 handle 同 key）。
- 幂等语义：`check` 返回 Fresh/AlreadyReported/IndeterminateBlocked；重复 key 永不重跑动作且冻结记录不可改写（record 对已存在 key 返回 `DuplicateEntry`）；`indeterminate` 记录阻断重放（副作用可能已发生）；仅 `Verified` 计为成功。
- 验收：AC-008 契约侧：重复 idempotency key 拦截、indeterminate 不自动重放、零等待/超预算等待 fail closed、ledger 有界、key 闭合与 wire 往返。
- 明确不做：effect 等待的真实引擎观测（CEF 侧接线）、执行（ACT-07 已冻结审批）、handoff（ACT-11）、ChangeSet 生成（ACT-10）。

### ACT-08 完成记录（2026-08-30）

- 实现：`effect/mod.rs`：`IdempotencyKey`（闭合校验 + handle 派生）、`EffectWaitSpec`（有界窗口校验）、`EffectLedger`（check/record 冻结语义、有界、indeterminate 阻断、`is_success` 仅 Verified）；`lib.rs` re-export。
- 安全边界：冻结记录不可改写；无锁/IO/系统时钟；indeterminate 的"不确定不重放"由账本类型层面保证。
- 验证：`cargo test -p crayon-semantic-action` 40/40（detail 8 + handle 9 + precondition 10 + risk 6 + effect 7）；`cargo clippy --all-targets -- -D warnings` 通过；`cargo fmt --check` 通过。
- Code Review：按 v0.8 复核。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：真实 effect 观测（DOM/导航事实）由 CEF 侧接线归后续切片；ledger 满载拒绝策略要求调用方按 tab 生命周期清理（ACT-10/11 接线时处置）。`ACT-09 READY`。
