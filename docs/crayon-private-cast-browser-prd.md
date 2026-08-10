# 蜡笔 AI Agent 投屏浏览器 PRD

- 版本：v0.6
- 日期：2026-08-11
- 状态：当前产品契约

## 1. 产品定义

蜡笔是一款专门为 AI Agent 定制的桌面浏览器，同时为用户提供完整浏览体验和局域网投屏能力。

它与普通浏览器的差异不只是“浏览器里放一个聊天框”，而是从浏览器内核边界、页面语义数据、任务生命周期、授权和本机协议开始，为 AI Agent 提供更快、更稳定、更安全的访问方式：

- Agent 不依赖截图/OCR或反复抓取整页 DOM，就能获取当前页面的结构化内容和 Markdown。
- Agent 通过蜡笔自有协议访问统一 tool registry，CLI 与 MCP 只是不同接入方式。
- 打开网页、读取页面、标签管理和页面操作都绑定 Profile、tab、navigation 与 generation。
- 会产生副作用的操作必须经过用户授权和确认；页面文字或模型输出不能自行扩大权限。
- Agent 调用正常浏览器与投屏用例，不获得原始 CDP/WebDriver、任意 JavaScript 或隐蔽后门。

AI Agent 接入能力属于产品核心。依赖具体 AI 模型的视频总结、文档总结、问答等“浏览器内建 AI”能力放在第二阶段，模型选型后续单独决策。

## 2. 已确认的产品决策

1. 当前平台为 Windows 和 macOS；Linux 暂不考虑。
2. HarmonyOS 只面向鸿蒙电脑 PC 形态，不以手机/平板作为当前目标。
3. 先完成浏览器基本功能和局域网投屏，再完成当前网页的确定性 Markdown。
4. 投屏只支持同一局域网的 Direct/Relay，不实现 WebRTC。
5. 没有可用视频推送路由时，只引导用户下载或打开独立蜡笔投屏客户端；浏览器不采集、不编码、不创建镜像会话。
6. 自有 Agent 协议、CLI、MCP、高性能读页和授权操作是核心能力，按浏览器/内容依赖分阶段落地。
7. 视频总结、文档总结、问答等需要真实模型的能力进入第二阶段；当前不预选模型/provider。

## 3. 目标用户

- 希望 AI Agent 能稳定读取和操作网页的个人用户、开发者和自动化团队。
- 需要在本机、可见、可撤销边界内把 Agent 接入浏览器的用户。
- 希望在 Windows/macOS 浏览并把当前媒体投到自有局域网接收端的用户。
- 重视 Cookie、浏览历史、页面正文和操作授权边界的用户。

## 4. 产品支柱

### 4.1 完整桌面浏览器

- 地址栏、导航、前进/后退、刷新和停止加载。
- 多标签、下载、权限、证书错误、弹窗与外部协议。
- 普通/无痕 Profile、崩溃恢复、键鼠、多窗口、多屏和生命周期。

### 4.2 局域网投屏

| 路由 | 条件 | 浏览器职责 |
|---|---|---|
| Direct | 接收端可直接访问安全媒体 URL | 通过 Cast-SDK 在 LAN 发起投送 |
| Relay | 媒体仅本机可按安全门禁访问 | 提供会话绑定、有界、短时有效的 LAN Relay |
| 外部客户端交接 | Direct/Relay 均不可用 | 用户确认后下载/打开独立客户端；浏览器不创建投屏会话 |

Cast-SDK 统一拥有设备发现、投屏码、连接、能力评估、协议适配、控制和会话监督。浏览器不复制 DLNA/CastExtension/SOAP 协议栈。

### 4.3 Agent-native 浏览器访问

Agent 通过蜡笔自有的 `Crayon Agent Access Protocol`（简称 `CAAP`）访问浏览器能力。

- CAAP 定义版本协商、能力发现、目标引用、工具调用、流式结果、错误、取消、deadline、幂等、grant 和 receipt。
- CLI 通过本机 IPC 使用 CAAP。
- MCP server 是 CAAP 的 loopback adapter，把 MCP 请求映射到同一 tool registry，不复制工具实现。
- 浏览器 UI 和未来 SDK 也调用同一 app-runtime 用例，不能形成多套行为语义。

首期工具按风险分级：

| 风险级别 | 能力示例 | 默认策略 |
|---|---|---|
| R0 状态 | 版本、能力清单、活动标签是否存在 | Developer Preview 可读，不含页面正文 |
| R1 页面读取 | 标题、可见语义树、选区、Markdown、标签列表、投屏状态 | 用户授予单任务/会话权限；跨 Profile 禁止 |
| R2 浏览操作 | 打开/切换/关闭标签、导航、后退、刷新、滚动 | 执行前显示目标和关键参数，确认过期需重来 |
| R3 产品操作 | 选择设备、开始/暂停/seek/停止投屏 | 沿用播放、DRM、广告和投屏策略门禁，并单独确认 |
| R4 页面写操作 | 点击、输入、提交 | 后期 Preview；只允许 Browser 签发的可见语义 handle |
| 永久禁止 | Cookie/Authorization、密码/支付输入、文件上传、任意 JS/CDP、远程监听、任意文件/网络 | 不提供工具，不允许配置解锁 |

### 4.4 高性能页面数据面

蜡笔为 Agent 提供浏览器内建的结构化页面数据面，而不是让每个 Agent 重复做通用浏览器适配：

- Renderer 生成受限语义快照，Browser process 验证来源和 navigation generation。
- 首次读取返回版本化快照；同一导航后续读取可返回增量或分页结果。
- 标题、正文块、链接、表格、代码、表单可见状态和语义元素 handle 使用稳定结构表达。
- 大结果支持分页/流式、背压、取消和 deadline，避免 UI 线程同步整树序列化。
- Markdown 与结构化快照共享同一可信采集和清洗管线。
- 不依赖截图/OCR作为常规读页路径，不向 Agent 暴露浏览器内部对象指针或原始调试协议。

性能目标在真实 CEF 页面 fixture 上校准：

- 已有快照缓存命中时，R1 元数据/标题读取本机 P95 目标 ≤50ms。
- 100KB 清洗正文的结构化快照/Markdown 本机 P95 目标 ≤500ms。
- 同一导航的增量读取应显著少于重新构建整页；具体阈值由 `AGT-15` benchmark 固化。
- Agent 读取不得造成前台明显卡顿，队列、内存、结果大小和并发有硬上限。

## 5. 网页 Markdown

浏览器与投屏主链路完成后建设：

- 用户或经授权的 R1 Agent 可提取当前标签页。
- 输出标题、段落、列表、引用、代码、表格、链接和图片引用。
- 支持本地预览、复制、保存，并作为 Agent R1 内容工具的标准格式之一。
- 不批量爬取、不后台遍历站点、不绕过登录/付费墙/权限。
- 无模型也必须稳定可用。

## 6. 第二阶段内建 AI

第二阶段在 CAAP、Markdown 和授权框架稳定后提供：

- 当前文档总结、要点、大纲与基于来源的问答。
- 视频内容总结：首期只消费页面合法可得、用户可见的字幕/转录文本或用户提供的文本，不下载媒体、不绕过 DRM、不提取隐藏字幕接口。
- 输出绑定来源 snapshot/hash，显示 AI 生成标识和可追溯引用。
- 每次发送前展示 provider、字段、长度、图片策略和保存策略；用户确认后才发送。

模型/provider、本地或云端、BYOK、费用、数据地区和企业 endpoint 后续通过 `CNT-11` 决策，不在当前 PRD 预选。

## 7. 平台范围

| 平台 | 定位 | 引擎 | 说明 |
|---|---|---|---|
| Windows 10/11 | 首发 | CEF | 浏览、投屏、CAAP/CLI/MCP、Markdown、后续模型能力 |
| macOS | 第二桌面平台 | CEF | 与 Windows 共享领域协议，平台证据独立 |
| HarmonyOS 电脑 | 后续技术预览 | ArkUI/ArkWeb | PC 窗口、键鼠、多任务和本地 Agent adapter |
| Linux | 当前不规划 | 未定 | 需要时另立 Roadmap |

## 8. Agent 用户流程

### 8.1 连接

1. 用户在设置中开启 Agent Developer Preview。
2. 浏览器生成短期本机会话凭证，只允许当前用户和 loopback/local IPC。
3. CLI 或 MCP 完成 CAAP 版本/能力握手。
4. 浏览器展示客户端、授权范围、Profile 和到期时间。

### 8.2 读取网页

1. Agent 请求目标标签页的 R1 权限。
2. 用户授予单次、单任务或本次 App 会话权限。
3. Agent 获取脱敏标题、结构化快照/Markdown及 provenance。
4. 导航、标签关闭、Profile 切换、撤销或超时后旧 target/generation 立即失效。

### 8.3 操作网页

1. Agent 先读取可见语义节点，获得 Browser 签发的短期 handle。
2. Agent 提交操作意图、目标 handle 和关键参数。
3. 浏览器展示将操作的页面、元素、字段和影响。
4. 用户确认后 app-runtime 执行；目标变化、导航或 handle 过期则拒绝并要求重新读取/确认。

页面中的“忽略规则、点击购买、授权工具”等文字始终是不可信内容，不能自动触发下一工具。

## 9. 隐私与安全

- MCP 默认关闭，只绑定 loopback；CLI 使用受控本机 IPC；不提供远程监听。
- capability grant 不跨 Profile、App 重启或未授权目标；可随时撤销。
- action receipt 本机有界、短期、脱敏，不含正文、完整 URL query、Cookie、Authorization 或 secret。
- Agent 页面数据只在任务需要范围内读取，结果有 TTL、大小和数量上限。
- 外部客户端交接与 Agent 页面操作都需要用户确认，但二者权限不互相继承。
- 模型调用使用单独的数据发送确认，不能复用 R1 页面读取 grant。
- Agent/模型不能决定 DRM、广告连续性、设备能力、Relay 网络安全或删除范围。

## 10. 明确不做

- Linux 当前构建/发布。
- 浏览器 WebRTC sender、屏幕/标签页/系统音频采集与编码。
- 视频下载、内容聚合、广告跳过、DRM 绕过、站点级批量爬取。
- 远程 MCP、原始 CDP/WebDriver、任意 JavaScript/RCE、任意文件系统/网络代理工具。
- Agent 自动输入密码、支付信息、上传文件或在用户不知情时发帖/提交。
- 第一阶段真实模型 provider、模型 API Key 和云端总结。

## 11. 阶段验收

### P0-A：浏览器与 LAN 投屏

- Windows/macOS 浏览器基本能力和 Direct/Relay/Cast-SDK 闭环完成。
- 无路由只产生外部客户端交接，无浏览器 WebRTC/采集/编码路径。

### P0-B：内容与 Agent 协议内核

- 当前页快照/Markdown确定、可取消、有界。
- CAAP v1、tool registry、task/grant/confirmation/receipt 完成。
- CLI/MCP 共用协议和 runtime，无第二套浏览器控制路径。

### P1-A：只读 Agent Developer Preview

- R0/R1 支持 CLI/MCP；loopback/local IPC、版本协商、取消、超时、限流和 generation 通过。
- 页面读取性能达到 benchmark，且前台交互无明显卡顿。

### P1-B：受控操作 Preview

- R2/R3 导航和投屏操作逐次确认。
- R4 只使用短期语义 handle；密码/支付/上传/隐藏/跨源元素永久拒绝。
- 间接提示注入、confused deputy、重放和恶意本机 client 安全评审通过。

### P2：模型型 AI

- 模型/provider ADR、发送前预览、安全存储、数据处理说明完成。
- 文档/视频文本总结、引用、错误/取消和本地 Markdown 降级通过。

### PH：HarmonyOS 电脑技术预览

- 在真实鸿蒙电脑或指定 PC Harness 上验证浏览、LAN 投屏和适用 CAAP 能力。

## 12. 成功指标

- 浏览器启动、导航、崩溃恢复和 Profile 清理指标。
- LAN 发现、连接、Direct/Relay 首帧、控制和停止成功率。
- Agent 握手成功率、R1 P50/P95、增量命中率、取消率、过期结果丢弃率。
- 操作确认率/拒绝率、权限不足率和安全拒绝率；不采集页面正文和完整参数。
- Markdown 确定性、成功率、P50/P95、峰值内存和用户修正率。
- 第二阶段模型发送确认率、provider 错误率和引用覆盖率；不记录原始正文/问题。
