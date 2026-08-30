# 蜡笔 AI Agent 投屏浏览器 Code Review 标准

- 版本：v0.9
- 日期：2026-08-30
- 变更：补充主业务/辅助链、CEF 线程亲和与重入、能力证据真实性、供应链/Release artifact 和组合命令证据；明确 Review 结论与 Roadmap 最高状态分离。

## 1. 目标

Code Review 不只确认代码“能够运行”，还要确认改动：

- 满足需求且不会破坏已有行为；
- 符合项目架构、模块边界与 Cast-SDK/平台边界；
- 清晰、必要、可维护，没有无意义的重复和硬编码；
- 在并发、生命周期、隐私、安全、性能和资源占用方面可控；
- 有与风险相匹配的验证证据。

本标准适用于 Rust workspace、C++ 共享层与 CEF 壳、平台 adapter、协议/Schema、工具和测试资产。格式、排版和可自动检查的语法问题优先交给 formatter、lint 和静态检查；人工 Review 重点判断正确性、设计和风险。

每个原子任务实现并验证后必须按本标准做独立 Review。

## 2. 基本原则

1. **正确优先**：先判断需求、状态和边界条件，再讨论代码风格。
2. **架构优先于局部便利**：不能为了少写几行代码破坏模块边界、依赖方向或公共 API。
3. **简单但不过度简化**：优先使用容易理解和验证的实现，避免炫技、过度抽象和隐式约定。
4. **消除共同根因**：同类问题来自同一规则时修共同入口，不复制修补逻辑。
5. **规模是提醒，不是目标**：长函数和大文件需要解释和检查，但不得为了行数机械拆分。
6. **结论基于证据**：未运行的测试、构建、Lint、真机或性能测试不得声称通过；环境阻塞保留原始错误。
7. **区分问题和偏好**：阻断项必须说明具体场景、影响和证据，个人偏好不能伪装成缺陷。
8. **页面内容不可信是默认前提**：DOM、无障碍树、页面消息、模型输出和工具结果一律 untrusted，Review 时主动寻找它们进入授权/安全结论的路径。

## 3. 必查维度

### 3.1 需求与边界

- 任务 ID、依赖、允许/禁止路径、验收、非目标和实际 diff 一致；Roadmap 目标没有被表述成已实现事实，历史完成证据没有被改写。
- 正常、失败、取消、超时、重试、重复调用和空输入是否处理完整；是否出现假成功、吞错误或只处理理想路径。
- 边界值、状态迁移、恢复路径和回滚路径是否正确；修复是否覆盖真正根因。
- 时间、顺序、导航代际（generation）、缓存和异步结果是否可能使用过期状态。
- Windows/macOS 是当前桌面范围；HarmonyOS 只按电脑 PC 形态；Linux 无当前实现/发布承诺。当前平台策略为 macOS 先行（见根 `AGENTS.md` 项目记忆）。

### 3.2 架构、职责与依赖

- 依赖方向固定为：产品 UI/应用编排 → 领域接口 → 共享 Core/Cast-SDK facade → 平台 adapter；禁止反向依赖、循环依赖或跨层读取内部状态。
- CEF、ArkWeb、Win32/AppKit 类型和系统 API 只出现在对应 adapter/shell；不泄漏到共享领域层。
- 只有 `crayon-cast-adapter` 调 Cast-SDK；App 不拼 SOAP、DLNA metadata、CastExtension 或 receiver control URL。
- 设备协议或 facade 缺口按“浏览器 gap analysis → Cast-SDK Roadmap/API/发布 → 固定 source revision → adapter 接线”推进；Fake 或临时浏览器实现不能替代外部能力。
- 入站 MCP 与出站 Partner MCP/API 位于不同 crate/registry/session/token/network/audit 边界，无双向权限继承。
- 状态唯一所有，不直接修改其他模块的集合、锁、缓存或 generation；新抽象对应稳定业务概念。
- 公共 schema/状态机/持久化/安全边界变化先有独立 Roadmap、previous/current golden 和迁移方案。
- `lib.rs`/`main.rs` 只做装配与 re-export；禁止无边界 `utils.rs`/`manager.rs`。
- 按是否决定用户可见结果区分主业务/辅助链，不按名称判断；授权、策略、协议 terminal、恢复/fallback 和资源释放不能依赖日志/诊断成功。辅助链必须单向、非阻塞、有界并可统计 dropped，不能生成 grant、risk 或 route。

### 3.3 浏览器与投屏

- 页面消息不能替代 Browser process 的可信输入与播放推进验证（CEF-09 观测 untrusted，CEF-10 门禁唯一定夺）。
- observer 只提供事实，policy 才产生 `Direct/Relay/ExternalClientHandoff/Reject`。
- Direct/Relay 仅限 LAN；Relay route opaque、会话/设备/TTL/allow-set 绑定且不是通用代理。
- `ExternalClientHandoff` 需要用户确认，不创建 receiver handle、Relay token、WebRTC transport、采集器或编码器，结果类型上不存在“投屏已开始”。
- 新代码不得引用历史 `Mirror`、`tab_video`、`system_audio`；兼容读取只在 `MED-19` 明确窗口内。
- DRM/EME/受保护表面只允许识别和拒绝，不提取 key/license 或绕过保护；禁止自动点击播放/广告/跳过和按广告域名过滤候选。

### 3.4 页面内容、语义动作、CAAP 与模型

- 只处理用户触发的当前 tab/navigation/generation 快照；导航/关闭/取消后的旧结果不能覆盖新页面。
- 节点、深度、文本、URL、时间、输出和文件写入都有界且可取消。
- 隐藏表单值、跨源正文、Cookie、Authorization、页面存储和危险 scheme 不进入结果。
- CLI/MCP 只映射同一 CAAP/tool registry/guard/app-runtime；不存在第二套工具、错误或授权语义。
- CAAP 有版本/能力握手、current/previous golden、消息/chunk/递归/并发上限、cancel/deadline/幂等和 generation；错误码为闭合枚举且 golden 锁定。
- Grant 默认 deny、四元组绑定、撤销与目标失效立即生效；R2～R4 绑定确认；页面/模型结果不能间接扩权（AG-005）。
- 不暴露 remote bind、raw CDP/WebDriver、任意 JS、Cookie/Authorization、密码/支付、文件上传或通用文件/网络工具；永久 deny list 命中为零。
- action_id 绑定 target/navigation/generation/TTL；外部无长期 CSS/XPath；内部多信号定位执行前重新验证唯一、可见、同源和风险；`Indeterminate` 副作用停止而非自动重放。
- 第二阶段模型 provider 有独立发送确认、origin/redirect、安全存储和 payload readback；模型输出保持 untrusted，不参与权限/风险/路由决策。

### 3.5 Workflow、Challenge 与 Capability Hub

- trace 只记录最小语义意图和 verified effect；失败/取消/未知结果不能生成 Recipe；写盘前移除字段值、正文、secret 和账户标识。
- 个人 Site Skill 必须用户预览保存、按 OS user/Profile 加密隔离、每次重新授权，具备 schema/version/health/disable/rollback；版本变化不静默覆盖。
- Challenge Detector 仅检测、暂停、交给用户和重新验证；不存在自动解题、打码、自动点击或隐藏挑战路径。
- checkpoint 短期、有界、加密、无 secret；恢复重做 snapshot/risk/grant/precondition。
- self-heal 只接受唯一、低风险、效果可验证的等价变化；高风险/跨源/低置信度/语义变化必须审阅；Partner API 失败不得静默降级网页执行。
- Registry 声明 source/trust/version/lifecycle/data scope；Router 返回 route_reason；fallback 重新授权、确认与幂等判断。
- connector 校验来源/签名/版本/revoke/kill switch、OAuth state/PKCE/scope/tenant、endpoint/DNS/redirect/SSRF、schema/大小/限流/熔断和脱敏审计。
- Partner/TV Cast Manifest 仍由 Cast-SDK/接收端拥有；浏览器只消费正式 facade，无 raw manifest、协议或控制 URL 拼接。

### 3.6 并发、线程与死锁

任何涉及锁、线程、回调、队列、会话或网络 IO 的改动，都必须检查死锁和竞态：

- 列出共享状态的所有者、同步方式和必要的锁顺序；多把锁保持全局一致顺序。
- 检查正向与反向调用链，防止 `A -> B` 与 `B -> A` 的 ABBA 死锁。
- 不在持锁期间执行外部回调、listener 分发、网络/文件 IO、IPC、播放器/引擎调用、阻塞等待、线程 `join` 或不可控耗时操作；不在锁内 `await`。
- Stop/Release 与 callback、worker、timer 的停止顺序明确，避免互相等待或释放后回调；start/stop、导航、设备切换、网络切换、休眠唤醒和退出幂等并逆序释放资源。
- 等待、重试、队列和条件变量有明确唤醒条件、取消路径和合理上限；检查重复回调、回调错序、旧会话污染新会话、代际复用、丢失唤醒和饥饿。
- 并发容器、原子变量或无锁结构仍需验证组合操作一致性；不能因为“没有 mutex”就认为没有竞态。
- CEF/Win32/AppKit/ArkWeb 对象的创建、使用和销毁必须遵守 UI/IO/Renderer 等线程亲和；跨线程投递绑定 generation/weak owner，检查同步回调、嵌套 message loop 和 listener 重入造成的二次 stop 或锁内回调。
- 高风险并发修复应补锁序推演、压力测试或长稳验证。

存在可永久死锁、稳定竞态、释放后访问或线程泄漏时，不能批准合并。

### 3.7 性能、日志与诊断

- 识别热路径：导航、Relay 逐分片、socket 读写、渲染、Agent 页面快照/增量流、高频 UI 事件和状态轮询。
- 热路径避免不必要的内存分配、复制、JSON 序列化、字符串格式化、文件 IO、同步网络调用、锁竞争和重复整树遍历。
- 队列、缓存、连接、并发、重试、日志和采样窗口必须有界，并定义满载时的背压、覆盖、降级或丢弃策略与 dropped 计数。
- 辅助日志/诊断/遥测不参与主业务正确性；生产者非阻塞，消费者缺失、变慢或离线不得反压主业务。
- 辅助链初始化、flush 和销毁不能占用主链 executor、锁或退出 deadline；关闭状态下避免构造昂贵 payload，满载/离线日志必须限频。
- 不允许默认开启逐帧、逐分片、逐像素或高频轮询日志；`trace`/diagnostics 默认关闭且关闭时接近零成本。
- 日志结构化、等级与频率匹配；正常重试和预期状态不得持续刷 `WARN/ERROR`。
- 日志、receipt、诊断、trace 不输出凭证、Cookie、Authorization、URL query token、用户内容或不必要设备隐私数据。
- 性能优化给出基线、口径、测试条件和修复后数据；不得以关闭必要校验、破坏兼容性或删除错误处理换取指标。

高频日志/trace 明确造成卡顿、延迟、吞吐下降、锁竞争或内存增长时，按实际影响定为 P0/P1，不作为可选建议。

### 3.8 生命周期、错误与资源

- start/stop、connect/disconnect、导航、无痕清理、Profile 切换是否完整且按契约幂等。
- Socket、文件描述符、线程、timer、listener、引擎对象、临时文件和子进程是否释放；部分初始化失败时逆序清理已成功资源（无 orphan：每次退出恰一次确认）。
- 错误保留 operation、stage 和可诊断原因且不泄漏底层实现给产品 UI；错误恢复不伪造成功，失败后状态收敛到可重试或已终止。
- 无痕清理失败显式报告，不得把 best-effort 宣称为已清除。

### 3.9 公共 API、协议与兼容性

- 公共 API 最小、稳定、语义明确，不暴露内部实现和可变状态；参数校验、默认行为、错误语义和线程约束明确。
- 协议字段、错误码、事件和持久化结构变化向前/向后兼容；CAAP/golden/schema 变化走独立 Roadmap 与 previous/current 向量。
- 用户文案进入本地化资源（`browser/shared-ui/locales`），不硬编码在业务代码。
- 废弃接口保留合理迁移路径，不直接破坏已有集成方。
- capability/feature advertisement 必须来自真实装配与运行时检查；Fake、Mock、header 编译或跨平台编译不能把未接线能力宣告为 available。

### 3.10 安全与隐私

- 外部输入、协议消息、URL、重定向、DNS、文件路径、消息长度和数量全部边界验证；覆盖 SSRF、DNS rebind、开放代理、重放和路径穿越。
- 删除前验证显式根、规范路径、符号链接/目录联接和目标数量；失败停止，不扩大范围。
- Cookie、Authorization、浏览历史、完整签名 URL、token 和 key 不出可信内存边界；LAN 不暴露通用 extract/proxy 或无鉴权控制接口。
- Debug 入口、远程诊断、测试开关和敏感日志不得意外进入 Release 默认能力。
- 本机 IPC/loopback 明确 ACL、peer identity、短期 secret 与监听范围；外部客户端 handoff 需要用户确认、可信路径/签名与参数边界，不能经 shell 拼接。
- 依赖升级检查来源、许可证、维护状态、包体和跨平台影响。
- receipt、trace、checkpoint、Skill Store、route/audit 和 connector cache 全部最小化、有界、可撤销并按 Profile/provider/tenant 隔离。

### 3.11 硬编码、配置与数据模型

- 不散落超时、端口、重试次数、容量、协议字符串、错误码、编码参数、URL、UA 和平台路径等 magic value；可能独立变化的值使用命名常量、强类型配置、枚举或能力模型。
- 协议标准固定值集中在协议定义附近并注明语义；平台/设备差异用 capability，不堆型号判断。
- 不得在源码、日志、fixture 或文档示例中写真实凭证、Cookie、Authorization、私有签名 URL、本机绝对路径和生产秘密。
- 多个布尔参数、字符串状态或松散 Map 应改为能表达约束的类型；局部天然清晰的字面量不要求形式化提取。

### 3.12 清晰度、内聚与规模

- 命名表达领域含义，避免 `data`、`info`、`manager2`、`handleSomething`；函数处于一致抽象层次，一句话可描述职责。
- 注释解释约束、背景和“为什么”，不复述代码；删除不可达代码、废弃分支和临时调试逻辑。
- 同一业务规则、协议解析、错误转换或常量不重复实现；不通过无意义 wrapper 制造形式复用。
- 规模阈值只触发提醒，不是合并门槛：

| 对象 | 提醒级别 | Review 要求 |
|---|---:|---|
| 函数 | `100–199` 行 | 检查多职责、深层分支、状态阶段和可测试性 |
| 函数 | `>= 200` 行 | 强提醒；给出保持整体或拆分的明确理由 |
| 生产文件 | `2000–2999` 行 | 检查是否混入多个领域或平台职责 |
| 生产文件 | `>= 3000` 行 | 强提醒；建立模块化 Roadmap 或说明 vendor/生成例外 |
| 测试文件 | `>= 2000` 行 | 按 fixture、领域和场景审查拆分 |
| 测试文件 | `>= 3000` 行 | 不允许合并；生成数据除外且生成源可审查 |

自动生成代码、第三方 vendor 代码可不按阈值拆分，但必须审查生成源、版本来源和接入风险；历史大文件不因小改动被强制机械重构。

### 3.13 测试、构建与可验证性

- 验证层级、变更类型最小矩阵和证据字段以 [测试标准](testing-standard.md) 为唯一事实源；Review 不复制易变测试计数。
- 新功能有行为测试；Bug 修复先有能复现原问题的失败测试。
- 生产源码不包含测试实现、fixture、Mock/Fake、测试入口、故障注入或 `xxxForTest` API；Rust 生产文件只允许 `#[cfg(test)] mod xxx_tests;` 声明（`#[path]` 指向独立文件）；C++ 用独立 test target；`test-support` 不进入生产依赖图。
- 覆盖正常、失败、空输入、边界、重复调用、取消、超时、旧结果、恢复和资源释放；敌意输入任务补伪随机风暴/LCG 不变量。
- 不使用固定长 `sleep`、公共网络或第三方影视站作为成功条件；用确定性时钟、本地 fixture 和 mock upstream。
- Review 必须列出 commit/range、平台/架构、配置、实际 Test/Build/Lint/真机命令、退出码与 `PASS/FAIL/TIMEOUT/NOT_RUN`；组合命令逐项可审计，不能让后项成功掩盖前项失败。
- Fake/Mock、cross-compile、编译、短 smoke、Harness 与真机分别记录；平台/设备任务没有对应证据时必须限制 Roadmap 最高状态。
- 发布相关改动对真实交付目录运行 `repo-guard --artifact-path`，核对 Release 中的测试/调试入口、远程控制、secret、许可与固定 vendor 资产；只扫描源码树不够。
- 性能结论使用正确的端到端口径；内部阶段点不能替代用户可感知结果。

### 3.14 改动范围与文档同步

- 改动围绕一个清晰目标，不夹带无关重构、格式化、依赖升级或生成文件变化；大规模重构与行为变更分开。
- 同步更新公共 API、架构、协议、测试和必要文档；Roadmap 完成记录包含实现、验证、Review 与未覆盖。
- 生成文件与源定义一致；锁文件变化有明确原因；不保留临时开关、调试文件和本地环境产物。
- vendor/generated/submodule/lockfile 变化核对上游来源、固定版本/hash、许可、可复现生成、离线闭包和包体；不得审查不透明的大型生成 diff 而跳过生成源与 manifest。

## 4. 问题等级

| 等级 | 含义 | 处理要求 |
|---|---|---|
| `P0 阻断` | 严重安全/隐私事故、数据破坏、核心能力不可用、稳定崩溃/死锁、无法构建发布 | 必须修复并验证后才能合并 |
| `P1 必须修改` | 明确功能错误、架构边界破坏、兼容性回退、重要竞态/资源泄漏、显著性能问题 | 原则上必须在本次合并前修复 |
| `P2 应当修改` | 可维护性明显下降、重复规则、职责混乱、重要测试缺失或可预见中期风险 | 应修复；延期必须记录理由和后续任务 ID |
| `P3 可选建议` | 命名、表达、局部简化或非关键优化 | 不阻塞合并，由作者判断 |

定级示例：绕过 grant/确认、泄漏 secret、开放代理或 DRM 绕过为 P0；把未运行写成通过、Release 携带测试/远控入口、错误 capability 宣告、协议兼容回退通常为 P1；缺少与风险匹配但不影响当前正确性的专项测试通常为 P2。最终按可触发影响定级，不能仅凭关键词机械套用。

补充标记：

- `Question`：现有证据不足，需要作者解释设计、约束或行为。
- `Nit`：非常轻微且不阻塞的建议，不得反复拉扯。

每条 P0/P1/P2 发现必须包含：

1. 具体文件和尽可能精确的行号。
2. 可触发场景或调用链。
3. 实际影响，而不是抽象地说“可能有问题”。
4. 证据或推演过程。
5. 修复方向；不要求 Reviewer 代写完整实现。

## 5. Review 执行顺序

1. 固定被审 commit/range，检查分支、工作区、submodule/lockfile 与无关改动；确认任务 ID、Roadmap 原子范围和明确不在范围内的内容。
2. 阅读相关架构、协议、公共 API、实现和测试，不能只看 Diff 的孤立代码。
3. 先检查正确性、架构、公共契约、并发和安全/隐私，再检查性能、可维护性与规模。
4. 根据改动影响追踪调用方、被调用方、线程边界、资源所有权和跨平台路径。
5. 运行与风险相匹配的 Test、Build、Lint、Harness 或真机验证。
6. 先输出按严重度排序的发现，再输出验证证据和剩余风险。
7. 没有发现时也说明审查范围、实际验证和未覆盖项，不能只写“LGTM”。

## 6. 合并条件

满足以下条件才可以批准：

- 需求和边界明确，实现与权威文档一致。
- 没有未处理的 P0/P1。
- 架构职责、依赖方向、公共 API 和协议兼容性正确。
- 不存在已知死锁、稳定竞态、释放后访问或无界资源增长。
- 热路径没有不受控日志、trace、分配、阻塞或 IO。
- 页面/模型/工具内容不可信边界没有被突破；隐私红线（Cookie/Authorization/历史/正文）无泄漏路径。
- 必要的错误处理、资源释放和生命周期路径完整。
- Test、Build、Lint 和专项验证与风险匹配，结果有证据。
- 未覆盖项和剩余风险已明确且可以接受。

P2 可以在有明确理由和后续任务入口时延期；P3、Nit 和纯个人偏好不能阻塞合并。函数或文件触发规模提醒本身不构成 P0/P1，也不能单独作为拒绝合并的理由。

`APPROVE` 只表示当前 diff 满足合并门禁，不自动把任务提升为 `DONE`。Reviewer 必须另列“Roadmap 最高可达状态”：缺平台/设备/发布门禁时即使代码可合并，也只能是 `IMPLEMENTED` 或 `VERIFIED`。

## 7. Review 输出模板

```markdown
任务：<TASK-ID> <名称>

范围：<文件、行为、明确不做>
被审对象：<commit/range；分支；工作区/submodule 状态>

结论：APPROVE | CHANGES_REQUIRED | BLOCKED
- P0 / P1 / P2 / P3 数量：
- Roadmap 最高可达状态：IMPLEMENTED | VERIFIED | DONE

## 发现

### [P1] <准确标题>
- 位置：`path/to/file:line`
- 场景/调用链：
- 影响：
- 证据：
- 修复方向：

## 专项检查
- 架构与依赖：
- 主业务/辅助链与能力真实性：
- 并发、锁序与生命周期：
- 隐私/安全红线：
- 性能与热路径日志：
- vendor/generated/submodule/Release artifact：
- 规模提醒（函数/文件）：

## 验证
- 平台/OS/架构与配置：
- `<完整命令>`：exit <code>；<数量/耗时>；PASS/FAIL/TIMEOUT/NOT_RUN
- 真机或 Harness：<设备/runtime/步骤/结果，未运行写 NOT_RUN>
- Release artifact：<路径/大小/SHA-256/扫描结果，非发布任务写 N/A>

## 未覆盖与剩余风险
- <没有则写“无”>
```

无发现时“发现”节写“无”，但专项检查、验证与未覆盖三项必须逐条填写。
