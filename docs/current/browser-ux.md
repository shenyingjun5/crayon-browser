# 蜡笔 AI Agent 投屏浏览器桌面体验契约

- 版本：`browser-design-v1`
- 状态：`BUX-01 DONE`，`browser-design-v1` 已冻结
- 范围：Windows/macOS CEF 桌面浏览器；HarmonyOS 后续复用语义和可访问性原则，不复用桌面像素假设
- 2026-09-03 投屏交互修订：`cast-interaction-v1` 补充入口顺序/常驻灰态/显式选择，旧 BUX 完成证据保留；新实现由 PLT-CAST-R 切片验收。
- 2026-09-04 宿主决定：一期开始自定义 Shell＋CEF Alloy，按 [PLT-SHELL](../plans/desktop-shell-roadmap.md) 迁移。本文双行结构、design token、三语言、键盘与无障碍不变；原生 Chrome 标签栏/omnibox 不再是目标 UI owner。控件由 Browser 拥有，内容视图不得遮挡顶部安全区域。

## 1. 设计目标

桌面 UI 采用 Chrome/Chromium 用户熟悉的两层信息架构和快捷键心智，但所有品牌、功能 glyph、内置页面和服务均为蜡笔自有实现。顶部第一层是标签栏与窗口控制，第二层是导航栏；投屏始终是一级产品动作。页面内容不得绘制、遮挡或伪造浏览器安全 UI、投屏状态或窗口控制。

## 2. 顶部结构

| 区域 | 逻辑高度 | 顺序与职责 |
|---|---:|---|
| 标签栏 | 40 DIP | 应用身份、标签集合、新建标签、标签搜索、窗口控制 |
| 导航栏 | 48 DIP | 后退、前进、刷新/停止、主页、omnibox、投屏、书签、下载、Profile、主菜单 |
| 顶部合计 | 88 DIP | 页面 viewport 从该区域下方开始，不允许网页覆盖 |

功能 glyph 使用 24×24 DIP 画布、默认 20 DIP 可见线条；最小点击目标 32×32 DIP，首选 36×36 DIP。焦点环为 2 DIP，不依赖颜色之外的单一状态信号。

## 3. 标题栏与图标规则

1. 应用身份图标只使用 `app-icon-v1:micro`。允许位置仅为原生标题栏、任务栏、Dock 和窗口切换器；不得用作投屏、连接、权限、Agent、Challenge 或错误图标。
2. 后退、前进、刷新、停止、主页、标签、书签、下载、投屏、Profile 和菜单使用 `browser/shared-ui/design/icons/manifest.json` 注册的自有 SVG glyph。不得从 Chrome 安装包、Google 页面、字体或第三方图标库提取。
3. SVG 只表达几何形状并继承 `currentColor`。rest/hover/pressed/focus/disabled、eligible/casting/error 等状态由 design token 和受审状态机着色，不能在 SVG 内编码业务状态。
4. Windows/macOS 默认使用系统原生最小化、最大化/还原、关闭按钮。若后续采用自绘框架，只能使用 manifest 中 `window.*` glyph，点击语义和系统可访问性仍由平台 adapter 负责。
5. 图标本身 `aria-hidden`；按钮必须从本地化资源提供可见 tooltip 和 accessible name。不可只靠图标方向、颜色或动画传达危险/错误状态。

## 4. 响应式与主题

- 宽窗口从 800 DIP 起显示完整导航动作；720 DIP 规格下保留后退、前进、刷新/停止、omnibox、投屏和主菜单，主页、书签、下载、Profile 进入受控 overflow。投屏不能因窄窗口降为页面内隐藏动作。
- omnibox 最小宽度 160 DIP；空间不足时优先收缩标签和低优先级动作，不能重叠窗口控制、投屏或地址输入。
- light/dark 使用语义 token，不以网页主题直接驱动浏览器 chrome。高对比度和系统强调色适配由平台任务追加能力映射，不在组件散落平台判断。
- 100%/200% 只改变物理像素，不改变逻辑 DIP、焦点顺序或图标 role。

## 5. 组件状态与可信边界

- 按钮：`rest/hover/pressed/focus-visible/disabled`；disabled 仍有可访问原因，不以页面消息启用。
- 标签：`inactive/hover/active/attention/dragging`；active tab 与键盘 focus 分离，旧 tab/navigation 事件不得覆盖当前状态。
- omnibox：`rest/hover/focused/editing/invalid`；站点身份属于浏览器 chrome，页面不可伪造。
- 投屏：`unavailable/eligible/selecting/casting/error`；状态只消费 Browser process 可信播放事实和受审投屏用例。`casting` 必须对应真实 session，不显示虚假成功。
- 投屏按钮在网址输入框外侧紧邻其后，零有效候选常驻灰色禁用；面板区分选择、连接、评估、提交和真实播放。多视频明确选择，连接不自动播放，播放器覆盖层只预选并打开同一面板。所有权、兼容、有效期和失效规则按 [投屏交互契约](cast-interaction.md)；本段不代表当前 CEF 标题栏实现已迁移。

## 6. 键盘、焦点与无障碍

- 保留常用桌面心智：`Ctrl/Cmd+L` 聚焦 omnibox，`Ctrl/Cmd+T` 新标签，`Ctrl/Cmd+W` 关闭标签，`Ctrl/Cmd+Shift+T` 恢复标签，`Ctrl/Cmd+Tab`/`Ctrl/Cmd+Shift+Tab` 切换标签，`Alt+Left/Right`（macOS 使用平台等价项）前进后退，`Ctrl/Cmd+R` 刷新，`Esc` 停止加载或关闭当前临时层。
- Tab 顺序按视觉层级稳定推进：活动标签及标签动作 → 导航动作 → omnibox → 页面。临时菜单打开后圈定焦点，关闭时还给触发控件；窗口关闭或导航后不得把焦点还给已销毁对象。
- 所有图标按钮必须有本地化名称、禁用原因和键盘激活；tooltip 不能作为唯一说明。读屏顺序与视觉顺序一致，RTL 仅镜像 manifest 明确允许的方向性 glyph。

## 7. UX-001 规格门禁

`browser/shared-ui/design/golden/` 固定 light/dark × narrow/wide × 100%/200% 八种数据规格，覆盖逻辑/物理尺寸、语义颜色、可见/overflow 控件和一级投屏入口。该 golden 用于阻止 token 漂移，不代表 Windows/macOS 像素截图、IME、读屏、多屏或真机 DPI 已通过；平台实现仍需 UX-016 和 BUX-18 证据。

## 8. 本契约不实现

本文件不表示完整 UI 已完成。实际 UI shell/命令 registry 由 BUX-02，起始页由 BUX-03，omnibox/导航由 BUX-04/05，标签由 BUX-06/07，投屏状态绑定由 CEF-13，平台窗口与截图/真机门禁由后续 CEF/BUX/QAR 任务完成。
