<!-- rulepack-simple type=development -->

# 蜡笔 AI Agent 投屏浏览器 Agent 规则

本文件是仓库级常驻规则。任何 Agent 在修改代码、测试、构建、依赖或 Roadmap 前必须完整读取本文件；更深目录存在 `AGENTS.md` 时，仅补充该目录的专有规则，不得放松本文件约束。

## 1. 项目定位

- 产品：面向 AI Agent 定制的“蜡笔 AI Agent 投屏浏览器”。当前桌面范围为 Windows、macOS CEF；HarmonyOS 面向鸿蒙电脑 PC 形态，使用 ArkUI/ArkWeb；Linux 暂不进入当前产品和开发范围。
- 核心定位：除用户直接浏览与局域网投屏外，浏览器必须通过自有版本化 Agent 协议为 CLI/MCP 提供高性能页面读取和经用户授权的受控操作；MCP 是协议 adapter，不是第二套业务实现。成功任务可在用户确认后沉淀为版本化 Workflow/个人 Site Skill，并通过受控漂移检测与低风险自愈提高复用效率。
- 交付顺序：先完成浏览器基础功能与局域网投屏闭环；随后完成确定性页面快照/Markdown、Agent 协议和 CLI/MCP 只读能力；再完成语义 Action/Form/Media/Risk/Change Map 与经确认的网页操作；Workflow/Challenge Handoff/个人技能、自愈、Capability Hub/Partner Connector 按独立后续 Roadmap 分波次；依赖模型的视频/文档总结等内建 AI 能力放在第二阶段。
- 投屏边界：浏览器只支持局域网内的 Direct/Relay 媒体投送，不实现 WebRTC、标签页采集、系统音频采集、编码或镜像传输。没有可投视频时只引导下载/打开独立的蜡笔投屏客户端。
- 共享能力：媒体候选、策略、relay、隐私契约、确定性页面快照/Markdown、语义 Map、Agent tool registry/capability/receipt、Workflow/Skill 契约、Capability Registry/Router、自有协议适配和 UI 状态机。
- Cast-SDK 边界：设备发现、投屏码、设备连接、能力评估、DLNA/CastExtension、播放控制和会话监督复用 Cast-SDK；浏览器项目不得复制协议栈。
- 非目标：Linux 当前适配、浏览器内建 WebRTC 镜像、视频下载、内容聚合、站点级批量抓取、广告跳过、DRM 绕过、批量账号、代理池、反检测指纹和云端媒体代理。第一阶段不接真实模型 provider，但 Agent 协议、CLI/MCP 和授权控制面属于核心范围。

## 2. 事实来源与阅读顺序

开发前按顺序读取：

1. 本 `AGENTS.md`。
2. `docs/current/README.md` 指向的当前 PRD、架构、测试和 Code Review 契约。
3. `docs/plans/README.md` 和当前任务所属模块 Roadmap。
4. 任务涉及的生产代码、现有测试和相邻调用方。
5. 需要历史背景时才读归档文档；归档不得覆盖 current 契约。

权威顺序：当前 PRD/架构/协议与安全契约 > 当前模块 Roadmap > 代码与测试确认的现状 > 历史文档。Roadmap 描述不等于代码已实现，必须读取真实代码和 Git 状态。

## 3. Roadmap 驱动执行

- 实质性开发只能从 `docs/plans/README.md` 中的活跃 Roadmap 领取一个原子任务。
- 开工前确认任务 ID、依赖、范围、目标文件、验收、测试命令和明确不做项均完整；有占位或冲突时先修订 Roadmap。
- 同一 Agent 一次只把一个原子任务置为 `IN_PROGRESS`。任务通常应在一个可审查提交内完成，并能够独立回退。
- 任务状态统一为：`TODO`、`READY`、`IN_PROGRESS`、`BLOCKED`、`IMPLEMENTED`、`VERIFIED`、`DONE`。
- `IMPLEMENTED` 只表示代码完成；`VERIFIED` 表示规定的自动化验证有证据；涉及平台/设备门禁的任务只有真机或指定 Harness 完成后才能 `DONE`。
- 完成任务时在 Roadmap 记录实际命令、结果、未覆盖项和剩余风险，不得写“应该通过”。
- 依赖未完成不得偷跑后续任务；可以先补特征测试、接口草案或文档，但不能伪造前置能力。
- 任务超出原范围、需要新协议/状态机/持久化 schema/平台能力或明显扩大数据处理时，先更新 Roadmap 并重新评审。

### 3.1 原子任务标准

一个原子任务必须同时满足：

- 只有一个可描述的交付目标和一个主要变化原因。
- 明确输入、输出、依赖、允许修改路径和禁止修改路径。
- 正常、错误、边界、取消/超时和资源释放至少有适用的验收项。
- 测试可以单独运行，失败能定位到该任务。
- Diff 足够小，Reviewer 能完整推演；行为变化与大规模纯移动不得混在同一任务。

出现以下任一情况必须建立独立模块 Roadmap，不得塞进现有任务：

- 新增跨模块协议、公共 API、状态机、持久化格式或安全边界。
- 同时改变两个以上稳定领域的所有权或依赖方向。
- 预计超过 2 个工程日、修改超过约 10 个生产文件或净新增约 1000 行生产代码。
- 需要独立性能、兼容、压力、长稳、安全或多平台真机矩阵。
- 触发 3000 行文件强提醒且正确拆分需要多阶段迁移。

## 4. 架构与模块化规则

- 依赖方向固定为：产品 UI/应用编排 -> 领域接口 -> 共享 Core/Cast-SDK facade -> 平台适配；禁止反向依赖。
- CEF、ArkWeb、Windows/macOS/HarmonyOS 系统 API 只能出现在对应 adapter/shell 模块。
- 共享策略不得出现散落的 `if windows/macos/harmony` 或设备型号判断；使用 `PlatformCapabilities`/receiver capability。
- Cast-SDK 通过固定 git revision 的源码 submodule 和 `crayon-cast-adapter` 接入；只有 adapter 可以依赖 SDK 公开 facade。App 不得拼 SOAP、DLNA metadata、CastExtension 或接收端控制 URL。
- 浏览器业务不得进入 Cast-SDK；若发现 SDK 公共能力缺口，在 Cast-SDK 建独立 Roadmap/API 变更，浏览器侧不得复制临时协议实现。
- `lib.rs`、`main.rs` 只负责装配、re-export 和生命周期入口，不放大段业务实现、注入脚本或测试正文。
- 禁止创建无边界的 `utils.rs`、`common.rs`、`manager.rs`、`misc.rs`；共享代码必须属于稳定领域概念。
- 状态必须有唯一所有者。一个模块不得直接修改另一个模块的内部集合、锁或缓存。
- 辅助日志、诊断、遥测不得参与主业务正确性；生产者非阻塞，队列/缓存/重试必须有界。
- Agent 访问必须统一经过版本化 tool registry、capability guard 和 app-runtime 正常用例；CLI/MCP 不得直接调用 CEF、ArkWeb、CDP、Cast-SDK、Relay 或平台 API。
- 自有 Agent 协议的逻辑 schema 与 transport 分离；CLI 使用本机 IPC，MCP 只做 loopback adapter，两者共享握手、工具、错误、取消、幂等、generation 与审计语义。
- 入站 MCP（外部 Agent 调用蜡笔）与出站 Partner MCP/API（蜡笔调用合作方）必须使用不同 transport、凭证、命名空间、授权和审计 owner；Partner Connector 不能反向调用浏览器内部工具或复用 Agent grant。
- Workflow/Skill 只从已验证成功的任务生成候选，必须由用户预览确认后保存；Recipe 不含密码、验证码、Cookie、Authorization、Token、支付信息或原始文件路径，版本变化不可静默覆盖。

## 5. 硬编码与配置

- 禁止散落端口、超时、重试次数、容量、协议字符串、错误码、编码参数、URL、UA 和平台路径等 magic value。
- 可能独立变化的值使用命名常量、强类型配置、枚举、能力模型或签名数据规则；标准固定值集中在协议定义附近并注明语义。
- 用户文案进入本地化资源，不在 Rust/C++/ArkTS/TypeScript 业务代码中硬编码。
- 不得在源码、日志、fixture 或文档示例中写真实凭证、Cookie、Authorization、私有签名 URL、本机绝对路径和生产服务秘密。
- 站点 adapter 必须注册化、版本化、可关闭并有独立测试；禁止继续扩张中心化站点 `match` 或把第三方内容源目录带入产品。
- 固定的测试 loopback 地址、协议规范常量和天然清晰的局部字面量可以保留，但要限定在测试或所属协议模块。

## 6. 文件和函数规模

规模是 Review 提醒，不是机械拆分目标：

| 对象 | 提醒 | 要求 |
|---|---:|---|
| 函数 | 100～199 行 | 检查多职责、深层分支、状态阶段和可测试性 |
| 函数 | >= 200 行 | 强提醒；必须拆分或在 Review 中说明保持整体的理由 |
| 生产文件 | 2000～2999 行 | 检查是否混入多个领域或平台职责 |
| 生产文件 | >= 3000 行 | 强提醒；必须建立模块化 Roadmap 或说明生成/vendor 例外 |
| 测试文件 | >= 2000 行 | 按 fixture、领域和场景审查拆分 |
| 测试文件 | >= 3000 行 | 不允许合并；生成数据除外，但生成源必须可审查 |

不得为了行数制造大量只转发一行的 wrapper。拆分依据是状态所有权、变化原因、依赖方向和测试边界。

## 7. 生产与测试代码隔离

- 生产源码不得包含测试实现、fixture、Mock/Fake、测试入口、故障注入分支或 `xxxForTest` API。
- Rust 生产文件只允许 `#[cfg(test)] mod xxx_tests;` 声明，测试正文放独立 `xxx_tests.rs`；优先使用 crate `tests/` 做公共行为测试。
- C++ 使用独立 test target；TypeScript 使用 `*.test.*`/`tests/`；ArkTS 使用 `ohosTest`；测试依赖不得进入生产构建图。
- 可复用 fixture 放 `test-support`，生产模块不得依赖它。
- Debug 诊断工具使用独立 debug target/module；Release 不得包含测试脚本、测试资源、内部远程控制和调试依赖。
- 不得为了测试扩大公共 API；使用同模块测试、正常依赖注入或集成测试。

## 8. 产品、安全与隐私红线

- 只有 Browser process 验证可信用户输入和真实播放推进后才能启用投屏；页面消息本身不可信。
- 禁止自动点击播放、广告或跳过按钮，禁止修改广告 `currentTime`、速率、可见性，禁止按广告域名过滤媒体候选。
- DRM/EME/私有加扰只允许识别和拒绝直投；不得提取 key/license、注入 CDM 绕过或规避 protected surface。
- Cookie、Authorization 和浏览历史不得进入接收端命令、媒体 URL、云端、日志或持久化诊断。
- 页面正文、DOM 和无障碍树都不可信；不得据此改变 DRM、广告、投屏授权或网络安全结论。
- 第一阶段 Release 不得包含真实模型 provider 或 API Key。Agent/CLI/MCP 按独立 Roadmap启用，但不得暴露原始 CDP/WebDriver、任意 JavaScript、Cookie/Authorization、密码/支付、文件上传、任意文件系统、远程监听或通用网络代理能力。
- 页面正文、无障碍树、模型输出和 MCP/CLI 输入都不可信，不能生成或扩大 grant、改变目标、跳过确认或触发额外工具；写操作必须使用 Browser 签发的短期语义 handle 并受 tab/navigation/generation 约束。
- Challenge Detector 只允许识别、暂停、人工接管、安全复检和短期断点续跑；禁止求解验证码、自动滑块/点选、读取短信/邮箱验证码、打码平台、反检测指纹或规避风控。
- 高风险动作不得自动改变定位目标、自动修订 Skill 或因 Partner API 失败静默降级到网页执行；任何 route/fallback 都重新校验语义、scope、风险、数据流、幂等和确认。
- 通用文件上传仍不可表达；未来若需要素材附件，必须新建 scoped file grant Roadmap，只允许用户明确选择的短期文件引用，不开放任意路径。
- 浏览器不得包含 WebRTC sender、标签页/窗口采集、系统音频采集或硬件编码投屏实现。镜像入口只允许调用受控的外部客户端 handoff；安装/打开必须由用户明确确认。
- LAN 不得暴露通用 `/api/extract`、任意 URL proxy 或无鉴权控制接口。媒体路由必须是高熵 session/resource ID，并绑定设备、route、TTL 和上游 allow-set。
- 所有 URL、重定向、DNS、文件路径、消息长度和数量需要边界验证；必须覆盖 SSRF、DNS rebinding、开放代理、重放和路径穿越。
- P0 设备发现、投屏码、连接和控制均使用 Cast-SDK 现有局域网能力；产品云端不承载媒体或设备控制。
- Partner/TV Cast Manifest、广告/正片/下一集、字幕和播放回传属于 Cast-SDK/接收端公共协议变更；浏览器只做 gap analysis 并消费获批 facade，不得先行定义或拼装接收端命令。
- 无痕清理失败必须显式报告，不得把 best-effort 宣称为已清除。

## 9. 并发、生命周期与性能

- 不在持锁期间执行网络/文件 IO、外部回调、IPC、等待、线程 join 或不可控平台调用；不在锁内 `await`。
- 多锁必须记录全局锁序；检查 stop/release 与 callback/timer/worker 的反向调用和旧会话污染。
- 队列、缓存、连接、并发请求、重试和日志均必须有界，定义满载行为和 dropped 计数。
- 逐分片、socket、渲染和 Agent 页面快照/增量热路径禁止默认高频日志、重复整树序列化、字符串格式化、同步 IO 和不必要复制；协议必须支持分页/流式、取消、deadline 和背压。
- start/stop、导航、设备切换、网络切换、休眠唤醒和 App 退出必须幂等并逆序释放资源。

## 10. 开发验证

- 新功能必须有行为测试；Bug 修复先增加能复现问题的失败测试。
- 每个任务执行适用的 Format、Lint、Unit、Integration、Build；平台、设备、性能、安全任务追加对应 Harness/真机验证。
- 测试覆盖正常、失败、空输入、边界、重复调用、取消、超时、旧结果、恢复和资源释放。
- 不使用固定长 `sleep`、公共网络或第三方影视站作为自动化成功条件；使用确定性时钟、本地 fixture 和 mock upstream。
- 现有核心基线：`cargo test -p get-video --lib` 为 53 项通过；全 workspace 命令曾在 120 秒超时，不能视为失败或通过。首批工程任务会建立分层入口。
- 没有实际运行的命令必须标记“未运行”；环境阻塞必须保留原始错误和影响判断。

## 11. Code Review 与完成门禁

- 每个原子任务实现并验证后必须按 `docs/current/code-review-standard.md` 做独立 Review。
- Review 顺序：需求/边界 -> 正确性 -> 架构/API -> 并发/生命周期 -> 安全/隐私 -> 性能 -> 测试 -> 可维护性。
- P0/P1 未关闭不得合并；P2 延期必须记录理由和后续任务 ID；没有发现也要记录审查范围、验证和未覆盖项。
- 完成不以“已修改”为准，必须同时具备：实现、规定测试证据、文档同步、Review 结论和可接受的剩余风险。
- 不得把工作区中无关用户改动一起格式化、回滚、提交或覆盖。

## 12. 权限与外部动作

- 范围内本地读写、构建和测试可以直接执行。
- 新增/升级依赖先检查来源、许可证、维护状态、包体和跨平台影响。
- 发布、推送、打 Tag、部署、上传、应用市场提交、使用凭证和修改 Cast-SDK 外部仓库必须获得明确授权。
- 默认使用中文汇报；代码、命令、API、路径和原始错误保持原文。

## 13. Agent 交付模板

```markdown
任务：<TASK-ID> <名称>
状态：IMPLEMENTED | VERIFIED | DONE | BLOCKED

改动：
- <文件与行为>

验证：
- `<实际命令>`：通过/失败/超时
- <真机/Harness 证据>

Code Review：
- P0/P1/P2：<数量与结论>

未覆盖与风险：
- <明确项；没有则写“无”>

Roadmap 更新：
- <状态、证据和下一任务>
```
