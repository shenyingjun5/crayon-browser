# 蜡笔 AI Agent 投屏浏览器测试用例目录

状态值：`AUTO` 自动化、`HARNESS` 专项设施、`DEVICE` 真机、`RELEASE` 发布产物。每个用例实现后必须在测试代码注释或测试名称中保留 ID。

## 1. Repo、模块与构建

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| RG-001 | AUTO | 扫描生产依赖图 | 无 production -> test-support/test framework 依赖 |
| RG-002 | AUTO | 扫描生产源中的内联测试正文、Mock/Fake/testOnly | 零命中；Rust 只允许独立测试模块声明 |
| RG-003 | AUTO | 统计函数/文件行数 | 触发 100/200、2000/3000 提醒；测试文件 >=3000 阻断 |
| RG-004 | AUTO | 扫描端口、UA、超时、token、绝对路径和协议字符串 | 业务可变值只来自配置/常量/能力模型，无凭证 |
| RG-005 | AUTO | 解析 workspace 依赖图 | 无循环依赖；仅 cast-adapter 依赖 Cast-SDK |
| RG-006 | AUTO | 构建 Release 并扫描符号/资源 | 不包含 test、fixture、debug remote control 和测试框架 |
| RG-007 | AUTO | 修改 IPC/schema 后运行兼容检查 | 当前与前一协议版本 golden vectors 均通过 |
| RG-008 | AUTO | 检查 Cast-SDK source lock、`.gitmodules`、checkout HEAD 与后续 Cargo 依赖；gitlink 由 submodule 状态命令复核 | 固定 commit；无 branch 漂移或本机 path dependency |

## 1A. 品牌图标资产

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| BI-001 | AUTO | 校验参考源路径、SHA-256、尺寸和像素格式 | 与 `brand-assets.md`/manifest 一致；参考 PNG 不进入平台产物 |
| BI-002 | AUTO | 解析三个 SVG 源并扫描外部资源、脚本、嵌入位图 | SVG 可解析；无 script、foreignObject、网络 URL 或 data image |
| BI-003 | AUTO | 在相同输入上连续生成两次 | 全部生成文件 SHA-256 一致 |
| BI-004 | AUTO | 读取所有 PNG 的 IHDR/alpha 与尺寸 | 尺寸匹配 manifest；Windows/Harmony 透明角无黑边；macOS 方形底板完整 |
| BI-005 | HARNESS | 生成 16/20/24/32/48/64/128/256/512/1024 contact sheet，在明/暗背景检查 | micro/master 切换正确；浏览器+蜡笔可辨；无脏边/裁切/不可见细节 |
| BI-006 | AUTO | 解析 `app.ico` 目录和 PNG payload | 16/20/24/32/40/48/64/128/256 齐全，32-bit alpha，offset/length 合法 |
| BI-007 | AUTO | 解析 macOS iconset 与 ICNS chunk | 16～1024 变体齐全，chunk 类型/长度合法；macOS runner 后续复核包内资源 |
| BI-008 | AUTO | 校验 Harmony 输出、manifest 和目标路径契约；模拟路径逃逸与 symlink/junction | 1024/512/256 资产来自同一版本；`HM-02` 可直接消费且无 Windows 路径假设；受管删除拒绝仓库外路径与 reparse parent |

## 2. 浏览器、播放门禁与媒体观察

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| BR-001 | AUTO | 导航本地 HTTPS fixture，前进/后退/刷新 | URL、历史和 load state 正确 |
| BR-002 | AUTO | 两标签分别登录不同 fixture origin | Cookie/存储按 Profile 正常工作且不跨 origin |
| BR-003 | AUTO | 页面脚本伪造 `playing`，无可信输入 | 投屏按钮保持禁用 |
| BR-004 | AUTO | 用户点击播放且 `currentTime` 推进 | 当前标签变为 `PlaybackEligible` |
| BR-005 | AUTO | 点击页面非播放区域后页面自动播放 | 不满足播放门禁 |
| BR-006 | AUTO | 两个视频同时播放，一个可见且最近操作 | 选择最近操作且可见面积最大的媒体 |
| BR-007 | AUTO | 导航后旧 frame/worker 迟到上报 | navigation ID 不匹配，事件丢弃 |
| BR-008 | AUTO | 页面含 iframe/Worker/MSE 网络候选 | observation 带正确 frame/navigation/source，不泄露正文 |
| BR-009 | AUTO | 页面包含广告与正片顺序播放 | 不点击跳过、不 seek、不改速率、不按广告域过滤 |
| BR-010 | AUTO | fixture 监听 click/play/currentTime setter | 产品注入不产生自动点击和广告快进 |
| BR-011 | AUTO | EME `encrypted` signal 与 clear URL 同时出现 | 关联候选 protection 升级，直投被拒绝 |
| BR-012 | AUTO | `blob:`/MediaStream 无底层 URL | 不伪造直投 URL；返回外部客户端交接或明确不支持 |
| BR-013 | AUTO | 关闭标签/窗口时仍在探测 | observer 取消，旧事件不能重建候选 |
| BR-014 | DEVICE | 中文/英文输入法、缩放、全屏、下载、无障碍 | 支持平台行为符合 PRD，无崩溃/焦点丢失 |

## 2A. 桌面浏览器产品体验

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| UX-001 | AUTO | 渲染不同宽度/主题的顶部标签栏与导航工具栏 | Chrome-inspired 熟悉层级，蜡笔品牌独立；控件不重叠且焦点顺序稳定 |
| UX-002 | AUTO | 打开普通/无痕新标签页并记录请求 | 本地起始页可搜索/导航；普通页只显示允许的固定项，无痕不显示历史；零默认公网请求 |
| UX-003 | AUTO | 地址栏输入 URL、搜索词、空白、危险/未知 scheme 与超长文本 | URL/搜索判定稳定；危险 scheme 拒绝；长度有界；provider/Profile 配置生效 |
| UX-004 | AUTO | 导航、停止、刷新、前进/后退并切换 HTTP/HTTPS/证书错误 fixture | 控件状态、URL、加载进度、站点身份与错误页一致，页面不能伪造浏览器安全 UI |
| UX-005 | AUTO | 新建/切换/关闭/拖动标签，重复关闭并恢复最近关闭 | 顺序与 active tab 正确；旧事件不复活；资源释放且可恢复 |
| UX-006 | AUTO | 固定、复制、静音、标签搜索/分组并跨窗口移动 | 状态/音频指示准确；键鼠可操作；窗口关闭无孤儿 Browser |
| UX-007 | AUTO | 页面请求弹窗、全屏、画中画和外部窗口 | 来源可见、策略可控；拒绝/取消明确；焦点与退出可恢复 |
| UX-008 | AUTO | 新增/编辑/删除/搜索书签，切换书签栏，导入导出损坏/超大文件 | store 事务一致；顺序/文件夹正确；非法输入不破坏既有数据 |
| UX-009 | AUTO | 生成历史/最近关闭，按范围删除并在普通/无痕间检查 | 查询/恢复正确；删除范围准确；无痕不持久化；跨 Profile 隔离 |
| UX-010 | AUTO | 下载正常/取消/暂停/恢复/重复命名/危险文件并打开所在位置 | 状态与文件一致；路径/外部打开受控；失败和危险项不假成功 |
| UX-011 | AUTO | 页面查找、缩放、全屏、打印/PDF 与保存页面，覆盖取消/失败 | 结果/状态正确；取消无残留；输出路径受控；不泄漏其他 Profile 数据 |
| UX-012 | AUTO | 修改启动页、默认搜索、外观、下载和内容设置并重启 | 配置 schema/version 有效；损坏回安全默认；设置 UI 与实际行为一致 |
| UX-013 | SECURITY | 摄像头/麦克风/定位/通知/剪贴板、证书错误、弹窗与外部协议 fixture | 默认最小权限；origin/Profile 绑定；页面不能诱导伪造授权或静默绕过 |
| UX-014 | AUTO | 普通/无痕/Profile 切换、启动恢复、崩溃恢复和清理失败 | 会话按策略恢复；无痕不恢复；清理失败显式报告；跨 Profile 零污染 |
| UX-015 | AUTO | 链接/图片/选中文本上下文菜单、拖放、复制粘贴和本地文件入口 | 菜单随上下文最小化；危险 scheme/路径拒绝；页面不能触发隐藏外部动作 |
| UX-016 | DEVICE | Windows/macOS 快捷键、中文/英文 IME、无障碍树、浅/深色、多屏/DPI | 与平台习惯一致；缩放/焦点/读屏/窗口迁移稳定 |
| UX-017 | SECURITY | 本地地址自动填充的保存确认、匹配、编辑、删除、无痕/Profile/Agent 访问 | 用户可见确认；本机隔离可删除；无痕不保存；Agent/页面数据面不可读取原始 PII |
| UX-018 | RELEASE | clean profile 完成浏览器基线 E2E、冷启动/首导航/内存/包体与泄漏扫描 | UX-001..017 适用项通过；P0/P1=0；无 Chrome/Google 商标资产或未批准服务依赖 |

## 3. 候选、预检与策略

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| PL-001 | AUTO | 相同 URL 来自 DOM/network/currentSrc | 合并为一个 candidate，保留多份证据 |
| PL-002 | AUTO | URL 含短期签名 query | 不错误去 query；日志只保留脱敏 origin |
| PL-003 | AUTO | MP4 HEAD 405，Range 返回 `ftyp` | 有界 fallback 后识别 MP4，不下载主体 |
| PL-004 | AUTO | HLS master 含 variant/audio/subtitle | parser 识别完整资产关系 |
| PL-005 | AUTO | HLS 含 AES-128/SAMPLE-AES/SESSION-KEY | 按当前合规策略拒绝直投/relay，不请求 key |
| PL-006 | AUTO | DASH `ContentProtection` | DRM 拒绝；无可用直投 URL |
| PL-007 | AUTO | 接收端不支持候选 codec/protocol | 策略选择 `ExternalClientHandoff` 或稳定 unsupported |
| PL-008 | AUTO | 候选需要 Cookie/Authorization | secret 不交给接收端；只允许安全 Relay、外部交接或拒绝 |
| PL-009 | AUTO | 广告连续性 unknown 且选择从头播放 | 不走 Direct；返回外部交接或稳定拒绝 |
| PL-010 | AUTO | 未通过用户播放门禁但其他条件均满足 | 结果必须 Reject |
| PL-011 | AUTO | 平台不支持外部客户端交接 | capability 驱动稳定拒绝并返回明确原因 |
| PL-012 | AUTO | 旧 candidate plan 超 TTL 或 receiver 变化 | 重新规划，不能复用旧 Direct |
| PL-013 | AUTO | 同一输入在 Windows/macOS/Harmony 电脑 fake capability 下运行 | 安全/隐私结论一致，只有可用路由不同 |
| PL-014 | AUTO | Probe 网络错误、超时、取消 | 不升级权限，不从普通失败推导 relay |
| PL-015 | AUTO | Direct/Relay 均不可用并选择外部客户端交接 | 必须用户确认；浏览器不创建 WebRTC、receiver session 或 Relay token，也不报告投屏已开始 |

## 4. Session Relay 安全与媒体行为

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| RL-001 | AUTO | 构建正式 LAN router | 不存在 `/api/extract`、任意 URL `/proxy`、player/probe 页面 |
| RL-002 | AUTO | 创建 session | token/resource ID 至少 128-bit CSPRNG，不含上游 URL |
| RL-003 | AUTO | 非当前 Cast-SDK route、错误 receiver ID/IP 请求资源 | 401/403，不访问 upstream |
| RL-004 | AUTO | 停止 session 后重复访问 | 10 秒门禁内失效；vault/registry 清空 |
| RL-005 | AUTO | 导航、route lost、设备替换、Profile 销毁、App exit | 每个触发器均撤销 session 和 secret |
| RL-006 | AUTO | 上游重定向到 localhost/RFC1918/link-local/metadata | 每跳拒绝；不返回内部响应 |
| RL-007 | AUTO | DNS 校验公有 IP 后重绑定私网 | 实际连接固定已校验地址或拒绝 |
| RL-008 | AUTO | 猜测 token/resource ID、路径穿越、超长头、POST/CONNECT | 全部拒绝且有界处理 |
| RL-009 | AUTO | MP4 GET/HEAD/Range 0-, suffix、越界 | 正确返回 200/206/416，seek 可用 |
| RL-010 | AUTO | HLS master/media 相对、绝对、query URI | 全部改写为 opaque resource ID，字节语义正确 |
| RL-011 | AUTO | fMP4 init/segment 和 TS segment | 输出 hash 与 upstream 一致，不经文本转换 |
| RL-012 | HARNESS | 8 并发 segment + 慢接收端 + 上游卡死 | 无全局串行；并发/缓存/超时有界 |
| RL-013 | HARNESS | 30 分钟 VOD/live | 内存不随媒体时长线性增长；停止后回落 |
| RL-014 | AUTO | 扫描日志、DTO、诊断、磁盘 | 无完整 URL query、Cookie、Authorization、token |
| RL-015 | AUTO | 上游跨 origin redirect | header scope 逐跳校验，敏感头不泄露 |

## 5. Cast-SDK 适配与设备会话

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| CS-001 | AUTO | Fake facade start/refresh/stop discovery | 生命周期幂等，UI 只消费设备快照 |
| CS-002 | AUTO | 设备同名、UDN 冲突、多网卡候选 | UI 使用稳定 device ID，不缓存 IP |
| CS-003 | AUTO | 六位码成功、格式错误、未找到、取消 | 映射稳定连接状态；浏览器不实现算法副本 |
| CS-004 | AUTO | receiver capability 变化 | policy 使用 SDK 最新 assessment，旧缓存失效 |
| CS-005 | AUTO | Direct plan 投送 | 只通过 Cast-SDK facade，App 不拼 SOAP/URL |
| CS-006 | AUTO | pause/seek/volume/stop 带旧 session handle | SDK/adapter 拒绝 stale generation |
| CS-007 | AUTO | receiver 自然结束、电视端 Stop、route lost | App 状态收敛并撤销 Direct/Relay 资源；外部交接无 SDK session |
| CS-008 | AUTO | SDK 返回 unsupported/protocol/permission 错误 | 映射稳定产品错误码，不解析自然语言 |
| CS-009 | AUTO | Cast-SDK revision 升级 | gitlink/source lock、API contract、行为 golden、依赖树和回滚检查通过 |
| CS-010 | DEVICE | 自动发现和投屏码分别连接蜡笔接收端 | 发现、连接、首帧、控制、停止闭环 |
| CS-011 | CONTRACT | 审查 Partner/TV Cast Manifest 缺口和外部 API 提案 | 签名、版本、能力、字幕、队列、结果回报均有 Cast-SDK/receiver owner；浏览器无临时协议 |
| CS-012 | DEVICE | 固定版本 Cast-SDK 正式 facade 接入已批准 Partner/TV receiver | 仅经 facade 完成 manifest 验证与能力闭环；App 无 raw manifest、IP、控制 URL 或协议复制 |

## 6. Profile、隐私与安全存储

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| PV-001 | AUTO | 隐私 Profile 登录、写 Cookie/LocalStorage/IDB/Cache/SW | 会话内可用 |
| PV-002 | AUTO | 关闭最后一个隐私窗口并新建会话 | 前述存储全部不可读 |
| PV-003 | AUTO | 清理一项被锁定/失败 | UI 明确失败；不宣称清理完成 |
| PV-004 | AUTO | 两个常用空间登录同一 origin | Cookie、权限和存储互不可见 |
| PV-005 | AUTO | 销毁常用空间 | 先停止投屏/撤销 secret，再删除受控目录 |
| PV-006 | AUTO | 构造符号链接/目录联接逃逸 | 拒绝删除目标，外部文件不受影响 |
| PV-007 | DEVICE | DPAPI/Keychain/HUKS 写读删敏感配置 | 敏感值不以明文落盘，空间销毁后删除 |
| PV-008 | AUTO | 默认启动并检查网络/日志 | 遥测关闭，无浏览 URL/标题上报 |
| PV-009 | AUTO | 标准/严格防追踪模式 | 统一策略，不为不同 Profile 生成唯一随机指纹 |
| PV-010 | AUTO | 诊断导出预览 | 用户可见内容与实际发送一致，无秘密 |

## 7. 网页内容、Markdown 与第二阶段模型

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| CT-001 | AUTO | 顶层 fixture 含标题、段落、列表、链接、图片、表格和代码块 | `PageSnapshot` 字段/顺序/schema 正确，节点/字节有界 |
| CT-002 | AUTO | 页面伪造来源/旧 generation/超大或畸形快照 | Browser gateway 全部拒绝，不进入内容 Core |
| CT-003 | AUTO | 密码、隐藏表单、脚本、跨源 iframe 与 data/blob/javascript URL | 快照不包含敏感值/脚本/跨源正文，危险 URL 被拒绝或标记 |
| CT-004 | AUTO | 长文、多栏、空页、导航页、重复节点和无限列表 fixture | 确定性主内容/阅读顺序稳定；截断与未包含项显式 |
| CT-005 | AUTO | Markdown golden 覆盖 Unicode、fence、表格、链接和图片引用 | 输出可复现、转义正确、来源默认无 query/fragment，不含 HTML/script 注入 |
| CT-006 | AUTO | 表格/链接/图片/代码块超量、重复和超长字段 | Markdown 输出有界且稳定截断，UI 不阻塞 |
| CT-007 | AUTO | 复制/保存/覆盖/取消/非法文件名/部分写失败 | 只写用户选择路径；原子结果或明确失败，无静默残留 |
| CT-008 | AUTO | 预览长文后导航、关闭标签、切换 Profile | 阅读视图失效，旧结果不覆盖新页；键盘/无障碍可用 |
| CT-009 | CONTRACT | 未选择模型/provider、配置损坏、版本不兼容 | 本地内容能力正常；不发网络请求；模型 feature 明确不可用 |
| CT-010 | AUTO | 比较发送前预览与 Fake provider 实际 payload | 字段逐项一致；无 Cookie/Authorization/完整 query/隐藏 DOM/其他标签 |
| CT-011 | AUTO | Fake provider 正常、超时、取消、流式中断、额度和畸形响应 | 状态收敛、有界重试、不自动换 provider；Markdown 保留 |
| CT-012 | AUTO | 文档摘要/问答输出含有来源和无来源结论 | 输出绑定 snapshot/hash；可定位引用；无来源结论不标成网页原文 |
| CT-013 | AUTO | 文档在发送/流式过程中导航、关闭、撤销或切换 Profile | 取消请求并丢弃旧输出；provider key/task buffer 可验证释放 |
| CT-014 | SECURITY | 视频总结输入来自可见字幕/用户文本，尝试媒体下载、隐藏字幕、DRM 或跨源抓取 | 只接受允许的文本来源；其他路径拒绝且不产生媒体/私网请求 |

## 7A. 本地 Markdown 查看器

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| MD-001 | AUTO | 主菜单文件对话框、拖放 `.md`、omnibox 本地路径三种入口；尝试目录、非 `.md`、超长/控制字符路径与无用户手势触发 | 仅用户手势可打开本地 `.md`；目录与非 `.md` 拒绝；路径/大小有界；页面内容不能触发打开动作 |
| MD-002 | SECURITY | 渲染 golden 覆盖标题/列表/引用/代码块/表格/链接/图片/Unicode；注入原始 HTML、script、事件属性与远程引用 | 输出确定可复现；转义与标签白名单生效；无 script、事件属性、外链脚本/样式或网络请求 |
| MD-003 | AUTO | 超大文件、非法 UTF-8、BOM、CRLF/LF、空文件与二进制伪装 `.md` | 有界加载或稳定错误提示；UI 不阻塞、不崩溃、无半渲染状态 |
| MD-004 | AUTO | 源码/预览切换、分栏模式编辑与实时渲染、滚动位置与连续编辑 | 视图状态一致；预览随编辑确定性更新；旧渲染结果不残留 |
| MD-005 | AUTO | dirty 状态下关闭标签、切换文件与导航离开；确认与取消路径 | 未保存变更显式确认；取消不丢内容；确认后不静默写盘 |
| MD-006 | AUTO | 保存/另存为、覆盖已有文件、只读位置、盘满/权限失败、保存期间外部修改 | 原子写（`.tmp`+rename）或明确失败，无静默残留/半写文件；外部修改冲突显式提示 |
| MD-007 | DEVICE | Windows 实机打开本地 `.md`、分栏编辑、实时预览、保存写回；IME、快捷键、浅/深色与无痕窗口 | 平台行为符合 PRD；无崩溃；无痕会话内可用且不持久化任何痕迹 |
| MD-008 | CONTRACT | 校验锁定 `mermaid` Full tarball、运行时 import closure、manifest/hash/MIME/大小、LICENSE/NOTICE/SBOM；对 ESM 路由尝试未知路径、穿越、编码分隔符、query/fragment、错误方法和断网 | 只打包 manifest 精确枚举的完整浏览器运行时闭包；无 tiny、CDN、npm runtime、缺失 chunk、任意文件路由或许可漂移；普通 Markdown 零 Mermaid 资产读取 |
| MD-009 | SECURITY | 标准 ```` ```mermaid ```` 分别渲染 flowchart、sequenceDiagram、mindmap、architecture-beta、classDiagram、stateDiagram-v2、erDiagram；混入非法 DSL、HTML/script/event、危险链接、外部资源与 CSS URL | 图类型由 Mermaid Full 自行识别并逐 block 输出安全 SVG；strict + SVG policy gate 生效；错误只影响当前 block，其他 Markdown/图表继续可用且无公网请求 |
| MD-010 | PERF | 50 个 Mermaid block 覆盖离屏、重复、错误和快速编辑；切换主题、导航、关闭、Renderer 终止与内存压力 | viewport lazy、有界并发/cache/revision fencing 生效；旧 SVG 不落位；资源停止后回落；记录普通首屏、首次 import、首图/可见图完成、CPU/RSS/UI delay 与资产字节 |
| MD-011 | CONTRACT | 校验 MDV toolbar glyph manifest、动作/label/tooltip/shortcut/context 完整映射；注入重复 ID、未登记文件、外链、事件、script/style/foreignObject/href、固定颜色与错误 viewBox | 24×24 原创 glyph 闭合集完整、`currentColor`、零外部引用；非法资产 fail closed；基线 15 个动作、结构菜单和三视图无缺失/重复 |
| MD-012 | AUTO | 对空/单行/多行/文首文尾/CRLF/UTF-8 选区执行包裹、标题替换、列表/任务/引用、骨架、缩进/反缩进和 GFM 表格列对齐；复现旧 `linePrefix` 重复正文 | 每次产生单一 replacement，选区外字节不变、下一选区确定、重复操作可预测；非结构缩进与非表格对齐 fail closed；旧重复正文用例关闭 |
| MD-013 | DEVICE | macOS/Windows 分别验证图标工具栏 hover/focus tooltip、Meta/Ctrl 快捷键、Tab/方向键/Home/End、overflow、中文/英文 IME、读屏、浅深色、窄分栏与 100%/200% DPI | 平台标签与实际按键一致；IME/AltGr 不误触；工具栏单 Tab stop、焦点可见、点击区达标；无遮挡/截断/横向溢出，快捷键被 Chromium 消费时不虚假展示 |

## 7B. Markdown Runtime Extension Framework

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| MR-001 | CONTRACT | 执行 `markdown-runtime.md` §13 的 `RP-*`/`MF-*`/`OUT-*`/`REG-*` current vectors：四类 node、空 plan、未知 schema/kind、重复 ID、字节/range/revision、预算、额外字段、matcher 冲突、文档 manifest/模块/URL/capability、输出 policy 与 partial publish | current schema 接受且 Level A fallback 保持；四类节点/能力/错误闭合；编译期 registry 原子发布；文档/AI 不能注册扩展或扩权；冲突双方 fail closed；零资产/网络/文件副作用 |
| MR-002 | AUTO | CommonMark/GFM golden 与 inline/block/fence/container 事实并行生成；未知/禁用/大小写/超界 info string | 标准 HTML 逐字节不回退；只有启用的精确 matcher 分发；其余保持安全代码块/文本 |
| MR-003 | SECURITY | 大型 extension 按需加载、重复/并发/失败/超时/导航/Renderer 终止；攻击 manifest 路由与 cache key | 无匹配节点零加载；资源只来自 manifest；错误隔离、generation/cache/清理有界且无正文日志 |
| MR-004 | SECURITY | 多语言 fenced code、未知语言、恶意 token/HTML、浅深主题与 grammar 懒加载 | 代码始终按文本处理；allowlist grammar 本地按需加载；未知语言纯文本回退；无 script/网络 |
| MR-005 | SECURITY | KaTeX inline/block golden、未闭合定界、危险宏/HTML/URL、超长/深嵌套公式、快速编辑 | 仅契约语法启用；危险能力拒绝；单公式错误隔离；字体/CSS 离线且旧结果不落位 |
| MR-006 | AUTO | 重复/空/超深标题、编辑更新、Unicode 搜索、超大文档、取消与无痕关闭 | TOC/Outline 锚点会话内稳定且有界；搜索只查当前内存文档、不持久化 query/路径 |
| MR-007 | SECURITY | ECharts 合法 JSON 与 function/eval/callback/URL/prototype pollution/超大 series/非法 component | 只接受 schema allowlist 纯 JSON；无代码/网络执行；单图错误隔离，Canvas/SVG 与 listener 完整释放 |
| MR-008 | PERF | Mermaid/Highlight/KaTeX/ECharts/Graphviz 混合 50-block 文档，覆盖离屏、重复、主题、编辑、满载和关闭 | 各 extension lazy、有界并发/cache；普通首屏不回退；记录 CPU/RSS/UI delay/资产字节并在停止后回落 |
| MR-009 | SECURITY | Graphviz WASM 处理合法/递归/超大/高耗时/畸形 DOT，覆盖超时、取消、worker crash 与恶意 SVG | CPU/内存/时间有界；worker 可终止；SVG policy 生效；无本机 Graphviz/Java/网络依赖 |
| MR-010 | AUTO | Normal/Presentation 状态切换、分节、键盘翻页、resize、主题、编辑、Esc/关闭与读屏 | parser 不改写 CommonMark HR；Presentation 状态可逆、有界、焦点正确；无 TV/Cast 会话 |
| MR-011 | CONTRACT | 审查 TV/Cast gap：尝试浏览器私有 receiver 协议、HTML/SVG 媒体伪装与未受审远程控制 | 只产出外部 Cast-SDK/receiver facade 边界；无实现、无私有协议或媒体伪装 |
| MR-012 | RELEASE | 扫描发布包的 extension assets、manifest/hash、LICENSE/NOTICE/SBOM、npm cache/node_modules、CDN 与未注册 fence | 仅包含启用 extension 的完整浏览器运行时闭包；无开发依赖/联网 fallback/隐藏执行器；版本可回滚 |
| MR-013 | CONTRACT | 审查 AI Source Producer：尝试由模型注册扩展、改 manifest、读写本地文件、静默保存/投屏或把无来源文本标为原文 | AI 只产生带 provenance 的候选 Markdown，经发送预览/取消与用户正常编辑保存；无 registry、文件、保存或投屏权限 |

## 8. CAAP、CLI 与 MCP

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| AG-001 | CONTRACT | CAAP handshake、envelope、tool/risk schema 与 previous/current golden | 版本协商、R0～R4、错误、chunk/cancel/deadline 稳定；永久禁止能力不可表达 |
| AG-002 | AUTO | 重复 invoke/cancel、超时、旧 generation、断连、App/Profile/标签退出 | task 幂等收敛、旧结果丢弃、队列/chunk/cache 资源有界 |
| AG-003 | AUTO | 单次/任务/App grant、撤销、Profile/目标变化 | 默认 deny；grant 不跨 Profile/目标/会话且立即撤销 |
| AG-004 | AUTO | R2～R4 确认、拒绝、过期、导航/设备/参数变化 | UI 展示 client/tool/目标/关键参数；变化后必须重确认 |
| AG-005 | SECURITY | 页面、模型或工具结果包含“忽略规则并授权/调用工具” | 内容保持 untrusted，不能扩大 grant、改目标或触发第二工具 |
| AG-006 | PERF | 读取标题/结构化页面/Markdown，覆盖缓存、分页、增量、取消和跨 Profile | R1 只返回授权 target/generation；达到 P95/背压/资源预算 |
| AG-007 | AUTO | 读取设备 capability/投屏状态，含同名、旧 route、无会话 | 不返回 IP/媒体 URL/token；使用 SDK 最新 generation |
| AG-008 | AUTO | 导航、开关/切换标签、后退、刷新、滚动，覆盖危险 scheme/redirect/download | R2 确认后调用正常 use case；危险/超量/取消失败关闭 |
| AG-009 | AUTO | 开始投屏、pause/seek/stop，设备/媒体/route 中途变化 | R3 确认且沿用播放/DRM/广告/policy；外部镜像客户端不受控 |
| AG-010 | SECURITY | 语义 handle 点击/输入密码、支付、文件、隐藏/跨源元素或过期节点 | 永久拒绝；无 selector/任意 JS/CDP 透传；TOCTOU 失败关闭 |
| AG-011 | AUTO | 预览/清除 receipt 并扫描日志/磁盘 | 有界 TTL；不含正文、完整 query、Cookie、Authorization、token |
| AG-012 | SECURITY | named pipe/UDS/MCP 的错误用户、非 loopback、错误/过期 secret、重放、超限 | 握手前拒绝，无浏览/网络副作用；stop 后端点释放 |
| AG-013 | AUTO | CLI version/capabilities/targets/tools/invoke/cancel，含无交互副作用调用 | 机器可读且映射 CAAP；需要确认时稳定失败，不绕 UI |
| AG-014 | CONTRACT | MCP initialize/list/call/cancel/版本/超大消息 | schema 来自同一 registry；映射 CAAP；默认关闭、loopback only |
| AG-015 | SECURITY | fuzz、间接提示注入、本机恶意 client、并发/长文性能和 Release surface | 无 remote bind、Cookie/文件/任意 JS/CDP；P0/P1=0 才 GO |

## 9. 页面语义地图与可验证动作

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| AC-001 | CONTRACT | Page/Action/Form/Media/Risk Map、ChangeSet、effect 和 previous/current golden | schema/version/错误稳定；无 CEF/ArkWeb/DOM 对象 |
| AC-002 | SECURITY | 请求 compact/standard/internal-full 并构造超深/超大页面 | 对外字段有界；raw DOM/HTML/CDP/对象指针永不出界 |
| AC-003 | AUTO | 同 generation 重读、导航、Profile 切换、TTL/nonce 重放 action_id | 有效窗口内稳定引用；跨 target/generation/TTL 全部失效 |
| AC-004 | AUTO | role/name/text/结构变化、重复目标、遮挡和动态列表 | 内部多信号定位唯一目标；外部结果无 CSS/XPath/JS selector |
| AC-005 | AUTO | 目标隐藏、不可操作、跨源、被遮挡或页面状态不满足 | precondition fail closed，未产生输入/网络副作用 |
| AC-006 | SECURITY | 页面/模型要求降低风险，包含密码、支付、file、隐藏元素 | 风险只升不降；敏感元素不产生可执行 action_id |
| AC-007 | AUTO | 点击/输入/滚动经 action_id 执行，覆盖取消、deadline 和导航竞态 | 只调用 app-runtime 正常用例；旧 generation 无副作用 |
| AC-008 | AUTO | 动作效果成功、失败、超时、不确定及重复 idempotency key | 仅 verified 报成功；indeterminate 不自动重放，重复副作用被拦截 |
| AC-009 | SECURITY | FormMap 含 required/format/error、密码/支付/file/隐藏字段 | 只返回语义与状态，不返回字段值；敏感/file 不可执行 |
| AC-010 | PERF | 高频动态页生成 ChangeSet、分页、背压、取消和旧 revision | 增量有界且按序；旧增量丢弃；UI/Renderer 不阻塞 |
| AC-011 | AUTO | action 无法安全继续、用户接管、取消或完成后恢复 | 返回可解释 handoff；恢复前重新读取/授权，不继承旧确认 |
| AC-012 | SECURITY | fuzz 地图/handle/effect、视觉 fallback、慢 consumer 与 Release scan | 无 selector/CDP/任意脚本/敏感 surface；性能和资源预算达标 |

## 10. Workflow Learning、Challenge 与个人 Site Skill

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| WF-001 | AUTO | captcha/滑块/登录确认/风控 fixture 与相似非挑战页面 | 确定性检测并进入 AwaitingHuman；误报/漏报证据可定位 |
| WF-002 | SECURITY | 搜索自动解题、打码服务、自动点击/隐藏挑战路径 | 零实现/零网络请求；只允许检测、暂停和用户操作 |
| WF-003 | AUTO | AwaitingHuman 的继续、取消、导航、关闭、超时和无障碍 | 状态收敛、UI 原因清晰、无后台动作 |
| WF-004 | SECURITY | checkpoint 写读删、损坏、过期、Profile/无痕清理并做 canary 扫描 | 加密有界；无 secret/字段值/正文；跨 Profile 不可读 |
| WF-005 | AUTO | 用户完成挑战后页面匹配/漂移/仍有挑战/副作用未知 | 重新 snapshot/risk/grant/precondition；不安全场景终止 |
| WF-006 | AUTO | 已授权动作正常/失败/取消/旧结果和超长任务 | trace 只记录最小语义步骤与 verified effect，容量/TTL 有界 |
| WF-007 | SECURITY | 输入密码、邮箱、token、正文、完整 query 和账户标识 canary | 写盘前 redaction；只保存参数 placeholder 和必要 hash |
| WF-008 | AUTO | verified success、failed、cancelled、challenge 未完成、indeterminate | 只有 verified success 生成候选 Recipe |
| WF-009 | AUTO | 预览技能名称/origin/参数/步骤/风险/权限/数据流后保存、拒绝、变更 | 用户显式确认才保存；预览变化或过期需重确认 |
| WF-010 | AUTO | 两 OS user/Profile、无痕、配额、损坏和 schema migration | Skill Store 加密隔离；失败禁用，不猜测迁移 |
| WF-011 | HARNESS | 本地 fixture/沙箱验证 matcher、参数、步骤和效果 | 结果可重复；不访问公共网络或后台批量巡检生产站点 |
| WF-012 | AUTO | 运行技能时权限撤销、导航、challenge、重复调用和取消 | 每次新 grant/当前 action_id；幂等收敛并支持人工接管 |
| WF-013 | AUTO | 连续成功/失败、禁用、升级、崩溃和回滚 | health/version/rollback 一致；旧版本不能覆盖新状态 |
| WF-014 | AUTO | locator 漂移、challenge、permission、network、effect unknown | 正确分类并生成证据；不把未知失败视为可修复漂移 |
| WF-015 | SECURITY | 唯一低风险变化与高风险/跨源/低置信度/语义变化 | 仅前者可受控修复并验证；其余生成审阅候选或停止 |

## 11. Capability Hub 与 Partner Connector

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| HB-001 | CONTRACT | built-in/personal/partner descriptor、版本、trust/lifecycle 冲突与撤销 | registry 确定、不可未签名覆盖、撤销立即生效 |
| HB-002 | AUTO | 注册 browser/content/cast/handoff 内建能力 | schema 来自权威来源；无重复工具和隐藏强能力 |
| HB-003 | CONTRACT | 相同 RouteInput 重复求值并检查候选和 route_reason | RouteDecision 稳定、理由完整、无 secret/内部 endpoint |
| HB-004 | AUTO | partner/skill/web/human 的 trust、health、risk、偏好组合 | 默认优先级与覆盖规则确定；不可用路径不被选择 |
| HB-005 | SECURITY | route 失败后 fallback，含已提交/未知副作用和不同 provider | 重新 scope/risk/grant/确认/幂等；未知副作用停止 |
| HB-006 | AUTO | 用户查看/覆盖 route，涉及数据外发、成本和风险 | 预览与实际 route/provider 一致；覆盖有范围和到期 |
| HB-007 | AUTO | 两 Profile 的健康/禁用/版本不同 Site Skill | adapter 只暴露当前 owner/Profile 的健康版本 |
| HB-008 | SECURITY | 入站 CLI/MCP search/describe/preview capability | 只经 CAAP；不泄漏 OAuth token、partner endpoint 或隐藏工具 |
| HB-009 | SECURITY | 静态/运行时检查入站 MCP 与出站 connector | crate、registry namespace、session、token、network client 和审计隔离 |
| HB-010 | SECURITY | connector 包/manifest 正常、篡改、降级、撤销、离线和 kill switch | 只启用受信兼容版本；撤销/kill switch fail closed |
| HB-011 | SECURITY | OAuth state/PKCE/redirect/scope、token 到期/撤销和跨 tenant | 防 CSRF/redirect 逃逸/scope 扩张；token vault 串租户零泄漏 |
| HB-012 | SECURITY | endpoint/redirect/DNS rebinding 指向 loopback/private/link-local/metadata，超大响应 | 每跳重验并拒绝；无内部请求；消息/时间/并发有界 |
| HB-013 | SECURITY | Partner MCP tool description/schema/response 注入本地指令或高权限工具 | namespace/schema allowlist 生效；内容保持 untrusted，不扩权 |
| HB-014 | HARNESS | rate/quota、超时、429/5xx、慢流、熔断、恢复和取消 | retry budget/熔断/health 有界；副作用默认不自动 retry |
| HB-015 | SECURITY | 扫描 route/connector 审计、指标、错误和诊断 | 只有 provider/tenant hash/capability/结果/延迟；无正文/token/完整参数 |

## 12. 平台生命周期与外部客户端交接

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| CP-004 | DEVICE | 休眠/唤醒、锁屏、网络切换 | 旧 session 不误恢复；用户可重连 |
| CP-W01 | DEVICE | Windows DPAPI、本地网络/防火墙、多网卡、更新、外部客户端下载/启动 | 生命周期和错误反馈明确；不创建浏览器镜像 session |
| CP-M01 | DEVICE | macOS Keychain、本地网络权限、签名/公证、更新、外部客户端下载/启动 | 生命周期和错误反馈明确；不创建浏览器镜像 session |

## 13. 端到端、稳定性与发布

| ID | 类型 | 前置/步骤 | 预期 |
|---|---|---|---|
| E2E-001 | DEVICE | 打开 clear fixture -> 点击播放 -> 自动发现 -> Direct -> Stop | 首帧、控制、清理闭环 |
| E2E-002 | DEVICE | 登录 clear MP4/HLS -> Direct/Relay -> seek -> Stop | 不泄露凭证，Range/HLS 正常，token 失效 |
| E2E-003 | DEVICE | DRM fixture 播放后点击投屏 | 不产生 Direct/Relay；可显示外部客户端交接但浏览器不采集/绕过 DRM |
| E2E-004 | DEVICE | 无 Direct/Relay 路由时选择外部客户端交接 | 必须确认；取消/未安装/下载或启动失败明确；无投屏 session |
| E2E-005 | DEVICE | 100 次 Direct/Relay 开始/停止/设备切换和交接取消 | 无线程/socket/token/临时目录持续增长 |
| E2E-006 | DEVICE | 8 小时 Direct/Relay 与浏览/Profile 长稳 | 无崩溃；网络与资源使用稳定 |
| E2E-007 | DEVICE | VPN/多网卡/IPv6/防火墙切换 | 不广播错误 LAN 地址，不继续使用过期 route/session |
| UP-001 | RELEASE | 干净机器安装、首次启动、卸载 | 签名正确；用户数据边界符合说明 |
| UP-002 | RELEASE | Stable N -> N+1 覆盖升级 | Profile/Cast-SDK revision 配置按 schema 迁移；失败可恢复 |
| UP-003 | RELEASE | 尝试降级到已知高危内核 | 阻断或明确安全策略，不静默降级 |
| UP-004 | RELEASE | 扫描安装包 SBOM/NOTICE/源码映射 | 组件、许可、版本和产物一致 |
| UP-005 | RELEASE | 扫描 H.264/AAC/CDM 组件 | 只有书面放行组件进入产物 |
