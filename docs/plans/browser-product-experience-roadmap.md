# BUX：桌面浏览器产品体验 Roadmap

状态：`BUX-01/02 DONE`，`BUX-03 VERIFIED`；Chrome-inspired 信息架构、共享 design token、标题栏/标签栏/导航栏自有 glyph 与平台适配边界已经冻结，Windows UI shell、typed command、focus owner 与 engine event adapter 骨架已完成实机门禁。当前阶段优先完成 Windows 全部基础浏览器功能，macOS 对齐后置。本 Roadmap 把“基本浏览器有的功能”拆成可审查原子任务；视觉、品牌、内置页面与服务均为蜡笔自有实现。

## 产品设计结论

- 窗口顶部采用两层紧凑结构：标签栏/窗口控制，导航栏（后退、前进、刷新/停止、omnibox、书签、投屏、主菜单）。投屏是蜡笔的一级产品动作，但状态只来自受审投屏用例，不能由页面直接点亮。
- 新标签页使用本地 `crayon://newtab` 资源：居中 omnibox、用户固定快捷入口、最近关闭恢复和投屏入口；不放新闻流、广告、云推荐或默认公网内容。无痕页不显示历史/常用站点。
- 使用共享 design token、蜡笔品牌色和平台窗口适配；不复制 Chrome logo、Google 图标、Google 账号/同步、专有 URL 或受商标保护页面。
- 密码、支付卡、账号/云同步和扩展安装会改变 credential、供应链或远端服务边界，不在本 Roadmap 偷跑；若进入产品，必须分别建立独立 Roadmap、threat model、数据迁移和 Release 门禁。地址自动填充仅在 `BUX-17`/PRV 安全边界内提供。
- Windows/macOS 共用状态机、命令 registry、组件契约和本地化；CEF/Win32/AppKit 只在 adapter。HarmonyOS 后续复用领域契约，不复用桌面平台像素假设。

## 原子任务

| ID | 状态 | 依赖 | 目标路径 | 单一交付 | 测试/验收 |
|---|---|---|---|---|---|
| BUX-01 | DONE | CEF-01D | `docs/current/browser-ux.md`,`browser/shared-ui/design` | 冻结 Chrome-inspired 信息架构、密度、token、组件状态、键盘/无障碍和品牌禁用规则 | UX-001；light/dark、窄/宽窗口、100%/200% 规格 golden |
| BUX-02 | DONE | BUX-01,CEF-02W | `browser/shared-ui/shell` | Windows 首发 UI shell、命令 registry、focus owner 与 engine event adapter 骨架；共享层保持跨平台 | UX-001；重复 command、旧 tab event、窗口释放；Windows 实机 |
| BUX-03 | VERIFIED | BUX-02,CEF-03 | `browser/shared-ui/new-tab` | 本地 `crayon://newtab`、普通/无痕差异、固定快捷入口模型 | UX-002；零默认公网请求、损坏配置、安全 resource handler；待 Windows CEF 构建/实机门禁后转 `DONE` |
| BUX-04 | TODO | BUX-02,CEF-03,PRV-06 | `browser/shared-ui/omnibox` | omnibox 编辑/提交、URL/搜索判定、建议 owner 与 provider 配置契约 | UX-003；scheme/长度/取消/旧建议/Profile 隔离 |
| BUX-05 | TODO | BUX-04 | `browser/shared-ui/navigation` | 后退/前进/刷新/停止、加载状态、站点身份和页面动作绑定 | UX-004；导航竞争、证书/HTTP/HTTPS、页面伪造安全 UI |
| BUX-06 | TODO | BUX-02,CEF-03 | `browser/shared-ui/tabs/basic` | 新建/切换/关闭/拖动/恢复关闭标签与 active/focus 状态机 | UX-005；重复关闭、旧事件、崩溃恢复、释放 |
| BUX-07 | TODO | BUX-06 | `browser/shared-ui/tabs/advanced` | 固定、复制、静音、搜索、分组和跨窗口移动 | UX-006；容量/顺序/音频/多窗口与键盘操作 |
| BUX-08 | TODO | BUX-06,CEF-05 | `browser/shared-ui/windows` | 多窗口、受控 popup、全屏与画中画 UI/策略绑定 | UX-007；来源、取消、焦点、关闭与恢复 |
| BUX-09 | TODO | BUX-03,PRV-03 | `browser/bookmarks`,`browser/shared-ui/bookmarks` | 书签 store、栏、管理器、搜索、导入/导出 | UX-008；事务/损坏/超大/重复/跨 Profile |
| BUX-10 | TODO | BUX-06,PRV-03 | `browser/history`,`browser/shared-ui/history` | 历史、最近关闭、搜索、范围删除与恢复 | UX-009；无痕零持久化、删除边界、跨 Profile |
| BUX-11 | TODO | CEF-05,BUX-02 | `browser/downloads`,`browser/shared-ui/downloads` | 下载 shelf/页、暂停/恢复/取消/重命名/打开位置和危险状态 | UX-010；路径、重复、断点、外部打开、失败释放 |
| BUX-12 | TODO | BUX-05,PLT-02 | `browser/shared-ui/page-tools` | 查找、缩放、全屏、打印/PDF 与保存页面命令 | UX-011；取消/失败/输出路径/跨 Profile |
| BUX-13 | TODO | BUX-01,PRV-03 | `browser/preferences`,`browser/shared-ui/settings` | 版本化 preference store 与启动/搜索/外观/下载/内容设置 UI | UX-012；migration/corrupt/reset/restart readback |
| BUX-14 | TODO | CEF-05,BUX-05,BUX-13 | `browser/shared-ui/site-controls` | 站点权限、安全信息、证书错误、popup 与外部协议确认 UI | UX-013；origin/Profile/TTL/取消/伪造/危险 scheme |
| BUX-15 | TODO | CEF-04,PRV-01..04,BUX-06 | `browser/shared-ui/profiles`,`browser/session` | Profile picker、普通/无痕窗口、启动会话与崩溃恢复编排 | UX-014；清理失败、无痕不恢复、旧 session、跨 Profile |
| BUX-16 | TODO | BUX-05,BUX-11,PLT-02 | `browser/shared-ui/context-menu` | 上下文菜单、拖放、剪贴板与受控本地文件入口 | UX-015；上下文最小化、路径/scheme、取消/外部动作 |
| BUX-17 | TODO | BUX-13,PRV-05,PRV-11 | `browser/autofill/address`,`browser/shared-ui/autofill` | 仅地址/联系信息的本地保存确认、匹配、编辑和删除；明确排除密码/支付 | UX-017；PII redaction、无痕、Agent 不可见、跨 Profile |
| BUX-18 | TODO | BUX-01..17,CEF-14,PRV-12 | `tests/e2e/desktop/browser-ux`,`docs/current` | Windows/macOS 浏览器体验、性能、包体、隐私与品牌总 Review | UX-001..018；Debug/Release、P0/P1=0、未覆盖真机明确 |

## 开发规则

- 每次只领取一项；涉及公共 schema、持久化格式、credential、扩展或云服务时先建立独立 Roadmap，不扩大当前任务。
- UI 只发送 typed command 到 app/runtime 或 engine adapter；组件不持 CEF 指针、Profile 路径、Cast-SDK handle 或 Relay token。
- 所有可见文案进入本地化资源；图标来自自有 glyph/品牌资产，不从 Chrome 安装包或 Google 页面提取。
- 内置页面只从签名/编译期本地资源提供，有独立 origin、CSP、资源上限和禁用任意网络/脚本能力；网页内容不能绘制或覆盖浏览器安全 UI。
- 自动化只使用本地 fixture；UI golden 不能替代键盘、读屏、IME、多屏/DPI 与真实平台验证。

## BUX-01 原子范围（已完成）

- 状态：`DONE`；依赖 `CEF-01D DONE`。
- 单一目标：冻结桌面浏览器顶层信息架构和平台中立视觉契约，提供可由后续 Windows/macOS UI shell 消费的版本化 design token、自有 SVG glyph manifest 与确定性规格 golden；本任务不把空白 CEF bootstrap 改造成完整浏览器。
- 输入：当前 PRD/架构、`app-icon-v1` 品牌契约、Chrome/Chromium 用户熟悉的桌面信息架构与快捷键心智、Windows/macOS 100%/200% DPI 和窄/宽窗口需求。
- 输出与允许修改：`docs/current/browser-ux.md`；`browser/shared-ui/design/` 下的平台中立 token、SVG glyph、manifest、规格 golden、contract 测试和模块说明；本 Roadmap/current/index 的状态与证据。
- 禁止修改：`assets/brand/generated/` 与品牌生成器、`browser/cef-shell` 平台窗口实现、CEF/Win32/AppKit/ArkUI adapter、Rust workspace/schema、Cast-SDK、媒体/Relay/Profile/权限/Agent 逻辑；不得复制 Chrome/Google 图标或专有资源，不得把 App 图标复用为投屏、连接、权限、Agent 或错误状态。
- 图标边界：Windows/macOS 窗口身份图标只消费 `app-icon-v1` 的平台 `micro` 资产；后退、前进、刷新、停止、主页、标签、书签、下载、投屏、菜单等功能图标使用自有单色 glyph，并通过语义 role/state 着色，不在 SVG 内硬编码业务状态颜色。
- 错误与边界：未知 token/glyph/state、重复 ID、非 24×24 viewBox、外部引用、脚本/事件属性、内联位图、硬编码品牌文件路径和缺失 light/dark/窄/宽/100%/200% 组合必须由 contract 稳定拒绝；本任务不声明像素级真机一致。
- 验收与测试：UX-001；检查信息架构、命令/焦点/无障碍契约、功能 icon role 完整性和品牌禁用；使用确定性 contract 生成/比对 light/dark × narrow/wide × 100%/200% 的八份规格 golden；运行 `scripts/check.ps1 brand-assets`、适用 fast/security、格式检查和 `git diff --check`。
- 明确不做：实际 CEF UI shell、按钮点击、标签状态机、本地新标签页、omnibox、投屏业务绑定和平台截图；分别由 `BUX-02..06`、`CEF-02W/03/08/13` 实现和验证。

完成记录（2026-08-11）：

- 失败基线：先建立独立 CMake/Node contract；首次 `ctest --test-dir .cache/build/browser-design --output-on-failure` 因 `tokens.json` 不存在按预期 `0/1` 失败，证明验收不是在缺失实现时预先通过。
- 规范与资产：新增 `browser-design-v1` 两层桌面信息架构、DIP 密度、light/dark 语义色、窄/宽优先级、组件状态、键盘/焦点/无障碍契约；21 个自有 24×24 单色功能 glyph 覆盖窗口控制、标签、导航、书签、投屏和常用入口。应用身份继续只引用 `app-icon-v1:micro`，没有修改 `assets/brand/generated/`。
- Golden：确定性生成并比对 light/dark × narrow/wide × 100%/200% 共 8 份规格 golden；重复生成前后 SHA-256 集合不变。全部 21 个 SVG 经 XML 解析器验证为 well-formed。
- 自动验证：独立 configure 成功，`ctest --test-dir .cache/build/browser-design --output-on-failure` 为 `2/2` 通过；正向 contract 检查闭合 token/role/surface/state、App-icon 禁用、SVG 主动内容/外链/状态色和 golden，拒绝 contract 证明缺投屏 role、外链 SVG、stale golden、缺主题、未审状态和未注册图标均 fail closed。Node 三个脚本 `--check`、`scripts/check.ps1 brand-assets`、`fast`、`security` 和 `git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；发现并关闭 1 个 P1（SVG 主动内容与 token/surface/state 未完全闭合），最终 P0/P1/P2/P3 均为 `0`。
- 未覆盖与风险：这些是平台中立规范、SVG 和数据 golden，不是 Windows/macOS 像素截图或交互 UI。实际 shell 接入、tooltip 本地化、平台高对比度/IME/读屏/多屏 DPI 和投屏状态绑定分别由 `BUX-02`、`UX-016`、`CEF-13`、`BUX-18` 验证；不得把 BUX-01 描述为完整浏览器 UI 已完成。

## BUX-02 原子范围（Windows 首发 UI shell 骨架）

- 状态：`DONE`；依赖 `BUX-01 DONE`、`CEF-02W DONE`，并消费已完成的 `CEF-03` Windows 窗口/标签生命周期，不修改其完成证据。
- 单一目标：建立平台中立、可独立测试的 shell 状态 owner，把 UI 输入归一为 typed command，把 engine 事件归一为有 generation 的 shell view state，并在 Windows CEF Chrome-style 壳接入命令观察、focus 释放和窗口退出；本任务只交付后续自有控件可依赖的骨架，不实现 BUX-03..17 的具体产品功能。
- 输入：`browser-design-v1` 的两层信息架构、command/focus/无障碍契约，冻结的 `browser/engine-api` 事件类型，以及 `CEF-03` 的 TabController 回调与 Windows 实机基线。
- 输出与允许修改：新增 `browser/shared-ui/shell/` 生产状态、typed command registry、focus owner、engine event adapter 和独立单测/CMake；为装配只允许修改根/CEF shell CMake、`browser/cef-shell/src/browser/window` 的窄 callback/command hook、Windows `app.*`、CEF/source contract，以及本 Roadmap/current/总 Roadmap 索引。生产实现不超过 6 个新文件，Windows adapter 不得进入共享目录。
- 禁止修改：`browser/engine-api` 公共头与语义、BUX-01 token/glyph/golden、macOS/Harmony 源码、Profile/持久化、起始页、omnibox 判定、最终标签交互、下载/权限/投屏业务、Cast-SDK、Relay、Agent/模型；不得引入 JSON/UI framework/第三方图标依赖，不得暴露 CEF/Win32 handle 给共享 shell。
- 状态与错误边界：command 必须使用闭合 enum 与单调 sequence；未知 enum、重复/旧 sequence、无 active tab、窗口 closing 状态稳定拒绝且不产生第二次副作用。Focus token 绑定 owner generation 与可选 tab；tab/window 销毁后旧 token 不得恢复焦点。Engine event adapter 只接受已创建 tab，按 navigation ID 拒绝旧导航事件；重复 close 幂等；shutdown 后忽略全部迟到事件并释放 target/sink 引用。
- Windows adapter：CEF command ID 只在 CEF 层以 `cef_id_for_command_id_name()` 做版本安全映射；原生 Chrome shortcut 可在自有控件完成前保持 pass-through，但必须先归一为 typed command/focus observation，不能把 IDC 数值泄漏到共享层或同步伪造 engine 完成。窗口关闭必须先使 shell inactive，再走 CEF-03 已验证的 browser 关闭链。
- 验收与测试：UX-001 的骨架部分覆盖 command/focus 顺序与两层 surface role；单测覆盖正常 dispatch、未知/重复 command、旧 tab/navigation event、重复 close、focus token 失效、shutdown/迟到事件和释放。执行独立 shell configure/build/ctest、Windows Debug/Release build+ctest、适用 clang-format、`scripts/check.ps1 fast/security`、`git diff --check`；Windows 实机覆盖 Ctrl+L/T/W/R、焦点切换、整窗退出和完整路径零残留。
- 明确不做：本任务不宣称自有像素 UI、起始页、omnibox、标签拖动/恢复、书签/下载/Profile/菜单或投屏按钮已完成；具体可见控件与行为分别由 `BUX-03..17`，自有 Views/location toolbar 接入由 `BUX-04..06`，跨平台 DPI/IME/读屏总验收由 `BUX-18/UX-016` 完成。

完成记录（2026-08-16）：

- 失败基线：先加入独立 shared shell target/test；首次 configure 因 `src/command_registry.cc` 不存在按预期失败。Windows 真机首次启动又触发 `tab_controller.cc` 的 `CefCurrentlyOn(TID_UI)` 断言；随后先扩展 `window_adapter_contract`，修复前该契约按预期失败，再把 callback 安装移动到 `OnContextInitialized` 的 CEF UI 线程并在首个 `CreateMainWindow` 前完成。
- 实现：新增平台中立的闭合 `ShellCommand`/`CommandOrigin`、单调 sequence registry、两层 surface/focus 顺序、可失效 focus token、Profile/Tab/Navigation view state 和 `EngineEventAdapter`；重复 close 幂等，未知/退休 tab、旧导航 generation、shutdown 后迟到事件稳定拒绝。Windows CEF adapter 以 `cef_id_for_command_id_name()` 映射原生 Chrome command，先归一观察再 pass-through，不泄漏或硬编码 IDC 数值；最后浏览器关闭时先 shutdown shell。
- 构建修正：Windows 首次链接暴露官方 CEF 静态 CRT 与新 target 默认动态 CRT 不一致，根 CMake 仅在 `MSVC && CRAYON_ENABLE_CEF` 统一为静态 CRT；未使用 `NODEFAULTLIB` 或跨平台全局特例。
- 自动验证：独立 shared shell configure/build 成功，`ctest --test-dir .cache/build/shared-shell --output-on-failure` 为 `6/6`；Windows Debug/Release build 均成功，修复后 `ctest --preset windows-cef-debug -C Debug/Release --output-on-failure` 均为 `11/11`；`scripts/check.ps1 fast`、`security`、Google-style clang-format dry-run 与 `git diff --check` 通过。MSBuild 保留既有共享中间目录与 CEF delay-load warning，无编译/链接错误。
- Windows 实机：Debug 启动为唯一 `CrayonBrowser.exe` 窗口；`Ctrl+L` 聚焦并选中地址栏，`Ctrl+T` 从 1 个标签增加到 2 个，`Ctrl+W` 恢复为 1 个，`Ctrl+R` 后窗口/文档仍正常；Debug 与 Release 均由 `Ctrl+Shift+W` 关闭，轮询结果为窗口 `0`、运行 app/process `0`。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；Windows 真机门禁发现并关闭 1 个 P1（CEF UI 线程前安装 callback），最终 P0/P1/P2/P3 均为 `0`。共享目录未出现 CEF/Win32/AppKit/ArkWeb 类型，新增生产文件均低于规模提醒线。
- 未覆盖与风险：当前仍是供后续自有控件消费的 shell 骨架；Windows 可见界面继续使用 CEF Chrome-style 原生 UI，`NewTab/FocusOmnibox` 的 product-origin target 在自有控件完成前保持 unavailable，原生 shortcut 只做 typed observation/pass-through。未做 macOS 实机、像素 UI、IME/读屏/多屏 DPI、起始页/omnibox/完整标签功能；分别由 `CEF-02M`、`BUX-03..06`、`BUX-18/UX-016` 完成。

## BUX-03 原子范围（本地新标签页）

- 状态：`VERIFIED`；依赖 `BUX-02 DONE`、`CEF-03 DONE`。平台中立实现和自动化契约已验证，等待 Windows CEF Debug/Release 与实机门禁后转 `DONE`。
- 单一目标：交付编译期内置、无默认公网请求的 `crayon://newtab/` 页面与平台中立模型；普通模式可显示用户已固定的快捷入口，Private 模式隐藏快捷入口和任何历史/最近关闭数据，并为后续 omnibox、书签和投屏 UI 保留显式但不具备业务副作用的入口语义。投屏作为一级入口在两种模式中均保留，但在 `CEF-13` 前保持禁用。
- 输入：`browser-design-v1` 起始页信息架构、`ShellState` 的 Profile/Tab 事实、调用方提供的本地化字符串，以及用户配置层未来可提供的固定快捷入口快照。本任务不建立持久化 store。
- 输出与允许修改：新增 `browser/shared-ui/new-tab/` 的模型、HTML/resource builder、独立 CMake 与测试；在根 CMake 接入独立 target；仅为在 Browser/Renderer/GPU 等进程一致注册 `crayon` 标准自定义 scheme、提供只读 resource handler 和把初始页切换为 `crayon://newtab/`，允许窄修改 `browser/cef-shell` 的 Windows app/process adapter、CMake 与 source contract；新增起始页可见文案只进入 `browser/shared-ui/locales/`；同步 current/总 Roadmap/索引。
- 禁止修改：`browser/engine-api`、既有 shell command/schema、Profile 持久化、书签/历史/最近关闭 store、omnibox URL/搜索判定、Cast-SDK/Relay/投屏状态机、Agent/模型、macOS 平台行为和 BUX-01 生成资产；不引入 JS、远程字体、外部图片、第三方 UI/JSON 依赖或页面到 Browser 的命令桥。
- 模型与边界：固定入口最多 12 项；只接受 `http`/`https` 且具备非空 host 的 URL、非空且有长度上限的标题，非法/重复项在构建快照时稳定丢弃且保持输入顺序。Private 模式即使收到入口或最近关闭输入也输出空集合。模型不保存 Profile 路径、浏览历史、凭证、query 内容或网页正文。
- Resource 安全：只服务精确 `GET`/`HEAD crayon://newtab/`；拒绝 userinfo、非空端口、query、fragment、子路径、未知 host/method 和路径穿越；响应固定 UTF-8 HTML、`Cache-Control: no-store`、`X-Content-Type-Options: nosniff` 与不允许网络、脚本、frame、表单提交的 CSP。HTML/CSS 和本地化/用户标题均有大小上限，动态文本必须转义；HEAD 不返回 body。handler 不启动线程、网络、文件 IO 或计时器，重复请求互不共享可变状态，释放后无回调。
- 验收与测试：UX-002；独立 configure/build/ctest 覆盖普通/Private、零/上限/超量入口、非法/重复 URL、危险标题转义、损坏配置降级、精确 request allowlist、GET/HEAD、header/CSP、超大字符串与确定性输出；CEF source contract 检查 scheme 注册为 standard/local/secure 且 handler 只消费编译期资源。执行适用 format、`scripts/check.sh fast/security`、CEF 可用平台 build/ctest 和 `git diff --check`。
- 明确不做：真实快捷入口持久化与编辑、最近关闭恢复、omnibox 提交、书签、投屏动作、像素级 Windows/macOS 验收和 Private Profile 创建/清理；分别由 `BUX-04/06/09/10/13/15`、`CEF-13`、`BUX-18` 与 `PRV` 任务完成。本任务中的对应控件只能是静态、无副作用或隐藏状态。

### BUX-03 验证与 Review 记录（2026-08-16）

- 实现：新增平台中立 `new-tab` 模型、编译期本地化 HTML/resource builder 和独立测试 target；Windows Browser/child process 一致注册 `crayon` standard/local/secure scheme，Browser process 只为精确 `GET/HEAD crayon://newtab/` 返回内存资源，并将初始页从 `about:blank` 切换为本地新标签页。Private 模型丢弃全部快捷入口；两种模式均保留禁用的投屏一级入口，不建立持久化、命令桥或公网资源。
- 失败基线：在仅接入新 target、尚未加入实现文件时执行 `cmake -S . -B /tmp/crayon-bux03-failing -DCRAYON_ENABLE_CEF=OFF -DCRAYON_BUILD_TESTS=ON`，configure 按预期因缺少 `browser/shared-ui/new-tab/src/new_tab.cc` 失败，证明测试/build graph 先于实现建立。
- `cmake -S . -B /tmp/crayon-bux03 -DCRAYON_ENABLE_CEF=OFF -DCRAYON_BUILD_TESTS=ON` 与 `cmake --build /tmp/crayon-bux03 -j2`：PASS；平台中立 C++17 target 在 AppleClang `-Wall -Wextra -Wpedantic -Werror` 下构建通过。
- `ctest --test-dir /tmp/crayon-bux03 --output-on-failure -R 'browser_(engine|shared_shell|new_tab)_contract'`：PASS，3/3；覆盖普通/Private、入口过滤/去重/上限、host/URL 边界、危险标题转义、损坏 locale、精确 GET/HEAD allowlist、确定性、64 KiB 页面上限、CSP/no-store 与空 HEAD body。
- `cmake -DCRAYON_CEF_SHELL_SOURCE=browser/cef-shell -P browser/cef-shell/tests/source_contract.cmake` 与 `window_adapter_contract.cmake`：PASS；确认 scheme 在 Browser/child process 一致注册、handler/CMake 接线和本地资源安全 header。CEF API 对照固定 revision `8042e43` 和官方接口复核。
- `bash scripts/check.sh fast`：PASS；沙箱内首次因 loopback fixture `Operation not permitted` 失败，允许本地端口的同命令复跑后 guard/format/formal workspace/legacy unit 全部通过。
- `bash scripts/check.sh security`：PASS；沙箱内首次因 security fixture 无法绑定 loopback 失败，允许本地端口的同命令复跑后 guard/relay unit/relay security 全部通过。
- `xcrun clang-format --style=Google --dry-run --Werror <BUX-03 C++ files>` 与 `git diff --check`：PASS。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；发现并关闭 1 个 P1（CEF response 的 MIME type 与 charset 原混合传入 `SetMimeType`，已拆为 `SetMimeType("text/html")`/`SetCharset("utf-8")` 并补契约）。最终未关闭 P0/P1/P2/P3 均为 `0`，结论 `APPROVE`。
- 未覆盖与风险：当前 macOS 环境未安装固定版 Windows CEF distribution，Windows Debug/Release configure/build/ctest、真实 `CrayonBrowser.exe` 页面加载、零公网请求观察、普通/Private Profile 实际接线与退出残留进程门禁均 `NOT_RUN`；因此任务保持 `VERIFIED`，不得转 `DONE`。Private Profile 的创建/清理属于 `PRV`，像素/键盘/读屏/高 DPI 跨平台验收属于 `BUX-18`。
