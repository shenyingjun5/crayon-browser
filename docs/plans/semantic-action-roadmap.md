# ACT：页面语义地图与可验证动作 Roadmap

- 状态：执行中；`ACT-01 DONE`，`ACT-02 READY`
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
| ACT-02 | READY | ACT-01 | `crayon-semantic-action/detail/**` | `compact/standard/internal-full` 字段和资源预算 | `AC-002`; raw DOM/HTML/CDP 永不出界 |
| ACT-03 | TODO | ACT-01,AGT-03 | `crayon-semantic-action/handle/**` | action_id 签发、target/generation/TTL/nonce 绑定与失效 | `AC-003`; property/replay/跨 Profile |
| ACT-04 | TODO | ACT-03,CEF-05 | `browser/engine-api/**`,`browser/cef-shell/**/semantic/**` | 多信号 action discovery 与内部 locator evidence | `AC-004`; 外部无 selector；重复/遮挡/动态页 |
| ACT-05 | TODO | ACT-04 | `crayon-semantic-action/precondition/**` | 可见、唯一、可操作、同源、页面状态前置条件 | `AC-005`; stale/hidden/cross-origin fail closed |
| ACT-06 | TODO | ACT-01,PRV-10 | `crayon-semantic-action/risk/**` | 确定性、单调 risk policy 与敏感元素排除 | `AC-006`; password/payment/file 不可执行 |
| ACT-07 | TODO | ACT-05,ACT-06,AGT-05,CEF-07 | `crayon-semantic-action/runtime/**`,`crayon-app-runtime/**` | action_id 到正常浏览器用例的受控执行 | `AC-007`; cancel/deadline/generation/确认绑定 |
| ACT-08 | TODO | ACT-07 | `crayon-semantic-action/effect/**` | 有界效果等待、verified/failed/indeterminate 和幂等语义 | `AC-008`; 不确定副作用不重放 |
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
