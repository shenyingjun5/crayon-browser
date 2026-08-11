# BUX：桌面浏览器产品体验 Roadmap

状态：`BUX-01 DONE`；Chrome-inspired 信息架构、共享 design token、标题栏/标签栏/导航栏自有 glyph 与平台适配边界已经冻结。`BUX-02 TODO`，等待 `CEF-02` 后接入实际 UI shell。本 Roadmap 把“基本浏览器有的功能”拆成可审查原子任务；视觉、品牌、内置页面与服务均为蜡笔自有实现。

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
| BUX-02 | TODO | BUX-01,CEF-02 | `browser/shared-ui/shell` | UI shell、命令 registry、focus owner 与 engine event adapter 骨架 | UX-001；重复 command、旧 tab event、窗口释放 |
| BUX-03 | TODO | BUX-02,CEF-03 | `browser/shared-ui/new-tab` | 本地 `crayon://newtab`、普通/无痕差异、固定快捷入口模型 | UX-002；零默认公网请求、损坏配置、安全 resource handler |
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
- 明确不做：实际 CEF UI shell、按钮点击、标签状态机、本地新标签页、omnibox、投屏业务绑定和平台截图；分别由 `BUX-02..06`、`CEF-02/03/08/13` 实现和验证。

完成记录（2026-08-11）：

- 失败基线：先建立独立 CMake/Node contract；首次 `ctest --test-dir .cache/build/browser-design --output-on-failure` 因 `tokens.json` 不存在按预期 `0/1` 失败，证明验收不是在缺失实现时预先通过。
- 规范与资产：新增 `browser-design-v1` 两层桌面信息架构、DIP 密度、light/dark 语义色、窄/宽优先级、组件状态、键盘/焦点/无障碍契约；21 个自有 24×24 单色功能 glyph 覆盖窗口控制、标签、导航、书签、投屏和常用入口。应用身份继续只引用 `app-icon-v1:micro`，没有修改 `assets/brand/generated/`。
- Golden：确定性生成并比对 light/dark × narrow/wide × 100%/200% 共 8 份规格 golden；重复生成前后 SHA-256 集合不变。全部 21 个 SVG 经 XML 解析器验证为 well-formed。
- 自动验证：独立 configure 成功，`ctest --test-dir .cache/build/browser-design --output-on-failure` 为 `2/2` 通过；正向 contract 检查闭合 token/role/surface/state、App-icon 禁用、SVG 主动内容/外链/状态色和 golden，拒绝 contract 证明缺投屏 role、外链 SVG、stale golden、缺主题、未审状态和未注册图标均 fail closed。Node 三个脚本 `--check`、`scripts/check.ps1 brand-assets`、`fast`、`security` 和 `git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；发现并关闭 1 个 P1（SVG 主动内容与 token/surface/state 未完全闭合），最终 P0/P1/P2/P3 均为 `0`。
- 未覆盖与风险：这些是平台中立规范、SVG 和数据 golden，不是 Windows/macOS 像素截图或交互 UI。实际 shell 接入、tooltip 本地化、平台高对比度/IME/读屏/多屏 DPI 和投屏状态绑定分别由 `BUX-02`、`UX-016`、`CEF-13`、`BUX-18` 验证；不得把 BUX-01 描述为完整浏览器 UI 已完成。
