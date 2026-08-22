# BUX：桌面浏览器产品体验 Roadmap

状态：`BUX-01..03 DONE`，`BUX-04A DONE`，`BUX-05 DONE`，`BUX-06 DONE`，`BUX-07 DONE`，`BUX-08 DONE`；Chrome-inspired 信息架构、共享 design token、标题栏/标签栏/导航栏自有 glyph 与平台适配边界已经冻结，Windows UI shell、typed command、focus owner、engine event adapter 与本地新标签页已完成实机门禁。当前阶段优先完成 Windows 全部基础浏览器功能，macOS 对齐后置。本 Roadmap 把"基本浏览器有的功能"拆成可审查原子任务；视觉、品牌、内置页面与服务均为蜡笔自有实现。

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
| BUX-03 | DONE | BUX-02,CEF-03 | `browser/shared-ui/new-tab` | 本地 `crayon://newtab`、普通/无痕差异、固定快捷入口模型 | UX-002；零默认公网请求、损坏配置、安全 resource handler |
| BUX-04A | DONE | BUX-02,CEF-03,FND-11 | `browser/shared-ui/omnibox/core` | URL/搜索判定引擎、本地建议索引结构、建议状态机与闭合编辑命令 | UX-003A；scheme/长度/取消/旧建议/本地索引边界 |
| BUX-04B | DONE | BUX-04A,PRV-06 | `browser/shared-ui/omnibox/provider` | 搜索 provider 配置契约、HTTPS 默认升级与隐私默认集成 | UX-003B；provider 校验/隐私参数注入/配置降级 |
| BUX-05 | DONE | BUX-04A | `browser/shared-ui/navigation` | 后退/前进/刷新/停止、加载状态、站点身份和页面动作绑定 | UX-004；导航竞争、证书/HTTP/HTTPS、页面伪造安全 UI |
| BUX-06 | DONE | BUX-02,CEF-03 | `browser/shared-ui/tabs/basic` | 新建/切换/关闭/拖动/恢复关闭标签与 active/focus 状态机 | UX-005；重复关闭、旧事件、崩溃恢复、释放 |
| BUX-07 | DONE | BUX-06 | `browser/shared-ui/tabs/advanced` | 固定、复制、静音、搜索、分组和跨窗口移动 | UX-006；容量/顺序/音频/多窗口与键盘操作 |
| BUX-08 | DONE | BUX-06,CEF-05 | `browser/shared-ui/windows` | 多窗口、受控 popup、全屏与画中画 UI/策略绑定 | UX-007；来源、取消、焦点、关闭与恢复 |
| BUX-09 | DONE | BUX-03,PRV-03 | `browser/bookmarks`,`browser/shared-ui/bookmarks` | 书签 store、栏、管理器、搜索、导入/导出 | UX-008；事务/损坏/超大/重复/跨 Profile |
| BUX-10 | DONE | BUX-06,PRV-03 | `browser/history`,`browser/shared-ui/history` | 历史、最近关闭、搜索、范围删除与恢复 | UX-009；无痕零持久化、删除边界、跨 Profile |
| BUX-11 | DONE | CEF-05,BUX-02 | `browser/downloads`,`browser/shared-ui/downloads` | 下载 shelf/页、暂停/恢复/取消/重命名/打开位置和危险状态 | UX-010；路径、重复、断点、外部打开、失败释放 |
| BUX-12 | VERIFIED | BUX-05,PLT-02 | `browser/shared-ui/page-tools` | 查找、缩放、全屏、打印/PDF 与保存页面命令 | UX-011；取消/失败/输出路径/跨 Profile |
| BUX-13 | DONE | BUX-01,PRV-03 | `browser/preferences`,`browser/shared-ui/settings` | 版本化 preference store 与启动/搜索/外观/下载/内容设置 UI | UX-012；migration/corrupt/reset/restart readback |
| BUX-14 | DONE | CEF-05,BUX-05,BUX-13 | `browser/shared-ui/site-controls` | 站点权限、安全信息、证书错误、popup 与外部协议确认 UI | UX-013；origin/Profile/TTL/取消/伪造/危险 scheme |
| BUX-15 | VERIFIED | CEF-04,PRV-01..04,BUX-06 | `browser/shared-ui/profiles`,`browser/session` | Profile picker、普通/无痕窗口、启动会话与崩溃恢复编排 | UX-014；清理失败、无痕不恢复、旧 session、跨 Profile |
| BUX-16 | VERIFIED | BUX-05,BUX-11,PLT-02 | `browser/shared-ui/context-menu` | 上下文菜单、拖放、剪贴板与受控本地文件入口 | UX-015；上下文最小化、路径/scheme、取消/外部动作 |
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

## BUX-03 完成证据（Windows 本地新标签页）

- 状态：`DONE`；依赖 `BUX-02 DONE`、`CEF-03 DONE`。
- 单一目标：交付平台中立、可独立测试的 `crayon://newtab/` 页面模型与确定性 HTML/CSS renderer，并在 Windows CEF Browser/child process 以安全 custom scheme handler 接入，使启动页和 Chrome UI 新建标签进入蜡笔本地页；本任务不实现地址/搜索判定、真实无痕窗口或持久化。
- 输入：当前 PRD 的本地起始页/零默认公网要求、`browser-design-v1` 语义色与键盘心智、BUX-02 shell/CEF-03 tab lifecycle、共享 `zh-CN/en-US` locale 资源和 CEF 150 custom scheme/resource handler API。
- 输出与允许修改：新增 `browser/shared-ui/new-tab/` 的纯 C++17 model/router/renderer 与独立 contract test/CMake；新增 `browser/cef-shell/src/browser/new_tab/` 的窄 CEF adapter；允许修改根与 CEF CMake、Windows `app.*`/bootstrap、`TabController` 的精确内置新标签重定向 hook、Windows string resource、共享 locale、CEF source contract 和本 Roadmap/current/总 Roadmap索引。新增生产文件不超过 4 个；共享模块不得出现 CEF/Win32/AppKit/ArkWeb 类型。
- 模型与配置边界：固定入口配置是调用方提供的内存快照，不在本任务定义磁盘/云同步 schema。`schema_version=1`、条目数、ID、UTF-8 标题和 `http/https` URL 均有硬上限；重复 ID、控制字符、credential URL、危险 scheme 或任一损坏条目使整份配置 fail closed 为零入口，不部分猜测恢复。默认配置为空，不内置任何公网、广告、推荐或第三方服务。
- 普通/无痕边界：普通模型只显示验证通过的用户固定入口；无痕模型强制丢弃固定入口、历史/常用/最近关闭与跨会话提示，只呈现本地隐私说明。真实 private Profile/window/context 与清理语义仍由 `BUX-15/PRV-01..04` 拥有；Windows 当前 handler 只绑定普通模型，不允许 query/path/页面内容选择无痕模式，避免伪造隐私状态。
- Resource handler 安全边界：`crayon` scheme 必须在 Browser/child process 以相同 `STANDARD|SECURE|DISPLAY_ISOLATED` 选项注册；factory 只接受精确 `crayon://newtab/`、`/index.html`、`/styles.css` 与 `GET/HEAD`，拒绝 credential、port、query、fragment、未知 host/path/method。响应只来自编译期/内存资源，无文件/网络 IO；HTML 转义所有配置/locale 字段，不包含 script、form、iframe、object、img、远程字体或公网 URL；CSP 仅允许同 origin stylesheet，并设置 `no-store`、`nosniff`、`no-referrer`、`frame-ancestors 'none'` 等头。
- Windows 接入：首窗口直接加载 `crayon://newtab/`；仅对 CEF 精确内置 `chrome://newtab/` 主 frame 导航重定向至受管本地页，不改写 popup/about:blank/用户 URL。页面搜索/导航继续使用原生 omnibox 与 `Ctrl+L`；页面内搜索控件、provider/URL 判定归 `BUX-04A`，不得偷跑 Google/第三方搜索服务。
- 验收与测试：UX-002；shared contract 覆盖普通/无痕、空/合法/损坏/超量/重复配置、HTML 注入转义、route method/host/path/URL component 矩阵、HEAD 与 shutdown-safe immutable response；输出扫描证明默认 document/CSS 除 `crayon://newtab/styles.css` 外无网络引用/主动内容。执行独立 new-tab configure/build/ctest、Windows Debug/Release build+ctest、适用 Google-style clang-format、`scripts/check.ps1 fast/security`、`git diff --check`；Windows 实机验证启动页、`Ctrl+T` 新页、原生 omnibox 搜索/导航入口和完整退出零残留。
- 明确不做：不实现真实无痕窗口/Profile、最近关闭恢复、历史/常用站点推断、shortcut 持久化/编辑 UI、页面内搜索框、搜索 provider、omnibox 判定、书签/投屏按钮、JavaScript bridge、远程内容、Service Worker、任意文件读取或 macOS 实机；分别由 `BUX-04A/04B/06/09/10/13/15`、`CEF-13`、`CEF-02M` 与后续平台总验收完成。
- 实现：新增 4 个生产文件，平台中立层提供有界 `schema_version=1` shortcut model、严格 HTTP(S) URL/UTF-8 校验、确定性 HTML/CSS renderer 与精确 route classifier；CEF adapter 注册 `STANDARD|SECURE|DISPLAY_ISOLATED` custom scheme，响应只来自不可变内存并设置 CSP、`no-store`、`nosniff`、`no-referrer`、same-origin/deny framing 头。Windows Browser/child process 使用同一 scheme 注册，启动页直接进入 `crayon://newtab/`；CEF Chrome UI 的 `IDC_NEW_TAB` 通过有界待处理命令令牌关联下一个内部创建标签，并保留精确 `chrome://newtab/` 主 frame 防御性重定向。
- 失败基线：独立 CMake configure 在缺少 `src/new_tab_page.cc` 时失败；CEF adapter contract 在缺少 handler header/source 时失败，证明新测试先于实现生效。
- 自动验证：Google `clang-format 19.1.5 --dry-run --Werror --style=Google` 通过；独立 `cmake -S . -B .cache/build/new-tab -G Ninja -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF`、`cmake --build .cache/build/new-tab` 与 `ctest --test-dir .cache/build/new-tab --output-on-failure` 通过（1/1）；`cmake --build .cache/build/windows-cef-debug --config Debug` 与 `ctest --preset windows-cef-debug --output-on-failure` 通过（13/13）；`cmake --build .cache/build/windows-cef-debug --config Release` 与 `ctest --test-dir .cache/build/windows-cef-debug -C Release --output-on-failure` 通过（13/13）；`scripts/check.ps1 fast` 和 `scripts/check.ps1 security` 最终通过，locale JSON/key parity 为 25，`git diff --check` 通过。`fast` 首轮暴露目录迁移后 Cargo test 二进制仍嵌入旧 `D:\get-video` 路径；仅触发相关测试目标重新编译，未清理 1.8 GiB workspace cache，最终门禁通过且源码无旧路径改动。
- Windows 实机：Debug、Release 均验证启动首屏为本地 `crayon://newtab` 中文页；`Ctrl+T` 从 1 个标签增加到 2 个且新标签仍为本地页，不再进入 Google/Chromium 默认 NTP；`Ctrl+L` 聚焦并选中地址栏；`Alt+F4` 后应用/进程残留均为 0。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核。实机门禁发现并关闭 1 个 P1（CEF Chrome 内置 NTP 绕过普通 navigation callback）；静态审查关闭 UTF-8 首字符截断、shortcut host/port 边界和 C++17 编译标准漂移问题。最终 P0/P1/P2/P3 均为 `0`；新增生产文件 92/402/18/209 行，均低于规模提醒线，共享模块无 CEF/Win32/AppKit/ArkWeb 类型。
- 未覆盖与风险：真实 private Profile/window 与清理语义、shortcut 持久化/编辑、omnibox/provider、完整标签功能和 macOS 实机仍归后续任务。CEF Chrome 新标签关联依赖固定 CEF 150 的 `OnChromeCommand -> OnAfterCreated` 顺序，当前有界并经 Debug/Release 实机覆盖；升级 CEF 时必须重新验证。

## BUX-04A 原子范围（omnibox 核心 URL/搜索判定引擎）

- 状态：`TODO`；依赖 `BUX-02 DONE`、`CEF-03 DONE`、`FND-11 DONE`。
- 单一目标：交付平台中立、可独立测试的 omnibox 核心引擎，包括 URL/搜索词判定、本地建议索引结构、建议列表状态机和闭合编辑命令；本任务不实现搜索 provider 配置、HTTPS 默认升级或隐私默认集成。
- 输入：`browser-design-v1` 的两层信息架构与键盘心智、BUX-02 的 `ShellCommand`/`FocusArea` 骨架（含 `kFocusOmnibox`、`kOmnibox` focus token）、CEF-03 的导航生命周期、`crayon-domain::config` 配置加载框架（FND-11）与共享 `zh-CN/en-US` locale 资源。
- 输出与允许修改：新增 `browser/shared-ui/omnibox/core/` 的生产判定引擎、建议状态机、闭合命令与独立 contract test/CMake；允许修改根 CMake（条件装配 omnibox test target）、`browser/shared-ui/shell/command_registry.h`（窄扩展 `ShellCommand` 枚举，新增 `OmniboxEdit`/`OmniboxSubmit`/`OmniboxCancel`/`OmniboxNavigate`）、共享 locale JSON（新增 omnibox 文案键）和本 Roadmap/current/总 Roadmap 索引。新增生产文件不超过 5 个；共享模块不得出现 CEF/Win32/AppKit/ArkWeb 类型。
- 禁止修改：`browser/engine-api` 公共头、BUX-01 token/glyph/golden、macOS/Harmony 源码、Profile/持久化、搜索 provider URL 模板、HTTPS 升级策略、权限/隐私默认、下载/投屏业务、Cast-SDK、Relay、Agent/模型；不得引入 JSON/UI framework/第三方依赖，不得暴露 CEF/Win32 handle 给共享层。
- 判定引擎边界：
  - URL 判定：含 scheme 前缀（`http`/`https`/`file`/`crayon` 等白名单）、含已知 TLD（使用 Public Suffix List 子集，不随 DNS 查询）、IPv4/IPv6 字面量、含路径分隔符或 `@`/`:`/`?`/`#` 的输入，均判定为 URL；所有其他输入判定为搜索词。
  - Scheme 白名单为闭合枚举，未知 scheme fail closed 为搜索词，不得静默添加 `http://` 前缀。
  - 输入长度有硬上限（如 2048 字节），超限 fail closed 为搜索词。
  - 危险 scheme（`javascript:`/`data:`/`vbscript:`）稳定拒绝为搜索词，不进入 URL 解析。
  - IDN/Punycode 输入保留为搜索词判定（不展开 Punycode），由后续导航层处理；本任务不做 IDN 转码。
- 本地建议索引边界：
  - 建议来源为可插拔索引接口，当前由空 fixture/静态条目驱动；历史/书签数据由 `BUX-09/10` 填充，本任务只定义索引契约和内存结构。
  - 建议条目有硬上限（如 8 条），按相关性和来源优先级排序；重复条目去重。
  - 建议响应只含标题、URL/搜索词和来源标记（history/bookmark/shortcut），不含 Cookie、正文或页面敏感数据。
  - 索引查询在本地内存完成，不涉及网络请求；查询超时/容量溢出降级为空列表。
- 状态机与命令边界：
  - 状态：Idle（无焦点）、Editing（用户输入中）、Suggesting（显示建议列表）、Loading（已提交等待导航）、Committed（导航已确认）。
  - 命令：`FocusOmnibox`（BUX-02 已有）、`OmniboxEdit`（输入内容变化）、`OmniboxSubmit`（用户确认提交）、`OmniboxCancel`（Escape/失焦取消）、`OmniboxNavigate`（内部导航结果）。
  - 旧 generation/tab 关闭/导航完成后的提交结果稳定丢弃；shutdown 后所有 pending 查询和命令拒绝。
  - 建议列表的异步加载与取消：提交后、导航前或取消时立即丢弃未完成的查询结果。
- 与 BUX-04B 的分工：本任务输出"判定结果"（URL 或搜索词），搜索词的 provider 转换和 HTTPS 升级由 BUX-04B 消费；BUX-04A 的判定引擎预留隐私配置注入接口（`PrivacyDefaults` 占位结构），BUX-04B 负责填充具体值。
- 验收与测试：UX-003A；判定 contract 覆盖 URL/搜索词边界矩阵（scheme/TLD/IP/特殊字符/长度/危险 scheme/中文/空输入）、建议索引空/静态/超量/去重/排序、状态机正常/取消/旧结果丢弃/shutdown 拒绝、命令 sequence 单调性与未知命令拒绝。执行独立 omnibox configure/build/ctest、Windows Debug/Release build+ctest、适用 Google-style clang-format、`scripts/check.ps1 fast/security`、`git diff --check`；Windows 实机覆盖 `Ctrl+L` 聚焦后输入 URL/搜索词的判定与导航行为。
- 明确不做：不实现搜索 provider URL 模板、HTTPS 默认升级、远程搜索建议 API、隐私默认设置、真实历史/书签索引、omnibox 视觉渲染、地址栏安全标识（锁图标/证书信息）、自动填充、语音输入或 macOS 实机；分别由 `BUX-04B/09/10/13/14/17`、`CEF-05/13`、`CEF-02M` 与后续平台总验收完成。

## BUX-04A 完成记录（omnibox 核心 URL/搜索判定引擎）

- 状态：`DONE`；依赖 `BUX-02 DONE`、`CEF-03 DONE`、`FND-11 DONE`。
- 新增 `browser/shared-ui/omnibox/core/` 判定引擎、建议状态机、闭合命令与独立 contract test/CMake；窄扩展 `ShellCommand` 枚举（`kOmniboxEdit`/`kOmniboxSubmit`/`kOmniboxCancel`/`kOmniboxNavigate`）与共享 locale 文案键。
- 判定 contract 覆盖 URL/搜索词边界矩阵（scheme/TLD/IP/特殊字符/长度/危险 scheme/中文/空输入）；建议索引空/静态/超量/去重/排序；状态机正常/取消/旧结果丢弃/shutdown 拒绝；命令 sequence 单调性与未知命令拒绝。
- 自动验证：独立 `cmake -S . -B .cache/build/omnibox -G Ninja -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功；`ctest --test-dir .cache/build/omnibox --output-on-failure` 为 `2/2` 通过（`omnibox_parser_contract`、`omnibox_state_contract`）；`cmake --build .cache/build/windows-cef-debug --config Debug` 与 `ctest --preset windows-cef-debug --output-on-failure` 通过（含 omnibox target）；`scripts/check.ps1 fast`/`security` 与 `git diff --check` 通过。
- Code Review：P0/P1/P2/P3 均为 `0`；共享模块无 CEF/Win32/AppKit/ArkWeb 类型。
- 未覆盖与风险：搜索 provider URL 模板、HTTPS 默认升级、远程搜索建议、隐私默认、真实历史/书签索引、omnibox 视觉渲染、地址栏安全标识、自动填充和 macOS 实机归后续任务。

## BUX-05 完成记录（导航命令与站点身份）

- 状态：`DONE`；依赖 `BUX-04A DONE`。
- 新增 `browser/shared-ui/navigation/` 的 `NavigationController` 与 `SiteIdentity`；覆盖后退/前进/刷新/停止、加载状态、站点身份（HTTP/HTTPS/证书/不安全标志）与页面动作绑定。
- 自动验证：独立 configure/build 成功；`ctest --test-dir .cache/build/navigation --output-on-failure` 为 `1/1` 通过（`navigation_contract`）；全量 tabs build `ctest` 中 `navigation_contract` 通过。
- Code Review：P0/P1/P2 均为 `0`。
- 未覆盖与风险：导航竞争真机验证、证书错误 UI、页面伪造安全 UI 渲染和 macOS 实机归后续任务。

## BUX-06 完成记录（基础标签状态机）

- 状态：`DONE`；依赖 `BUX-02 DONE`、`CEF-03 DONE`。
- 新增 `browser/shared-ui/tabs/basic/` 的 `TabStripStateMachine`；覆盖新建/切换/关闭/拖动/恢复关闭标签与 active/focus 状态机；有界最近关闭栈（`kMaxRestorableTabs = 10`）和最大标签数（`kMaxTabCount = 32`）。
- 自动验证：独立 configure/build 成功；`ctest --test-dir .cache/build/tabs --output-on-failure -R tab_strip_contract` 为 `1/1` 通过；全量 tabs build 中 `tab_strip_contract` 通过。
- Code Review：P0/P1/P2 均为 `0`。
- 未覆盖与风险：崩溃恢复、真机键盘操作和 macOS 实机归后续任务。

## BUX-07 完成记录（高级标签功能）

- 状态：`DONE`；依赖 `BUX-06 DONE`。
- 新增 `browser/shared-ui/tabs/advanced/` 的 `AdvancedTabStripStateMachine`；在基础状态机上扩展固定（Pin）、复制（Duplicate）、静音（Mute）、搜索（Search）、分组（Group，上限 `kMaxTabGroups = 8`）和跨窗口移动就绪查询（`CanMoveTabToWindow`）。
- 自动验证：独立 configure/build 成功；`ctest --test-dir .cache/build/tabs --output-on-failure -R advanced_tab_strip_contract` 为 `1/1` 通过；全量 tabs build 中全部 3 项 tab 相关测试通过。
- Code Review：P0/P1/P2 均为 `0`。
- 未覆盖与风险：跨窗口移动的实际窗口管理器实现、按标题/URL 搜索、分组颜色/折叠和 macOS 实机归后续任务。

## BUX-08 原子范围（多窗口、受控 popup、全屏与画中画策略）

- 状态：`DONE`；依赖 `BUX-06 DONE`、`CEF-05 DONE`。
- 单一目标：交付平台中立、可独立测试的多窗口状态机与受控 popup/全屏/画中画策略模型，统一窗口创建/焦点/关闭、popup 来源判定与容量、全屏和画中画模式互斥与恢复；本任务不做 CEF/Win32/AppKit 接线、不实现窗口像素 UI 或会话恢复。
- 输入：`browser-design-v1` 的两层信息架构与窗口控制心智、BUX-06/07 的标签状态机窗口归属语义、CEF-05 的默认最小权限原则（程序化 popup 默认拒绝）。
- 输出与允许修改：新增 `browser/shared-ui/windows/` 的 `WindowStateMachine`（窗口注册/焦点/关闭/模式）、`PopupPolicy`（来源判定与容量决策）、独立 contract test/CMake；允许修改根 CMake（条件装配 windows target）和本 Roadmap/current/总 Roadmap 索引。新增生产文件不超过 4 个；共享模块不得出现 CEF/Win32/AppKit/ArkWeb 类型。
- 禁止修改：`browser/engine-api`、BUX-01 token/glyph/golden、tab strip 状态机语义、macOS/Harmony 源码、Profile/持久化、权限 store、下载/投屏业务、Cast-SDK、Relay、Agent/模型；不得引入 UI framework/第三方依赖；不存储 popup 目标 URL 或页面数据。
- 状态与错误边界：
  - 窗口 ID 非空且 ≤ 64 字符；重复创建、未知/旧窗口事件、关闭后迟到事件稳定拒绝；容量上限 `kMaxWindows = 8` 满时 fail closed。
  - popup 必须关联存在的 opener 窗口；来源为 `kProgrammatic`（无用户手势脚本请求）默认拒绝；`kUserGesture` 允许但受 opener 每窗口 `kMaxPopupsPerWindow = 4` 与全局窗口容量约束；决策结果闭合枚举，不产生第二次副作用。
  - 全屏/画中画互斥：同一窗口同时只能处于一种模式；进入新模式前必须先退出当前模式，混合请求稳定拒绝；popup 窗口不得进入全屏或画中画。
  - 关闭聚焦窗口后焦点回退到最近使用的普通窗口；关闭 opener 不级联关闭 popup；关闭最后一个窗口后 `has_windows()` 为 false；`Shutdown()` 清空全部状态并拒绝后续命令。
  - 退出全屏/画中画恢复到 `kNormal` 模式；窗口关闭自动清除其模式状态，不残留。
- 与 CEF-05 的分工：popup/全屏/PiP 的平台触发入口由后续 CEF adapter 消费本状态机决策；本任务只交付确定性策略与状态。
- 验收与测试：UX-007；contract 覆盖创建/焦点/关闭/重复与旧事件、容量、popup 来源/取消/容量矩阵、全屏/PiP 互斥与恢复、popup 窗口模式拒绝、焦点回退、shutdown 拒绝。执行独立 windows configure/build/ctest、适用 Google-style clang-format、`git diff --check`。
- 明确不做：不实现 CEF/Win32/AppKit 窗口接线、窗口像素 UI、多显示器/DPI、窗口会话持久化恢复（归 `BUX-15`）、跨窗口标签移动的窗口管理器实现（`BUX-07` 只提供就绪查询）、macOS 实机；分别由 `CEF-08/13/14`、`BUX-15/18` 完成。

## BUX-09 原子范围（书签领域模型、版本化编解码与书签栏视图）

- 状态：`DONE`；依赖 `BUX-03 DONE`、`PRV-03 DONE`。
- 单一目标：交付平台中立、可独立测试的书签树模型（文件夹/书签 CRUD、移动、搜索、重复检测）、版本化编解码与原子持久化（临时文件改名，损坏 fail closed）和书签栏视图状态机；本任务不做 Chrome JSON 导入、像素 UI 或跨设备同步。
- 输入：`browser-design-v1` 书签栏/管理器心智、BUX-02 typed command 契约、PRV-03 的按 Profile 目录隔离。
- 输出与允许修改：新增 `browser/bookmarks/`（`BookmarkStore` 树模型 + `BookmarkCodec` 序列化/原子存取 + 独立测试/CMake）与 `browser/shared-ui/bookmarks/`（`BookmarkBarStateMachine` + 独立测试/CMake）；允许修改根 CMake 和本 Roadmap/current/总 Roadmap 索引。新增生产文件不超过 6 个；不得出现 CEF/Win32/AppKit/ArkWeb 类型；不引入 JSON 框架等第三方依赖。
- 禁止修改：`browser/engine-api`、BUX-01 token/glyph、omnibox 判定、PRV-01/02/03 语义、下载/投屏业务、Cast-SDK、Relay、Agent/模型。
- 领域边界：节点为闭合两类（folder/bookmark）；ID 单调分配不重用；URL 仅接受 http/https、≤2048 字节、无控制字符；标题 ≤512 字节 UTF-8；树容量 ≤4096 节点、深度 ≤32、单文件夹子级 ≤256；移动拒绝环（不得移入自身后代）；删除文件夹级联删除后代；搜索大小写不敏感子串、结果 ≤64；每个 Store 实例绑定单一 Profile 根，跨 Profile 不共享状态。
- 编解码边界：格式 `CRAYON-BOOKMARKS v1` 头 + 长度前缀记录；头部错误、截断、长度越界、未知记录类型均 fail closed（返回错误不部分恢复）；保存经 `<path>.tmp` 写入后 rename 原子替换；round-trip 确定性。
- 视图边界：书签栏只持有（节点 ID、标题）有界投影（≤128），显示/隐藏、当前页 starred 状态查询；不持 URL 之外的页面数据，不直接读写文件。
- 验收与测试：UX-008；contract 覆盖 CRUD/移动环拒绝/级联删除/容量/深度、URL/标题校验、重复 URL 检测、搜索边界、编解码 round-trip/损坏矩阵（坏头/截断/超长/未知类型）、原子保存的临时文件不残留、跨 Profile 隔离、栏容量与 starred 状态。执行独立 configure/build/ctest、`-Wall -Wextra -Wpedantic -Werror` 零告警、共享层回归、`git diff --check`。
- 明确不做：Chrome/Edge/Firefox JSON 或 HTML 导入解析、书签管理器像素 UI、同步/云、favicon 抓取、macOS 实机；导入解析如需支持另立任务评估解析器安全边界。

## BUX-10 原子范围（历史领域模型、版本化编解码与历史页视图）

- 状态：`DONE`；依赖 `BUX-06 DONE`、`PRV-03 DONE`。
- 单一目标：交付平台中立、可独立测试的历史记录 store（访问记录、搜索、范围删除、最近关闭栈）、版本化编解码与原子持久化和历史页视图状态机；本任务不做像素 UI、 synced 历史或与 omnibox 建议索引的接线。
- 输入：BUX-06 的最近关闭语义、PRV-03 的按 Profile 目录隔离、PV-001/PV-004 的无痕零持久化与删除边界要求。
- 输出与允许修改：新增 `browser/history/`（`HistoryStore` + `HistoryCodec` + 独立测试/CMake）与 `browser/shared-ui/history/`（`HistoryPageStateMachine` + 独立测试/CMake）；允许修改根 CMake 和本 Roadmap 索引。新增生产文件不超过 6 个；不得出现 CEF/Win32/AppKit/ArkWeb 类型；不引入第三方依赖。
- 禁止修改：`browser/engine-api`、BUX-09 书签、omnibox、PRV crate、下载/投屏业务、Cast-SDK/Relay/Agent。
- 领域边界：条目含单调 ID、URL（仅 http/https，≤2048，无控制字符）、标题（≤512）与调用方注入的秒级时间戳（模块不读真实时钟）；容量 4096，超出逐出最旧；`RecordVisit` 在 ephemeral 实例上稳定拒绝（无痕零持久化）；`DeleteRange(from,to)` 闭区间、from>to 拒绝；`DeleteUrl`/`ClearAll` 精确生效；最近关闭栈 ≤10 条且只记录可恢复 URL/标题；持久化在 ephemeral 实例上拒绝。
- 编解码边界：`CRAYON-HISTORY v1` 头 + 长度前缀记录；坏头/截断/越界/未知记录 fail closed；保存经 `.tmp` 原子 rename。
- 视图边界：`HistoryPageStateMachine` 持有 ≤256 条投影、搜索查询状态与清空事件；不持完整 URL 之外的页面数据，不直接读写文件。
- 验收与测试：UX-009；contract 覆盖记录/逐出、ephemeral 拒绝、范围删除边界（空/逆序/精确端点）、URL 删除、清空、最近关闭栈容量与顺序、搜索边界、编解码 round-trip/损坏矩阵、跨实例（Profile）隔离。执行独立 configure/build/ctest、零告警、共享层回归、`git diff --check`。
- 明确不做：历史页像素 UI、按站点 favicon、与 BUX-04A 建议索引的接线（后续接线任务）、同步、macOS 实机。

## BUX-13 原子范围（版本化偏好 store 与设置页视图）

- 状态：`DONE`；依赖 `BUX-01 DONE`、`PRV-03 DONE`。
- 单一目标：交付平台中立、可独立测试的版本化偏好 store（闭合键注册表、强类型值、schema 迁移、损坏 fail-closed、重置默认值）与设置页视图状态机；本任务不做像素 UI、平台设置页集成或与 PRV-06 隐私默认值的合并装配。
- 输入：BUX-01 的设计 token 与设置页信息架构、PRV-03 的按 Profile 目录隔离、UX-012 的 migration/corrupt/reset/restart readback 要求。
- 输出与允许修改：新增 `browser/preferences/`（`PreferenceStore` + `PreferenceCodec` + 独立测试/CMake）与 `browser/shared-ui/settings/`（`SettingsPageStateMachine` + 独立测试/CMake）；允许修改根 CMake 和本 Roadmap 索引。新增生产文件不超过 6 个；无 CEF/Win32/AppKit/ArkWeb 类型；无第三方依赖。
- 禁止修改：`browser/engine-api`、BUX-01..11 模块、PRV crate、Cast-SDK/Relay/Agent；偏好值不得含凭证、Cookie 或任意文件内容。
- 领域边界：键为闭合注册表（startup_policy/theme/show_bookmark_bar/download_directory/search_provider），每键固定类型与默认值；`Set` 类型不符或未知键稳定拒绝；`Reset`/`ResetAll` 恢复默认；值边界：枚举闭合、字符串 ≤1024 字节无控制字符、路径键仅做长度/字符校验不做存在性检查。
- 编解码边界：`CRAYON-PREFERENCES v<schema>` 头；当前 schema=1；加载 schema=0 文档执行注册迁移（丢弃未知键、应用默认值），更高 schema 或损坏 fail closed；保存经 `.tmp` 原子 rename；重启回读 = 保存后重新加载必须逐键一致。
- 视图边界：`SettingsPageStateMachine` 持有闭合 section 列表与当前 section、dirty 标记与重置事件；不直接读写文件，不持有真实偏好值之外的页面数据。
- 验收与测试：UX-012；contract 覆盖每键 Set/Get/类型拒绝/未知键、Reset/ResetAll、迁移（v0→v1 丢未知键补默认值）、损坏矩阵、原子保存无残留、保存-重载逐键一致、视图 section/dirty/重置。执行独立 configure/build/ctest、零告警、共享层回归、`git diff --check`。
- 明确不做：设置页像素 UI、chrome://settings 拦截替换（CEF 层后续任务）、偏好与 PrivacyDefaults 的运行时合并装配（后续 adapter）、同步、macOS 实机。

## BUX-14 原子范围（站点控制面板：权限、安全信息、证书错误与外部协议确认）

- 状态：`DONE`；依赖 `CEF-05 DONE`、`BUX-05 DONE`、`BUX-13 DONE`。
- 单一目标：交付平台中立、可独立测试的站点控制面板状态机：站点安全身份信息、按 origin 的权限条目（含 TTL）、证书错误决策和外部协议确认模型；本任务不做面板像素 UI、CEF handler 接线或真实证书链解析。
- 输入：CEF-05 的 `PermissionStore`（默认 deny）与 `ExtractSiteOrigin`、BUX-05 的 `SiteIdentity`、BUX-13 的偏好契约、UX-013 的 origin/Profile/TTL/取消/伪造/危险 scheme 要求。
- 输出与允许修改：新增 `browser/shared-ui/site-controls/`（`SiteControlsStateMachine` + `PermissionPromptQueue` + 独立测试/CMake）；允许修改根 CMake 和本 Roadmap 索引。新增生产文件不超过 4 个；不得出现 CEF/Win32/AppKit/ArkWeb 类型；可链接 `browser_navigation`（消费 `SiteIdentity`）。
- 禁止修改：CEF-05 handler 行为、PRV-06 默认值、`browser/engine-api`、下载/投屏业务、Cast-SDK/Relay/Agent；页面内容不得设置或伪造安全 UI 状态。
- 状态与错误边界：
  - 安全身份/证书状态只接受 `kEngine` 来源写入；`kPageContent` 来源稳定拒绝（防伪造）。
  - 权限条目按 (origin, kind) 存储，决策闭合（allow-session/allow-until/deny）；`allow-until` 携带调用方注入的到期时间戳，查询以注入 now 判定过期（过期视为无记录）；容量 ≤256 条，逐出最旧。
  - 权限请求队列为 FIFO、容量 ≤4、按 origin+kind 去重；grant/deny/dismiss 三态闭合；取消与超时（调用方注入 now）移除请求不产生副作用。
  - 证书错误为闭合错误类（expired/name-mismatch/untrusted/generic）；用户决策闭合（go-back/proceed-once），`proceed-once` 只作用于当前导航 generation，导航后失效。
  - 外部协议确认按 (scheme, origin) 记忆，闭合决策（allow-once/deny/remember-allow/remember-deny）；危险 scheme（javascript:/data:/vbscript:）请求稳定拒绝且不进入确认流程。
- 验收与测试：UX-013；contract 覆盖来源伪造拒绝、权限 TTL 到期/取消/容量逐出、队列 FIFO/去重/容量、证书决策与 generation 失效、外部协议记忆与危险 scheme 拒绝、shutdown 全拒绝。执行独立 configure/build/ctest、零告警、共享层回归、`git diff --check`。
- 明确不做：面板/气泡像素 UI、CEF permission/cert handler 接线（后续 adapter 任务）、真实证书链/吊销检查、按 Profile 隔离的持久化（复用 PRV-03 根，由后续装配任务完成）、macOS 实机。

## BUX-14 完成记录（站点控制面板：权限、安全信息、证书错误与外部协议确认）

- 状态：`DONE`；依赖 `CEF-05 DONE`、`BUX-05 DONE`、`BUX-13 DONE`。
- 实现：新增 4 个生产文件。`browser/shared-ui/site-controls/`：`SiteControlsStateMachine`（安全身份只接受 `kEngine` 来源写入、`kPageContent` 稳定拒绝防伪造；权限按 (origin, kind) 存储、决策闭合 deny/allow-session/allow-until、TTL 以调用方注入 now 判定、过期视为 deny、容量 256 条 LRU 逐出、clear/re-add 循环下 recency 有界；证书错误闭合四类、go-back/proceed-once 闭合决策、proceed-once 绑定授予时的 navigation generation、跨 generation 与新错误到达即失效；外部协议按 (scheme, origin) 记忆、容量 256 条 FIFO 逐出、javascript:/data:/vbscript: 危险 scheme 稳定拒绝且不进入确认流程；Shutdown 清空并全拒）与 `PermissionPromptQueue`（FIFO、容量 ≤4、按 origin+kind 去重、grant/deny/dismiss 三态闭合、仅队首可决议、取消与注入 now 的超时移除均无副作用）。origin 校验只接受 https/http/crayon scheme、≤256 字节、无控制字符/`@`/`|`；`PermissionKind` 与 CEF-05 覆盖一一对齐；全部时间戳调用方注入、模块不读真实时钟；错误为闭合枚举、不携带站点数据。
- 自动验证：独立 `cmake -S . -B .cache/build/site-controls -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功且 `-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R site_controls` 1/1 通过（12 组：身份/证书伪造拒绝、权限校验矩阵、TTL 到期、容量 LRU 逐出与刷新存活、clear/re-add 有界、队列 FIFO/去重/容量/队首决议、取消与超时无副作用、证书 proceed-once generation 绑定与失效、外部协议记忆与危险 scheme 拒绝、协议记忆容量、shutdown 全拒、枚举闭合）；共享层全量回归 23/23 通过（除本机缺 Ninja 的 `cef_build_graph_contract` 环境阻塞）；`git diff --check` 通过。
- 失败基线：首轮 `site_controls_contract` 失败——容量逐出测试暴露出初版实现按首次插入 FIFO 逐出（重复记录不刷新位置）与“逐出最旧”语义不符，先复现后改为 LRU 刷新，证明测试在错误实现下确实失败。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；关闭 1 个 P1（重复记录导致的 FIFO 逐出语义错误，改 LRU）、1 个 P2（origin 未拒绝内部键分隔符 `|`，已在形状校验中拒绝）和 1 个 P2（外部协议记忆 map 初版无界，已加 256 条容量 + FIFO 逐出 + recency 压缩），最终 P0/P1/P2 均为 `0`。文件规模：生产头 252/90 行、生产实现 224/91 行、测试 369 行，函数均 <100 行。
- 未覆盖与风险：面板/气泡像素 UI、CEF permission/cert handler 接线（后续 adapter 任务）、真实证书链/吊销检查、按 Profile 隔离的权限持久化装配（复用 PRV-03 根）与 macOS 实机归后续任务。`BUX-14` 转为 `DONE`。

## BUX-13 完成记录（版本化偏好 store 与设置页视图）

- 状态：`DONE`；依赖 `BUX-01 DONE`、`PRV-03 DONE`。
- 实现：新增 6 个生产文件。`browser/preferences/`：`PreferenceStore`（闭合五键注册表 startup_policy/theme/show_bookmark_bar/download_directory/search_provider，每键固定类型与默认值；未知键/类型不符/越界枚举/超长或含控制字符字符串全部稳定拒绝；设为默认值即清除 override；Reset/ResetAll）与 `PreferenceCodec`（`CRAYON-PREFERENCES v1`，只序列化非默认 override；v0 文档宽容迁移——未知键与非法值丢弃、其余补默认；更高版本与结构损坏 fail closed；`.tmp` 原子 rename，文件 ≤256KiB，u64 溢出防护）。`browser/shared-ui/settings/`：`SettingsPageStateMachine`（闭合五个 section、dirty 跟踪、两步重置确认、shutdown 全拒绝）。
- 自动验证：独立 configure/build 成功且 `-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R "preferences_contract|settings_page_contract"` 2/2 通过（store+codec 9 组、view 4 组：默认值/类型/越界矩阵、Reset 语义、round-trip 只含 override 且逐键一致、v0 迁移、严格 v1 拒绝未知键、损坏矩阵、重启回读逐键一致、临时文件不残留）；共享层全量回归 22/22 通过（除本机缺 Ninja 的 `cef_build_graph_contract` 环境阻塞）；`git diff --check` 通过。
- 失败基线：首轮 `preferences_contract` 失败——S 记录写出方漏发长度行导致解析失败，先复现后修复，证明测试在错误实现下确实失败。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；关闭 1 个 P1（S 记录写读格式不一致）和 1 个 P2（迁移模式下 bool 越界未走丢弃路径，已统一 `semantically_invalid` 语义），最终 P0/P1/P2 均为 `0`。
- 未覆盖与风险：设置页像素 UI、chrome://settings 拦截替换、偏好与 PRV-06 隐私默认值的运行时合并装配和 macOS 实机归后续任务。`BUX-13` 转为 `DONE`，解锁 `BUX-14`。

## BUX-10 完成记录（历史领域模型、版本化编解码与历史页视图）

- 状态：`DONE`；依赖 `BUX-06 DONE`、`PRV-03 DONE`。
- 实现：新增 6 个生产文件。`browser/history/`：`HistoryStore`（单调 ID、URL/标题校验、容量 4096 逐出最旧、时间戳调用方注入不读真实时钟、`DeleteRange` 闭区间且逆序拒绝、`DeleteUrl`/`ClearAll`、最近关闭栈 ≤10 且新者先恢复；ephemeral 实例对 `RecordVisit`/`RecordClosedTab`/持久化全部稳定拒绝）与 `HistoryCodec`（`CRAYON-HISTORY v1` + 长度前缀记录，损坏矩阵 fail closed，`.tmp` 原子 rename，文件 ≤4MiB，u64 时间戳溢出防护；ephemeral 保存显式 `kEphemeralRefused`）。`browser/shared-ui/history/`：`HistoryPageStateMachine`（≤256 投影、查询回显 ≤256 字节、清空事件、shutdown 全拒绝）。
- 自动验证：独立 configure/build 成功且 `-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R history` 2/2 通过（store+codec 11 组、view 4 组：校验矩阵、逐出顺序、ephemeral 全拒、最近关闭容量/顺序、范围删除端点、round-trip 含中文与查询串、损坏矩阵、临时文件不残留）；共享层全量回归 20/20 通过（除本机缺 Ninja 的 `cef_build_graph_contract` 环境阻塞）；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。无痕零持久化由 store 层与 codec 层双重门禁保证；错误与报告不含用户数据。
- 未覆盖与风险：历史页像素 UI、favicon、与 BUX-04A 建议索引的接线和 macOS 实机归后续任务。`BUX-10` 转为 `DONE`。

## BUX-09 完成记录（书签领域模型、版本化编解码与书签栏视图）

- 状态：`DONE`；依赖 `BUX-03 DONE`、`PRV-03 DONE`。
- 实现：新增 6 个生产文件。`browser/bookmarks/`：`BookmarkStore`（folder/bookmark 闭合两类、ID 单调不重用、URL 仅 http/https ≤2048 无控制字符、标题 ≤512、容量 4096/深度 32/单文件夹 256 子级、移动拒绝环与容量溢出、文件夹级联删除、大小写不敏感搜索 ≤64 结果、重复 URL 检测；每实例绑定单一 Profile 根）与 `BookmarkCodec`（`CRAYON-BOOKMARKS v1` 头 + DFS 深度/长度前缀记录；坏头/截断/长度越界/未知记录/深度跳变/内容越界全部 fail closed 不部分恢复；保存经 `<path>.tmp` 原子 rename，失败删除临时文件；文件 ≤4MiB）。`browser/shared-ui/bookmarks/`：`BookmarkBarStateMachine`（≤128 条 (id,title,kind) 投影、非法条目整批拒绝、当前页 starred 状态、shutdown 全拒绝）。
- 自动验证：独立 `cmake -S . -B .cache/build/bookmarks -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功且 `-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R bookmark` 2/2 通过（store+codec 14 组、bar 5 组：校验矩阵、环/级联/ID 不重用/深度容量、round-trip 含中文与查询串、损坏矩阵、原子保存无临时文件残留、跨文件缺失/损坏拒绝）；共享层全量回归 18/18 通过（除本机缺 Ninja 的 `cef_build_graph_contract` 环境阻塞）；`git diff --check` 与平台边界扫描通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；Review 发现并关闭 1 个 P1（编解码深度栈越界：`depth == stack.size()` 时 parent 会错绑到 root，已改为 `depth >= stack.size()` 拒绝）、1 个 P2（`ReadPayload` 在空剩余输入时 size_t 下溢，已改为显式 remaining 计算）和 1 个 P2（节点存储由带墓碑的 vector 改为只存活跃节点的 `unordered_map`，避免删除累积无界内存），最终 P0/P1/P2 均为 `0`。
- 未覆盖与风险：Chrome/Edge/Firefox JSON/HTML 导入解析未做（如需支持另立任务评估解析器安全边界）；书签管理器像素 UI、favicon、同步与 macOS 实机归后续任务。`BUX-09` 转为 `DONE`。

## BUX-11 原子范围（下载领域模型与下载栏视图）

- 状态：`DONE`；依赖 `CEF-05 DONE`、`BUX-02 DONE`。
- 单一目标：交付平台中立、可独立测试的下载领域模型（条目状态机、危险文件分类、文件名清理与唯一路径解析）和下载栏/页视图状态机；本任务不做 CEF 下载 handler 接线、真实文件 IO、下载 UI 像素或断点续传的真实网络恢复。
- 输入：`browser-design-v1` 下载入口心智、CEF-05 的 `CefDownloadHandlerAdapter`（后续消费方）、`BUX-02` shell typed command 契约。
- 输出与允许修改：新增 `browser/downloads/`（`DownloadItem` 领域状态机 + `DownloadPath` 文件名/路径工具 + 独立测试/CMake）与 `browser/shared-ui/downloads/`（`DownloadShelfStateMachine` 视图模型 + 独立测试/CMake）；允许修改根 CMake 和本 Roadmap/current/总 Roadmap 索引。新增生产文件不超过 8 个；两个模块均不得出现 CEF/Win32/AppKit/ArkWeb 类型。
- 禁止修改：`browser/engine-api`、CEF-05 permission/download handler 行为、Profile/持久化 schema、BUX-01 token/glyph、macOS/Harmony 源码、投屏/Cast-SDK/Relay/Agent；不读写真实文件系统（路径存在性由调用方谓词注入）；不记录下载 URL/文件名到日志。
- 领域边界：
  - 条目状态闭合：`kPendingDangerConfirm`（危险文件待用户确认）、`kInProgress`、`kPaused`、`kCompleted`、`kFailed`、`kCancelled`；`kCancelled` 为终态；命令越状态迁移稳定拒绝。
  - 进度有界：`received_bytes <= total_bytes`；`Complete` 仅允许 `kInProgress` 且字节数到齐；`Pause` 仅 `kInProgress`；`Resume` 仅 `kPaused`；`Retry` 仅 `kFailed`；`OpenItem`/`OpenLocation` 仅 `kCompleted`。
  - 危险分类为闭合扩展名集合（可执行/脚本类），命中进入 `kPendingDangerConfirm`，须显式 `ConfirmDangerous`/`DiscardDangerous`；未命中直接可开始。
  - 文件名为不可信输入：清理路径分隔符、控制字符、结尾点/空格，长度 ≤ 128 字节，清理后为空稳定失败；唯一路径解析用 " (n)" 后缀去重（n ≤ 999），注入谓词判断存在性，溢出 fail closed。
- 视图边界：`DownloadShelfStateMachine` 持有有界（≤ 64）条目投影（ID、显示名、状态、百分比），提供 shelf 开合、`ClearCompleted`、活跃计数；只接受领域层转发的事件投影，不直接持有路径或引擎句柄；shutdown 后拒绝全部事件。
- 验收与测试：UX-010；contract 覆盖状态迁移矩阵（含非法迁移）、危险确认/丢弃、进度边界、文件名清理（分隔符/控制符/结尾点空格/超长/空）、唯一名去重与溢出、视图容量/清理/打开位置门禁/失败释放。执行独立 downloads configure/build/ctest、适用 Google-style clang-format、`git diff --check`。
- 明确不做：不实现 CEF 下载 handler 接线和真实断点续传（归后续 CEF adapter 任务）、下载 shelf 像素 UI、持久化下载历史（归 `BUX-13` preferences 边界外另行评估）、病毒扫描、macOS 实机。

## BUX-11 完成记录（下载领域模型与下载栏视图）

- 状态：`DONE`；依赖 `CEF-05 DONE`、`BUX-02 DONE`。
- 实现：新增 8 个生产/测试文件。`browser/downloads/` 提供 `DownloadItem`（闭合六态状态机：危险文件进入 `kPendingDangerConfirm`，须显式确认/丢弃；进度字节有界；Pause/Resume/Cancel/Retry/Complete 越状态迁移稳定拒绝；`OpenItem`/`OpenLocation` 仅 `kCompleted`）、`ClassifyDownloadDanger`（闭合可执行/脚本扩展名集合、大小写不敏感、只取 basename 最终扩展名）与 `DownloadPath`（文件名清理：剥离分隔符/控制符/结尾点空格/纯点名，长度 ≤ 128；唯一路径 " (n)" 去重 n ≤ 999，存在性由调用方谓词注入，本模块零文件系统访问）。`browser/shared-ui/downloads/` 提供 `DownloadShelfStateMachine`（≤ 64 条有界投影、重复 ID/非法投影拒绝、新增自动展开 shelf、`ClearCompleted`、失败/取消后的 UI 侧释放、shutdown 全拒绝）。
- 自动验证：独立 `cmake -S . -B .cache/build/downloads -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功（`-Wall -Wextra -Wpedantic -Werror` 零告警）；`ctest -R download` 为 `2/2` 通过（19 + 9 组用例覆盖状态迁移矩阵、危险确认/丢弃、进度越界、清理与去重边界、容量、shutdown）；共享层全量回归（除 `cef_build_graph_contract` 因本机缺 Ninja 环境阻塞外）全部通过；`git diff --check`、`cargo fmt --all -- --check` 通过；边界扫描确认两个模块均无 CEF/Win32/AppKit/ArkWeb 引用。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；关闭 1 个 P3（`Create` 补充“须先经 `SanitizeDownloadFileName`”契约注释），最终 P0/P1/P2 均为 `0`。生产文件 44～143 行、函数均低于规模提醒线；领域与视图分离为两个独立 target。
- 未覆盖与风险：本机无 Ninja，`cef_build_graph_contract` 未运行（环境阻塞，与本次改动无关）；CEF 下载 handler 接线、真实断点续传、shelf 像素 UI、下载历史持久化与 macOS 实机归后续任务（CEF-08/14、BUX-13/18）。`BUX-11` 转为 `DONE`。

## BUX-04B 完成记录（omnibox provider 配置与隐私集成）

- 状态：`DONE`；依赖 `BUX-04A DONE`、`PRV-06 DONE`。
- 实现：新增 `browser/shared-ui/omnibox/provider/`（2 个生产文件）。`SearchProvider`/`SearchProviderSet`：schema v1 内存结构，默认空集（不内置任何第三方搜索引擎 URL），模板校验覆盖空名称、超长（≤2048）、非 http/https scheme、credentials（`user:pass@`）、控制字符、`{searchTerms}` 占位符缺失/重复，容量上限 8 且按优先级取首个合法 provider；`BuildSearchUrl` 对搜索词做 RFC 3986 unreserved 保留的 UTF-8 百分号编码，超长 fail closed。`ResolveSchemelessUrl` 按 `PrivacyDefaults::https_default` 为无 scheme URL 补 `https://`/`http://`，不做失败降级。`DeriveSearchRequestPolicy` 从 PRV-06 默认值派生 Referer 策略与第三方 Cookie 开关（仅 `kAllow` 才携带，隐私优先）。实际检查发现 BUX-04A core 并未留下 `PrivacyDefaults` 占位结构，provider 模块通过链接 `crayon::browser-privacy-standard` 直接消费，无需修改 core。
- 自动验证：独立 `cmake -S . -B .cache/build/provider -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功且 `-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R omnibox_provider_contract` 1/1 通过（校验矩阵、空集无 URL、容量/优先级、中文与 `&` 编码、HTTPS 升级开关、隐私注入矩阵）；全量回归 16/16 通过（除本机缺 Ninja 的 `cef_build_graph_contract` 环境阻塞）；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；Review 发现并关闭 1 个 P2（`ValidateProvider` 初版用越界哨兵枚举表示“无错误”，已改为 `std::optional<ProviderError>`），最终 P0/P1/P2 均为 `0`。
- 未覆盖与风险：远程搜索建议 API、搜索引擎发现、偏好持久化（`BUX-13`）与设置 UI 未做；`BUX-04B` 转为 `DONE`。

## BUX-08 完成记录（多窗口、受控 popup、全屏与画中画策略）

- 状态：`DONE`；依赖 `BUX-06 DONE`、`CEF-05 DONE`。
- 实现：新增 `browser/shared-ui/windows/` 4 个生产文件——纯函数 `PopupPolicy`（来源判定：程序化 popup 默认拒绝、opener 存在性、每 opener 上限 `kMaxPopupsPerWindow = 4`、全局窗口上限 `kMaxWindows = 8`）与 `WindowStateMachine`（窗口注册/焦点 recency/幂等关闭、全屏与画中画互斥及恢复、popup 窗口模式拒绝、聚焦窗口关闭后回退到最近普通窗口、shutdown 后全拒绝）。共享模块无 CEF/Win32/AppKit/ArkWeb 类型。
- 自动验证：独立 `cmake -S . -B .cache/build/windows -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功（`-Wall -Wextra -Wpedantic -Werror` 零告警）；`ctest --test-dir .cache/build/windows -R windows_contract --output-on-failure` 为 `1/1` 通过（23 组用例覆盖创建/焦点/关闭/重复与旧事件、容量、popup 来源/取消/容量矩阵、全屏/PiP 互斥与恢复、shutdown 拒绝、纯策略决策矩阵）；共享层全量回归（除 `cef_build_graph_contract` 因本机缺 Ninja 环境阻塞外）全部通过；`git diff --check`、`cargo fmt --all -- --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；Review 发现并关闭 1 个 P2（焦点回退循环初版为死代码，已改为真实 recency 重排），最终 P0/P1/P2 均为 `0`。生产文件 40～218 行、函数均低于规模提醒线；策略与状态机分离为两个组件。
- 未覆盖与风险：本机无 Ninja，`cef_build_graph_contract` 未运行（环境阻塞，与本次改动无关）；CEF/Win32/AppKit 窗口接线、窗口像素 UI、多显示器/DPI、窗口会话恢复（`BUX-15`）、跨窗口标签移动的窗口管理器实现与 macOS 实机归后续任务。`BUX-08` 转为 `DONE`，解锁依赖它的后续窗口集成任务。

## BUX-04B 原子范围（omnibox provider 配置与隐私集成）

- 状态：`DONE`；依赖 `BUX-04A DONE`、`PRV-06 DONE`。
- 单一目标：在 BUX-04A 判定引擎基础上，交付搜索 provider 配置契约、HTTPS 默认升级决策和隐私默认集成；本任务不重新实现 URL/搜索判定核心逻辑。
- 输入：BUX-04A 的判定结果（URL 或搜索词）、`crayon-domain::config` 配置框架（FND-11）、PRV-06 的隐私默认设置（HTTPS 默认、Referer 策略、第三方 Cookie 策略）。
- 输出与允许修改：新增 `browser/shared-ui/omnibox/provider/` 的 provider 配置模型、HTTPS 升级决策器、隐私参数注入器和独立 contract test/CMake；允许修改根 CMake、`browser/shared-ui/omnibox/core/` 的隐私配置注入接口（由占位结构改为具体实现）、共享 locale JSON（新增 provider/隐私文案键）和本 Roadmap/current/总 Roadmap 索引。新增生产文件不超过 3 个。
- 禁止修改：BUX-04A 的判定引擎核心逻辑、`browser/engine-api`、macOS/Harmony 源码、Profile/持久化、下载/权限/投屏业务、Cast-SDK、Relay、Agent/模型；不得引入远程搜索 API 调用、第三方搜索 SDK 或通用网络请求。
- Provider 配置契约边界：
  - 搜索 provider 为编译期/本地配置的内存结构，schema_version=1，含名称、搜索 URL 模板（含占位符如 `{searchTerms}`）、参数映射和编码方式。
  - 默认 provider 为空（用户未配置时不发送搜索请求），不内置 Google/Bing/百度等第三方搜索 URL。
  - Provider 配置校验：URL 模板必须是合法 `http`/`https` URL，含且仅含一个搜索词占位符；危险 scheme、credential、控制字符或超长老式 URL 稳定拒绝。
  - 多 provider 支持：允许配置多个 provider 并按优先级排序；搜索词提交时使用第一个合法 provider。
- HTTPS 默认升级边界：
  - 对 BUX-04A 判定为 URL 但无 scheme 的输入（如 `example.com`），根据 PRV-06 的隐私默认决定是否自动添加 `https://` 前缀。
  - 升级决策为配置注入：BUX-04A 的判定引擎接收 `PrivacyDefaults` 结构，其中 `https_default` 字段控制是否升级；该字段由 BUX-04B 从 PRV-06 配置填充。
  - 升级失败（如目标不支持 HTTPS）不自动降级到 `http://`，由导航层报告连接失败；不实现 HSTS 或证书固定。
- 隐私默认集成边界：
  - 搜索请求的 Referer 策略：根据 PRV-06 配置，搜索词提交时可设置为 `no-referrer` 或 `strict-origin`。
  - 搜索请求的 Cookie 策略：根据 PRV-06 配置，决定是否携带第三方 Cookie（默认不携带）。
  - 隐私配置与 provider 配置冲突时，以隐私配置为优先；冲突场景记录为 P2 跟踪项。
- 验收与测试：UX-003B；provider 配置覆盖合法/损坏/空/多 provider/危险 scheme、HTTPS 升级开关矩阵（开/关/无 scheme 输入/有 scheme 输入）、隐私参数注入（Referer/Cookie 策略）、配置冲突降级。执行独立 provider configure/build/ctest、Windows Debug/Release build+ctest、适用 Google-style clang-format、`scripts/check.ps1 fast/security`、`git diff --check`。
- 明确不做：不实现远程搜索建议 API、搜索引擎自动发现、搜索词补全、趋势搜索、语音搜索、地址栏安全标识渲染、自动填充或 macOS 实机；分别由后续任务和平台总验收完成。

### BUX-12 完成记录（2026-08-22）

- 实现：新增 `browser/shared-ui/page-tools/`（header/impl/CMake/契约测试各 1）。`FindBarController`：查找会话（query ≤1024 字节、大小写选项、match cursor wrap、EndFind 无残留清空）；`ZoomController`：17 档闭合缩放集合（25%..500%），ZoomIn/Out 沿集合步进且边界拒绝、SetZoom 仅接受集合成员；`FullscreenController`：Windowed→Entering→Fullscreen→Exiting 闭合迁移，过渡态抑制重复命令；`PageOutputJobController`：打印 PDF/保存页面共用作业管线（Idle→Preparing→Running→Succeeded/Failed/Cancelled，终态需 Acknowledge 复位），文件名闭合字符集校验（禁分隔符/前导点/超长），作业绑定 Profile token，跨 Profile 的成功投递 fail-closed 为 `kProfileMismatch` 且不产出（UX-011 输出路径受控、不泄漏其他 Profile）。无 CEF/平台类型、无 IO；单线程 UI 契约注明。
- 验证：`cmake -S . -B .cache/build/page-tools -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功，`-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R page_tools_contract` 1/1 通过（8 组：查找生命周期、缩放闭合集合与边界、全屏迁移、文件名矩阵、作业生命周期、失败/取消、跨 Profile fail-closed、Start 校验）；共享层回归 24/26 通过（2 失败为本机既有 CEF 环境阻塞 `cef_distribution_contract`/`cef_build_graph_contract`，与 PRV-06 记录一致，非本任务影响）；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1（FindBarController 的 case_sensitive 只能在 StartFind 设置、无独立开关命令——与 Chrome Ctrl+F 行为一致，若后续 UX 评审需要查找栏内切换，再补独立任务）。
- 未覆盖与风险：CEF shell 命令接线与 Windows/macOS 实机 UX 验收归 CEF/QAR 装配任务；打印/PDF 引擎侧与保存格式实现属 engine adapter。`BUX-12` 转为 `VERIFIED`（实机门禁后置）。

### BUX-16 完成记录（2026-08-22）

- 实现：新增 `browser/shared-ui/context-menu/`（header/impl/CMake/契约测试各 1）。`IsAvailableIn` 闭合可用性矩阵实现上下文最小化（Link/Image/Selection/Page 四上下文各自命令集，隐藏命令在 `Execute` 层稳定拒绝）；`ValidateContextUrl` 闭合 scheme 守卫（仅 `http`/`https` 可打开，`javascript:`/`data:`/`file:`/`vbscript:`/`blob:` 等一律 `kSchemeRejected`，空/无 scheme/超 2048 字节 `kMalformed`）；`ClipboardGuard` 用户命令复制（页面来源拒绝、1MiB 上限、Acknowledge 后清空缓冲）；`LocalFileEntryGuard` 受控本地文件入口（闭合字符集、禁分隔符/前导点/`..`/超长、单一 pending、两步 Request→Confirm/Cancel、页面来源不可发起）。无 CEF/平台类型、无 IO；单线程 UI 契约注明。
- 验证：`cmake -S . -B .cache/build/context-menu -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 零告警零错误；`ctest -R context_menu_contract` 1/1 通过（5 组：矩阵最小化、菜单生命周期与隐藏命令不可达、scheme 矩阵、剪贴板边界/来源、本地文件两步流）；共享层回归 25/27 通过（2 失败为本机既有 CEF 环境阻塞，与 BUX-12/PRV-06 记录一致）；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1（拖放（drag-drop）目标侧策略未建模——本任务只覆盖菜单/剪贴板/文件入口命令面，拖放归 CEF shell 装配任务时按同 scheme/路径守卫接线，已在此记录归属）。
- 未覆盖与风险：CEF shell 菜单呈现/拖放接线与实机验收归后续装配任务；剪贴板读取面（paste 内容来源）不在本模型。`BUX-16` 转为 `VERIFIED`（实机门禁后置）。

### BUX-15 完成记录（2026-08-22）

- 实现：新增两个平台中立模块（各 header/impl/CMake/契约测试）。
  `browser/session`（`SessionRestoreCoordinator`）：按 Profile 隔离的会话记录（闭合 id token、窗口 ≤32/Profile、Profile ≤16、tab ≤64 全部有界，满载拒绝）；无痕窗口在 RecordWindow 层直接拒绝进入恢复集（无痕不恢复）；启动策略映射 preference 的 `startup_policy` 值（NewTab/Restore），PlanRestore 返回闭合决策（NewTabOnly/RestoreRecorded/RestoreAfterCrash）；崩溃恢复只恢复 Checkpoint 过的窗口，未确认 tail 丢弃并显式报告 dropped 数（旧 session 不误恢复）；每 Profile epoch 单调递增，旧 epoch 结果拒绝；ClearProfile 释放。
  `browser/shared-ui/profiles`（`ProfilePickerModel`）：有界 Profile 列表（≤64、重复拒绝、id 与显示名分离字符集——id 无空格）；切换矩阵（Switched/UnknownProfile/AlreadyActive/Busy）；无痕窗口请求仅对活跃 Profile 成立且按构造不进入会话恢复；清理失败显式报告（ReportCleanupFailure 置位 pending、切换被 kBusy 阻塞、Acknowledge 后才放行——失败不被吞掉）。无 CEF/平台类型、无 IO；单线程 UI 契约注明。
- 验证：`cmake -S . -B .cache/build/bux15 -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 零告警零错误；`session_restore_contract`（7 组：id 校验、无痕拒绝、策略决策、崩溃 tail 丢弃、旧 epoch 拒绝、跨 Profile 隔离、容量）与 `profile_picker_contract`（5 组：列表管理、开合、切换矩阵、无痕请求、清理失败显式）均 1/1 通过；共享层回归 27/29（2 失败为本机既有 CEF 环境阻塞，与前次记录一致）；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1（会话记录为视图模型，未含磁盘持久化 schema——崩溃后进程内 tail 丢失语义与真实崩溃场景的 checkpoint 频率需在 CEF shell 接线时定义，持久化归后续 engine adapter 任务）。
- 未覆盖与风险：CEF shell 装配（picker UI 呈现、无痕窗口创建接线、崩溃标记来源）、磁盘 session 持久化与实机验收归后续任务。`BUX-15` 转为 `VERIFIED`（实机门禁后置）。
