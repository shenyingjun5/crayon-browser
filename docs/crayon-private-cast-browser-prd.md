# 蜡笔 AI Agent 投屏浏览器 PRD

- 版本：v0.7
- 日期：2026-08-11
- 状态：当前产品事实源
- 输入说明：`AI投屏浏览器_PRD更新稿_Agent-Native-Browser.md` 是本版的重要补充输入；与现有平台、安全或交付边界冲突时，以本 PRD、当前架构和专项安全契约为准。

## 1. 产品定义

蜡笔是一款专门为 AI Agent 定制、同时服务真人用户的桌面浏览器，并内建局域网投屏能力。它不是“在普通浏览器里放一个 AI 聊天框”，而是把浏览器重构为 Agent 可安全使用的任务执行环境：页面先被理解为稳定、紧凑、可引用的语义对象；操作通过受控动作而非原始选择器执行；重复任务可以在用户确认后沉淀为个人 Site Skill；遇到验证码、风控或身份确认时交还给用户；浏览器还可在统一能力中心中选择站点技能、合作方 API/MCP、网页自动化或人工接管。

核心飞轮是：

`理解页面 -> 执行并验证动作 -> 记录成功轨迹 -> 用户保存个人技能 -> 健康检查与受控修复 -> 更快完成下一次任务`

产品同时坚持三条边界：

- AI Agent 接入、CLI、MCP、页面数据面和授权操作是核心能力，不依赖模型才能成立。
- 视频/文档总结、问答等依赖具体模型的浏览器内建 AI 放在第二阶段，模型/provider 后续决策。
- Agent 只能调用与产品 UI 共用的正常用例，不获得原始 CDP/WebDriver、任意 JavaScript、Cookie、文件系统、通用网络代理或隐蔽后门。

## 2. 已确认的产品决策

1. 当前桌面平台为 Windows、macOS，浏览器内核使用 CEF；Linux 暂不考虑。
2. HarmonyOS 仅面向鸿蒙电脑 PC 形态，使用 ArkUI/ArkWeb，作为后续技术预览。
3. 首先完成桌面浏览器基本功能和局域网投屏闭环，然后交付当前页结构化数据与确定性 Markdown，再逐步开放 Agent 读写、工作流与生态能力。
4. 投屏只支持同一局域网的 Direct/Relay，不实现浏览器 WebRTC、屏幕/标签页/系统音频采集或编码。
5. 无 Direct/Relay 路由时，只引导用户下载或打开独立蜡笔投屏客户端；浏览器不创建镜像会话，Agent 也不能控制外部客户端的镜像权限。
6. CAAP 自有协议是统一 Agent 控制面；CLI 使用本机 IPC，入站 MCP 是 CAAP 的 loopback adapter。
7. 页面快照、语义地图、动作运行时、挑战接管、个人 Site Skill 与能力路由均不以模型为前提。
8. 真实模型总结与模型辅助理解/修复进入第二阶段；所有安全、权限、风险和路由门禁仍由确定性代码决定。

## 3. 用户与核心场景

### 3.1 真人用户

- 在 Windows/macOS 上完成日常浏览、多标签、下载、权限与 Profile 管理。
- 把用户主动播放的当前媒体投送到自有局域网接收端。
- 允许本机 Agent 读取当前页、完成可见任务，并随时查看、暂停、接管或撤销权限。
- 把一次成功且可复用的任务保存为个人 Site Skill，后续更快、更稳定地执行。

### 3.2 Agent 与开发者

- 通过 CLI 或 MCP 发现浏览器能力、打开网页、读取页面、获取 Markdown 和语义地图。
- 通过稳定 `action_id` 执行受控动作，并获得前置条件、效果验证、失败原因和恢复点。
- 在明确授权下调用投屏、站点技能或合作方能力，而不适配 CEF/ArkWeb/接收端细节。
- 遇到挑战页、高风险步骤或能力缺失时获得结构化的暂停、人工接管或拒绝结果。

## 4. 产品能力层

### 4.1 完整桌面浏览器

- 地址栏、导航、前进/后退、刷新、停止、多标签与多窗口。
- 下载、权限、证书错误、弹窗、外部协议、崩溃恢复与普通/无痕 Profile。
- Windows/macOS 键鼠、多屏、休眠唤醒和退出生命周期。

### 4.2 局域网投屏

| 路由 | 条件 | 浏览器职责 |
|---|---|---|
| Direct | 接收端可直接访问安全媒体 URL | 通过 Cast-SDK 在 LAN 发起投送 |
| Relay | 媒体仅本机可按安全门禁访问 | 提供设备/会话/资源绑定、有界、短时有效的 LAN Relay |
| ExternalClientHandoff | Direct/Relay 均不可用 | 经用户确认下载/打开独立客户端；浏览器不创建投屏会话 |
| Reject | DRM、广告连续性、能力或安全门禁不满足 | 明确拒绝并给出可操作原因 |

设备发现、投屏码、连接、能力评估、DLNA/CastExtension、播放控制和会话监督由固定版本 Cast-SDK 拥有。合作方/TV Cast Manifest 如需新协议、签名、字幕、队列或结果回报，必须先在 Cast-SDK/接收端建立独立 Roadmap 并发布受审 facade；浏览器只做缺口分析和消费已批准接口。

### 4.3 页面理解层

浏览器从经过 Browser process 验证的当前页事实生成：

- `PageSnapshot`：标题、正文块、链接、表格、代码、可见结构、provenance 与截断信息。
- `Markdown`：与快照共享清洗管线的确定性文本格式。
- `ActionMap`：当前可见、可操作对象及短期稳定 `action_id`、允许动作和风险提示。
- `FormMap`：字段语义、必填/格式/错误状态；不包含密码、支付、文件内容或隐藏值。
- `MediaMap`：页面可见媒体事实与产品可用投屏状态，不泄漏认证信息。
- `RiskMap`：挑战页、敏感字段、跨源、下载、外部协议及需人工接管的确定性标记。
- `ChangeSet`：同一导航 generation 内有界的语义增量，避免反复生成整页。

对外提供 `compact` 和 `standard` 两级；`full` 仅用于有界内部诊断、验证与受控修复，不能返回原始 DOM、HTML、CDP 或对象指针。截图/视觉分析只作为内部最后手段和人工辅助，不是常规读页路径。

### 4.4 可验证动作层

- Agent 使用 Browser 签发的 `action_id`/语义 handle，不提交长期 CSS/XPath 选择器。
- 浏览器内部可组合角色、名称、可见文本、结构邻近、可操作性与几何等多信号重新定位；页面变化后必须重新验证。
- 每次动作执行前检查 target、navigation、generation、可见性、可操作性、风险和用户确认。
- 执行后验证声明的效果，例如页面变化、字段状态、导航或投屏状态；不能把“点击已发出”当作成功。
- 失败返回稳定原因、是否可安全重试、是否需要重新读取或人工接管。幂等键防止重复提交副作用。

### 4.5 Workflow Learning 与个人 Site Skill

浏览器可以记录已授权任务的最小执行轨迹，但只有满足以下条件才可形成个人技能：

1. 任务已由动作效果或用户确认验证成功。
2. 浏览器生成参数化候选 Recipe，并移除敏感值、secret、正文与账户标识。
3. 用户预览名称、适用站点、步骤、参数、权限和风险后主动保存。
4. 技能在本地 fixture/沙箱中验证，通过后才进入个人 Skill Registry。
5. 每次运行仍经过当前页面、权限、风险和确认检查；技能不会继承旧 grant。

个人 Site Skill 具备来源、版本、健康度、适用范围、最近验证结果和回滚记录。站点漂移后可以在低风险、证据充分的范围内提出或执行受控修复；高风险动作、身份/支付/提交、跨源变化或低置信度结果必须停止并要求用户重新确认，禁止静默改目标。

### 4.6 Challenge-aware Agent

验证码、滑块、登录确认、异常风控、设备验证等挑战只能被检测，不能由浏览器或 Agent 代解、绕过或规避：

- 任务进入 `AwaitingHuman`，停止自动操作并高亮需要用户完成的页面区域。
- 保存不含 secret 的最小 checkpoint：任务、目标、已验证步骤、待完成意图和到期时间。
- 用户完成后，浏览器重新读取页面、重新检查权限/风险/前置条件，再从安全步骤恢复。
- 挑战变化、导航漂移、超时或副作用不确定时不自动重放。

### 4.7 Capability Hub

统一 Capability Registry 描述内建工具、个人 Site Skill、合作方 API/MCP 连接器和人工接管能力。路由器基于任务、站点、信任、健康、用户偏好、权限、成本和确定性策略选择路径，并返回可解释的 `route_reason`。

默认优先级为：`已批准合作方 API/MCP -> 健康的 Site Skill -> 受控网页自动化 -> Human Handoff -> Reject`。这不是绝对覆盖规则：用户偏好、能力缺失、安全风险、数据发送范围或健康状态可改变选择；每次 fallback 必须重新执行授权、风险、幂等与确认检查，不能沿用上一条路径的权限。

必须区分两类 MCP：

- 入站 MCP：外部 Agent 通过 loopback 调用蜡笔能力，是 CAAP adapter。
- 出站 Partner MCP/API：蜡笔作为客户端调用合作方能力，拥有独立 connector、OAuth/scope、token vault、网络策略、审计和熔断边界。

连接器必须支持来源信任、版本/签名、撤销与 kill switch，防止 SSRF、重定向逃逸、工具描述注入、超大响应、scope 扩张和跨租户数据混淆。

## 5. CAAP、CLI 与入站 MCP

`Crayon Agent Access Protocol (CAAP)` 定义版本协商、能力发现、目标引用、调用、流式结果、取消、deadline、grant、confirmation、幂等与 receipt。CLI 和 MCP 共用同一 registry、guard 与 app-runtime，不形成第二套控制路径。

| 风险 | 示例 | 默认策略 |
|---|---|---|
| R0 | 版本、能力、任务状态 | Developer Preview 可读，不含页面正文 |
| R1 | 快照、Markdown、语义地图、标签/投屏状态 | 单次/单任务/本次 App 会话授权，跨 Profile 禁止 |
| R2 | 打开/切换/关闭标签、导航、滚动 | 显示目标和关键参数；变化后重确认 |
| R3 | 投屏开始/控制/停止、外部数据发送 | 独立确认并沿用领域门禁 |
| R4 | 点击、输入、提交 | 后期 Preview；短期 action_id、前置检查与效果验证 |
| 永久禁止 | Cookie/Authorization、密码/支付、通用文件上传、任意 JS/CDP、远程监听、任意文件/网络 | 不提供能力，不允许配置解锁 |

通用文件上传当前不提供。未来如需受限上传，必须另立 Roadmap，采用用户逐次选择、用途/站点/文件绑定的短期 grant，不得把任意路径或长期文件权限暴露给 Agent。

## 6. 第二阶段内建 AI

- 当前文档总结、要点、大纲与基于来源的问答。
- 视频总结首期只消费页面合法可得且用户可见的字幕/转录或用户提供文本；不下载媒体、不绕过 DRM、不探测隐藏字幕接口。
- 可研究模型辅助的页面理解或修复建议，但模型结果保持不可信，不能决定权限、风险、路由或直接修改高风险技能。
- 每次外发前展示 provider、字段、长度、图片策略和保存策略；输出绑定 snapshot/hash 与引用。

模型/provider、本地或云端、BYOK、费用、数据地区和企业 endpoint 由第二阶段 ADR 决定；第一阶段不预选。

## 7. 平台范围

| 平台 | 定位 | 引擎 | 当前范围 |
|---|---|---|---|
| Windows 10/11 | 首发 | CEF | 浏览、LAN 投屏、页面数据、CAAP/CLI/MCP、后续工作流/Hub |
| macOS | 第二桌面平台 | CEF | 共享领域协议，平台验证独立 |
| HarmonyOS 电脑 | 后续技术预览 | ArkUI/ArkWeb | PC 窗口、键鼠、多任务和适用的本地 Agent adapter |
| Linux | 当前不规划 | 未定 | 需要时另立 Roadmap |

## 8. 隐私、安全与合规边界

- 入站 MCP 默认关闭且只绑定 loopback；CLI 使用当前用户可访问的本机 IPC；不提供 LAN/WAN Agent 监听。
- grant 不跨 Profile、App 重启、未授权目标或 connector；导航、撤销、到期和页面变化使旧引用失效。
- 页面、模型、合作方工具描述和返回内容均为不可信数据，不能生成授权或自动串联高风险调用。
- receipt、trace、checkpoint、skill 和审计日志本机有界、最小化、脱敏，不保存 Cookie、Authorization、密码、支付、完整敏感 query 或正文副本。
- 高风险动作、文件、外部数据发送、OAuth scope、Connector 和 Cast 能力不能互相继承权限。
- DRM、广告连续性、Relay 网络安全、设备能力、挑战检测和 skill 修复均由确定性策略门禁。

## 9. 明确不做

- Linux 当前构建/发布；移动形态 HarmonyOS。
- 浏览器 WebRTC sender、屏幕/标签页/系统音频采集与编码。
- 视频下载、内容聚合、广告跳过、DRM 绕过、批量账号、站点级爬取和反检测。
- 远程 MCP、原始 CDP/WebDriver、任意 JavaScript/RCE、通用文件系统或网络代理。
- 自动解决/绕过验证码和风控；Agent 自动输入密码、支付信息或静默提交高风险操作。
- 未经用户预览保存工作流、静默自我修改高风险技能、从失败/未验证任务学习。
- 浏览器自行定义合作方/TV 投屏协议，或复制 Cast-SDK 协议栈。

## 10. 分阶段验收

### P0-A：浏览器与 LAN 投屏

- Windows/macOS 浏览器基本能力和 Direct/Relay/Cast-SDK 闭环完成。
- 无路由只产生 ExternalClientHandoff，无浏览器 WebRTC/采集/编码路径。

### P0-B：页面数据与 Agent 协议内核

- 当前页快照/Markdown 有界、确定、可取消；CAAP v1、registry、task/grant/confirmation/receipt 完成。
- CLI/入站 MCP 共用协议和 runtime，无第二套浏览器控制路径。

### P1-A：语义理解与只读 Agent Preview

- Page/Action/Form/Media/Risk Map 和 ChangeSet 达到正确性、大小与性能门禁。
- R0/R1、generation、取消、超时、限流、provenance 和 prompt-injection 隔离通过。

### P1-B：受控操作 Preview

- R2/R3/R4 使用短期 action_id、前置条件、逐次确认、幂等与效果验证。
- 密码、支付、文件、隐藏/跨源元素永久拒绝；高风险目标变化不静默重试。

### P1-C：Workflow 与 Challenge Preview

- 挑战检测、暂停、人工接管、checkpoint 与安全恢复通过。
- 仅从已验证成功任务生成候选；用户预览保存；个人技能隔离、验证、版本、健康和回滚通过。
- 受控自修复只覆盖低风险变化，高风险与低置信度 fail closed。

### P1-D：Capability Hub 与合作方 Preview

- 本地 Registry、路由理由、Site Skill、fallback 重授权和入站 MCP 能力发现通过。
- 合作方 connector 的信任、签名、OAuth/scope、token、SSRF、限流、熔断、审计和 kill switch 通过后才可开放。
- Partner Cast Manifest 只有在外部 Cast-SDK/接收端正式发布受审 facade 后集成。

### P2：模型型 AI

- provider ADR、发送预览、安全存储、文档/视频文本总结、引用、取消与本地 Markdown 降级通过。

### PH：HarmonyOS 电脑技术预览

- 在真实鸿蒙电脑或指定 PC Harness 验证浏览、LAN 投屏和适用 CAAP 能力。

P1-C、P1-D 和 P2 属于独立功能门禁，不阻塞已满足范围的浏览器/LAN 投屏核心版本发布。

## 11. 成功指标

- 浏览器启动、导航、崩溃恢复、Profile 清理和前台交互延迟。
- LAN 发现、连接、Direct/Relay 首帧、控制、停止和 ExternalClientHandoff 转化。
- Agent 握手成功率、快照/Markdown/语义地图 P50/P95、增量命中率、取消率和旧结果丢弃率。
- action 前置失败率、效果验证成功率、重复副作用拦截率、人工接管成功率。
- 重复任务用时/调用数/传输字节下降比例，个人 Skill 成功率、健康度、回滚率和自修复误匹配率。
- Hub 路由命中率、route_reason 覆盖率、fallback 率、connector 健康/熔断/权限拒绝率。
- 第二阶段模型发送确认率、provider 错误率和引用覆盖率；所有遥测不记录原始正文、凭证或完整参数。
