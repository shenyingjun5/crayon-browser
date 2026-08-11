# ACT：页面语义地图与可验证动作 Roadmap

- 状态：规划完成，尚未开工
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
| ACT-01 | TODO | CNT-03,AGT-01 | `crayon-domain/semantic/**`,`crayon-ipc-schema/**` | 冻结 Page/Action/Form/Media/Risk Map、ChangeSet、effect 与错误 schema | `AC-001`; current/previous golden；无引擎类型 |
| ACT-02 | TODO | ACT-01 | `crayon-semantic-action/detail/**` | `compact/standard/internal-full` 字段和资源预算 | `AC-002`; raw DOM/HTML/CDP 永不出界 |
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
