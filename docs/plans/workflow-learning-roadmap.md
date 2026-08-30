# WFL：Workflow Learning、Challenge 与个人 Site Skill Roadmap

- 状态：执行中；`WFL-01 VERIFIED`（2026-08-30）；`WFL-02/04/06 READY`
- 任务数：16
- 目标：从用户授权且已验证成功的任务生成可预览、可保存、可验证、可回滚的个人 Site Skill，并安全处理验证码/风控的人机接管
- 非目标：自动解验证码、从失败任务学习、记录密码/正文/secret、技能继承旧授权、静默修改高风险步骤

## 1. 边界

- Workflow 记录最小语义 trace，不记录 DOM selector、字段值、Cookie、Authorization 或正文副本。
- Challenge Detector 只检测和暂停；用户完成后重新读取、重新授权、重新验证。
- 技能是当前用户/Profile 的本地资产；保存、升级、修复和回滚均有显式版本和证据。
- 模型可在第二阶段提出不可信 candidate，不决定风险或直接发布技能。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 单一交付 | 验收与测试 |
|---|---|---|---|---|---|
| WFL-01 | VERIFIED | ACT-12,AGT-03 | `crayon-domain/workflow/**`,`crayon-ipc-schema/**` | Trace/Recipe/SiteSkill/Challenge/Checkpoint schema 与状态机 | `WF-001`; golden/迁移/边界 |
| WFL-02 | VERIFIED | WFL-01,ACT-06 | `crayon-workflow/challenge/**` | 确定性 Challenge Detector，仅输出检测证据 | `WF-001`,`WF-002`; 禁止解题/绕过 surface |
| WFL-03 | TODO | WFL-02,AGT-05 | `crayon-workflow/handoff/**`,`apps/desktop-cef/**/handoff/**`,locales | `AwaitingHuman` UI 与继续/取消状态 | `WF-003`; 无障碍/关闭/导航/超时 |
| WFL-04 | TODO | WFL-01,PRV-07,PRV-08 | `crayon-workflow/checkpoint/**`,`crayon-platform-api/**` | 加密、短期、最小 checkpoint store | `WF-004`; 无 secret/正文；过期/清除/损坏 |
| WFL-05 | TODO | WFL-03,WFL-04,ACT-08 | `crayon-workflow/resume/**` | 用户完成后的重新 snapshot/risk/grant/precondition 与幂等恢复 | `WF-005`; challenge 仍在/漂移/未知副作用终止 |
| WFL-06 | TODO | WFL-01,ACT-08,AGT-11 | `crayon-workflow/trace/**` | 仅记录已授权步骤、语义意图和 verified effect 的有界 trace | `WF-006`; cancel/fail/旧结果/TTL |
| WFL-07 | TODO | WFL-06,PRV-10 | `crayon-workflow/redaction/**` | 写盘前敏感值移除与参数 placeholder | `WF-007`; seeded secret/canary 零泄漏 |
| WFL-08 | TODO | WFL-06,WFL-07 | `crayon-workflow/recipe/**` | 仅从 verified success 生成候选 Recipe | `WF-008`; fail/cancel/indeterminate 不学习 |
| WFL-09 | TODO | WFL-08,AGT-05 | `apps/desktop-cef/**/skill-preview/**`,locales | 技能名称、站点、参数、步骤、风险、权限、数据流预览和保存确认 | `WF-009`; 拒绝/过期/变更后重确认 |
| WFL-10 | TODO | WFL-09,PRV-07 | `crayon-workflow/store/**`,`crayon-platform-api/**` | 按 OS user/Profile 隔离的加密个人 Skill Store | `WF-010`; migration/corrupt/quota/无痕清除 |
| WFL-11 | TODO | WFL-10,FND-09 | `crayon-workflow/validation/**`,`test-support/**` | 本地 fixture/沙箱 matcher、参数、步骤和 effect 验证 | `WF-011`; 无公共网络/后台批量访问 |
| WFL-12 | TODO | WFL-11,ACT-08,AGT-04 | `crayon-workflow/runner/**`,`crayon-app-runtime/**` | 每次重新授权、用当前 action_id 执行的 Site Skill runner | `WF-012`; cancel/deadline/idempotency/人机接管 |
| WFL-13 | TODO | WFL-10,WFL-12 | `crayon-workflow/health/**`,`crayon-workflow/version/**` | health、失败窗口、禁用、版本和回滚 | `WF-013`; restart/crash/rollback/配额 |
| WFL-14 | TODO | WFL-13,ACT-10 | `crayon-workflow/drift/**` | drift 分类与修复候选，区分 challenge/permission/network/effect | `WF-014`; 低置信度不误报健康 |
| WFL-15 | TODO | WFL-14,ACT-06,ACT-08 | `crayon-workflow/heal/**` | 仅低风险、唯一匹配、效果可验证的受控修复 | `WF-015`; 高风险/跨源/语义变化必须人工确认 |
| WFL-16 | TODO | WFL-01..WFL-15 | threat model,Review,`docs/current/**` | Workflow/Challenge/Site Skill 隐私、安全、性能总 Review | 全 WF；P0/P1=0；feature 独立 GO/NO-GO |

## 3. 完成门禁

- 保存技能前必须有 verified success 和用户显式确认；运行技能时必须重新 grant/confirmation。
- Challenge 状态不保存解题数据，不接第三方打码，不自动点击或改变挑战可见性。
- self-heal 错配优先于成功率：无法证明唯一低风险等价目标时停止并生成审阅候选。
- 个人技能失败或本模块 NO-GO 不影响浏览器、投屏、Markdown 和只读 Agent 核心功能。


## WFL-01 原子范围（Trace/Recipe/SiteSkill/Challenge/Checkpoint schema 与状态机）

- 状态：`VERIFIED`；依赖 `ACT-12 DONE`、`AGT-03 DONE`。
- 单一目标：在 `crayon-domain/workflow/**` 冻结 workflow 家族 v1 schema 与核心状态机，并纳入 current/previous IPC golden 兼容窗口。
- 输入与输出：输入为 ACT-01 词汇（origin/node/action/effect）与 domain ids；输出仅限 `crates/crayon-domain/src/workflow/**`、`tests/workflow.rs`、`lib.rs` re-export、`schemas/{current,previous}/` 5 组 golden、`crates/crayon-ipc-schema/tests/v1_contract.rs` 登记与本 Roadmap。
- 语义与预算：Trace ≤64 steps（意图+verified outcome，无值/selector/正文）；Recipe ≤64 步、name `[a-z0-9_-]` ≤64B、version ∈ [1, 65535]；SiteSkill 闭合状态 Draft/Candidate/Enabled/Disabled，revision ∈ [1, 65535]，仅 Enabled 可运行且每次运行仍需新授权；Challenge 闭合 kind/phase 状态机 `Detected → AwaitingHuman → {Resumed|Cancelled|Expired}`，无解题 surface（wire 断言零 solution/solver/token 泄漏），evidence note ≤128B；Checkpoint payload ≤4096B、TTL ∈ (0, 300_000ms] 注入时钟、单次消费。
- 验收：WF-001 契约侧（schema/状态机/golden/边界）；闭合词汇与 golden wire 名锁定；unknown field 拒绝；Secrets never serialize 套件通过。
- 明确不做：Challenge 检测实现（WFL-02）、checkpoint 加密存储（WFL-04）、trace 记录器（WFL-06）、recipe 生成门（WFL-08）。

### WFL-01 完成记录（2026-08-30）

- 实现：`workflow` 模块 5 个子模块（trace/recipe/skill/challenge/checkpoint），全部 `deny_unknown_fields` wire + 预算命名常量 + 校验构造器；ChallengeSession/Checkpoint 为显式状态机，终态后一切转换拒绝（SessionClosed/NotLive）；`Checkpoint::consume` 先置位再返回保证单次。
- Golden：`workflow_trace.json`、`recipe.json`、`site_skill.json`、`challenge_session.json`、`checkpoint.json`（current/previous 逐字节镜像），并入 `v1_contract` roundtrip 与 unknown-field 扫描。
- 验证：`cargo test -p crayon-domain --test workflow` 6/6；`cargo test -p crayon-domain` 全量 67 项通过；`cargo test -p crayon-ipc-schema` 全量通过（v1_contract 含 5 组新 golden）；`cargo clippy --all-targets -- -D warnings` 通过；`cargo fmt --check` 通过；`bash scripts/check.sh security` 全绿；`git diff --check` 通过。
- Code Review：按 v0.8 复核；修正一处状态机错误分类（终态转换统一 SessionClosed，非终态非法转换 IllegalTransition）。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：trace 记录器/recipe 生成/store 由 WFL-02..12 续作；checkpoint payload 解释权归 checkpoint 层，本层不读内容。

## WFL-02 原子范围（确定性 Challenge Detector）

- 状态：`VERIFIED`；依赖 `WFL-01 VERIFIED`、`ACT-06 DONE`。
- 单一目标：消费 Browser 侧归一化的闭合挑战信号，确定性地产出 WFL-01 `ChallengeEvidence` 或“未检测到”；证据只含 kind、origin 和静态分类 token。
- 输入与输出：输入为可信 Browser adapter 汇总的布尔信号与经验证的当前 origin；输出限 `crates/crayon-workflow/src/challenge/**`、crate 装配、测试和本 Roadmap。
- 边界：信号数量闭合且无 DOM/正文/selector/验证码值；多信号按 captcha → risk check → login required 的保守优先级收敛；无信号不暂停；非法 origin fail closed。
- 验收：`WF-001` 的 captcha/滑块/登录确认/风控与相似非挑战 fixture；`WF-002` 静态/序列化面零 solution/solver/token/bypass、零网络与零自动操作接口；Format、Clippy、workspace/security 回归。
- 明确不做：验证码求解、自动点击、挑战隐藏、第三方打码、短信/邮箱验证码读取、人机接管 UI 与恢复（WFL-03/05）。

### WFL-02 完成记录（2026-08-30）

- 实现：新增 `crayon-workflow::challenge`；`ChallengeSignals` 只接受四个 Browser 归一化布尔事实，`ChallengeDetector` 为纯函数式分类器，按 captcha/slider → risk → login 的稳定优先级产出 WFL-01 有界证据；非法 origin（含无信号输入）统一 fail closed；无网络、回调、页面操作或解题字段。
- 验证：`cargo test -p crayon-workflow` 5/5；`cargo clippy -p crayon-workflow --all-targets -- -D warnings`、`cargo fmt --all -- --check`、`scripts/check.sh security`、`git diff --check` 通过。`cargo test --workspace` 首轮在未修改的 Windows named-pipe `same_user_client_is_admitted_end_to_end` 偶发 `OsDenied`，同进程后单独重跑 1/1 通过；本任务前已完成到该点的所有 crate 均通过。
- Code Review：按 v0.8 从需求边界、正确性、架构、安全、性能、测试检查；Review 中补上“无信号但 origin 非法”也必须拒绝，并修正测试文件隔离命名。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：Browser/CEF 信号采集与 AwaitingHuman UI 属 WFL-03；真实站点挑战准确率需后续 QAR fixture/真机矩阵，本任务只冻结确定性分类核心。
