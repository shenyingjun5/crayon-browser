# WFL：Workflow Learning、Challenge 与个人 Site Skill Roadmap

- 状态：规划完成，尚未开工
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
| WFL-01 | TODO | ACT-12,AGT-03 | `crayon-domain/workflow/**`,`crayon-ipc-schema/**` | Trace/Recipe/SiteSkill/Challenge/Checkpoint schema 与状态机 | `WF-001`; golden/迁移/边界 |
| WFL-02 | TODO | WFL-01,ACT-06 | `crayon-workflow/challenge/**` | 确定性 Challenge Detector，仅输出检测证据 | `WF-001`,`WF-002`; 禁止解题/绕过 surface |
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
