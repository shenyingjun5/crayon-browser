# 蜡笔 AI Agent 投屏浏览器技术方案

- 版本：v0.6
- 日期：2026-08-11
- 权威边界：`docs/current/architecture.md`

## 1. 技术目标

在 Windows/macOS CEF 和后续 HarmonyOS 电脑 ArkWeb 上建立一个 Agent-native 浏览器：人可以正常浏览和局域网投屏，AI Agent 可以通过自有 `CAAP` 协议、CLI 或 MCP 高性能读取页面，并在用户授权下执行受控操作。模型型总结能力在第二阶段接入。

## 2. 分层

```text
CLI / MCP adapter / Product UI
            |
CAAP protocol + agent-gateway
            |
task / grant / confirmation / receipt
            |
app-runtime normal use cases
      /          |           \
browser      content       cast
engine        page data    SDK/relay
      \          |           /
Windows/macOS/Harmony platform adapters
```

核心规则：任何入口都不能绕过 app-runtime；MCP 不复制工具；CAAP 不暴露 CEF/CDP 类型；模型不能调用 guard 扩权。

## 3. CAAP v1 设计

### 3.1 Envelope

逻辑 envelope 至少包含：

```text
protocol_version
message_id
client_session_id
message_kind
target_ref?
task_id?
deadline_ms?
idempotency_key?
payload
```

- `message_id/task_id/session_id` 为 opaque 高熵或强类型 ID。
- 长度、递归深度、数组数量、字符串、chunk 数和总结果都有硬限制。
- 高风险消息拒绝未知字段；错误使用稳定 code + 可本地化参数，不回显敏感 payload。
- v1 冻结前通过 benchmark 决定 length-prefixed Protobuf/CBOR 或严格 JSON；逻辑 schema、golden 和 adapter 行为不依赖编码选择。

### 3.2 Handshake

1. client 连接本机 transport。
2. 发送支持的 CAAP 版本、client 类型、feature 与最大 chunk。
3. browser 验证 OS 用户/短期 secret，返回选定版本、工具摘要、会话 TTL 和限制。
4. 任何不兼容、过期、重放、远程来源或错误 Profile 都在创建业务 task 前拒绝。

### 3.3 Tool registry

每个工具声明：稳定 ID/version、risk R0～R4、input/output schema、target 类型、是否确认、是否流式、资源预算、所调用的 app-runtime use case。

registry 是唯一事实来源：CLI help、MCP `tools/list`、确认 UI 和 Release surface test 都从同一声明生成或校验。

### 3.4 Target 与 handle

- `TargetRef` 绑定 Profile/tab/navigation/generation。
- 页面交互节点使用 Browser 签发的 `SemanticNodeHandle`，不接受 CSS/XPath/JS selector 直通。
- handle 只描述可见语义角色、可允许动作和短 TTL，不包含 DOM 指针。
- 页面变化、frame/origin 变化或 target 变化后 handle 失效。

## 4. 页面数据面实现

### 4.1 采集

- Renderer collector 从 DOM 与适用 accessibility facts 生成最小结构块。
- 过滤 script/style、隐藏敏感表单值、密码、跨源 iframe 正文和危险 URL。
- 消息按 frame/navigation/generation 分块，Browser gateway 验证后合并。
- collector 不执行页面提供的命令，不自动滚动加载，不点击或修改页面。

### 4.2 缓存与索引

- Browser owner 为每个当前 navigation 维护一个有界 snapshot generation。
- 缓存结构块、正文索引、链接/表格/代码索引和可见交互节点索引。
- DOM/布局变化使用 dirty region 或 revision 标记；不保证每次 mutation 即时全量重建。
- 多个 Agent R1 工具和 Markdown 共用同一 verified snapshot。
- navigation、tab close、Profile destroy、内存压力和 TTL 触发清除。

### 4.3 输出

- 小结果一次返回，大结果通过 `Chunk(sequence,cursor,data)` 流式。
- consumer 必须 ack/拉取下一页；服务端每 task 最多保留固定未确认 chunk。
- cancel/deadline 传播到采集、清洗和 transport；迟到 chunk 丢弃。
- provenance 标识 top frame/同源 frame、可见/截断、采集 revision 和 snapshot hash。

### 4.4 性能验证

基准比较：

1. CAAP 结构化读取。
2. 同一页面重新生成完整 snapshot。
3. 通用截图/OCR或外部自动化基线（仅 benchmark，不进入产品依赖）。

指标包括 handshake、first chunk、complete、CPU、峰值内存、UI event-loop delay、序列化字节和增量复用率。宣称“快于一般浏览器”前必须保留可重复 fixture、硬件和对照条件；产品初期只承诺内部预算，不作无证据营销比较。

## 5. 授权与确认

### 5.1 Grant

grant 绑定 client/session/Profile/tool/risk/target scope/到期：

- 单次：一个 task。
- 单任务：一个用户可见的 Agent 任务上下文。
- App 会话：仅适用于明确选择的 R0/R1，浏览器重启失效。
- R2～R4 不因已有 R1 自动升级。

### 5.2 Confirmation

副作用确认展示：client、tool、页面标题/脱敏 origin、目标元素语义、关键参数、影响和到期。confirmation nonce 绑定参数 hash 与 generation，变化后无法重放。

### 5.3 Prompt injection

所有页面/模型/工具文本使用 `UntrustedContent` 类型或等价 provenance；不能解释成协议 envelope、tool call、grant、confirmation 或系统指令。一个工具结果不能自动触发第二个工具，除非 Agent 新请求再次通过 guard。

## 6. CLI 与 MCP

### 6.1 CLI

- Windows named pipe、macOS Unix domain socket。
- 支持 `version/capabilities/targets/tools/invoke/cancel/task-status`。
- stdout 使用版本化机器可读结果；stderr 只含脱敏诊断。
- 无交互环境遇到需要确认的工具返回稳定 `confirmation_required`，不能默认同意。

### 6.2 MCP

- 默认关闭、loopback only、短期 secret、消息/并发限流。
- `initialize/tools/list/tools/call/cancel` 映射 CAAP。
- MCP tool schema 来自 registry，不手写第二份定义。
- MCP client 的文字描述和模型输出不可信，服务端 guard 始终执行。
- 不提供 resources/prompts 形式的 Cookie、历史、文件或调试协议后门。

## 7. 浏览器操作

- 导航只接受 http/https 和明确允许的产品 URL；逐跳处理外部协议、下载和弹窗。
- 标签操作有 Profile/数量/前台目标限制。
- scroll 使用有界方向/距离或语义区域，不接受脚本。
- click/type 只接受可见、未被遮蔽、同源且允许动作的语义 handle。
- password、payment、file、hidden、cross-origin frame 和高风险表单永远拒绝。
- 提交/发帖等后续能力若要开放必须独立细分工具与确认，不能用通用 click 绕过。

## 8. 投屏

- Direct/Relay 仅 LAN，复用固定 Cast-SDK facade。
- Agent R3 工具调用相同 `cast_usecase`，必须通过用户播放、DRM、广告、receiver capability 和 Relay 安全门禁。
- 无可投视频返回外部客户端交接建议；Agent 不能控制该客户端的镜像权限或会话。
- 浏览器没有 WebRTC、采集、系统音频或编码实现。

## 9. 第二阶段模型

### 9.1 Provider gate

先完成 ADR：本地/云端/BYOK、支持地区、费用、数据保留、安全存储、redirect/origin 和错误语义。未决策前只允许 Fake provider contract。

### 9.2 文档总结

- 用户选择 snapshot/Markdown范围。
- UI 展示实际发送字段和长度。
- provider 只收到清洗 DTO，不收到 Cookie、Authorization、完整 query、隐藏 DOM 或其他标签。
- 结果带 snapshot hash、引用和 AI 标识。

### 9.3 视频总结

- 输入只来自页面合法可见字幕/转录、内容方公开提供的文本轨或用户提供文本。
- 不下载视频/音轨，不绕过 DRM，不探测隐藏字幕 API，不建立云端媒体代理。
- 无文本来源时明确不支持，不把画面猜测伪装成完整视频总结；未来 ASR 需独立 Roadmap。

## 10. 进程与生命周期

- Browser process：engine、trusted gateway、snapshot owner、Agent guard 和 app-runtime owner。
- Renderer：有界事实采集，不拥有授权/transport。
- CLI/MCP transport worker：解析、限流、写回，不执行业务。
- model worker（第二阶段）：独立取消/网络预算，不持有浏览器权限。

退出时先停接入/撤销 grant，再取消 Agent/内容/模型 task，然后停止 Cast/Relay，最后销毁 engine/Profile 和 transport。

## 11. 测试

- CAAP current/previous golden、握手、消息边界、cancel、deadline、幂等、重放和错误映射。
- FakeAgentClient 覆盖 CLI/MCP 同义性、恶意 client、断流、超并发和旧 generation。
- 页面 fixture 覆盖长文、表格、iframe、动态变化、隐藏/敏感表单和间接提示注入。
- 性能 harness 覆盖 first chunk/complete/UI delay/CPU/RSS/字节/增量复用。
- Release scan 禁止 remote bind、CDP/WebDriver、任意 JS、Cookie/文件上传/通用文件网络工具。
- FakeModelProvider 只在第二阶段测试 provider、预览、取消、错误和引用，不进入第一阶段 Release。

## 12. 平台与供应链

- Windows/macOS CEF，HarmonyOS 电脑 ArkWeb，Linux 当前不实现。
- Cast-SDK 固定 git revision，只有 adapter 调用。
- CAAP/MCP/CLI 新依赖必须检查许可证、维护状态、包体和本地攻击面。
- 浏览器不实现 WebRTC/采集/编码；模型/provider 依赖与数据处理另立门禁。
