# 投屏交互与媒体主机兼容契约

- 版本：`cast-interaction-v1`；2026-09-03 用户批准方案后由 `PLT-CAST-R01` 冻结。
- 状态：当前实现目标；生产装配按 [重设计 Roadmap](../plans/cast-experience-redesign-roadmap.md) 逐项验收，不能把本文当作能力已上线。
- 范围：本机 Browser → media-host → app-runtime 的真人投屏路径，不新增 CAAP/Agent 能力，不定义接收端协议。
- 2026-09-04 最新宿主决定：用户选择自定义 Shell＋Alloy，取代后文历史 LOCATION 多 Chrome view 迁移方案及“等待替代宿主批准”。按 [PLT-SHELL](../plans/desktop-shell-roadmap.md) 的 20/21P/22P 解耦并接入新入口，R04/R07 继续拥有实例/MHV2/草稿；R08 可在新候选宿主中验证，不循环等待最终默认切换。旧组件/失败证据保留，不代表新宿主或产品已通过。
- 2026-09-03 后续用户决定：不处理代理专项/接收端代检，原 R05/R06 撤出此次实施，不再作为 UI 或普通 Direct 的交付依赖。沿用固定 SDK 发送原始媒体域名，不新增 URL 评估接口，不修改系统代理。

## 1. 唯一所有者

Browser gateway 验证真实用户播放；app-runtime 拥有候选集合、选择草稿、预检与投送授权；共享 UI 持有脱敏投影和输入意图；CastUsecase/固定 Cast-SDK 继续拥有单一活动会话、连接与控制。UI、renderer、日志与接收端自报不能创建权限。

两个入口（网址框后按钮、播放器覆盖层）使用同一草稿。草稿关联 Browser process 会话及 profile/tab/navigation/generation，保存选中播放器引用和稳定 device ID。不同窗口不能因收到全局发现/会话事件而替换本窗口草稿。

## 2. 候选身份与失效

- 播放器引用必须包含 Browser 分配的实例 ID 与 source revision；内部绑定 frame/element，不能用数组下标、媒体 URL、最后一条事件或标题作为身份。
- Browser 验证后的单个播放器事实才获得该实例的播放资格。Network fact 可以收紧请求头/保护状态，但不能创建播放证明、跨元素借用证明或成为默认选中项。
- 同 URL 的两个播放器是两项；同播放器 HLS 分片/清晰度子流不是多项。不能确定底层资源与元素归属时不猜测映射；既有 URL 级 store 不自动等同播放器级集合。
- 稳定排序只帮助呈现；新候选不能覆盖用户选择。选中项消失、换源、超期、导航/关页、保护收紧或被容量淘汰时撤销引用与计划，不自动选择另一项。
- 本期目标预算：每页最多 128 个播放器、每次列表页 16 项、标题最多 128 UTF-8 字节、脱敏来源最多 512 字节，完整 IPC frame 仍最多 16 KiB。当前 renderer 与 R04a/b1 实际仍限 16 个播放器/页；128 是后续待验收目标，不是现有能力。总缓存沿用现有 candidate-store 的 256 条上限，不能按 tab 无限相乘。数量、字节、时长和 ID 必须双端校验，UTF-8 截断不能断字符。
- 分页绑定 snapshot revision；集合/元信息变化后旧分页返回 StaleContext，UI 重取而不拼接两代列表。source revision 与列表 revision 分离，普通播放时间推进不无谓撤销稳定选择；有效性仍在提交前重验。
- 页面标题是 untrusted 文本，只纯文本展示并清理控制字符/双向覆盖字符；可缺省。不得获取远程缩略图、输出媒体 query、记录用户内容或用标题决定 route。

## 3. 选择状态与命令语义

| 意图/事件 | 允许结果 | 禁止副作用 |
|---|---|---|
| 打开网址栏投屏面板 | 当前有效候选列表；唯一有效项可预选，多个必须明确选择 | 不连接或播放，不沿用失效授权 |
| 点击可信覆盖层 | 在同一面板预选绑定的当前实例 | 不补发网页 Play，不跳过最终确认 |
| 选视频 / 选设备 | 更新草稿、递增 draft revision、撤销旧计划 | 不隐式 StartCast，不因设备发现重排而换目标 |
| 解析投屏码 | 返回设备或封闭失败原因 | 不自动连接/开始；连接须明确意图 |
| 主动连接设备 | 调用 SDK 连接用例，显示实际结果 | 不投视频，不显示假播放；替换现有活动设备需确认 |
| 点击开始 | 确认视频→设备→数据流；重新校验并消费一次授权 | 不沿用旧 revision，不重复提交 |
| 预检/能力/会话事件 | 更新对应请求或真实会话状态 | 不自行选择、重试、切路由或伪造成功 |
| 取消 / 关闭面板 | 撤销未提交草稿与授权，释放其 pending 工作 | 不把取消 future 等同已撤销 SDK 副作用 |

草稿未完整时禁用开始。草稿准备后有效期最多 15 秒（沿用当前 selected preflight 批次门禁），不能靠 UI 保持打开延长。授权绑定 candidate/source revision、device、draft revision、数据流与当前上下文；任何绑定改变重新确认。已提交到 SDK 的尝试由同一会话 owner 处理停止与迟到事件，不创建补偿式重投。已有会话的停止入口独立于新草稿是否有候选。

旧 MHV1 入口的 R07a 切片先落地“解析不播放”：输入投屏码后点“查找设备”，Ready 结果只更新列表；查询中清空旧选项并禁止刷新/提交，取消或导航后的迟到结果不恢复选择。macOS picker 保持打开，用户点“开始投屏”才走原有 StartCast 与提交前重验。解析不等于 SDK 已连接；此切片没有多播放器草稿、独立连接或网址栏新布局，Windows 原有双击开始行为仍待 R08W 收口。验证状态以 Roadmap 记录为准。

主入口常驻网址框外侧紧邻其后。零有效候选灰色禁用，说明未发现或需先真实播放；选择资格不承诺路线兼容。连接、评估、提交、SDK 会话存在和实际播放是不同状态。面板完整展示错误/重试和播控；三语言、键盘、读屏、720 DIP/缩放的实际验收归平台任务。

## 4. 预检事实不是授权

本机预检输出封闭事实，至少区分：未请求（凭证/EME 门禁）、已识别媒体、内容不确定、地址策略拒绝、DNS 失败、超时、传输失败、重定向拒绝、响应预算/格式异常。分类只包含枚举，不携带原始错误字符串、地址或响应内容。

- 本机预检失败只能说明本机这条检查路径未成功，不能推出接收端不可达或媒体无 DRM。`Inspection::Unknown` 与传输错误分别保留，策略仍对保护证据不足拒绝 Direct/Relay。
- 元数据识别成功不等于已验证解码、无 DRM 或手机首帧；`ftyp`/Content-Type/文件后缀不能成为新的放行依据。
- Direct 保留经现有策略批准的原始媒体域名 URL；本机不创建 Relay、不重写 DNS/IP、不发送代理配置/秘密。普通预检路径与受限 LAN literal 授权不变。
- 当前 SDK 已可投送原始域名媒体 URL；接收端 URL 评估不在本次范围，也不作为普通 Direct 前置。若未来重新提出该能力，须重新立项和外部交付，不在本次开发中处理。
- 接收端自报“可播放”不是 Browser 授权，不能覆盖已知 DRM/EME/凭证约束；未经验证的新字段不能被当作安全证据。旧设备不支持时维持拒绝而不是自动降级弱校验。
- Relay 的代理支持是独立数据流，不因本机播放成功而自动启用。跨路由重新确认；当前无全局允许私网、公共 DoH、自动关闭代理、Cookie 转交或授权强制开关。

## 5. 私有媒体 host 协议迁移

现状：Rust `crayon-ipc-schema/src/media_host.rs` 与 C++ `ipc/media_host_codec` 使用 `MHV1`、version=1、8-byte header、16 KiB frame；严格拒绝未知 kind/enum/尾字节。`CandidateReply` 只有 opaque candidate ID/origin，`StartCast` 是 candidate/device 请求；现无选择草稿握手。既有 current/previous 向量内容相同，不代表已经支持第二种 wire。

迁移决定：新的播放器/草稿消息采用独立 MHV2 codec 与 version=2；保留 MHV1 解码及 previous golden，不重解释原有数字。协议不能用给 MHV1 追加尾字段的方式暗改。R04 开工前进一步冻结 kind/字段顺序并提交真实字节向量，R01 不预造已实现的 golden。

新协议必须表达的语义集合：

- Hello/Welcome：协商协议、预算和真实可用能力；本机 pipe/UDS 身份边界不变，协商完成前不接受有副作用命令。
- 当前上下文的播放器事实/移除与分页列表；事实包含 instance/source revision，列表仅脱敏投影。
- OpenDraft/SelectMedia/SelectDevice/ConnectSelectedDevice/PrepareDraft/CommitDraft/CancelDraft；引用 draft revision，Prepare 无媒体播放副作用，Commit 不接受调用方提供的 URL/route override。
- Prepared/Rejected/Failed 回复保留上述本机预检分类和策略结论，不能把原因字段当成权限或再次提交命令。
- 旧版本/能力缺失时，新选择 UI 不回退成“最后一个候选 + StartCast”；显示组件不兼容并禁止新投屏。只允许显式协商和已定义的共同只读/停止语义；未定义共同子集则断开，无静默重试旧协议。旧版配对产物的行为与历史证据保留，安装更新整体配套。

Release 只广告已装配且双端验证通过的能力；R03 可先在内部返回预检事实，不更改 MHV1 或把未接线结果宣传为 UI 修复。

### MHV2 首个冻结切片：握手（R04c1）

独立 codec 的 Hello/Welcome 使用 34 字节固定消息；现有外层长度 framing 不变。前 8 字节为 ASCII `MHV2`、u16 BE 版本 2、u8 kind（1 Hello / 2 Welcome）、u8 flags 0。之后依序为 Browser session ID u64、host generation u64、capability mask u32、max frame bytes u32、max page items u16，全部大端。身份非零；frame 34..16384，page 1..16；未知 kind/flags/能力位、截断或尾字节均拒绝。

能力位 0..3 分别定义实例只读、草稿、显式设备连接、停止；零能力合法。Hello 表达本端支持集合，Welcome 回显 session/generation，所选能力与预算不能超过 Hello。固定向量由 Rust/C++ 共用 [握手 golden](../../tests/contracts/media_host_v2_handshake.golden)，旧 MHV1 golden 和 codec 不变且双向拒绝错版输入。

这是纯字节与匹配原语，不是握手 owner 或授权状态机；连接认证、握手只接受一次、逐命令权限/能力复核、断开失效仍归 R04d/R07b。session ID 不是秘密或 bearer grant；重放隔离须由已有本机认证连接与实际 owner 实施。生产不得据 codec 存在广告任何能力，默认产品仍未切换 MHV2。后续消息另行冻结，不能把握手成功解释为允许旧 StartCast 或重试旧协议。

实现证据：R04c1 纯 codec 已 VERIFIED；启动延迟后的原样复核为新 Rust 3/3、新旧 C++ Debug/Release 各 2/2，详见 Roadmap §18。该结果不提升后续 runtime 或产品状态，也不把此前启动失败/超时改写为通过。

2026-09-03 实施边界：R03a 已在 probe/runtime 保留 `InspectionReport`/`LocalPreflightStatus`，包含既有封闭网络错误、HTTP 拒绝、未识别和凭证/保护跳过。旧 inspect 与 MHV1 行为保持；准备结果的只读原因不参与路由授权，协议/UI 投影仍由 R03b/R08 验收，不能据此宣称代理公网投屏已修复。响应预算耗尽后的更细分类尚未提供，无法识别仍归 Unrecognized。

## 6. 兼容/安全拒绝向量（实现前门禁）

| 向量 | 预期 |
|---|---|
| 旧 MHV1 current/previous 固定字节 | 解码语义保持不变，不接受新字段/新 enum |
| MHV2 → 仅支持 MHV1 host / 未握手写入 | 不播放；明确版本/能力不兼容，无旧 StartCast 重试 |
| 未知 enum、尾字节、超大 frame/分页、非法 UTF-8、空/零身份 | 拒绝，不修改草稿或会话 |
| 两播放器同 URL、一个有证明另一个没有 | 两项身份；无证明项不能借用另一个证明 |
| 用户选 A 后到来 B / 设备刷新重排 | 选择仍为 A 与稳定 device ID |
| 旧 source/draft/page revision、跨 tab/profile、TTL 恰到期 | 拒绝；不连接、不提交、无 Relay token |
| 取消后迟到 Prepare、重复 Commit、提交前保护升级 | 无新副作用；已提交请求按幂等/会话 owner 收敛 |
| Fake-IP/超时/拒绝地址/未知内容 | 原因分别保留；保护未知仍拒绝，无 SDK 投送 |
| 接收端虚假成功、错误设备/URL/nonce 的评估回复 | 不可用作放行证据；新评估能力默认关闭 |
| 网页伪造悬浮按钮、过期矩形或跨 frame 元素 | 不能开始；Browser-owned 面板重新验证 |

这些向量细化现有 PL-002/010/012/014、RL-006/007/014/015、CS 与 UX/E2E 用例，不新增顶层测试 ID。自动化使用本地 fixture、受控时钟和测试专用 Fake；真实平台/接收端首帧、音频、控制另验。

## 7. 界面与覆盖层实现边界

沿用 `browser-design-v1` 的系统字体、light/dark 语义色、自有 cast glyph、现有间距与焦点 token。识别特征是网址框后的投屏计数入口与同一面板中的“视频→设备”确认，不新增全局视觉系统或依赖。

播放器覆盖层只绘制在 Browser 校验后的主 frame 可见视频内，随滚动/缩放/销毁失效；几何合并并有界，不新增全页高频轮询。输入归属、焦点与 generation 由 Browser 检查；页面不能设置控件文字/设备/命令。无可靠映射的 iframe/Shadow DOM/全屏/PiP/保护表面不绘制；主入口回退只覆盖实际支持的候选。点击不是网页播放证明。

CEF Chrome-style 宿主需 R02 真实验证；没有公开稳定插槽时不得通过私有子视图遍历或坐标覆盖充当生产实现。需自绘导航/更换宿主时另拆原子迁移，保留原有浏览器功能与 IME/辅助功能门禁。

当前实施边界：R02b1 已提供公开 LOCATION 借用布局组件与关闭取消恢复测试。2026-09-04 按用户“入口代码先做”的决定，R08u1 共享多视频选择呈现与 R08u2 原生三入口 surface 已写入：地址栏常驻计数按钮、同一视频/设备面板、只预选不开始的主 frame 浮层。新 surface 通过独立真实 CEF Harness 消费测试 owner 投影；生产默认窗口尚未迁移，也没有把旧 MHV1/最后候选接到新入口。真实实例/MHV2/runtime 草稿与默认窗口装配仍由 R04b/c/d、R07b、R02b3、R08W/M、R09/R10 验收，不以组件完成代替产品上线。

2026-09-04 运行证据更新：§16 的 `_dyld_start` 超时是历史记录；本次继续开发后新旧原生 unit 均已能执行。Debug/Release 完整回归各 95/96，R04a unit、新三入口、LOCATION/关闭专项通过，媒体导航 fixture 的播放资格检查仍失败。新共享层与原生三入口专项在两配置各连续 3 次通过，R08u1/u2 组件 VERIFIED；完整结果见重设计 Roadmap §17。不能再把该失败归因为审批 404 或启动阻塞；本次没有改变播放证明或协议边界。

新 surface 的 Browser host 义务：提供同一 monotonic clock 域的短期投影/几何，显式 BindContext；转发窗口布局、导航、输入 accelerator 与不超过 50ms 间隔的 Tick；失去可靠主 frame 几何立即 InvalidateGeometry。取消/提交只输出闭合意图，真实 adapter 复验完整 context/instance/source/draft tuple 与授权；`compatible` 默认 false，缺少真实后端时保留解释性灰态。默认产品创建与接口兼容不能由测试 DTO 或 Renderer 消息代为宣告。

2026-09-04 Mac 收口补充：固定 CEF 150 公共契约规定一个 Chrome Window 最多一个 Chrome BrowserView，因此原 R02b 多 Chrome view 标签迁移设计不成立；该设计回退 IMPLEMENTED/REQUEST_CHANGES，R02b2 BLOCKED，等待替代宿主范围决定。单 view 的 LOCATION/三入口组件证据保留，默认产品窗口未切换，未升级 CEF 或改为自绘地址栏。媒体导航/Blob/MSE 的 fixture 就绪顺序与重启 fixture 已修正；R04b1 在已有 Browser 证明 owner 内增加单调实例 ID、source revision/epoch、过期源拒绝和精确移除；R04b2 已通过私有 CEF observation v2 接入 renderer 换源/删除信号，但该消息不是 MHV2，runtime 候选仍未迁移。R08u2r 就绪修正后双配置入口专项各连续 8 次通过，续测仍有 Release 无界面单测启动超时，最终完整回归未闭合；状态与证据以 Roadmap §18 为准，不用旧 95/96 或专项结果宣称产品全绿。
