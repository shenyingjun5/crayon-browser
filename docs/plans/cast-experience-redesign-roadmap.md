# 投屏体验重设计：域名直拉、统一入口与多视频选择

- **2026-09-04 最新决定**：用户已批准自定义 Shell＋Alloy，宿主迁移统一由 [PLT-SHELL Roadmap](desktop-shell-roadmap.md) 承接。本文 R02b/b2 的旧 LOCATION 多 Chrome view 方案停止实施，原状态与失败记录保留为历史，不再等待方案选择；R02b3/R02c 的义务由 SHELL-24P 聚合。R08P 改依赖 SHELL-07P/15P/20 及原 R03b/R04/R07，不依赖最终默认切换，避免环路。投屏授权/协议/草稿仍在本计划，不复制业务。
- 日期：2026-09-03。
- 来源：用户要求处理代理环境、优先接收端直接拉取域名媒体 URL、投屏按钮紧邻网址框、无候选常驻置灰、多视频明确选择，并评估播放器悬浮入口。
- 性质：用户已授权实施的设计与后续切片，不代表功能已实现。2026-09-03 后续决定：不处理代理专项/接收端代检，R05/R06 移出队列，不再作为其他任务依赖；普通 Direct 沿用现有 SDK 发送原始媒体域名。
- 2026-09-04 后续执行决定：用户明确要求不因原生测试启动阻塞停止独立后续代码开发。允许按 §17 先实现 UI 组件与闭合意图接口；无运行证据时最高 IMPLEMENTED，通过规定自动化后组件可 VERIFIED，但不代替平台产品装配。未验证的播放证明、MHV2 与平台宿主不因 UI 已编译而被宣称可用。历史“未通过不领取任何后续实现”的表述由本条取代，真实授权/协议兼容/发布验收不降低。
- 归属：既有 `PLT-M05/PLT-W05` 的内部切片，ID 前缀 `PLT-CAST-R`，不增加 297 个顶层任务的统计。保留 Windows 首发顺序和已有 Mac 证据；两个平台分别验收。
- 关联：[受限 LAN 预检](lan-media-probe-roadmap.md)、[平台 Roadmap](desktop-platform-adapters-roadmap.md)、[当前体验契约](../current/browser-ux.md)、[架构](../current/architecture.md)、[威胁模型](../current/threat-model.md)。

## 1. 当前事实与根因

| 问题 | 已核对的事实 | 设计结论 |
|---|---|---|
| 为什么不能直接送域名？ | `crayon-cast-adapter::deliver` 的 Direct 已原样传递媒体 URL，接收端自行拉取；它不要求先下载，也不将域名替换成本机 IP | 保留 Direct 作为符合策略时的首选；发送的是视频/manifest 的媒体 URL，不是网页地址 |
| Mac 能播，投屏却无路由 | 本次系统 DNS 返回基准测试网段，probe 安全检查拒绝；runtime 把检查失败合并为 Unknown，最终 UI 只剩 NoRoute。Fake-IP/代理 DNS 是环境原因推断，未确认代理软件 | 拆开本机预检、内容保护和接收端可达性；不能把本机检查失败解释为接收端解码失败 |
| 多视频为什么没有选择 | runtime 有多个 candidate，但 `CastShellController` 只保存一个 `current_candidate_`，后来的事件覆盖前一个；选接收端/连接投屏码会立即启动 | 保留候选集合；明确视频、设备及开始动作，禁止“最后一个候选自动投” |
| 入口位置与错误可见性 | 当前 Mac 入口挂在标题栏右侧；无媒体时模型隐藏入口。AX 能读到拒绝文本，但真实窗口有长文案不可见现象 | 网址框后的常驻入口 + 独立弹层展示选择、进度、错误和播控，不再扩张标题栏文本行 |
| 能否在播放器浮按钮 | renderer 有元素 ID 与可见比例，但产品候选没有完整元素绑定/矩形；当前 observation bridge 主路径限制主 frame | 可以建设浏览器拥有的覆盖层，但不是现成的跨 iframe/全屏能力 |

代码入口：

- [Direct 执行](../../crates/crayon-cast-adapter/src/delivery.rs)、[SDK facade](../../crates/crayon-cast-adapter/src/facade.rs)。
- [媒体规划](../../crates/crayon-app-runtime/src/media_planning_runtime.rs)、[预检网络边界](../../crates/crayon-media-probe/src/http.rs)、[路由策略](../../crates/crayon-cast-policy/src/decide.rs)。
- [壳控制器](../../browser/cef-shell/src/browser/media_host/cast_shell_controller.cc)、[按钮模型](../../browser/shared-ui/chrome/src/chrome_toolbar.cc)、[Mac 入口](../../browser/cef-shell/src/macos/cast_chrome_mac.mm)。
- [CEF 窗口装配](../../browser/cef-shell/src/browser/window/tab_controller.cc)、[被动媒体观察](../../browser/cef-shell/src/renderer/media_observer/cef_media_observer_renderer.cc)。

## 2. 网络方案：Direct 优先，不等于跳过安全检查

### 2.1 分清三条路径

| 场景 | 目标行为 | 限制与失败反馈 |
|---|---|---|
| 无凭证、保护与广告门禁通过，接收端兼容且可访问 | Direct：接收端拉原始域名媒体 URL，不创建 Relay | Mac 的代理配置不发送给接收端；接收端使用自己的 DNS、网络、TLS 与解码能力 |
| Mac 预检因 Fake-IP/代理环境不能安全确认 | 记为“本机预检受网络环境影响，设备可达性尚未确认”；满足 2.2 才能走受控接收端评估 | 不自动推断不可投，也不直接把 Unknown 改成 Clear；未具备替代证据时保留安全拒绝 |
| 接收端不可直达，但本机可以安全访问，且媒体满足 Relay 条件 | 提示“设备无法直连，可经本机转发”，用户确认后创建限定会话 Relay | 重新检查候选、设备、数据流与授权；现有 no-proxy Relay 不自动获得代理穿透能力 |
| Cookie/Authorization 绑定、DRM/EME、需要 key/license、来源无法安全确认 | 不直投、不用 Relay 绕过；说明准确原因；符合既有规则时仅提示外部客户端交接 | 不提取密钥、不发送浏览器秘密、不自动打开外部客户端 |

URL 保留经策略批准的原始 hostname/path/query 语义，不擅自换 IP 或删除查询参数；媒体地址只进入短期受保护 DTO 和获准接收端命令，不进入日志、UI 文案或持久化诊断。禁止 URL 用户名/密码与凭证导出，不能借“原样传递”放宽现有秘密分类。

### 2.2 历史提案：接收端替代预检（已撤出本次范围）

以下 1～6 是 R00/R01 历史设计，不再是当前实施要求。用户明确不处理此特殊环境，继续 UI 与多视频工作；不新增接收端 URL 评估、不要求升级 SDK，也不把该能力作为一期前置。已完成的原因保留不回退；本次不改现有策略，不把未验证场景标为通过。

1. 拆分证据：本机传输预检结果、媒体保护状态、请求头依赖、设备格式能力、设备媒体可达性各有明确状态。错误不得再由一个 Unknown/NoRoute 吞掉。页面能播放、文件扩展名为 MP4、没有观察到 EME，均不能单独证明无保护且可转移。
2. 正常预检成功路径继续复用现有策略。代理阻塞路径优先评估“接收端受控检查媒体 URL”的 SDK 能力：用户已选定当前真实播放候选和设备后，签发短期、一次用途的媒体评估请求；不是任意 URL 请求接口。
3. 接收端检查本身必须受约束：校验 scheme/host、全部解析地址与每次重定向，拒绝未授权内网/元数据/基准地址，限制响应大小、数量和 deadline；只检查获准媒体元信息，不访问 key/license。返回封闭原因和足够的保护/格式证据，不回传秘密或完整 URL。正式播放须沿用有效评估绑定，防止检查后 DNS rebinding、换源或换设备。
4. 当前 `assess_receiver(device, media_kind)` 只评估格式能力，没有媒体 URL 参数，也不能证明接收端 DNS/重定向安全。该缺口必须先交 Cast-SDK/receiver 做正式 API 与版本交付；浏览器不得自行拼 DLNA 请求或偷偷增加接收端接口。外部仓库修改、版本发布另需明确授权。
5. 可另评估复用 Browser 已正常加载资源的有界、非敏感保护元信息，减少重复预检；renderer 自报、单纯播放推进或响应 Content-Type 不足以授权。此方向不能替代接收端实际取流处的网络安全检查，本提案不默认读取/缓存视频响应体或新增带 Cookie 请求。
6. 不支持受控 URL 评估的旧接收端，保留已验证的普通 Direct 路径；在本机预检无法确认且替代证据不足时，明确显示兼容性限制。不得给旧设备提供“忽略安全直接投”按钮。

不采用的方案：全局允许私网/基准地址、静默切换系统 DNS/公共 DoH、自动关闭用户代理、把 Mac 的 Fake-IP 发给电视、把 Cookie/代理凭证交给接收端。临时关闭代理只能作为用户授权的诊断对照，不作为产品使用前提。

需要代理的 Relay 是另一条网络数据流：代理类型、DNS 归属、upstream allow-set、凭证存储和禁用直连泄漏都须独立安全设计；R05 只分析缺口，不把本次受限 LAN 例外扩大为通用代理。

## 3. 一个主入口、一个选择面板

### 3.1 网址框后常驻按钮

```text
[后退][前进][刷新] [ 网址输入框                     ] [投屏·2] [书签][下载][…]
                                                      │
                         ┌────────────────────────────┘
                         │ 视频：○ 视频 1   ○ 视频 2
                         │ 设备：已发现设备 / 输入投屏码
                         │ 方式：设备直接播放 / 经本机转发
                         │ 状态或原因说明
                         │              [取消] [开始投屏]
                         └─────────────────────────────
```

- 投屏位于网址输入框外侧紧邻其后，不在窗口右上角，也不混入网址文本/安全标识区。窄窗口优先保留投屏和网址框。
- 无符合条件的视频：常驻灰色禁用；无障碍描述和提示解释“未检测到可投视频”或“请先播放网页视频”。仅 DOM 检测到 `<video>` 不等于可以投，仍须 Browser 验证真实播放。
- 存在当前有效候选：可点击；多候选用数量辅助，不只靠颜色。资格只说明可以进入选择，不保证所选设备与路线一定兼容。
- 正在连接/评估/提交：显示进度，阻止重复提交；已连接不等于正在播放。只有 SDK 会话/播放反馈支持时才显示对应播放状态，首帧验收另用真实设备证据。
- 当前有会话时，按钮保留进入播控的能力；若切页策略已停止会话，应显示实际停止状态。不能仅因新页面没有候选就隐藏仍有效会话的停止入口。
- 错误在面板内完整展示并提供明确重试/重新选择；重试重新验证，不沿用过期授权。三语言、键盘、读屏、缩放都验收。

### 3.2 视频与设备可以任意先选，但不会选完就误投

- 推荐默认顺序：选视频 → 选设备 → 开始投屏。视频格式会影响可选设备能力，先选视频便于解释不兼容。
- 只有一个有效视频时可预选；多个时明确让用户选择。记录用户选择后，新事件只能更新列表，不能把选择自动换成“最近发现/正在播放”的另一个视频。
- 同一个面板支持先选设备或主动连接设备，再选视频。设备选择、设备连接、开始播放是三个不同动作；投屏码解析/连接成功不自动调用 `StartCast`。
- 没有视频也想预连接时，从菜单中的设备管理进入；不为此把灰色投屏按钮偷偷变成可投。已选择但未连接时准确标“已选择设备”，不显示假连接。
- 列表以播放器实例为单位，不把 HLS 分片/清晰度子流重复列为不同视频，也不能把两个使用相同 URL 的播放器合并。显示经净化的标题、时长/清晰度（已有可靠数据时）、来源域名和“视频 1/2”定位信息；没有信息时用占位，不猜标题、不暴露签名 URL，不默认截图/抓取远程缩略图。
- 已检测但未实际播放、已失效或不符合策略的项可禁用并说明原因，不能静默消失后选中其他视频；候选淘汰、导航/换源/关页、证明过期均撤销原选择与开始资格。
- 最后按钮明确当前“视频 → 设备”。更换正在使用的设备/视频涉及替换已有会话时显式确认；发现刷新、连接成功、能力返回均不是隐式开始或静默降级授权。

## 4. 播放器悬浮按钮：可做，但只是快捷入口

- 对 Browser 已确认可选择的可见视频，在其右上方内侧安全区域显示小型投屏按钮；可随鼠标进入/键盘聚焦出现，避开播放器控制条、字幕与全屏控件。点击只预选这个视频，打开同一浏览器面板，仍需确认目标设备和开始。
- 按钮由 Browser/native/CEF 覆盖层绘制，网页不能调用真实投屏动作或伪造授权；最终视频/设备/路由确认始终位于浏览器拥有的面板中。DOM 注入按钮不作为可信入口。
- 元素 ID、矩形、可见性仅是不可信观察输入。Browser 将其绑定 profile/tab/navigation/frame/element/source revision，裁剪到有效 viewport，核对当前候选和真实输入；旧位置/换源/脱离 DOM/跨导航立即失效。
- 几何更新合并且有界，不新增高频整页扫描；滚动、缩放、resize 和 CSS 变化正确裁剪。不让页面改变按钮文字、命令、接收端或权限。
- 建议第一期只交付已验证的普通主 frame 视频覆盖层；iframe、Shadow DOM、全屏、画中画和受保护表面单独记录支持矩阵。无法可靠映射时不绘制悬浮按钮，网址栏主入口保留现有可支持候选，不能宣称所有 iframe 都已可投。
- 悬浮按钮点击仅是投屏意图，不代替网页实际播放动作；不自动点击播放/广告/跳过，不按广告域名过滤候选。

CEF 官方 [CefWindow 接口](https://raw.githubusercontent.com/chromiumembedded/cef/master/include/views/cef_window.h)提供 `AddOverlayView`；本地固定 CEF 150 头文件也有该接口。它证明有覆盖层原语，不证明当前壳已经接入。官方 [CefBrowserView 接口](https://raw.githubusercontent.com/chromiumembedded/cef/master/include/views/cef_browser_view.h)的 `GetChromeToolbar` 有 Views/Chrome-style 前提，不能据此假设可以任意向现有 omnibox 插入控件。

当前产品直接 `CefBrowserHost::CreateBrowser` 使用 Chrome runtime，不是现成的 CefBrowserView/CefWindow 宿主。R02 必须先验证可维护的地址栏相邻控件扩展/Views 装配路线，覆盖标签、地址安全标识、IME、快捷键、下载与窗口生命周期；不得硬编码屏幕坐标、依赖私有 Chromium 子视图遍历或在 Release 用 UI 自动化找地址栏。若需 CEF 源码变更/依赖升级或整体自绘框架，另拆计划与供应链 Review，不夹带进按钮移动。

## 5. 所有权与取消

- app-runtime 唯一拥有投屏选择草稿、候选有效性、授权和 route；共享 UI 只持有脱敏投影与选择意图。两个入口不各建一套连接/播放状态机。
- 现有 observation pipeline 提供元素/来源事实，Browser 验证播放；候选投影新增实例与 revision 后，经版本化媒体 host 协议交给壳，不将 OS/CEF 类型放入共享 DTO。
- 草稿绑定当前 profile/tab/navigation 和单一候选，设备通过稳定 ID 绑定 SDK；每次选择变化使旧计划失效。单一活动会话继续由既有 CastUsecase/SDK owner 管理，不新增跨窗口抢占 owner。
- 按需产生短期评估/开始授权；取消、换视频、换设备、导航、关页、退出、保护/凭证收紧使旧回调和令牌失效。SDK 已提交后不能把取消 future 当作已撤销播放，仍按原 owner 收敛 stop/session。
- 候选集合、选择面板事件、元数据和几何队列使用明确命名预算、满载策略、generation 和 deadline；候选被淘汰时撤销选择，不用索引指向另一项。
- 当前/前一版协议兼容、错误封闭枚举、取消/幂等与 reject vectors 先在 R01 定义；Agent/CLI/MCP 不因新入口新增媒体 URL、任意 JS 或网络能力。

## 6. 原子实施顺序

以下是实施队列，不是已领取的生产改动。每个切片开始前须补全具体文件、命名预算、完整构建目录及验收命令；生产改动不得直接从宽泛目录范围开工。外部 SDK 修改不在本次授权中。

| ID | 状态 | 依赖 | 单一目标与允许领域 | 主要验收 |
|---|---|---|---|---|
| PLT-CAST-R00 | VERIFIED | 用户重新设计要求 | 本设计、索引、原 LAN 计划衔接；仅文档 | 事实/链接、repo-guard、diff check、方案 Review |
| PLT-CAST-R01 | VERIFIED | R00 VERIFIED | current UX/架构/安全/媒体 host 协议修订与兼容设计；不改运行时 | 明确 Direct 替代证据授权、候选/草稿所有权、current/previous 与拒绝向量评审；未过不进入生产 |
| PLT-CAST-R02 | TODO | R01 VERIFIED | 宿主工作拆为 a 原语验证、b 产品迁移与 c 平台矩阵；汇总状态不作领取项 | 原语证据不能代替完整浏览器迁移 |
| PLT-CAST-R02a | VERIFIED | R01 VERIFIED | CEF 150 LOCATION + Views/overlay 独立测试 | Mac 宽/窄相邻布局、灰态/可用态、覆盖层与安全关闭；Windows 未覆盖 |
| PLT-CAST-R02b | IMPLEMENTED | R02a VERIFIED | 旧方案历史状态，停止领取；新方案由 PLT-SHELL 承接 | §18 限制与 REQUEST_CHANGES 保留；2026-09-04 已决定 Alloy，不再等待方向批准 |
| PLT-CAST-R02cW/R02cM | TODO | 对应 PLT-SHELL-24W/24M VERIFIED | 对应平台真实宿主装配总验收的映射项，不重复领取 | 窗口/标签/导航/IME/读屏/缩放/关闭取消；R08 已移至候选宿主验证，不反向等待本项 |
| PLT-CAST-R03 | TODO | R01 VERIFIED | 预检原因分层汇总；拆为 a 内部事实、b 协议/UI 投影，不作领取项 | 内部事实不能冒充产品错误提示已修复 |
| PLT-CAST-R03a | VERIFIED | R01 VERIFIED | probe/runtime：保留本机预检失败原因，不改变 Direct 资格 | 确定性 Fake-IP、超时、拒绝、Unknown 不变 Clear；runtime/probe unit + clippy |
| PLT-CAST-R03b | TODO | R03a、R04 的 MHV2、R07 VERIFIED | 原因枚举投影到新协议与共享面板模型 | 无 URL/原始错误、旧 MHV1 不变；平台显示在 R08 验收 |
| PLT-CAST-R04 | TODO | R01 VERIFIED | Browser/runtime/媒体 host：多播放器候选有界投影与 per-candidate proof | 双播放器/同 URL/manifest 子流/换源/淘汰/旧 generation；协议 current/previous 与 reject vectors |
| PLT-CAST-R00b | VERIFIED | 用户本轮范围调整 | 撤出代理专项与接收端代检前置，拆出独立显式开始修复 | current/计划一致、链接、repo-guard、diff check |
| PLT-CAST-R07 | TODO | R07a、R07b VERIFIED | 多视频/设备草稿与统一面板汇总；不作领取项 | 独立连接、显式开始、取消/替换确认 |
| PLT-CAST-R07a | VERIFIED | R01 VERIFIED | 旧 MHV1 入口先去除投屏码解析成功后的自动播放 | 解析零 start、取消/迟到/失败不播放；三语言文案；Mac Debug/Release 各 92/92，含原生 UI + controller |
| PLT-CAST-R07b | TODO | R04、R07a VERIFIED | 新协议多视频/设备草稿、独立连接与显式提交 | 唯一 runtime owner、版本/失效/取消/替换 |
| PLT-CAST-R08W | TODO | PLT-SHELL-07W/15W/20、R03b、R04、R07 VERIFIED | Windows Alloy 候选宿主网址栏按钮与选择/播控面板，对应 SHELL-21W | Debug/Release build/CTest、720 DIP/缩放/键盘/Narrator、真实多视频/设备选择 |
| PLT-CAST-R08M | TODO | PLT-SHELL-07M/15M/20、R03b、R04、R07 VERIFIED | macOS Alloy 候选宿主同等入口，对应 SHELL-21M；承接 b3c 视觉与 picker | Debug/Release build/CTest、缩放/IME/VoiceOver、完整错误截图、真实多视频/设备选择 |
| PLT-CAST-R09 | TODO | R04、R07 VERIFIED | 观察管线增加受限元素几何与候选绑定，不绘制按钮 | 主 frame/换源/滚动/缩放/裁剪/销毁与消息预算；不扩大播放证明 |
| PLT-CAST-R10W | TODO | R08W、R09 VERIFIED | Windows 普通视频 Browser-owned 悬浮快捷入口 | 遮挡/键盘/焦点/伪造页面/过期位置无错投；unsupported surface 无 overlay |
| PLT-CAST-R10M | TODO | R08M、R09 VERIFIED | macOS 对称悬浮快捷入口 | 同矩阵，真实 CEF 截图与点击目标，不能用 AX 元素存在代替视觉可用 |
| PLT-CAST-R11W | TODO | R08W、R10W VERIFIED | Windows 产品整链回归、证据与 Review | 对应 PLT-W05 Direct/Relay/拒绝/生命周期门禁；真实设备，未闭合不得 DONE |
| PLT-CAST-R11M | TODO | R08M、R10M VERIFIED | macOS 产品整链回归、证据与 Review | 普通 Direct/Relay 与 Release 真机；代理特殊环境不阻塞；Keychain 最后 |

当前一期继续网址栏主入口、多视频选择与普通主 frame 悬浮入口。R05（SDK URL 评估/代理 Relay 缺口）和 R06（替代预检接入）按用户决定移出队列，既不领取也不标为 DONE；高级覆盖层另行排期。普通 Direct 不等待新接口，仍保留原域名；保护/凭证、用户确认与现有地址边界不因范围调整而移除。

### R00 本次领取范围

- 单一目标：给出能解释现场失败并覆盖用户四项交互要求的整体方案，拆出明确实施顺序。
- 输入：main/cfaab39 工作区现状、LAN b4 人工证据、当前 PRD/UX/架构/安全/测试/Review 契约、CEF 150 公共头文件。
- 允许文件：本文件、`docs/plans/README.md`、`docs/plans/lan-media-probe-roadmap.md`；保留其他所有未提交改动。
- 禁止：生产/测试代码、当前授权策略与协议、SDK gitlink/外部仓库、依赖、系统代理/DNS/Keychain、自动播放、提交/推送/发布。
- 验收：`cargo run --quiet -p repo-guard -- scan --root .`、`git diff --check`，本次三个文档的本地 Markdown 文件链接检查与代码事实核对。
- 明确不做：在设计阶段改变路由、移动实际按钮、宣称手机可在当前代理环境直投，或宣布一期完成。

## 7. 验收矩阵与历史衔接

### R01 领取范围（2026-09-03，用户授权执行）

- 单一目标：把用户批准的方案转成可实施的产品/信任/状态/兼容契约，保持生产默认拒绝和 SDK source lock 不变。
- 输入：R00 VERIFIED、当前 MHV1 Rust/C++ 协议与调用方、候选/规划/runtime、现有 UX 和安全契约；不把规划当作已装配。
- 允许：本计划与计划索引、PRD §4.2、current README/browser-ux/architecture/threat-model；新增 `docs/current/cast-interaction.md`（现无独立媒体交互/兼容契约，不重复 CAAP）。
- 禁止：运行时代码、wire 字节/版本实现、golden 文件覆盖、SDK/CEF 升级、系统设置、凭证/Keychain、自动播放、提交/推送。
- 验收：`cargo run --quiet -p repo-guard -- scan --root .`、`git diff --check`、全部本次文档的本地链接与空白检查；静态核对 MHV1 现状、迁移与拒绝向量、唯一 owner、取消/幂等和安全默认值，独立方案 Review。
- 明确不做：用文档批准实际接收端网络权限；该接口的最终参数/预算/来源认证须 R05 外部交付评审，未有能力不得启用。实际新 wire golden 与双语言 codec 验证在 R04 实现任务完成。

- 自动化使用本地 HTTP/DNS/接收端 fake 与确定性时间，不依赖公共网或第三方影视站。覆盖合法公网域名、正常 DNS、Fake-IP 拒绝、接收端可达/不可达、重定向到私网、重绑定、凭证/DRM/key、超时/取消/重放。
- 真实设备分别记录浏览器真实播放、媒体评估、所选路由、接收端首帧/音频、暂停/恢复/seek/stop。必须证明 Direct 没有创建 Relay，不能只凭命令成功或设备在线。
- 多视频覆盖 0/1/多个、同 URL 两播放器、播放中切换、候选新增/删除、两个窗口、设备刷新/断线/投屏码；开始动作显示并提交同一个用户选择。
- UI 覆盖 light/dark、窄/宽窗口、缩放、多语言、键盘/读屏；所有错误在真实窗口可见，截图与语义状态一致。正式平台证据分别取得。
- 公共 W3C VP9/AVC 样片只用于人工补充；现有本地无声 VP9 Direct 首帧/播控 PASS 不扩大为公网、音频、所有格式或代理网络通过。
- 原 LAN b1/b2/b3a/b3b 的 VERIFIED 不回退；b4 公网阻塞仍保留。b3c 的真实布局/picker 核对由 R08M 承接验收，不重复宣称关闭。R03/R06/R11M 提供代理原因与新路径补证；不通过禁用用户代理消除产品问题。
- 本计划不替代 PLT 的 Relay/拒绝/长稳、QAR 发布签名/公证、安全与安装门禁，不改变 Keychain 可选且后置的用户决策。

## 8. R00 验证与 Review

- 对象：main/cfaab39 的既有 dirty 工作区之上的 R00 文档增量；macOS arm64，配置 N/A（纯文档）。本次只新增本计划并在两个既有计划中增加衔接段落，未覆盖其他未提交内容。
- `cargo run --quiet -p repo-guard -- scan --root .`：PASS/0；12 项检查中 9 passed、2 warning（既有 RG-003/004 规模/可配置字面量）、1 not_applicable（RG-006 无发布产物）；完整耗时未保留。不以源码扫描代替 artifact 验证。
- `git diff --check`：PASS/0，耗时小于 1 秒。`git diff --no-index --check /dev/null docs/plans/cast-experience-redesign-roadmap.md` 与 `git diff --no-index --check /dev/null docs/plans/lan-media-probe-roadmap.md` 均 exit 1、无诊断输出（no-index 存在文件差异）；未将这两次记作 exit 0。另用下述 Node 检查覆盖未跟踪文件的行尾空白和 EOF 换行，PASS/0。
- 本地链接与空白检查：PASS/0，3 文档、40 个本地文件链接，耗时小于 1 秒；完整命令如下。不联网复验全部历史网页链接。

```sh
node --input-type=module -e 'import fs from "node:fs"; import path from "node:path"; const files = ["docs/plans/cast-experience-redesign-roadmap.md", "docs/plans/README.md", "docs/plans/lan-media-probe-roadmap.md"]; let count = 0; for (const file of files) { const body = fs.readFileSync(file, "utf8"); if (/[ \t]+$/m.test(body) || !body.endsWith("\n")) throw new Error(`Whitespace: ${file}`); for (const match of body.matchAll(/\]\(([^)]+)\)/g)) { const href = match[1]; if (/^(https?:|#)/.test(href)) continue; fs.accessSync(path.resolve(path.dirname(file), href.split("#")[0])); count++; } } console.log(`PASS: ${files.length} documents, whitespace and ${count} local file links`);'
```

- 方案 Review：按需求/边界→正确性→架构/API→生命周期→安全/隐私→性能→证据→维护核对，R00 文档增量 P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED；不是对后续网络授权或代码实现的批准。明确保留 Unknown 拒绝、SDK 所有权、显式开始、候选有效性、旧设备兼容与 CEF 宿主门禁，未把可行性写成已实现。
- 未覆盖与风险：新 API/授权的最终安全 Review 归 R01/R05；工具栏实际接入归 R02；旧接收端可能无法走代理环境下的替代 Direct 路径；高级视频覆盖层无产品证据。生产实现、构建/Unit/Integration、Mac/Windows UI、手机公网首帧、签名/公证和发布验证本次均 NOT_RUN。没有操作代理或 Keychain，没有提交/推送。
- 下一任务：R01 契约与兼容设计；随后 R02 验证实际 CEF 宿主。实现队列保持 TODO，原 LAN 公网 b4 保持 BLOCKED。

## 9. R01 完成记录（2026-09-03）

- main/cfaab39 既有 dirty 工作区上的文档增量，macOS arm64；新增 current/cast-interaction，修订 UX 入口顺序、PRD、架构、安全和索引，不修改生产/MHV1/golden/SDK。
- 冻结：两入口同一 runtime 草稿、单播放器证明、source/draft/snapshot revision、显式开始、15 秒准备授权、分页/文本预算；网络错误事实与保护/路由分离。MHV2 新语义迁移、旧 MHV1 previous 向量保持、混版拒绝和无降级 StartCast；实际 codec/golden 在 R04 实现。
- `cargo run --quiet -p repo-guard -- scan --root .`：PASS/0，9 passed、2 既有 RG-003/004 warning、RG-006 N/A，完整耗时未保留。`git diff --check`：PASS/0，<1s。下列链接/空白命令 PASS/0，8 文件/77 本地链接、<1s。

```sh
node --input-type=module -e 'import fs from "node:fs"; import path from "node:path"; const files = ["docs/plans/cast-experience-redesign-roadmap.md", "docs/plans/README.md", "docs/crayon-private-cast-browser-prd.md", ...["README", "browser-ux", "cast-interaction", "architecture", "threat-model"].map(x => `docs/current/${x}.md`)]; let links=0; for (const file of files) { const text=fs.readFileSync(file,"utf8"); if (/[ \t]+$/m.test(text)||!text.endsWith("\n")) throw Error(file); for (const m of text.matchAll(/\]\(([^)]+)\)/g)) { if (/^(https?:|#)/.test(m[1])) continue; fs.accessSync(path.resolve(path.dirname(file),m[1].split("#")[0])); links++; } } console.log(`PASS ${files.length} documents, ${links} links, whitespace`);'
```

- Review：按需求→正确性→所有权/兼容→生命周期→安全→预算→证据→维护核对，P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED（契约任务）。没有用设备自报或错误文本授予路线，不开放代理/私网；实际字段/字节向量未实施且明确后续门禁。
- 未覆盖：生产构建/运行时/真实 UI/设备 NOT_RUN；R02 宿主验证、R05 外部能力参数与来源验证仍必须完成，不能因 R01 通过广告新能力。frontend-design 仅约束沿用系统字体/语义色/单一开始动作与键盘入口，没有引入 Web UI 栈或新依赖。

## 10. R02a 领取范围（原 R02 明确拆分）

- 单一目标：用固定 CEF 150 的 `CEF_CTT_LOCATION` + Views 验证地址组件后相邻按钮和 Browser-owned overlay 的真实宿主原语，识别产品窗口迁移门禁，不替换当前产品窗口。
- 输入：R01 VERIFIED、本地固定 CEF 公共头与官方 cefclient/views_window 示例、现有 CEF integration 独立测试 target。
- 允许：新增 `browser/cef-shell/tests/cast_toolbar_host_probe.{h,cc}`；既有 `page_snapshot_cef_integration_mac.mm` 仅添加独立测试模式分发；`browser/cef-shell/CMakeLists.txt` 仅将该文件加入测试构建图及注册独立 CTest；本计划/索引。复用测试 bundle 避免再复制 CEF framework。
- 禁止：生产 tab_controller/app、CEF/vendor/SDK、真实投屏/播放证明、真实用户 profile、网络页面和系统设置。仅打开 about:blank，测试按钮不发投屏命令。
- 验收：双配置 `cmake --build .cache/build/macos-arm64-cef-{debug,release} --target crayon_page_snapshot_cef_integration_test -j4`；对应 `ctest --test-dir <build> --output-on-failure -R '^cast_toolbar_host_probe$'`、完整适用 CTest（包含此测试时可复用完整结果）、clang-format、repo-guard、diff check。验证 Chrome 原生 location view 有效、按钮逻辑相邻且不重叠、宽/窄布局、灰态/可用态、overlay 可见/撤销和浏览器/窗口关闭。
- 不做：将独立宿主测试冒充生产装配、IME/VoiceOver 或多标签真机通过。原语通过后必须把产品多标签/导航/权限/快捷键迁移拆成 R02 后续切片，R08 依赖该迁移完成。

### R02a 验证与 Review（2026-09-03）

- 对象：main/cfaab39 dirty 工作区上的上述独立测试增量；macOS 26.6.2/arm64，固定 CEF 150.0.10。未修改产品窗口，未升级依赖。
- `cmake --build .cache/build/macos-arm64-cef-debug --target crayon_page_snapshot_cef_integration_test -j4`、`cmake --build .cache/build/macos-arm64-cef-release --target crayon_page_snapshot_cef_integration_test -j4`：均 PASS/0；完整 build 耗时未保留。
- `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure`：PASS/0，92/92，149.16s；新原型 0.51s。Release 同命令替换目录为 `.cache/build/macos-arm64-cef-release`：PASS/0，92/92，141.19s；新原型 0.38s。CEF GUI/loopback 在获准的非沙箱测试进程运行，使用 mock Keychain。
- 新 `.h/.cc` 的 `clang-format --dry-run --Werror` 与既有入口仅 `--lines=899:953` 格式检查 PASS/0；`git diff --check` PASS/0；`cargo run --quiet -p repo-guard -- scan --root .` PASS/0，9 passed、2 既有 warning、1 N/A；这些检查 <1s 或完整耗时未保留。仅调整测试文件格式，没有全仓格式化。
- 初次失败保留：编译因错误调用 `CefView::SetMinimumSize`/缺 callback 头失败；沙箱 GUI 启动 abort；CEF wrapper 指针比较导致 readiness timeout；缺 ButtonDelegate 导致 SIGTRAP；原语成功后因借出的 Chrome location 未先移出父视图，关闭时 raw_ptr 检查 SIGTRAP。均已在测试中修正；两次 LLDB exit 0 只表示调试器正常退出，不能当测试通过。
- 原语事实：1040/720 DIP 下 location 宽 968/648、相邻按钮宽 72；灰态→可用态、overlay 显示/隐藏/销毁、Browser/Window 都正常关闭。没有投屏命令或网页播放副作用。
- Review：按标准逐项核对独立测试构建图、UI 线程/回调重入、借用视图释放和失败退出，P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。当前测试仅 about:blank；**产品不能照搬 CanClose 先拆工具栏的策略**，须在 R02b 处理 beforeunload 取消后恢复控件，或选择不会提前拆除的生命周期方案。
- 未覆盖：真实产品标签/弹窗/导航安全/IME/VoiceOver、Windows、实际多视频选择与投屏全部 NOT_RUN。本次既有 ad-hoc 启动签名警告未用于宣称正式签名/公证通过。R02 汇总仍 TODO；R02b/c 是正式按钮接入的前置门禁。

## 11. R03a 领取范围（2026-09-03）

- 单一目标：在 probe 与 app-runtime 保留无敏感数据的本机预检原因，不改变保护判定、请求预算、Direct/Relay 资格和旧协议。
- 输入：R01 VERIFIED、当前 inspect/http/assess、media_planning_runtime、media_host_runtime 的 prepare/commit 与本地 upstream 测试。
- 允许：`crates/crayon-media-probe/src/{inspect,lib}.rs`、`tests/inspect.rs`；`crates/crayon-app-runtime/src/{media_planning_runtime,media_planning_runtime_tests,media_host_runtime,media_host_cast_runtime_tests}.rs`；本计划/索引与 current/cast-interaction 的实现边界说明。
- API 冻结：新增 inspection report + 封闭识别状态；现有 `inspect`/`inspect_selected_lan` 返回类型兼容。runtime 原因仅为识别状态、既有封闭 `ProbeHttpError` 或凭证/EME 跳过；选设备 prepare 的私有一次结果保存原因。不给 UI 原始 URL/错误文本，不在日志持久化，不产生 route。
- 禁止：HTTP/DNS/代理配置放宽、实际网络重试、SDK/receiver/依赖、MHV1 wire/golden、UI 文案、真实播放/用户 profile/Keychain、提交推送。Unknown 仍按旧 policy 拒绝，不把识别成功称为无 DRM。
- 验收：先补失败行为用例；`cargo test -p crayon-media-probe -p crayon-app-runtime -p crayon-media-host -p crayon-ipc-schema`；`cargo clippy -p crayon-media-probe -p crayon-app-runtime -p crayon-media-host --all-targets -- -D warnings`；改动 Rust 文件 rustfmt check、`cargo build -p crayon-media-host --release`、repo-guard、diff check。使用本地 fixture，Fake-IP literal 在网络前拒绝；检查 skip 零请求、错误保留、无 SDK 投送、旧 prepare/commit 失效矩阵。
- 不做：在 UI 展示新原因（R03b/R08）、让代理环境公网 Direct 通过（R05/R06）、增加协议或安全权限。仅内部原因保留可达 VERIFIED，不关闭 R03 汇总或原 LAN 公网阻塞。

### R03a 完成记录与 Review

- 对象：main/cfaab39 既有 dirty 工作区上的本节 7 个 Rust 文件增量，macOS 26.6.2/arm64；没有覆盖既有 LAN/取消实现、升级依赖或改 SDK gitlink/lockfile。四个生产文件分别承载报告、re-export、规划事实和一次准备结果。
- 实现：`InspectionReport` 保留识别/未知/3xx 拒绝/上游拒绝，旧 inspect API 通过同一实现返回旧类型；`LocalPreflightStatus` 保留既有封闭网络错误与凭证/保护跳过状态。普通规划与已选设备 prepare 都返回原因，`ReadyMediaHostCastStart` 只提供只读封闭 getter，不暴露 URL/原始错误，也不把原因传给授权策略。
- 不变量：HEAD→有界 Range、405 回退、manifest 识别、网络 pin/拒绝、15 秒准备期限、取消与提交前复检均不变；没有新增网络请求/重试/日志。识别到受保护 DASH 仍拒绝；Fake-IP（含 IPv4-mapped IPv6）、未知内容及拒绝响应仍不能 Direct/Relay。旧 MHV1 不追加字段，尚不向 UI 展示原因。
- 先补用例：`cargo test -p crayon-media-probe --test inspect detailed_report` 首次 FAIL/101（E0432/E0599，缺少新报告 API，属于编译红灯，未执行行为测试）；实现后下述完整用例通过。新增 4 个测试函数并加强既有断言：HEAD/Range 两阶段的拒绝、未知与受保护内容、Fake-IP 保留、prepare→commit 不连接/不投送；既有超时、凭证/EME 零请求、旧上下文与取消矩阵复验。

| 实际命令 | 结果/退出码 | 数量与耗时 |
|---|---|---|
| `cargo test -p crayon-media-probe -p crayon-app-runtime -p crayon-media-host -p crayon-ipc-schema` | PASS/0 | 175 passed、0 failed/ignored；编译 3.56s，测试报告合计约 11.43s，工具收到最终输出 42.09s（含采集间隔，不冒充执行时长） |
| `cargo clippy -p crayon-media-probe -p crayon-app-runtime -p crayon-media-host --all-targets -- -D warnings` | PASS/0 | 3.93s |
| `cargo build -p crayon-media-host --release` | PASS/0 | 24.99s；仅编译，不是发布包或签名/公证验收 |
| `rustfmt --edition 2021 --check --config skip_children=true crates/crayon-media-probe/src/inspect.rs crates/crayon-media-probe/src/lib.rs crates/crayon-media-probe/tests/inspect.rs crates/crayon-app-runtime/src/media_planning_runtime.rs crates/crayon-app-runtime/src/media_planning_runtime_tests.rs crates/crayon-app-runtime/src/media_host_runtime.rs crates/crayon-app-runtime/src/media_host_cast_runtime_tests.rs` | PASS/0 | 7 文件，<1s |
| `cargo run --quiet -p repo-guard -- scan --root .` | PASS/0 | 9 passed、2 既有 RG-003/004 warning、RG-006 N/A；完整耗时未保留 |
| `git diff --check` | PASS/0 | <1s |

- Review：需求→正确性→API/兼容→生命周期→安全/隐私→预算→测试→维护独立检查本切片，P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。未引入持久状态、后台 worker、锁或新授权分支；一次结果保持不 Clone/Debug，取消/过期/替换后不能提交。HTTP 原始错误不格式化；当前元信息 parser 能力未扩大，识别状态不能承诺无 DRM/可解码。
- 未覆盖与风险：Windows 编译/真机、代理公网接收端首帧、UI 新原因展示、发布 artifact/正式签名/公证 NOT_RUN。本次不新增响应超限的精细分类，达到预算但无法识别仍为 Unrecognized；若面板需要更细粒度，须在 R03b 明确且不得改变原有保护判断。当前代理公网问题仍未关闭，不将 R03a VERIFIED 扩大为整个 R03/R06 完成。
- 下一步：R02b 冻结完整产品宿主迁移切片；R04 先冻结 MHV2 字节/兼容向量并拆分播放器集合实现；R05 完成外部能力缺口。然后按依赖交付 R07/R03b/R08，最后接入主 frame overlay。未提交、未推送；保留原有未提交改动。
- 文档同步后复验：复用 §9 完整 Node 命令，PASS/0，8 文档/77 本地链接及空白检查；`git diff --check` PASS/0，均 <1s。R02a/R03a 均无残留 IN_PROGRESS；此状态仅对应已记录切片，不是整套体验或一期发布完成。

## 12. R00b 用户范围调整（2026-09-03）

- 单一目标：执行“不处理代理接收端特殊情况、继续后续工作”，解除 R05/R06 的新增前置；历史失败/证据保留，不改写为成功。
- 允许：本计划、索引、LAN 计划衔接、current/cast-interaction/architecture/threat-model、PRD §4.2；禁止生产代码、SDK/代理/DNS/Keychain、协议与安全策略更改。
- 验收：上述文档本地链接/空白、`cargo run --quiet -p repo-guard -- scan --root .`、`git diff --check`；Review 原域名 Direct、移出依赖与未覆盖表述。
- 顺序调整：R07a 是旧入口独立正确性修复，只拆掉解析成功自动 Start，不新增草稿 owner，不依赖 R04/MHV2；R04/R07b 再完成多视频实例与草稿。正式网址栏仍需 R02b/c，不用私有视图或坐标覆盖。
- 验证：`cargo run --quiet -p repo-guard -- scan --root .` PASS/0，9 passed、2 既有 warning、1 N/A；`git diff --check` PASS/0。Node 本地文件链接/空白检查 PASS/0，7 文件/51 链接（七个允许文件逐一校验相对 Markdown 文件链接，忽略 http/anchor）；完整耗时未保留。首次 patch 上下文不匹配未写入，重读后修正，不影响最终检查。
- Review：仅范围/依赖与 current 衔接，P0/P1/P2/P3=0/0/0/0，APPROVE；最高 VERIFIED。没有外部调用、运行时放行或伪造真机结果。历史 R00/R01/R03a 中的 R05/R06 后续描述以本节取代。

本地文件链接与空白检查完整命令（同步 R07a current 说明后再次执行）：

```sh
node --input-type=module -e 'import fs from "node:fs"; import path from "node:path"; const files=["docs/plans/cast-experience-redesign-roadmap.md","docs/plans/README.md","docs/plans/lan-media-probe-roadmap.md","docs/current/cast-interaction.md","docs/current/architecture.md","docs/current/threat-model.md","docs/crayon-private-cast-browser-prd.md"]; let links=0; for(const file of files){const body=fs.readFileSync(file,"utf8"); if(/[ \t]+$/m.test(body)||!body.endsWith("\n")) throw Error(file); for(const m of body.matchAll(/\]\(([^)]+)\)/g)){if(/^(https?:|#)/.test(m[1])) continue; fs.accessSync(path.resolve(path.dirname(file),m[1].split("#")[0])); links++;}} console.log(`PASS ${files.length} files, ${links} links`);'
```

## 13. R07a 领取范围

- 单一目标：旧投屏码入口解析成功只更新设备列表，不调用 StartCast；用户在仍打开的 picker 点“开始投屏”才提交。解析按钮改称“查找设备”，不把解析伪称已连接。
- 输入：R01/R00b VERIFIED、CastShellController 的 ResolveCastCodeReply→SelectReceiver 自动 Start 调用链、两个平台 picker、共享 coordinator/catalog、现有独立 AppKit 测试。
- 允许：`browser/cef-shell/src/browser/media_host/cast_shell_controller.{h,cc}` 与 `tests/cast_shell_controller_test.cc`；`tests/cast_chrome_mac_test.mm` 和 CMake 中该测试 target 的 controller source；三个 `browser/shared-ui/locales/*.json` 的两个既有 key 与 generator 对应输出；`browser/shared-ui/product-strings/tests/product_strings_test.cc`；本计划/current/cast-interaction/计划索引。
- 禁止：MHV1/MHV2 字节、Rust/SDK/Relay/probe、正式窗口布局、自动网页播放、根规则、依赖和系统设置。Windows adapter 不改，原双击行为由 R08W 收口；不把本切片称为完整“仅按钮开始”跨平台验收。
- 边界：code request ID/取消/导航/host unavailable 继续失效；仅 Ready 设备可进入列表，解析中不允许旧设备被提交或并发刷新混入发现结果；普通设备最终按钮仍走同一个 StartCast/原域名 Direct 与 runtime 重验。无需视频先连接、多播放器候选与 runtime 草稿归 R04/R07b，不能因本切片通过而广告。
- 验收：先改 controller 回归复现解析自动 Start；双配置完整 `cmake --build .cache/build/macos-arm64-cef-debug -j4` / `cmake --build .cache/build/macos-arm64-cef-release -j4` 与对应完整 `ctest --test-dir <build> --output-on-failure`；新增 AppKit+真实 controller 联合测试验证查询后 sheet 保留/零 start、按钮明确提交一次和取消零 start；`node tools/locales/generate.mjs --check`、`node --test tools/locales/generate.test.mjs`、改动行 clang-format、repo-guard、diff check。

### R07a 完成记录与 Review（2026-09-03）

- 对象：main/cfaab39 既有 dirty 工作区上的本切片；macOS 26.6.2（25G83）/arm64，CEF 150.0.10。仅控制器解析/显式开始/刷新互斥、对应测试、两个既有本地化 key 的三语言生成输出及文档；Git 完整 diff 中的其他投屏、LAN、Rust、界面改动不归本切片。没有提交或推送。
- 实现：`ResolveCastCodeReply` 不再调用 `SelectReceiver`；仅 Ready 结果更新列表。查询只可在 picker 打开时发起，清空旧选项并阻止并发刷新/提交；取消、导航、旧 request ID、离线或失败回复都不能自动播放。既有最终按钮保留原 StartCast 用例，文案明确区分“查找设备”和“开始投屏”。
- 原生测试：`RunCodeLookupWithController` 通过真实 AppKit 控件与生产 CastShellController 联合验证，查找后同一 sheet 仍打开且零 start，取消零 start，点击开始仅一次 start。注入的媒体证明与命令端口属于测试 fixture，不是网页真实播放、SDK 连接或手机首帧证据。生产 target 没有加入测试代码或依赖。
- Fail-first：先构建 `crayon_cast_shell_controller_mac_test`，`ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -R '^cast_shell_controller_mac$'` FAIL/8（1.10s），命中 `log.starts.empty()`，复现原自动播放；修复后 PASS/0（1/1，1.12s）。补并发刷新用例后直接运行 `.cache/build/macos-arm64-cef-debug/browser/cef-shell/Debug/crayon_cast_shell_controller_mac_test` FAIL/1（0.302s），命中 `!controller.RefreshReceivers()`；加入互斥后该命令 PASS/0，最终完整 CTest 再覆盖。

| 实际命令 | 最终结果 | 数量 / 耗时 / 证明边界 |
|---|---|---|
| `cmake --build .cache/build/macos-arm64-cef-debug -j4` | PASS/0 | 完整 Debug build；完整耗时未保留 |
| `cmake --build .cache/build/macos-arm64-cef-release -j4` | PASS/0 | 完整 Release build；完整耗时未保留；不是发布签名/公证 |
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure` | PASS/0 | 最终代码 92/92，150.05s；controller 0.01s、原生 UI 4.50s |
| `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure` | PASS/0 | 最终代码 92/92，156.32s；controller 0.43s、原生 UI 4.53s |
| `node tools/locales/generate.mjs` | PASS/0 | 3 语言、159 key、9 输出；仅按 generator 生成 |
| `node tools/locales/generate.mjs --check` | PASS/0 | 相同数量，无漂移，<1s |
| `node --test tools/locales/generate.test.mjs` | PASS/0 | 6/6，最终单独运行 45.926ms |
| `cargo run --quiet -p repo-guard -- scan --root .` | PASS/0 | 9 passed、2 既有 RG-003/004 warning、RG-006 N/A；完整耗时未保留 |
| `git diff --check`、§12 Node 文档链接/空白命令 | 各 PASS/0 | 7 文档/51 本地链接，均 <1s；包含未跟踪文档 |

- 两套最终 CTest 用 `&&` 顺序执行，Debug 成功后才启动 Release，整体 exit 0，未用后项掩盖失败。初轮 Debug 92/92、223.93s 发生在刷新互斥补齐前，不作为最终代码证据。GUI/loopback 测试在获准的桌面进程中执行，CEF 使用 mock Keychain；没有访问可选 SecureStore 或修改代理/DNS。
- 格式检查使用 Xcode clang-format 21，仅检查改动行；以下五条实际命令均 PASS/0、<1s。没有全仓格式化。

```sh
xcrun clang-format --dry-run --Werror --lines=132:137 --lines=155:193 --lines=342:358 browser/cef-shell/src/browser/media_host/cast_shell_controller.cc
xcrun clang-format --dry-run --Werror --lines=71:72 browser/cef-shell/src/browser/media_host/cast_shell_controller.h
xcrun clang-format --dry-run --Werror --lines=315:318 --lines=369:370 --lines=375:432 browser/cef-shell/tests/cast_shell_controller_test.cc
xcrun clang-format --dry-run --Werror --lines=6:7 --lines=350:440 browser/cef-shell/tests/cast_chrome_mac_test.mm
xcrun clang-format --dry-run --Werror --lines=59:73 browser/shared-ui/product-strings/tests/product_strings_test.cc
```

- Code Review：对本切片单独自审，按需求/边界→正确性→所有权/API→生命周期→安全/隐私→性能→证据→维护检查，P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。沿用原 request ID、UI 线程和 session owner，无新协议、队列、网络请求或授权分支；生成资源维持 source-of-truth 和固定 ID。frontend-design 只用于沿用现有原生界面及明确操作层级，没有新增 UI 框架。
- 未覆盖与风险：本轮新流程的真实手机投送、Windows 构建/原生 UI、IME/读屏、正式发布 artifact/签名/公证 NOT_RUN。Windows 双击仍是既有显式开始入口，R08W 收口；多播放器/独立连接归 R04/R07b，网址栏与 overlay 仍未生产装配。Release Ninja 曾报告 `premature end of file; recovering` 后自动恢复并成功构建；既有重复库及 ad-hoc 重签名提示不等于发布验收通过。
- Roadmap：R07a VERIFIED，R07 汇总仍 TODO；下一步 R02b 产品宿主迁移设计和 R04 多播放器/协议切片，之后 R07b/R03b/R08，最后 R09/R10 覆盖层与平台真机验收。R05/R06 不再是依赖，不处理代理专项。

## 14. R02b 产品宿主迁移边界（2026-09-03）

- 单一目标：冻结公开 Views 接口的产品迁移切片，覆盖生命周期与旧功能保留；本项不改变生产默认入口。
- 输入：R02a/R07a VERIFIED、固定 CEF 150 的 BrowserView/Window/LifeSpan 公共头、TabController/TabModel、Mac/Windows Cast chrome 与现有真实 CEF 集成 target。
- 允许：本计划、计划索引、current/cast-interaction；禁止生产代码、SDK、wire、依赖、系统设置、提交/发布。验收为本地链接、`cargo run --quiet -p repo-guard -- scan --root .`、`git diff --check` 与设计 Review。

### 宿主与生命周期决定

1. 使用 `CefBrowserView` + `CefWindow`、`CEF_CTT_LOCATION`，继续由 CEF 提供原地址输入/站点身份控件；不从平台私有子视图找地址栏，不在网页注入主入口。新增导航/标签控件只调用既有 TabController 用例；TabModel 仍是逻辑标签 owner，Views 只拥有视觉对象。
2. 地址栏相邻按钮与普通视频 overlay 由同一 Browser-owned surface 承载，surface 只接收只读呈现和用户意图，不持有媒体 URL/草稿/授权或调用 SDK。
3. Chrome runtime 不调用 Alloy 的 `DoClose`，不能把最终清理放在该回调。公开 `TryCloseBrowser` 可同步/异步关闭；关闭探测前临时移出借用的 location view，返回 false 时恢复同一控件，真实销毁前解除借用，不能在 beforeunload 取消时永久拆掉导航栏。
4. 原多 BrowserView 标签映射方案已被 §18 取代：CEF 150 一个 Chrome Window 最多一个 Chrome BrowserView，不能按此实施多标签迁移。TabModel 唯一身份 owner、导航/焦点撤销原则保留；实际宿主须另行确定，不改用私有视图。
5. 迁移时必须保留后退/前进/刷新/停止、地址输入/证书反馈、标签/弹窗/菜单、下载、MDV、窗口及常用快捷键。缺少实装与对应验证前不切换产品默认宿主，不以单页原型替代完整窗口。

### 后续切片（均领取前再补具体增量范围）

| 切片 | 初始状态 | 依赖 | 单一目标 / 文件边界 | 验收重点 |
|---|---|---|---|---|
| R02b1 | IMPLEMENTED | R02b VERIFIED | `browser/cef-shell/src/browser/window/chrome_location_bar.{h,cc}` + 独立 CEF 宿主测试/CMake；生产可复用地址组件与相邻动作布局及借用生命周期，不切换产品默认宿主 | 地址栏阶段 Debug 93/93；Release 回归无输出后中断，需补跑，不记 VERIFIED |
| R02b2 | BLOCKED | R02b 修订、R02b1 VERIFIED | 原多 Chrome view 设计不可用；先确定替代宿主范围 | §18 公共契约/失败实验，默认窗口未迁移 |
| R02b3M | TODO | R02b2 VERIFIED | macOS AppKit/Views 句柄、主菜单/输入监听/MDV/下载/投屏面板装配；真实产品入口迁移 | 双配置完整 CTest、原生焦点/IME/快捷键/生命周期 |
| R02b3W | TODO | R02b2 VERIFIED | Windows HWND/Views 句柄、系统菜单/输入监听/MDV/下载/投屏面板对称装配 | Windows 双配置、缩放/键盘/窗口生命周期 |

R02cM/W 只聚合对应宿主验收；R08 再交付最终投屏面板。R04 多播放器及 MHV2 与宿主迁移独立，不新增代理/接收端代检依赖。所有平台证据分别记录；本地 Mac 验证不替代 Windows。

- R02b 验证：main/cfaab39 dirty 增量，macOS arm64、纯设计；`cargo run --quiet -p repo-guard -- scan --root .` PASS/0（9 passed、2 既有 warning、1 N/A），`git diff --check` PASS/0；本计划/索引/current cast 本地 Markdown 链接与空白校验 PASS/0，3 文件/40 链接，均完整耗时未保留或 <1s。Review 按标准独立自审，P0/P1/P2/P3=0/0/0/0、APPROVE，最高 VERIFIED；真实窗口/平台验证 NOT_RUN。只解锁内部组件，不广告产品能力。

### R02b1 领取范围

- 单一目标：将 LOCATION 借用布局移入可复用生产组件，并以真实 CEF 验证取消关闭时可恢复、最终关闭无悬垂引用；保持默认产品宿主不变。
- 允许：新增 `browser/cef-shell/src/browser/window/chrome_location_bar.{h,cc}`；既有 `tests/cast_toolbar_host_probe.{h,cc}` 改为消费组件并加本地 beforeunload 场景；`tests/page_snapshot_cef_integration_mac.mm` 仅模式/结果分发；CMake 加入生产/测试 source 和独立 CTest；本计划/索引/current cast 实施状态。
- API/边界：CEF UI 线程专用、单 parent/single BrowserView；只拥有相邻布局及借用的地址 View，不拥有播放/路由/草稿。临时移出与恢复用于同步/异步关闭探测，Detach 幂等。禁止任何 private view 遍历、任意 URL 页面操作入口或按坐标放置生产控件。
- 验收：双配置完整 `cmake --build .cache/build/macos-arm64-cef-debug -j4` / `cmake --build .cache/build/macos-arm64-cef-release -j4`、对应完整 CTest；`cast_toolbar_host_probe` 与 `cast_toolbar_close_probe` 使用 about:blank 测试文档、测试专用输入及 beforeunload 回调，验证取消后原 location/按钮仍可用，最终关闭释放；新组件/测试 clang-format、repo-guard、diff check。
- 不做：本切片不换产品默认窗口、不实现标签或媒体选择；不改变 CEF/SDK/协议/系统设置，不使用网页播放行为作为测试便利。不将测试输入当成真人媒体证明。Windows 真机证据后续独立取得。

### R02b1 实现与阶段验证

- 生产 `ChromeLocationBar` 只拥有 LOCATION 借用布局；UI 线程、同窗口、重复挂载拒绝、临时释放/恢复和幂等 Detach。外部动作按钮在 Detach 时显式移出，允许复用；CEF 提供的地址框本身带有内部 parent/attached 状态，按固定版本 cefclient 的公开 LOCATION 模式挂入，不遍历私有视图。
- 独立 CEF 测试消费生产组件，覆盖 1040/720 DIP、灰态/可用态、空/重复操作、拆除后复用、overlay 显隐销毁；新增真实 beforeunload 先取消再允许关闭，核对同一个地址控件、按钮和布局恢复。测试输入仅操作 about:blank 关闭 fixture，不生成媒体证明。超时强制关闭仅存在于独立测试。
- 最终两个完整 build 命令均 PASS/0；最终 Debug 完整 CTest PASS/0，93/93、206.02s；Release 最终完整 CTest 仍运行，代码冻结，不影响后续独立输入证明模块。最初新边界测试 FAIL/8（Debug 91/93、259.00s），发现 Detach 未解除动作按钮 attachment；修复后专项 `ctest --test-dir .cache/build/macos-arm64-cef-debug -V -R '^cast_toolbar_(host|close)_probe$'` PASS/0、2/2、4.47s，最终完整 Debug 再通过。初次挂载失败和 GetParent API 编译错误均已修复，不作为通过证据。
- 本节对象为 main/cfaab39 dirty 增量、macOS 26.6.2/arm64、CEF 150.0.10。正式产品窗口未迁移；Windows、真实媒体 overlay、设备投送、IME/VoiceOver、发布签名/公证 NOT_RUN。没有提交推送、Keychain/代理或依赖调整。Release 既有 Ninja recovery/重复库/ad-hoc 签名提示已保留，不冒充发布验收。

## 15. R04 多播放器切片与 R04a 领取（2026-09-03）

R04 拆为 a Browser 逐播放器证明隔离、b Browser 分配实例/换源 revision 与移除、c MHV2 字节/双语言 codec、d runtime 实例集合/分页与 host 装配；b/c/d 领取前冻结各自完整输入/字段/验收。汇总仍 TODO，不用 a 替代完整多视频 UI。

- R04a：IMPLEMENTED（2026-09-04 §17 更新：原生 unit 已恢复执行且通过，不再归因为历史审批/加载阻塞；完整媒体导航集成仍失败，未 VERIFIED。§16 保留此前启动失败的历史证据）。单一目标是在 Browser 观察入口按当前 tab/navigation/element/source 保存独立输入/进度基线，禁止播放器 A 的暂停、进度或旧源证明影响 B。依赖 R01 VERIFIED；与 R02b1 布局代码不重叠。
- 输入：`InputProofGate`、`CefObservationBridge`、renderer 媒体 DTO/collector、既有输入/观察测试；当前 collector 实际上限为 16，不是 128。128 是后续目标上限，本切片不提高现有采集数量。
- 允许：`browser/cef-shell/src/browser/input_proof/player_input_proof.{h,cc}` 与独立 `_test.cc`、该目录 CMake；`input_proof_gate.h` 仅撤销输入且保留进度的接口；`browser/observation_gateway/cef_observation_bridge.{h,cc}` 接入/导航/关闭清理；本计划/索引/current cast。
- 冻结边界：沿用现有封闭 ProofResult 和真实输入入口；每页最多 16、全局最多 256，满载拒绝新项并计数，URL 使用现有 2048 字节上限且仅在内存。未观察过的元素不继承历史点击；已知源改变撤销输入。标签切换撤销输入但保留进度基线；导航/关页清理。只支持已验证主 frame，网络事实不进入播放证明。
- 验收：独立 unit 覆盖同 URL 两元素、A 暂停不放行 B、B 的进度不替 A 推进、新元素/换源/导航/前后台、非法数字/超限与容量；双配置完整 build/CTest（包含现有真实 CEF 单媒体/拒绝/导航 fixture）；clang-format、repo-guard、diff check。新增测试先执行编译/行为红灯并如实区分。
- 不做：不改 MHV1 字节、Runtime URL store、路由/SDK/播放策略、DOM 自动操作或默认宿主；不把逐播放器 gate 当作候选分页、多视频面板或全帧真实性已完成。MHV2/实例 ID/列表撤销与最终提交重新验证仍是 b/c/d 的后续门禁。

### 本轮最终证据与 Review（R02b1 / R04a）

对象：main/cfaab39 dirty 增量，macOS 26.6.2/arm64，CEF 150.0.10。保留其他既有改动，没有提交或推送。R02b1 的 93 项测试发生在 R04a 编译装配之前，不能用于广告 R04a 通过。

| 实际命令 | 结果 / 证明边界 |
|---|---|
| `cmake --build .cache/build/macos-arm64-cef-debug -j4 && cmake --build .cache/build/macos-arm64-cef-release -j4` | 最终 PASS/0，两套均包含 R04a 生产入口和测试；完整耗时未保留，未用后项掩盖前项失败。仅构建，不是运行/发布证据 |
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure` | R02b1 阶段 PASS/0，93/93、206.02s；host/close 两专项 0.56s/0.47s。R04a 增量后的完整 94 项 NOT_RUN |
| `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure` | R02b1 阶段前 15 项通过，第 16 项开始后长期无输出；对本次启动会话发 Ctrl-C，exit 130。没有完整通过结果；R04a 增量后的 94 项 NOT_RUN |
| `xcrun clang++ -std=c++17 -fsyntax-only -I browser/cef-shell/src browser/cef-shell/src/browser/input_proof/player_input_proof_test.cc` | 新模块实现前 FAIL/1，缺少新头文件；这是编译红灯，不是已执行的行为复现 |
| `cmake --build .cache/build/macos-arm64-cef-debug --target crayon_player_input_proof_test -j4` | PASS/0，独立测试已编译。另一次临时独立编译/运行无输出后已 Ctrl-C/130，不计通过 |
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure --timeout 15 -R '^player_input_proof$'` | 沙箱运行 TIMEOUT/8、15.03s、0/1；超时清理伴随 `sh: /bin/ps: Operation not permitted`。原生复核同命令未启动，自动审批服务返回 `404 Not Found`；不把超时武断归因为代码或环境 |
| 新组件、新测试 `xcrun clang-format --dry-run --Werror`；`git diff --check` | 最终 PASS/0；既有文件仅格式化改动行，无全仓格式化 |

- R04a 实现：独立 element/source 进度基线，已观察项才记录输入；换源/换 frame/导航/关页撤销，前后台切换撤销输入而保留“已在播放”的基线。满载拒绝新项、计数饱和保护；不跨播放器传播暂停/进度，不让网络事实生成播放证明。新增五组 unit 覆盖同 URL、换源、上下文与边界，但尚未运行通过。
- R02b1 自审按需求→正确性→所有权/API→生命周期→安全→性能→验证→维护顺序完成，代码 Review P0/P1/P2/P3=0/0/0/0、APPROVE；最高仍 IMPLEMENTED，因规定 Release 运行门禁未完成。frontend-design 指导沿用原生地址框与已有动作控件，不新增 UI 框架。
- R04a 自审：P0/P1/P2/P3=0/1/0/0、REQUEST_CHANGES；P1 是新增证明路径的规定运行证据缺失/超时未定位，关闭前不合并或宣称可用。额外核对首个观察晚于用户输入、动态换源、blob/MSE 和跨站导航的现有 CEF 场景，必要时修正测试的观察就绪顺序，不能通过复用历史点击放宽生产门禁。
- 未覆盖与风险：R04a 的实际运行正确性及整链回归未证实；Runtime 仍是旧 URL 级 store，故本项不能宣称已完全消除跨实例借用或实现多视频选择。正式 Views 窗口、网址栏最终入口、多视频草稿/MHV2、元素几何绑定与播放器按钮、Windows、IME/VoiceOver、真实接收端均未完成本轮验收；悬浮原语不等于生产播放器悬浮入口。
- 当前阻塞：只读进程诊断及有界原生 unit 执行分别被自动审批服务 404 拒绝；未绕过。已停止本次两个无输出测试会话；继续构建/静态检查，不访问 Keychain、不改代理/安全设置。需要恢复/批准原生测试执行后，先收敛 R04a 超时和两套 94 项回归，再推进 R04b/c/d 与 R02b2；依赖未通过不接最终投屏入口。

## 16. 三项入口优先实施与 R04a 复验（2026-09-04）

- 用户要求：优先实施地址栏常驻入口、多视频选择、播放器悬浮入口。沿用本计划，不扩张代理/SDK/发布范围；先恢复 R04a 与 R02b1 运行验证，再推进 R02b2/3、R04b/c/d、R07b/R03b/R08，最后 R09/R10。平台证据仍分别取得。
- 被审对象：`main@cfaab39` 既有 dirty 工作区，macOS 26.6.2（25G83）arm64。本轮只更新计划/current 状态说明，未修改既有生产或测试代码，未提交/推送；未把历史通过证据套用于当前增量。
- R04a 从 BLOCKED 重新领取为 IN_PROGRESS；复验后恢复 BLOCKED。沙箱外执行审批本轮已成功，不能继续归因为历史 `404 Not Found`。

| 实际命令 | 结果与证明边界 |
|---|---|
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure --timeout 15 -R '^player_input_proof$'` | 沙箱内 TIMEOUT/8，0/1、15.03s，清理伴随 `sh: /bin/ps: Operation not permitted`；获准沙箱外复跑仍 TIMEOUT/8，0/1、15.04s，无该权限错误 |
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure --timeout 60 -R '^player_input_proof$'` | TIMEOUT/8，0/1、15.05s；该测试自身 CMake `TIMEOUT 15` 优先于 CLI 默认，未实际扩大为 60 秒，也未修改门禁 |
| `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure --timeout 15 -R '^(player_input_proof\|input_proof_gate)$'` | 获准沙箱外 TIMEOUT/8，0/2、30.10s；已有 `input_proof_gate` 与新增 `player_input_proof` 均超时 |
| `.cache/build/macos-arm64-cef-debug/browser/cef-shell/src/browser/input_proof/crayon_player_input_proof_test`；`sample 61296 1 1` | 直接启动无输出；采样 PASS/0，876 个样本全部停在 `_dyld_start`，内存 footprint 96K，尚无 binary images；本次测试进程用 Ctrl-C 停止，exit 1，不计通过 |
| `xcrun clang++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I browser/cef-shell/src browser/cef-shell/src/browser/input_proof/input_proof_gate.cc browser/cef-shell/src/browser/input_proof/player_input_proof.cc browser/cef-shell/src/browser/input_proof/player_input_proof_test.cc -o .cache/r04a-proof-recheck` | PASS/0，独立重编译以排除既有构建产物差异；完整耗时未保留，不是执行证据 |
| `.cache/r04a-proof-recheck`；`sample 62688 1 1` | 新编译程序仍无输出；采样 PASS/0，883 个样本全部停在 `_dyld_start`、96K；本次测试进程用 Ctrl-C 停止，exit 1，不计通过 |
| `codesign --verify --verbose=2 .cache/build/macos-arm64-cef-debug/browser/cef-shell/src/browser/input_proof/crayon_player_input_proof_test`；`codesign --verify --verbose=2 .cache/r04a-proof-recheck` | 均 PASS/0，valid on disk / satisfies its Designated Requirement；只证明本地签名校验，不等于已启动或发布签名通过 |

- 诊断结论：已定位为进入测试主体之前的启动/加载阶段阻塞；不能据此判定播放器算法通过，也不能仅凭 `_dyld_start` 指认某个系统服务为根因。已有单测同样受影响，单纯重新编译没有解除阻塞。未禁用系统安全服务、移除安全属性、改代理/DNS/Keychain 或扩大权限边界。
- 未覆盖：R04a 五组行为用例实际结果、当前增量双配置完整 94 项 CTest、R02b1 Release 验收以及三项入口的生产装配/平台/设备验收。运行前置未通过，不领取依赖它们的后续实现，不将原型或组件宣称为产品完成。
- Review：本轮证据/状态文档核对无新增代码发现；R04a 既有 P1（规定运行证据未闭合）保持 1，P0/P1/P2/P3=0/1/0/0，REQUEST_CHANGES，最高 BLOCKED。需要本机启动环境恢复后继续，不因用户调整优先级降低安全或验证门禁。
- 文档验证：`cargo run --quiet -p repo-guard -- scan --root .` PASS/0（9 passed、既有 RG-003/004 两项 warning、RG-006 N/A，完整耗时未保留）；`git diff --check` PASS/0（0.20s）；复用 §12 Node 链接/空白命令，文件数组限定本计划、计划索引与 current/cast-interaction，PASS/0（3 文件/40 链接、0.08s）。只读进程复核未见本轮 CTest/播放器单测残留；重新编译的诊断产物仅在忽略的 `.cache`，不进入 Release。

## 17. 入口 UI 代码先行（2026-09-04，用户明确授权）

不改变 R04a 的 BLOCKED 事实，但独立 UI 编码只依赖已冻结的 R01/R02b 契约。两个内部切片串行领取，均属于既有 R08/R10，不新增顶层任务；最终平台装配仍由 R02b3/R08W/M/R10W/M 验收。

### R08u1：共享选择面板呈现与闭合意图

- 状态：VERIFIED；依赖 R01/R02b VERIFIED 及本次用户代码先行决定。共享库/测试 Debug、Release 均编译通过；两配置 `cast_selection` 各 1/1 已运行通过，不把此前其他程序启动超时套到新测试。完整证据见本节收尾。
- 单一目标：实现地址栏/选择面板/覆盖层共用的只读 UI 投影与意图校验，不创建真实候选、草稿或投送授权。仅确认提交按钮可以生成 Commit 意图；选视频、选设备、解析投屏码、悬浮按钮不能调用 StartCast。
- 输入：current/cast-interaction、现有 CastUiCoordinator/locale catalog；UI Context 明确绑定 Browser session/Profile/tab/navigation/generation，播放器使用实例/source revision，列表与 draft 各有 revision。绑定上下文只能由 Browser owner 显式调用，不能由任意快照切换。
- 允许：`browser/shared-ui/features/cast/include/crayon/browser_cast_view/cast_selection.h`、`src/cast_selection.cc`、`tests/cast_selection_test.cc`、该模块 CMake；三语言 catalog 与 generator 输出；本计划/current cast/索引。不修改旧 CastShellController、播放证明、Rust/MHV1/MHV2、SDK 或系统设置。
- 边界：每个列表页至多 16 项、集合 256、标题 128 UTF-8 字节、设备 ID 128 字节、文本拒绝控制字符/双向覆盖/非法 UTF-8；无媒体 URL/HTML/JS、无日志/网络/持久化。旧 revision/缺少协议能力/过期准备/失效选择一律拒绝提交；会话停止独立于新候选数。UI 不自行预选或生成权限，渲染零命令，runtime 必须复验所有意图。
- 验收：新独立 C++17 行为测试覆盖零/一/多项、同 URL 不作为身份、更新不换选中项、换源/上下文/旧 revision/重复提交、过期、容量/文本边界和停止；双配置共享库/测试 build，clang-format、定向 CTest、repo-guard、diff check。运行阻塞如实记录，不以编译代替通过。
- 不做：MHV2 codec/runtime 草稿、真实设备投送、默认窗口迁移和平台 UI 验收。

### R08u2：CEF 原生三入口 surface

- 状态：VERIFIED（独立 UI 组件，不是默认产品装配）；代码依赖 R08u1、现有 R02b1 组件代码与 R02b VERIFIED；不切换默认宿主或旧 MHV1 到新投送路径。
- 单一目标：以公开 CEF Views 实现地址栏后常驻按钮、视频/设备选择面板、主 frame 视频内覆盖按钮；三入口同一呈现/意图 port。新 surface 消费共享投影，不直连 SDK 或旧 StartCast。
- 允许：`browser/cef-shell/src/browser/media_host/cast_entry_surface.{h,cc}`、该目录下独立 panel/overlay 实现（按职责拆分）、`browser/cef-shell/tests/cast_entry_surface_probe.{h,cc}`、既有 CEF test 模式分发、CEF CMake；本计划/current/索引。复用 ChromeLocationBar 与现有系统字体/design token，不新增依赖或 Web UI。
- 边界：Browser UI thread、公开 LOCATION/overlay API、灰态可解释、按钮/列表/错误纯文本、分页有界、单一明确提交、取消/导航/Detach 释放控件与旧回调；覆盖层只接受绑定当前实例/context/revision 的可见主 frame 几何，裁剪到 viewport，未知/过期/布局改变立即隐藏，不以点击产生播放证明。
- 验收：双配置完整 build 与独立真实 CEF surface Harness（地址相邻/窄窗/空态/多项选择/渲染零命令/显式提交/取消/过期几何/Detach）；clang-format、repo-guard、locales generator/test、diff check。若 CEF 启动仍受阻，保留 IMPLEMENTED 与 NOT_RUN/TIMEOUT，不再阻止其他独立代码开发。
- 不做：默认产品多标签宿主迁移、iframe/Shadow DOM/全屏/PiP/保护 surface、代理/SDK/协议升级、真实播放与发布声明。

### 实现与专项证据

- 被审范围：`main@cfaab39` dirty 增量的 R08u1/u2 新文件及对应 CMake/locale/测试分发/文档行；macOS 26.6.2 arm64、CEF 150.0.10、C++17 共享层。其他既有未提交改动保留，不把工作区整包当作本次变更；无提交、推送、SDK/依赖/系统设置变更。
- 共享呈现：只读候选/设备分页及显式选中摘要、session/profile/tab/navigation/generation 与实例/source/view/draft revision 校验、过期拒绝、重复提交抑制、取消后迟到响应不重开原草稿、无候选时已有会话仍可停止。非法 UTF-8/控制字符/重复身份/超额分页拒绝；没有 URL、HTML、JS、SDK 或 route override。
- CEF 控件：公开 LOCATION 后 96×48 DIP 常驻按钮与可投数量；原生视频/设备分区列表，同名视频按列表序号区分、稳定实例决定选择；投屏码查询/连接/准备均不启动，唯一“开始投屏”显示视频→设备摘要；主 frame 内 96×36 DIP 浮层仅发 OpenForMedia，导航/布局/几何超期撤销。使用现有系统字体/主题、36 DIP 命中高度、8 DIP 间距；frontend-design 指导保持原生紧凑、单一明确提交，不增加 Web UI 框架。
- 生命周期：输入回调不销毁当前原生 widget；呈现更新按 CEF UI task 合并，外部意图最多一个待派发，弱引用且 Detach 后丢弃。Esc/Tab/Shift+Tab 使用独立 accelerator，取消恢复入口焦点。Host 可经 `GetView` 路由自有控件的焦点/辅助功能，不扫描 Chromium 私有控件。读状态/渲染不产生外部命令；host 必须转发布局与 accelerator、按 50ms 上限调用 Tick、在导航绑定新上下文并在关闭前 Detach。
- 接线边界：新 surface 已纳入 Windows/macOS 生产 source list，但默认产品宿主尚未创建它；独立 CEF Harness 消费真实生产 surface 与测试 owner。`compatible` 默认 false，必须由后续真实 MHV2 adapter 显式置位；禁止临时转接旧 `CastShellController::SelectReceiver` / 最后候选 StartCast。

| 实际命令 | 结果与证明边界 |
|---|---|
| `cmake --build .cache/build/macos-arm64-cef-debug --target crayon_cast_selection_test -j4`；Release 同路径替换配置 | PASS/0，两个配置的共享呈现库与 unit 均编译；完整耗时未保留 |
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -R '^cast_selection$'` | PASS/0，1/1，2.23s；独立程序内 5 组行为覆盖，不等于设备投送 |
| `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure -R '^cast_selection$'` | PASS/0，1/1，0.43s |
| `cmake --build .cache/build/macos-arm64-cef-debug -j4`；`cmake --build .cache/build/macos-arm64-cef-release -j4` | 最终两配置 PASS/0，完整耗时未保留；包括新 surface/独立 Harness。初次 CEF API 与 locale 链接编译错误已修正，不计通过；既有重复库/ad-hoc 签名/Ninja recovery 提示不是发布证据 |
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -R '^cast_entry_surface_probe$'` | 最终 PASS/0，1/1，3.89s。验证空态常驻、地址相邻、宽/窄窗口、多项选择零隐式提交、显式提交防重复、取消、浮层预选、Esc、过期、导航与幂等 Detach；仅 about:blank + 测试 DTO/原生键盘输入，无播放证明或接收端命令 |
| 同一 CEF 专项开发期失败 | FAIL/8：6.91s 空 SetText 的 CEF DCHECK、3.62s 原生键盘处理期间同步销毁 widget 的 SIGSEGV；随后原生控件销毁/外部 callback 延后至 UI task 修复。19.20s/19.98s/18.61s/19.74s 为 Harness 有界 readiness 失败，不是通过；修正窗口 active 假设及跨 overlay widget 的主窗口模拟鼠标限制，最终使用真实原生焦点/按键并验证精确几何。未宣称物理鼠标已验收 |
| `node --test tools/locales/generate.test.mjs`；`node tools/locales/generate.mjs --check` | PASS/0，6/6、43.36ms；三语言 180 keys / 9 generated files 一致。新增 21 条选择面板本地化文案，原有键保留 |
| 新增 7 个 C++ 头/源/测试文件 `xcrun clang-format --dry-run --Werror`；`git diff --check` | PASS/0，完整耗时未保留；不执行无关全仓格式化 |
| `cargo run --quiet -p repo-guard -- scan --root .` | PASS/0，9 passed、既有 RG-003/004 warning、RG-006 N/A；新 cast_selection/cast_entry_surface 文件无 finding；完整耗时未保留 |

- 未覆盖：默认产品 Views 多标签宿主/MHV2 实例集合与 runtime 草稿接线、Windows 真机与打包、原生物理鼠标/触屏命中、IME/VoiceOver/高 DPI/多显示器、真实滚动与播放器几何 adapter、iframe/Shadow/全屏/PiP/保护表面及接收端播放。分别由 R02b2/3、R04b/c/d、R07b/R08W/M、R09/R10W/M 收口；本次不把组件 VERIFIED 升为完整产品 DONE。

### 完整回归与最终 Review

| 实际命令 | 结果 |
|---|---|
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure` | FAIL/8，95/96，82.74s；`cast_selection` 与三入口/LOCATION/关闭专项全部通过，`cast_entry_surface_probe` 0.72s；唯一失败为 `page_snapshot_cef_integration` 的 `media-navigation` 场景（35.58s） |
| `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure` | FAIL/8，95/96，92.02s；同一 `media-navigation` 场景失败；新 `cast_selection` / `cast_entry_surface_probe` 分别 0.56s / 0.60s 通过，LOCATION/关闭专项通过 |
| `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure --repeat until-fail:3 -R '^(cast_selection\|cast_entry_surface_probe)$' && ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure --repeat until-fail:3 -R '^(cast_selection\|cast_entry_surface_probe)$'` | PASS/0，两配置各 2/2 tests × 3 次，共 12 次执行全部通过；Debug 2.02s、Release 1.73s。重复验证新控件输入与窗口释放，不是长稳/真机媒体验收 |

- R04a 证据更新：Debug/Release `player_input_proof` 均 PASS（0.39s/0.41s），旧 `input_proof_gate` 也均 PASS（0.39s/0.46s）。不再归因为历史 `_dyld_start` 启动阻塞；R04a 当前 IMPLEMENTED，完整整链仍有 P1：媒体导航后 `media_received=31 / media_current=31 / media_denied=31 / media_eligible=0`。UI surface 仅由新测试模式创建，未接入该旧场景或默认宿主；本次没有修改证明门禁。后续须在 R04a 查清 fixture 输入/观察时序与生产导航生命周期，不能通过放宽播放证明消除失败。
- R08u1 自审：需求/边界→正确性→架构/API→生命周期→安全→性能→测试→维护全部核对；P0/P1/P2/P3=0/0/0/0，APPROVE，最高 VERIFIED。共享 UI 不授予播放、不创建运行时草稿、不解析媒体 URL，提交与取消仅局部防重，真实后端需复验。
- R08u2 自审：同序核对公开 CEF/线程约束、输入重入、合并呈现、弱引用、最大 16 overlays、最大 16 条/页、导航及 Detach 回调撤销、真实 CEF 键盘/几何/关闭结果；开发期 DCHECK/UAF 已修复并回归通过。P0/P1/P2/P3=0/0/0/0，APPROVE，仅针对本次独立 UI 增量，最高 VERIFIED；物理输入、默认产品接线和平台矩阵由前述任务验收。
- 工作区整体不宣称可合并或全绿：R04a 的媒体导航 P1 未关闭，整体仍 REQUEST_CHANGES。组件完成不解除默认产品迁移、MHV2、runtime 草稿与实际播放器几何映射门禁。
- Roadmap：R08u1/u2 VERIFIED，R08/R10 顶层仍待产品装配；R04a 从历史 BLOCKED 更新为 IMPLEMENTED/待集成修复。下一任务为 R04a 导航集成修复与既有 R02b2/3、R04b/c/d 接线切片，继续保留 Windows 首发优先和 macOS 证据不可替代 Windows 的边界。
- 文档验证：本计划/current cast/计划索引 3 个文件、40 个本地链接与行尾空白检查 PASS/0；`git diff --check` PASS/0。只更新本次范围的说明，不改写历史失败为通过。

## 18. Mac 可执行闭环收口（2026-09-04）

### R04c1 MHV2 握手字节与双端 codec 领取（后续连续实施）

- 状态：VERIFIED（纯握手 codec）；启动延迟后原样复核通过，完整过程保留在下文，不等于产品握手/能力已启用。单一目标：实现独立 MHV2 Hello/Welcome 的严格双语言 codec 和无副作用的 Welcome 匹配校验，为后续实例/草稿协议提供版本与预算边界。依赖 R01 VERIFIED 的独立 v2 迁移决定；该纯协议切片不消费 R04a/b2 的在建播放证明，不依赖尚未决定的窗口宿主，不因其通过启用产品能力。
- 输入：current/cast-interaction §5、既有 Rust/C++ MHV1 codec、真实 media-host reader 与 CEF adapter 调用方；沿用现有外层长度 framing，不修改 MHV1 字节或数字含义。
- 允许：新增 `crates/crayon-ipc-schema/src/media_host_v2.rs` 与 re-export、独立 contract test；新增 IPC `media_host_v2_codec.h/.cc`、独立 test target/CMake；共享纯文本 golden、current 契约和本 Roadmap/索引。禁止修改 media-host/runtime/默认 UI/SDK/依赖/旧 MHV1 实现，禁止接入任何网络、设备或页面行为。
- 字节冻结：8-byte header=`MHV2`+u16 BE version 2+u8 kind（1 Hello、2 Welcome）+u8 flags 0；body 固定为 Browser session ID u64、host generation u64、supported/selected capability mask u32、max frame bytes u32、max page items u16，均 BE，共 34 bytes。session/generation 非零；frame 34..16384、page 1..16。mask 位 0/1/2/3 分别为实例只读、草稿、显式设备连接、停止；未知位拒绝，零能力合法且不能启动投屏。这里只定义位，不代表已有实现支持。
- Welcome 校验：消息类型、非零身份、echo 的 session/generation 必须匹配；selected bits 是 Hello supported bits 子集，预算不大于 Hello。纯匹配不创建 session/grant 或播放权限；后续 runtime 在已认证本机连接上完成一次握手、逐命令复核实际能力和失效，未定义共同子集时断开，不自动回退 MHV1。
- 验收：先补新 API 缺失的编译红灯；两语言消费同一固定 golden，覆盖 Hello/Welcome round-trip、每字节截断、超限/未知 version/kind/flags/bit、零身份、非法预算、错 session/generation、能力扩大和预算扩大、双向 v1/v2 拒绝；原 MHV1 contract 不变。Rust fmt/clippy/contract、C++ Debug/Release 独立 build/CTest（无 GUI）、guard/diff/文档链接。运行受阻保留证据，不宣称 VERIFIED。
- 不做：实例事实/分页与草稿消息（R04c 后续切片）、握手 owner/副作用分发（R04d/R07b）、默认产品接线和真机投送。规模预计 3 个新生产文件，不整体复制旧千行 codec。
- 开工自审：固定长度/无字符串/无动态枚举解释、版本拒绝和预算双端校验；不扩大现有运行数据流。P0/P1/P2/P3=0/0/0/0、APPROVE 进入实现，非产品验收结论。

#### R04c1 实现、证据与 Review

- main/cfaab39 dirty、macOS 26.6.2 arm64；新增 254 行生产 codec/头文件，lib.rs 仅模块导出，独立测试 target 不进入产品。三组共用 golden 包含 Hello、Welcome、u64 高位/最小预算/零能力；额外独立 Node 按固定 offset 与大端解析三组向量 PASS/0，不替代原生 codec 测试。
- 红灯：`cargo test -p crayon-ipc-schema --test media_host_v2_contract --no-run` 首次 FAIL/101，缺少新模块；实现后 PASS/0、16.20s。C++ 首次 build FAIL/1，为新测试遗漏旧 v1 API 必填 error 参数，补 `nullptr` 后构建通过。未修改旧 codec 或任何拒绝断言。
- 最终 Rust：`cargo test -p crayon-ipc-schema --test media_host_v2_contract --no-run` PASS/0、19.68s；`cargo clippy -p crayon-ipc-schema --tests -- -D warnings` PASS/0、2.46s。此前 `cargo test -p crayon-ipc-schema --test media_host_v2_contract --test media_host_v1_contract` 中旧 v1 3/3 PASS、执行时间 0.00s，新 v2 启动后无输出；该批次 TERM 中断/143，不是整体通过。测试/向量补高位边界后重新编译并冻结，最终直接 `perl -e 'alarm 45; exec @ARGV; die "exec failed";' target/debug/deps/media_host_v2_contract-847408bc75c14a2b --nocapture` TIMEOUT/142、45s，无测试输出。没有把旧 binary 或只构建结果算作最终行为验证。
- C++ 双配置：`cmake --build .cache/build/macos-arm64-cef-debug --target crayon_media_host_v2_codec_test crayon_cef_shell_ipc_test -j2` 与对应 `macos-arm64-cef-release` 命令均 PASS/0；最终测试补边界后两配置再次 `--target crayon_media_host_v2_codec_test -j2` PASS/0，完整耗时未保留。Release 既有 Ninja log recovering warning 保留，不等于失败或发布签名证据。
- `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure --timeout 20 -R '^(ipc_channel_contract|media_host_v2_codec)$'` FAIL/8、0/2、35.09s；Release 对称命令 FAIL/8、0/2、35.07s，均启动无输出后 TIMEOUT。最终边界向量重编译后两配置 `ctest --test-dir <对应 build 目录> --output-on-failure -R '^media_host_v2_codec$'` 仍 TIMEOUT/8、0/1，Debug 15.04s、Release 15.05s；未延长原 15s 门禁。本轮没有启动 GUI 或设备测试。
- `rustfmt --edition 2021 --check crates/crayon-ipc-schema/src/media_host_v2.rs crates/crayon-ipc-schema/tests/media_host_v2_contract.rs`、三个新增 C++ 文件的 `xcrun clang-format --dry-run --Werror`、`git diff --check` PASS/0。`cargo run --quiet -p repo-guard -- scan --root .` 本轮最终 PASS/0（9 passed、既有 RG003/004 warning、RG006 N/A，耗时未完整保留），不覆盖前次中断记录。
- 按标准独立自审：边界和旧版不变；所有截断在读取前检查、固定长度无输入字符串/无界分配；C++ enum 编码也拒绝非法 cast；能力和预算各有独立扩大拒绝向量；无锁/线程/网络/日志/额外依赖。无已确认代码缺陷；P0/P1/P2/P3=0/1/0/0、REQUEST_CHANGES，P1 为两语言规定运行证据仍缺失，最高 IMPLEMENTED。不把协议代码存在宣称为产品握手、runtime 多视频集合或草稿完成。
- 下一步：先恢复/定位有界原生单测启动，再关闭 R04c1/R08u2r/R04a/b2 验证；后续 R04c 消息、R04d/runtime、R07b 草稿按依赖串行。默认宿主 R02b2 仍需替代方案范围决定；没有修改 CEF/SDK/系统设置、凭证或外部仓库。

R04c1 最终补证：只读系统日志出现对应测试程序的 syspolicyd 评估/provenance 记录后，对同一冻结二进制/向量原样复核，未重签名、改设置或放宽超时。日志只提供相关时间线，不能据此确定系统内部根因。

- 上述新旧 C++ 两项 `ctest ... --timeout 20 -R '^(ipc_channel_contract|media_host_v2_codec)$'` Debug PASS/0、2/2、0.08s，Release PASS/0、2/2、0.04s。
- 上述最终 Rust `perl ... --nocapture` 同一 45s 上限下 PASS/0、3/3，实际用例 0.00s、启动总耗时未完整保留。旧 v1 Rust 3/3 和全部固定字节保持原证据；新测试没有公网或真实用户数据。
- `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure --timeout 30 -R '^(player_input_proof|input_proof_gate|media_observer|media_collector_lifecycle|observation_gateway)$'` PASS/0、5/5、0.10s，关闭此前这五项的启动复核缺口，不替代 GUI/完整回归。
- 最终 R04c1 独立自审 P0/P1/P2/P3=0/0/0/0、APPROVE；本项纯协议规定运行门禁已补齐，最高 VERIFIED。R08u2r/R04a/b2 的完整平台验证与默认宿主/MHV2 后续消息/runtime/草稿/真机仍分别待办，不随之提升。

#### Mac 一期本轮无 GUI 回归补证

- `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -E '^(page_snapshot_cef_integration|cast_toolbar_host_probe|cast_entry_surface_probe|cast_toolbar_close_probe|cast_chrome_mac)$'` PASS/0、93/93、107.56s。五项会激活窗口的测试明确未运行，本轮没有修改原有测试断言或将筛选后的结果称为全量平台通过。
- 对应 Release 同命令批次：前 15 项通过，本地化 contract 启动延迟后 PASS/115.93s；采样期间 879 次均位于 `_dyld_start + 0`。该批次在第 16 项开始时中断/130，不能记为完整通过。采样不包含其他测试，不能推广为所有超时的确定根因。
- 从未完成项续跑 `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure --timeout 120 -I 16,98 -E '^(page_snapshot_cef_integration|cast_toolbar_host_probe|cast_entry_surface_probe|cast_toolbar_close_probe|cast_chrome_mac)$'`：78 项中已结束 21 项，18 PASS、3 TIMEOUT，随后中断/130。超时为 `page_markdown_export_contract` 120.04s、`chrome_contract` 120.02s、`cast_selection` 按原门禁 15.03s，均无测试输出；其他未完成项 NOT_RUN/中断，不冒充通过。对 chrome 的采样因进程已被 timeout 终止而失败，未声称已有该项调用栈。
- 上述两段 Release 没有重编译或修改源码，累计 33 项已通过，但不是 93/93；两段的完整总耗时未保留。新协议专项的独立通过证据仍有效，整个工作区 Release 回归仍有未关闭门禁。已停止本轮 CTest 与子进程；不要求重启、不改系统安全设置或签名，不以反复全量运行代替启动诊断。
- 一期实际剩余：R04 后续实例/分页协议与 runtime、R07b 草稿/提交、R03b 原因投影、R02b2/3M 与 R08M/R09/R10M 默认三入口；M05b4..b6/M05c/R11M 真机和生命周期；LOC-09M/10；CNT/MRT/PRV/PLT-19M 总审；QAR Mac CI/性能/长稳/签名公证/安装升级回滚/SBOM/Go-NoGo。窗口宿主范围、真实系统语言/设备操作及发布凭证分别取得必要授权；用户“全部做完”不降低依赖或发布门禁。

- 用户明确要求先完成 Mac 端本块所有可执行工作；本轮按既有切片串行推进，不改变 Windows 首发发布范围，不新增代理/SDK 外部修改、发布或系统设置权限。
- R04a 导航集成修复：IMPLEMENTED，专项已通过，完整双配置复验中。输入为 §17 双配置稳定失败的 media-navigation 与逐播放器证明契约；单一目标是收口真实 CEF 媒体观察/输入时序回归，不允许未观察元素继承历史点击。
- 允许追加修改 `browser/cef-shell/tests/page_snapshot_cef_integration_mac.mm`、`tests/e2e/desktop/browser/run_page_snapshot_fixture.py` 及对应独立 fixture 测试；生产范围仍限 R04a 输入/观察文件。先核对实际顺序，必要时修正 Harness 就绪条件；不为测试放宽生产授权。
- 验收：定向媒体 CEF 场景、双配置完整 build/CTest、新增/改动行 clang-format、repo-guard、diff check；实际命令/耗时/失败在本节追加。仅 fixture 的自动输入不作为物理用户输入或接收端首帧证据。
- 后续继续 R02b2/3M、R04b/c/d、R07b/R03b/R08M、R09/R10M；每项开工前补原子文件与验收范围。真实设备/签名/IME/读屏等证据独立标注，不使用编译或组件测试替代。

### R02b2 领取

- 状态：BLOCKED；本轮原方案被固定 CEF 公共契约否定，已撤回仅本轮在建接线，未切换默认产品。需新的宿主方案与范围授权，不能继续“同一窗口多 Chrome BrowserView”。
- 允许：`browser/cef-shell/src/browser/window/views_browser_host.{h,cc}`、`tab_controller.{h,cc}`、CEF CMake、独立 `tests/views_browser_host_probe.{h,cc}` 与现有测试模式分发；本计划/current/索引。必要导航文案复用现有 catalog，新增窗口操作文案仅修改三语言源及生成输出。
- 输入/所有权：TabModel 保留唯一逻辑标签 owner；宿主仅持有 browser/view/window 的有界视觉映射（全局沿用 32 tabs），不持有 URL 投送、证明或草稿。只用公开 LOCATION/Views API，无私有子树访问。控件动作延后到 UI task，关闭前解除 LOCATION 借用，拒绝关闭后恢复；隐藏标签不改变当前投屏目标。
- 验收：独立真实 CEF 验证新标签/切换/关闭/多窗口、导航命令、地址相邻布局与关闭恢复；双配置 build/CTest、格式、repo-guard、diff check。默认产品未切换前仅组件 VERIFIED；Mac app/菜单/MDV/输入监听/旧投屏迁移属于 b3M。
- 不做：Windows 真机、CEF/SDK/依赖升级、MHV2、真实设备/发布、私有地址框遍历、自动网页播放或增加测试专用生产 API。

- R02b2 发现：固定 CEF 150 `include/internal/cef_types_runtime.h` 明确规定一个 Chrome Window 最多一个 Chrome BrowserView（可有多个 Alloy BrowserView）。§14 第 4 项的原多 Chrome view 迁移方案不成立，R02b 设计最高状态回退 IMPLEMENTED/REQUEST_CHANGES；R02a/R02b1 单 view 原语证据保留，不升级为多标签能力。
- 后续只读参考：2026-09-04 检查 [上游 master 的 ChromeBrowserHostImpl::CreateBrowser](https://github.com/chromiumembedded/cef/blob/master/libcef/browser/chrome/chrome_browser_host_impl.cc)，Views 路径把普通窗口类型改为 popup；这说明不能假设“单个 Views 宿主自然保留原生多标签”，也不能承诺简单升级即解决。该链接不是当前固定 distribution revision 的源码取证，仍以本地固定公共契约/指定 Harness 为门禁；没有更改依赖或扩大宿主实现范围。
- 实验结果：独立 `views_browser_host_probe` Debug 多次 FAIL/8（19.31/19.39/22.91/20.63/21.11/23.37s），第一 view 的 LOCATION 未绘制且退出有 `ToolbarButtonProvider::kDataKeyImpl` 生命周期检查失败；没有到达多 view 创建，所以不能把该崩溃直接归因为多 view 限制。LLDB 只确认 Attach 调用被执行且无 false 返回；调试器 exit 0 不计测试通过。
- 已撤回本轮新增的两个宿主文件、两个实验测试文件、对应 TabController/CMake/测试模式/六条实验文案，未覆盖此前用户改动；四个原型文件可从忽略的 `.cache/r02b2-rejected/` 恢复，仅用于诊断，不在生产图。公开契约限制独立足以否定原设计，无理由带失败的多 view 实验进入产品。后续方案可能涉及 CEF 扩展/升级或更换宿主，已向用户请求范围决策；不擅自升级依赖或替换原生地址安全控件。

### R04b1 Browser 实例身份领取

- 状态：VERIFIED；单一目标是在已有逐播放器证明 owner 内生成稳定实例 ID/source revision，支持显式源 epoch 与移除，不创建第二套证明状态。R04b2 后续接 renderer/CEF 生命周期；本项不宣称实际页面已经发出删除事件。
- 允许：现有 `browser/cef-shell/src/browser/input_proof/player_input_proof.{h,cc}`、独立 `_test.cc`、必要封闭 `input_proof_gate.h` 结果枚举；本计划/current/索引。不改 MHV1、SDK、默认窗口或媒体策略。
- 输入：R04a 隔离/容量基线、current/cast-interaction 实例/source 契约。Browser 单调分配非零实例，URL 不是身份；源 epoch 只作不可信失效信号，同 URL 重新加载/两个 blob 源也须撤销旧证明。移除释放容量，重建不重用旧实例；旧 epoch 拒绝，ID/revision 溢出拒绝，最多 16/page、256/global 不变。
- 验收：独立行为测试覆盖同 URL 双实例、时间推进稳定身份、同 URL 新 epoch、过期 epoch、换源返回、remove/recreate、导航/关闭/容量释放；Debug/Release build 与 `player_input_proof` CTest、clang-format、repo-guard、diff check。真实 renderer/协议/runtime/UI 接线仍由 b2/c/d 负责。

- 实现/证据：Browser 单调非零实例（删除/导航不复用）、独立 source revision、旧 epoch 拒绝、精确移除；计数溢出永久拒绝该耗尽引用，不回绕。实现前目标编译 FAIL/1（新 API 缺失，编译红灯）；实现后同目标 build PASS/0，Debug 独立程序 7 cases PASS/0，完整 Debug 中 `player_input_proof` PASS/0、0.38s；`ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure -R '^player_input_proof$'` PASS/0、1/1、1.39s。两套完整 build PASS/0（耗时未保留），clang-format/diff check/repo-guard PASS/0（9 passed、既有 RG003/004 warning、RG006 N/A）。对象仍 main/cfaab39 dirty、macOS 26.6.2 arm64；没有外部 IO/日志/线程/新依赖。按 Review 标准独立自审 P0/P1/P2/P3=0/0/0/0、APPROVE，最高组件 VERIFIED。

### R04b2 页面生命周期失效信号领取

- 状态：IMPLEMENTED，媒体双配置整链已通过，完整回归等待下述独立 surface Harness 修复；单一目标是将当前主 frame 的换源 epoch/移除信号接到既有 Browser 证明 owner，消除 collector 生命周期 16 项永久占满及 URL-less 源变化无法撤销的问题。依赖 b1 VERIFIED；不依赖待决的产品地址栏宿主。
- 允许：`renderer/media_observer/media_observer.{h,cc}`/`cef_media_observer_renderer.cc`，`ipc/media_observation_cef_message.{h,cc}`，`browser/observation_gateway/cef_observation_bridge.cc`；对应独立 unit、Node collector 行为测试、CEF codec 检查文件与测试 target/fixture 接线；本计划/current/索引。生产文件预计 6 个，不增加第二个状态 owner。
- 冻结 CEF 私有消息：新 `crayon.media.observation.v2`，固定 10 项 = 既有 8 项 + 非零 source epoch 十进制字符串 + removed bool；此为 renderer→Browser CEF 消息，不是 MHV2。移除只允许 idle/unknown/空 URL/零时间与可见度/无 EME 的规范记录。旧 v1 不重解释、不降级，新旧 Helper 必须配套。主 frame/真实 sender/navigation 校验不变，epoch 只收紧/撤销，不授权；MHV1 完全不改。
- Collector：最多 16 个活跃元素，单调正 int32 element ID 不复用；源对象/原始源变化及 loadstart/emptied 递增 epoch，溢出撤销该元素；脱离文档后解除事件监听、发送删除、释放名额。只观察，不调用 play/click/seek/rate。轮询仅遍历有界活跃集合，DOM 变动后补入空位，不新增全页高频遍历。
- 验收：Node VM 运行真实 collector 脚本覆盖同 URL 双元素、加载/Blob/stream 换源、删除/迟到事件/重新插入/容量回收与零页面命令；独立 observer unit；真实 CEF 初始化下 codec round-trip/非法类型/超限/移除规范/旧版拒绝；已有 CEF 播放与拒绝 fixture、双配置 build/完整 CTest、格式/guard/diff check。新增测试先运行红灯；测试工具不进入产品。
- 不做：MHV2/runtime 候选删除/草稿/分页与设备投送、iframe/Shadow/播放器几何、默认窗口迁移、CEF/SDK/依赖更新或发布。旧 runtime URL store 暂仍按其原 TTL 工作，不把 Browser 证明撤销宣称为最终 UI 候选删除。
- 开工边界 Review：封闭消息只增加失效语义，保持既有主 frame 与用户输入信任边界；容量与 listener 释放明确，旧 MHV1 bytes 不变。APPROVE 进入实现，不等于代码或产品验收。

#### R04b2 阶段证据

- `node --test browser/cef-shell/tests/media_collector.test.mjs`：实现前行为 FAIL/1、0/4、87.66ms（缺少 epoch/移除、容量不能释放）；实现后 PASS/0、5/5，最终单次 100.80ms。覆盖同 URL 双身份、同源 reload、Blob/stream 对象变化、删除监听/容量/重插入不复用、迟到消息与超长源持续撤销。使用 VM 中的独立 DOM 替身，不冒充 CEF/真机。
- `python3 tests/e2e/desktop/browser/page_snapshot_fixture_test.py` PASS/0、4/4、<1s；新两场景只登记 Mac，Windows 路由不假称已经支持。`media-forged` fixture 同步新 Native 参数，以真实无输入拒绝而不是参数个数错误通过。
- 新 CEF codec 检查置于独立 test-only 文件，由真实 CEF 初始化后的 fixture 调用。初次编译 FAIL/1（测试 CHECK 宏与 CEF 重名），修正专有测试宏后构建通过。首次新场景 FAIL/251（子进程 -5），因测试试图向 CefListValue 写入其自身禁止的 NaN，尚未进入产品解码；改用可表示的负时间向量，NaN 仍由已有纯 proof unit 验证。未删除生产有限数检查或弱化拒绝。
- `python3 tests/e2e/desktop/browser/run_page_snapshot_fixture.py .cache/build/macos-arm64-cef-debug/browser/cef-shell/Debug/crayon_page_snapshot_cef_integration_test.app/Contents/MacOS/crayon_page_snapshot_cef_integration_test media-source-reload`：中间 FAIL/1、stage=2；旧证明已经被拒绝，重放 fixture 顶层 const 导致后续测试播放未执行。将测试播放脚本封装 IIFE，避免重复声明；最终 PASS/0，stage=3、48 messages/16 eligible、residue=0。同命令末参数 `media-player-replace` 最终 PASS/0，stage=3、45 messages/16 eligible、residue=0。两者均验证先拒绝无新输入的播放、观察暂停、再显式测试输入后恢复；耗时未完整保留，不是真人输入或设备首帧。
- 新 6 个生产/相邻 unit 文件的改动行 `xcrun clang-format --dry-run --Werror`、新增 codec 检查头/源格式、`git diff --check` PASS/0；`cargo run --quiet -p repo-guard -- scan --root .` PASS/0，9 passed、既有 RG003/004 warning、RG006 N/A，耗时未保留。双配置完整回归仍在本节最终记录，不用定向通过替代。
- 性能自审修正：初版在不足 16 项时每次 DOM 变化都扫 document；新增稳定用例先 FAIL/1（20 次无关变化使全页查询 1→21），改为新增子树扫描、仅实际释放名额后补扫一次，250ms tick 只遍历活跃集合。最终 Node PASS/0、6/6、57.09ms；没有通过延长轮询或弱化失效来规避问题。发现该点时主动中断本轮 Debug 完整 CTest（exit 130，前 83 项通过，第 84 项运行中），没有记成完整通过；只读进程检查无该轮 CEF/content/media 子进程残留。修后重新构建并从头跑双配置回归。

### R08u2r Mac surface Harness 呈现就绪修复领取

- 状态：IMPLEMENTED；双配置 surface 专项各连续 8 次通过，最终完整回归尚未闭合（见下述续测记录）。单一目标是使独立 Harness 等待实际控件呈现后再输入/断言，消除把 owner Ack 当作完成原生 Render 的竞态。只修改 `browser/cef-shell/tests/cast_entry_surface_probe.cc` 和本计划/current/索引，不改生产 Render 延后机制、意图/权限、超时门禁或 SDK。
- 输入/复现：R04b2 修后完整 Debug PASS/0、97/97、263.19s；Release FAIL/8、96/97、257.90s，完整媒体 157.60s 通过，唯一 `cast_entry_surface_probe` stage=2/device_choices、1.44s。Ack 先增加 intent 计数、Apply 更新模型并排队 Render；旧按钮在 Update 中因 revision 过期禁用，Check 不应据计数立即操作旧控件。
- 验收：在独立测试 owner 中明确重现 Ack 后、Render 前的检查顺序；原生按钮实际存在/可用/绘制后才发送输入，语义错误仍立即失败，缺少就绪仍按既有 750 polls/30s CTest deadline 失败。覆盖多步骤选择、Commit 不重复、overlay 实际出现后再测过期；双配置专项重复与完整 CTest、格式/diff check。不得用固定长 sleep、单纯重跑或增加生产测试 API 规避失败。
- 复现与收敛：Debug 定向首次同步 Ack 中检查复现 stage=2 后，测试失败关闭又触发 CEF GetColorProvider/SIGTRAP（FAIL/8、7.14s）；将检查排到 Apply 的 Render task 前而非同步回调内，复跑 FAIL/8、6.34s（stage=1，同类未呈现控件），不把测试关闭崩溃归为产品路径。修复各步骤实际控件就绪后，首次 8 次重复在第 5 次 stage=7 超时（前 4 次通过，共24.50s）：布局通知可以撤销刚提交的 anchor，测试 owner 必须在浮层缺失时补当前几何，并避免重复创建活 widget；已补这一义务，同时合并测试轮询，保持既有 deadline。最终验证另记，不用中间 4 次通过替代收口。

#### 2026-09-04 续测记录与当前结论

对象仍为 main/cfaab39 dirty、macOS 26.6.2 arm64、CEF 150.0.10；本次续测没有修改生产或测试源码，没有提交。用户因界面测试干扰工作要求暂停，随后明确要求继续测试；因此只集中执行剩余入口专项，不再次运行逐场景新建窗口的完整页面套件。复用专用 Tab 的批量 Harness 尚未实现，不宣称已有后台测试模式。

- 暂停前 `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure --repeat until-fail:8 -R '^cast_entry_surface_probe$'` PASS/0、连续 8/8、21.98s。随后完整 Debug CTest 按用户反馈中断，exit 130，前 83 项通过、第 84 项开始；不是完整 PASS，相关测试进程已退出。
- `cmake --build .cache/build/macos-arm64-cef-release --target crayon_page_snapshot_cef_integration_test -j4` PASS/0，确认最后的 test-only 就绪修正已编入 Release；耗时未完整保留。ad-hoc 构建签名不替代发布签名/公证。
- `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure --repeat until-fail:8 -R '^cast_entry_surface_probe$'`：受限执行首次 FAIL/8、0/1、3.76s，SIGABRT 位于 `NSApplication sharedApplication` → `_RegisterApplication`，尚未进入 CEF 用例。改为获批桌面执行后 PASS/0、连续 8/8、6.02s；保留首次失败，不把该复核当作生产代码修复。
- `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -R '^(player_input_proof|input_proof_gate|media_observer|media_collector_lifecycle|observation_gateway|page_snapshot_fixture_platform_contract|localization_generated_check|localization_generator_contract)$'` PASS/0、8/8、0.98s。
- 同一定向命令将 build 目录换为 `macos-arm64-cef-release`，首次前 4 项通过、`media_observer` 无输出，主动中断 exit 130。获批执行并追加 `--timeout 30` 后 FAIL/8、4/8、105.35s：`media_observer`、`input_proof_gate`、`observation_gateway` 各约 30s TIMEOUT，`player_input_proof` 按原 target 15s TIMEOUT。其余 4 项 PASS；没有将启动超时计作行为断言失败或通过。
- 对独立启动的 `crayon_media_observer_test` 做 `sample <本轮单测 PID> 1 1`，882 次采样均停在 `_dyld_start + 0`，尚未进入 main；`codesign --verify --verbose=2 .cache/build/macos-arm64-cef-release/browser/cef-shell/src/renderer/media_observer/crayon_media_observer_test` PASS/0、valid on disk。该栈只直接证明被采样单测，其他超时的底层原因仍未证实；不据此要求重启、重签或更改系统安全设置。
- `git diff --check && xcrun clang-format --dry-run --Werror browser/cef-shell/tests/cast_entry_surface_probe.cc` PASS/0。`cargo run --quiet -p repo-guard -- scan --root .` 本轮无输出，停止并保留未完成结论；前文 PASS 是此前证据，不能覆盖本轮。诊断单测与 guard 最终以 TERM 停止，只读进程检查无本轮测试浏览器/CTest/对应诊断残留。
- 按 Review 顺序独立自审：就绪等待与有界轮询、旧 revision 禁用、重入时序、浮层实际呈现后过期检查未发现新增代码问题；不放宽授权、不改生产行为。P0/P1/P2/P3=0/1/0/0、REQUEST_CHANGES：剩余 P1 为最终规定的双配置完整验证尚未闭合。R08u2r 保留 IMPLEMENTED，R04a/R04b2 不在本次提升状态；三入口仍是组件证据，不是默认产品装配或真机投送完成。
- 下一步先解决/复核无界面原生程序的启动等待，再补最终完整回归；焦点/生命周期测试集中执行，不能在用户工作期间无提示反复启动全套 GUI。MHV2/runtime 与默认宿主门禁保持不变。

### R04a 本轮阶段证据

- 本轮仅修复独立 Harness 时序并补逐播放器 unit 断言：页面完成加载不保证 Browser 已收到媒体观察；先等待新的暂停观察再提交测试输入。blob/MSE 先加载新源，经测试独立 query 通知就绪，再等待 Browser 下一次暂停观察；不在生产暴露测试 API，不让通知本身授权播放。
- `python3 tests/e2e/desktop/browser/run_page_snapshot_fixture.py .cache/build/macos-arm64-cef-debug/browser/cef-shell/Debug/crayon_page_snapshot_cef_integration_test.app/Contents/MacOS/crayon_page_snapshot_cef_integration_test media-navigation`：修前 FAIL/1（31 条全部拒绝），修后 PASS/0（32 条、15 eligible、residue=0）；完整耗时未保留。对应 `media-blob`、`media-mse` 定向命令均 PASS/0（各 15 eligible、residue=0）。测试输入不是物理真人证据。
- `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure -R '^page_snapshot_cef_integration$'`：首次仅修导航时 FAIL/8、69.66s，继续定位到 blob 换源；完成就绪修正后全量 Debug `ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure` PASS/0、96/96、213.12s，其中完整 CEF 集成 141.80s。
- `cmake --build .cache/build/macos-arm64-cef-debug -j4 && cmake --build .cache/build/macos-arm64-cef-release -j4`：PASS/0，两配置包含更新 fixture 与 unit；完整耗时未保留。后续宿主在建代码不能套用这个通过记录。
- `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure`：第一次 FAIL/8、93/96、184.49s（media-host process、media-host-crash 集成、surface）；当时与 Debug 原生测试并行，不作为隔离稳定性证据。串行第二次 FAIL/8、94/96、158.99s：process 与 surface 已通过，但 media-host-crash 仍失败；另有在建宿主新增文案时 generator 数量断言 180→186 尚未同步，现已同步，须最终重建复验。未将反复运行当作根因修复。
- 后续明确修正 crash fixture：旧进程 Shutdown 前已排队的 CandidateReply 不能消耗唯一的“重启后 Decide”请求；等待 generation 恢复，并让独立本地 fixture 音频循环以保持恢复期观察。生产不循环播放或放宽证明；最终结果待追加。
- crash fixture 修正后，Release 定向 `media-host-crash` PASS/0：46 messages/37 eligible、host_recovered/candidate_before/candidate_after 均为 1、residue=0、max tick delay=40ms；耗时未完整保留。R04b1 完成后、b2 开工前 Debug 完整 CTest 再次 PASS/0、96/96、237.79s，CEF aggregate 145.36s。两条证据不能替代最终 b2 双配置回归。
- 已执行 `xcrun clang-format --dry-run --Werror browser/cef-shell/src/browser/input_proof/player_input_proof_test.cc`、fixture 改动行格式检查、`git diff --check` PASS/0；`cargo run --quiet -p repo-guard -- scan --root .` PASS/0（9 passed、既有 RG003/004 warning、RG006 N/A）。完整耗时未保留。R04a 暂留 IMPLEMENTED，Release 集成未闭合前不标 VERIFIED。
