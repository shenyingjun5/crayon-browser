# 桌面三语言本地化契约

- 版本：`desktop-localization-v1`
- 日期：2026-09-02
- 支持语言：英文 `en-US`、简体中文 `zh-CN`、繁体中文 `zh-TW`
- 发布顺序：Windows 10/11 x64 先完成真实产品门禁；macOS 平台特有验证后置且证据独立
- 所属 Roadmap：[LOC 三语言本地化 Roadmap](../plans/localization-roadmap.md)

## 1. 产品口径

本地化是第一期网页 Markdown、LAN Direct/Relay 投屏、本地 Markdown 编辑三大闭环的横切发布质量，不是第四条业务闭环。每个平台的同一候选包必须同时携带三套产品文案与 CEF locale 资源，并按当前用户首选系统 UI 语言自动选择。

一期只支持跟随系统：Browser process 在 `CefInitialize` 之前解析一次语言，形成进程生命周期内不可变的 `LocaleSnapshot`。运行中不监听或热切换；系统语言改变后必须完整退出并重启产品。Preference/Profile、页面、URL、CLI、Agent、环境变量和远程输入均不能覆盖产品语言。

## 2. 语言协商

### 2.1 输入和回退

平台 adapter 只能提供当前用户有序 UI 语言 tag；区域格式、时区、键盘布局和 IME 不是语言选择输入。

| 首个可支持 tag | `AppLocale` | CEF locale | HTML `lang` |
|---|---|---|---|
| `zh-CN`、`zh-Hans-*`、`zh-SG` | `kZhCn` | `zh-CN` | `zh-CN` |
| `zh-TW`、`zh-Hant-*`、`zh-HK`、`zh-MO` | `kZhTw` | `zh-TW` | `zh-TW` |
| `en-*` | `kEnUs` | `en-US` | `en-US` |
| 裸 `zh` | `kZhCn` | `zh-CN` | `zh-CN` |
| 空、非法、超界、API 失败或无支持项 | `kEnUs` | `en-US` | `en-US` |

协商按输入顺序选择第一个支持项。ASCII 比较大小写不敏感，输入中的 `_` 先归一为 `-`；单 tag 最大 64 字节，最多 32 项，总输入最大 4096 字节。非法或超界项稳定忽略，最终没有可选项则回退 `en-US`。

Windows 只使用 `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` 取得有序用户 UI 语言；API 失败向 resolver 提供空列表，不能改读用户区域格式。macOS 使用 `CFLocaleCopyPreferredLanguages`。平台 API、CEF、Win32 和 AppKit 类型不得进入共享 resolver。

### 2.2 网页可观察语言

`LocaleSnapshot` 同时派生 CEF locale、产品 catalog 和固定最小 Accept-Language：

| `AppLocale` | `CefSettings.locale` | `accept_language_list` |
|---|---|---|
| `kZhCn` | `zh-CN` | `zh-CN,zh,en-US,en` |
| `kZhTw` | `zh-TW` | `zh-TW,zh,en-US,en` |
| `kEnUs` | `en-US` | `en-US,en` |

不得复制完整系统语言偏好到 header、日志、诊断或 Profile。`crayon://` 页面必须输出 snapshot 对应的 `html lang`；本地 fixture 逐字核对 `Accept-Language`、`navigator.language` 和 `navigator.languages`。

## 3. 资源与所有权

`browser/shared-ui/locales/{en-US,zh-CN,zh-TW}.json` 是唯一手写产品文案事实源。三份文件必须满足：

- key 集合、值类型、placeholder 集合和 accelerator 语义一致；key 稳定且只增不改。
- UTF-8、非空、无重复 key、无不允许控制字符；动态内容使用有界 typed placeholder，不拼接句子片段。
- `zh-TW` 必须人工审校，不能在运行时从简体转换。
- 缺 key、错误 placeholder 或过期生成结果必须在检查/构建阶段失败；运行时不能静默混用其他语言。

确定性 generator 从三份 JSON 和 manifest 生成并提交：

- 共享 C++17 只读 `LocaleCatalog` 数据。
- Windows RC 字符串资源。
- macOS `en/zh-Hans/zh-Hant.lproj` 的 `.strings`。

产品构建消费已提交生成物，不要求 Node，不在运行时解析 JSON、读取 locale 文件或访问网络。`browser/shared-ui/localization` 拥有 `AppLocale`、`LocaleSnapshot`、resolver 和 catalog；业务 surface 只接收 snapshot/catalog 的只读值，不重复判断平台或 tag。

CEF Windows Release staging 只装配 `en-US/zh-CN/zh-TW` locale pak 及固定 distribution 必需的 gender pak；不得修改或删除上游 CEF cache。macOS bundle 只声明三种受支持语言。

## 4. 文案边界和术语

必须本地化：产品可见文本、按钮/菜单、状态/错误、tooltip、占位提示、窗口/页面标题，以及 `aria-label`、原生 accessibility name/description。稳定错误码、日志 key、协议字段和 wire 值保持英文机器语义，用户界面通过独立 key 显示对应文案。

冻结术语：

| 概念 | `en-US` | `zh-CN` | `zh-TW` |
|---|---|---|---|
| 产品名 | Crayon AI Agent Cast Browser | 蜡笔 AI Agent 投屏浏览器 | 蠟筆 AI Agent 投影瀏覽器 |
| 浏览器简称 | Crayon Browser | 蜡笔浏览器 | 蠟筆瀏覽器 |
| 投屏 | Cast | 投屏 | 投影 |
| 直接投屏 | Direct cast | 直接投屏 | 直接投影 |
| 中继投屏 | Relay cast | 中继投屏 | 中繼投影 |
| 外部客户端交接 | Open the Crayon cast client | 打开蜡笔投屏客户端 | 開啟蠟筆投影用戶端 |
| 无痕 | Incognito | 无痕 | 無痕 |
| 源码/预览/分栏 | Source / Preview / Split | 源码 / 预览 / 分栏 | 原始碼 / 預覽 / 分割 |
| 接收端 | Receiver | 接收端 | 接收端 |

`Markdown`、`Mermaid`、`KaTeX`、`CEF`、`Agent`、`Profile`、`Direct`、`Relay` 等产品/技术专名按表中既定写法保留；不得翻译网页内容、用户 Markdown、网页标题、URL、文件路径、设备名或接收端返回内容。品牌最终写法如需调整必须先走 RNM/BRD 决策。

## 5. 第一期 surface 清单

| Surface owner | 必须覆盖的可见面 | 资源 key/来源 |
|---|---|---|
| CEF/Chromium | 菜单、上下文菜单、错误页、原生按钮、文件选择器系统文本 | CEF locale pak；自有附加项来自 catalog |
| Window/chrome | 应用/窗口标题、导航、omnibox、标签、窗口按钮、站点身份、安全反馈 | `app.*`,`nav.*`,`address.*`,`omnibox.*`,`error.*` |
| New Tab | 普通/无痕标题、说明、地址栏提示、固定入口及错误 | `new_tab.*` |
| Browser daily UI | 书签、历史、下载、设置、Profile/无痕、权限、上下文菜单、更新 | 对应 BUX catalog key；缺失 key 在 LOC-02/04 补齐 |
| Page Markdown | 预览、复制、另存为及结果状态 | `page_markdown.*` |
| Local MDV | 页面标题、视图、状态、保存确认、工具栏、tooltip、Mermaid 错误/弹层 | `mdv.*` |
| Cast | 按钮、设备选择器、投屏码、Direct/Relay、播控、拒绝和外部交接 | `cast.*` |
| Autofill | 保存、建议、编辑、删除、无痕说明和字段名 | `autofill.*` |
| 第二期关闭面 | Agent confirmation、Workflow handoff、Capability route | 现有 `agent.*`,`workflow.*`,`capability.*` 保持三语言 parity；有资源不代表 Release 开启 |
| Native package | Windows VERSION/STRINGTABLE、任务栏名称；macOS InfoPlist/菜单 | generator 输出 |

LOC-04 必须以 Release 可达调用图再次扫描，任何产品可见硬编码都要迁移或明确证明属于网页/用户/设备/协议数据。翻译不能改变授权、删除、投屏拒绝、外部交接、隐私清理失败等安全语义。

## 6. 可访问性、布局和输入

- 可见 label、tooltip、accessibility name 和快捷键提示必须由同一 key 族派生；隐藏图标按钮必须有本地化 accessible name。
- 三语言分别覆盖浅/深主题、窄/宽窗口、100%/200% DPI；文本不能遮挡安全信息、确认按钮或投屏状态。
- Windows 真机覆盖 Narrator、简体/繁体/英文 IME 和键盘操作；macOS 后续覆盖 VoiceOver 与对应输入法。
- locale 不改变快捷键命令、命令 ID、焦点顺序、语义 handle、授权、路由或状态机。

## 7. 验证与完成证据

共享自动化复用 `UX-001/002/004/013/016/018` 和 `RG-006`，不新增唯一测试 ID。每个 LOC 原子任务记录 commit/range、平台/架构、配置、完整命令、退出码、数量、耗时和未覆盖项。

Windows `DONE` 至少需要：

1. Windows 10/11 x64 Debug/Release build 与完整 CTest。
2. 三种真实用户 UI 语言和一个非支持语言，从 clean Profile 启动真实 CEF。
3. Chromium、自有页面、原生 UI、`html lang`、JS language 和本地 header fixture 一致。
4. 三语言 package/resource allowlist、artifact scan、大小/hash/NOTICE。
5. Narrator、IME、100%/200% DPI、浅深色、窄宽、重启切换及安装/升级/回滚适用回归。

Mock tag、单元测试、截图 OCR、编译通过或 capability model 不能代替真实系统语言与 CEF 产品证据。macOS 的共享构建不能替代真实 macOS locale、VoiceOver、IME、签名包或原生硬件门禁。

## 8. 非目标

- 不支持第四种语言、RTL、在线翻译、网页内容翻译、用户翻译包或运行时插件。
- 一期不提供手动语言选择、热切换或 BUX-13 preference schema 升级。
- 不改变 Cast-SDK/CAAP/Relay 协议、业务错误码、Profile 隔离、授权和安全行为。
- 不把 Node/JSON parser/翻译服务加入 Release 生产依赖图。
