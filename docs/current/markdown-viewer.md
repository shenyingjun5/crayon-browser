# 本地 Markdown 查看器（MDV）契约

- 版本：v1.4（`MDV-21..24` 编辑器图标工具栏实现与平台装配修订）
- 日期：2026-08-28
- 上位依据：PRD v0.8 §4.1；本契约是 `MDV-02..24` 的验收输入，冲突时以上位契约为准。

## 1. 定位与非能力声明

- 查看器是桌面基线的**用户**文档能力：查看本地 `.md` 文件、渲染预览与分栏编辑。
- **它不是 Agent 能力**：不进入 CAAP tool registry，不暴露任何 CAAP 工具；Agent 无法打开本地文件、无法读取查看器内容或触发保存。AGT 永久禁止清单中的任意文件访问语义在此不受影响。
- 查看器运行在独立 origin `crayon://mdv`，与 `crayon://newtab`、web 页面互不共享权限或存储。

## 2. Scheme / Origin / CSP 与资源路由

- Scheme：`crayon://mdv`，复用 `BUX-03` 确立的内置页自定义 scheme handler 模式（Browser process 内存资源 + 受控 resource handler）。
- 资源路由：页面框架（HTML/CSS/JS）只从编译期/内存资源提供，带内容 hash 校验的固定路径（如 `/app.html`、`/app.css`、`/app.js`）；Mermaid Full 的 ESM 入口与运行时 chunk 由构建期生成的只读 manifest 精确枚举在 `/assets/mermaid/<version>/<upstream-relative-path>` 下，路径与逐文件 hash 分别锁定，同样只从应用内资源提供。渲染内容 HTML 由 Browser process 确定性生成后经受控绑定注入，**不经网络、不经任意文件系统 URL**。
- CSP（全页强制，resource handler 下发）：

```
default-src 'none';
script-src 'self';
style-src 'self';
img-src 'self' https:;
connect-src 'none';
font-src 'none';
media-src 'none';
object-src 'none';
frame-src 'none';
base-uri 'none';
form-action 'none';
frame-ancestors 'none'
```

- 零默认网络请求：不加载远程脚本、样式、字体、图片或任何子资源；内联事件属性禁止，脚本仅来自内存资源的独立文件。
- `crayon://mdv` 不响应任何携带文件路径参数的导航：路径只存在于 Browser process 内存状态中，永不出现在 URL、query 或页面 DOM 属性里。

## 3. 入口与手势门禁

入口有且仅有三种，全部要求真实用户手势；**页面内容不能以任何方式触发打开动作**（无 JS bridge、无 scheme 导航入口、无拖放伪造通道）：

| # | 入口 | 手势定义 | 说明 |
|---|---|---|---|
| E1 | 主菜单"打开文件"对话框 | 点击菜单项 | 对话框过滤器仅 `.md` |
| E2 | 拖放文件到窗口 | OS 拖放释放 | 仅接受单个 `.md` 文件 |
| E3 | Omnibox 输入本地路径 | 用户键入并提交 | 复用 `BUX-04A` 本地路径判定；判定为本地 `.md` 后路由进查看器 |
| E4 | 右键上下文菜单"在文档查看器中打开" | 用户在 `.md` 的 `file://` 页面或 `.md` 链接上呼出菜单 | 菜单项仅在 `.md` 目标出现；点击后按 §4 矩阵进入查看器 |

- 页面发起的任何导航（链接、脚本、重定向）指向 `crayon://mdv` 时一律拒绝进入查看器并显示错误页；E1/E2/E3 之外不存在第四条路径。
- 同一时刻一个标签只打开一个文件；重复打开按"切换文件"流程处理（见 §7 dirty 确认）。

## 4. 路径校验矩阵（E1/E2/E3 共用）

所有入口在 Browser process 内执行同一路径校验（复用 `PRV-04` path_guard 原语），任一命中即稳定拒绝并给出可操作提示：

| 类别 | 规则 |
|---|---|
| 后缀 | 必须以 `.md` 结尾（大小写不敏感）；`.markdown` 等其他后缀拒绝 |
| 类型 | 拒绝目录、设备文件、命名管道；必须是常规文件 |
| 字符 | 拒绝含控制字符（< 0x20 与 DEL）的路径 |
| 长度 | 路径字节数超过平台安全上限（Windows `MAX_PATH` 扩展语义 / macOS `PATH_MAX`）拒绝 |
| 穿越 | symlink/junction/.. 解析后逃出用户声明的根拒绝（path_guard） |
| 存在 | 文件必须已存在且可读（查看器不创建新文件；新建走另存为之外的用户工作流，不在 V1） |

## 5. 文件加载边界

| 项 | 上限/规则 | 超界行为 |
|---|---|---|
| 大小 | ≤ 5 MiB（5*1024*1024 字节） | 稳定错误提示，不加载、不部分渲染 |
| 编码 | UTF-8 严格校验；UTF-8 BOM 接受并在加载时剥离一次 | 非 UTF-8（含 UTF-16/二进制伪装）→ 稳定错误，不做替换式解码 |
| 换行 | CRLF 与 LF 均接受，内部统一为 LF；保存写回保留加载时的原始换行风格 | — |
| 空文件 | 合法，渲染为空预览 | — |

- 加载在 Browser process 有界缓冲完成，超界立即终止；UI 不阻塞、无半渲染状态（MD-003）。

## 6. 渲染语法范围（闭合清单）

基线 = CommonMark 常用子集 + GFM 表格。**未列入的语法一律按纯文本（转义）渲染**。启用清单：

**块级**
- ATX 标题 `#`..`######`
- Setext 标题（`===`/`---` 下划线）
- 段落与硬换行（行尾两空格或反斜杠）
- 围栏代码块 ``` 与 ~~~（含 info string 显示）；缩进代码块（4 空格）
- 标准 Mermaid 围栏代码块 ```` ```mermaid ````：仅精确、大小写敏感的 `mermaid` info string 进入图表扩展；图类型由 Mermaid Full 自行识别，不新增 ```` ```mindmap ````、```` ```architecture ```` 等蜡笔私有方言
- 引用块（可嵌套）
- 无序/有序列表（可嵌套、松散/紧凑）；GFM 任务列表项 `- [ ]` / `- [x]`（只读勾选态展示，编辑器内可改字符）
- GFM 表格（含对齐冒号）
- 主题分割线 `---`/`***`/`___`

**行内**
- 强调 `*em*`/`_em_`、强强调 `**strong**`/`__strong__`
- 删除线 `~~text~~`（GFM）
- 行内代码 `` `code` ``
- 链接 `[text](https://… "title")`
- 图片引用 `![alt](…)`（见 §7 加载规则）
- 尖括号自动链接 `<https://…>`、`<mailto:…>`

**明确不在清单内（按纯文本转义渲染）**：原始 HTML（块/行内）、脚注、定义列表、顶层数学公式、wiki 双链、admonition、高亮/上下标/剧透扩展、自动 URL 裸链（未加尖括号者保持纯文本）。Mermaid Full 随包携带的 KaTeX/图标等能力不等于自动扩张 Markdown 语法；需要外部字体、图标包、网络资源或新 Markdown 方言的能力默认关闭，后续必须单独修订契约。

## 7. 渲染安全规则

- 输出确定性：同一输入字节串在任何平台任何时刻产出逐字节相同的 HTML（golden 锁定，MD-002）。
- 全量转义：原始 HTML 片段永不透传——解析引擎以"禁用 raw HTML"模式运行，HTML 语法按纯文本转义输出。
- 生成标签白名单：输出中允许出现的标签闭合枚举为 `h1-h6, p, br, hr, blockquote, pre, code, ul, ol, li, table, thead, tbody, tr, th, td, em, strong, del, a, span, input(checkbox 只读禁用态)`；属性白名单为 `href, title, align, class(限内部样式标记), type/checked/disabled(任务列表)`。白名单外的一切生成内容不允许存在。
- 链接目标：仅接受 `http:`/`https:`/`mailto:` 绝对地址为可点击链接（点击走浏览器正常导航门禁）；相对路径、`file:`、`javascript:` 及其他 scheme 一律渲染为纯文本。
- 图片加载（v1.1 修订，原"永不加载"作废）：
  - 云端：仅 `https://` 直载（CSP `img-src 'self' https:`）；`http:` 一律占位（防明文劫持）；加载失败显示占位框与 alt 文本。
  - 本地：仅当前文档同目录及子目录内的相对/绝对路径；格式白名单 `png/jpg/jpeg/gif/webp/bmp/svg`（svg 经 `<img>` 上下文渲染，无脚本执行面）；大小 ≤20 MiB；解析后不得逃逸文档目录。
  - 页面 DOM/URL 中永不出现本地路径：渲染管线把合法本地引用改写为不透明序号路由 `crayon://mdv/img/<N>`，N 到绝对路径的映射只在 Browser 进程内存中，文档切换时代际失效。
  - 拒绝集合占位：非白名单格式、`data:`/`javascript:`/其他 scheme、不存在/非常规文件、超限文件均渲染为占位框并显示 alt 文本与引用地址文本。
- 输出不包含：script、事件属性（on*）、iframe/object/embed、表单动作、外部引用（src/href 指向非用户点击链接的外部资源）。
- Mermaid fence 在 Browser process 只生成带不透明 block ID 的占位节点；原始 DSL 作为不可信文本交给页面内的受控扩展运行时，不拼进脚本、URL 或事件属性。Mermaid 返回的 SVG 不复用 Markdown HTML 白名单：必须先经过独立 SVG policy gate，拒绝 script、事件属性、`javascript:`/`data:`/`file:`、外部 URL、`foreignObject`、`@import` 与 CSS `url()`，通过后才可局部替换对应占位节点。

## 8. 视图与编辑交互

- 三种视图态：源码视图、渲染预览、分栏模式（左源码右实时预览）。切换即时，滚动位置在会话内保持。
- 源码面板在源码/分栏态为可编辑 `<textarea>`；分栏两面板之间的间隔条支持左右拖动调整宽度（纯前端调整，会话内有效）。
- 分栏滚动联动：编辑侧滚动时预览侧按同一滚动比例跟随（单向，右→左不联动以防循环）。V1 为滚动比例近似——渲染引擎不产出源码行号映射，逐行精确同步属于后续契约扩展。
- 编辑工具栏（源码/分栏态源码面板顶部）：基线闭合动作集共 **15 项**——标题 H1/H2/H3、加粗、斜体、删除线、行内代码、无序列表、有序列表、任务列表、引用块、代码块、表格、链接、分割线。历史 `MDV-12` 文档中的“14 项”是计数错误，以真实 action 集与本修订为准。交互语义三类：包裹（选中文本→包裹标记并保留选区，无选中→插标记对光标居中）、行前缀（对选中行施加，无选中对当前行）、骨架插入（结构化模板，光标落首个占位）。全部经单次 `setRangeText`（保留撤销历史）并触发既有编辑通道更新预览；预览态与无文档态隐藏；不引入新 binding、不携带外部资源。
- 工具栏视觉采用 icon-only 顶层控件：24×24 DIP 图标画布、20×20 DIP glyph、点击区优选 36 DIP 且不得低于 32 DIP、2 DIP 可见焦点环；图标必须是蜡笔原创资产、继承 `currentColor`、无外部引用，不复制第三方产品 glyph。源码/预览/分栏同样改为图标分段控件；窄分栏保持单行并按冻结优先级收入“更多”，不得通过多行换行持续挤压编辑区。
- 每个图标按钮必须有本地化 `aria-label` 与两行 tooltip（动作名/平台快捷键 + 简短 Markdown 语义说明）；hover 450ms 后显示，键盘 focus 立即显示，`Escape`/blur/scroll/激活后关闭。工具栏使用 roving tabindex：Tab/Shift+Tab 只进出一次，左右键移动，Home/End 到首尾；`prefers-reduced-motion` 下关闭非必要过渡。
- 快捷键 profile 由平台 adapter 以闭合语义注入：macOS 主修饰键显示/匹配 Meta（`⌘`），Windows 显示/匹配 Control（`Ctrl`）；共享 UI 不通过 UA/路径猜平台。闭合快捷键为 H1/H2/H3=`Primary+Alt+1/2/3`、加粗=`Primary+B`、斜体=`Primary+I`、删除线=`Primary+Shift+X`、无序列表=`Primary+Shift+8`、有序列表=`Primary+Shift+7`、链接=`Primary+K`。其余动作无稳定行业组合时不显示快捷键；IME composing、keyCode 229 与 AltGr 期间不得触发格式动作。
- “缩进和表格对齐”作为结构菜单，不扩张 Markdown 方言：增加/减少缩进只作用于无序/有序/任务列表和引用层级，`Tab`/`Shift+Tab` 仅在这些结构上下文拦截；表格列默认/左/中/右对齐只改写 GFM delimiter cell（`---`/`:---`/`:---:`/`---:`），光标不在可确定识别的 GFM 表格时禁用。普通段落首行缩进、居中、右对齐保持不可表达，禁止用 raw HTML、CSS 或私有标记模拟。
- 文档宿主 tab：编辑/保存/未保存确认只作用于打开文档的那个标签；其他标签打开文件独立互不影响（V1 每窗口一份文档状态，多标签同时编辑两份为明确不做项）。
- 分栏编辑：右侧预览随左侧编辑确定性增量更新；连续快速输入时合并渲染帧（去抖 ≤100ms），旧渲染结果不残留（MD-004）。
- Mermaid block 默认显示轻量占位，进入 viewport 后才触发图表渲染；普通 Markdown 或不含 Mermaid fence 的文档不得加载 Mermaid ESM。主题变化与编辑 revision 变化只重绘受影响 block，迟到 SVG 按文档/revision/block generation 丢弃。
- Mermaid 单 block 失败只显示局部错误卡片与源码切换入口，其他 Markdown 和其他图表继续可用；错误不得回显本地路径、堆栈或整篇文档内容。
- 编辑只产生内存 dirty 状态，绝不自动写盘。
- dirty 状态下关闭标签、切换文件、导航离开：显式三选确认（保存并继续 / 放弃更改 / 取消）；取消不丢内容，放弃不写盘（MD-005）。

## 9. 保存语义与外部修改冲突

- 两条保存路径：写回原文件（Ctrl+S）；另存为（对话框选择新位置，仍受 §4 路径矩阵约束，但允许写入已存在的 `.md`）。
- 原子写：写入同目录唯一临时文件（如 `<name>.md.tmp-<pid>`）→ flush → rename 覆盖目标；任一步失败即显式报错，临时文件尽力清理，清理失败必须报告残留路径（不得静默宣称成功）。
- 外部修改冲突：加载与每次保存前记录 `(size, mtime)`；保存前重新 stat，若与已知值不符则弹冲突提示（覆盖我的 / 另存为 / 取消），绝不静默覆盖他人修改（MD-006）。
- 只读位置、盘满、权限不足：稳定错误文案，无半写文件、无静默残留。

## 10. 无痕窗口与会话边界

- 无痕窗口可用查看器全部功能；最近文件、滚动位置等会话状态不持久化，窗口关闭即消失。
- 普通模式 V1 同样**不持久化最近文件列表**（避免本地路径进入磁盘 Profile 的隐私面）；滚动位置仅内存态。
- 保存是用户显式动作，无痕不阻止向用户选择的路径写盘，但不记录该路径。

## 11. 明确不做

- 不做远程 `.md` URL 渲染（http/https 地址进查看器的请求直接拒绝）。
- 不做双链/wiki 扩展语法、协同编辑、导出 PDF、打印排版优化。
- 不开放目录枚举、文件系统监控（外部修改检测仅针对当前打开文件的 stat）、任意路径访问。
- 不作为 Agent/CAAP 能力暴露（§1）；页面内容不能触发打开/保存/导出。
- 不支持 `.markdown`/`.mdown` 等扩展名、加密/压缩文档、二进制格式。
- 不引入运行时动态下载的渲染库或样式。

## 12. 渲染选型评审结论

**决定：采用 vendored md4c 0.5.3（MIT）作为解析引擎；若 `MDV-02` 开工时 vendor 评审任一项不过，退回自研同等闭合语法子集（本契约 §6 为唯一语法规范，引擎可替换不影响契约）。**

评审证据（2026-08-24，基于上游仓库 README/CHANGELOG）：

| 维度 | 结论 |
|---|---|
| 来源 | 上游 `github.com/mity/md4c`，vendor 进 `third_party/md4c` 并锁定 revision；下载产物须记录内容 hash |
| 许可证 | MIT（LICENSE.md），允许 vendor 与再分发，无 copyleft 传染 |
| 语言/依赖 | C99 单源文件 + 单头文件，仅依赖标准 C 库；跨平台（Qt、LibreOffice、ONLYOFFICE 生产采用） |
| 合规性 | 宣称完全符合 CommonMark 0.31；扩展经编译/运行 flag 启用——tables、task lists、strikethrough 在我们的启用清单内，其余 flag 全关 |
| 关键能力 | 支持**禁用 raw HTML**（块/行内分别可关），与本契约 §7 全量转义要求直接匹配 |
| 包体 | 单文件约数千行 C，vendor 后包体增量可忽略 |
| 维护状态 | 持续维护（0.5.3 为最新 release），多语言绑定活跃 |
| 锁定版本 | `md4c 0.5.3`；升级属协议化变更，需修订本契约并重跑 MD-002 golden |

`MDV-02` 开工前置条件：完成实际 vendor（固定 revision + hash 记录 + LICENSE 保留）并复核上表；任一项不符即触发自研退路。

## 13. 验收映射（MD-001..007）

| 用例 | 主要对应章节 |
|---|---|
| MD-001 入口与手势 | §3、§4 |
| MD-002 渲染 golden 与注入 | §6、§7 |
| MD-003 超大/编码边界 | §5 |
| MD-004 视图切换与实时预览 | §8 |
| MD-005 dirty 确认 | §8 |
| MD-006 保存原子性与冲突 | §9 |
| MD-007 Windows 实机 | 全文（平台门禁归 `MDV-07`） |
| MD-008 Mermaid 供应链与路由 | §2、§14、§16 |
| MD-009 Mermaid 图类型与安全 | §6、§7、§8、§14 |
| MD-010 Mermaid 性能与生命周期 | §8、§16 |
| MD-011 工具栏 glyph 与交互契约 | §8 |
| MD-012 编辑变换与缩进/表格对齐 | §6、§8 |
| MD-013 双平台快捷键、IME 与无障碍 | §8、§10 |

## 14. 图表渲染选型评审结论（2026-08-28，Mermaid Full）

**决定：撤销 2026-08-27 的 `@mermaid-js/tiny` 选型，采用官方完整版 `mermaid` 11.17.2（MIT）的离线 ESM 运行时闭包。若 `MDV-14` 的依赖闭包、许可证、CSP 或 CEF 兼容复核任一项不过，保持 Mermaid fence 为安全代码块，不以 tiny 版降级冒充完整支持。**

评审证据（2026-08-28，基于 npm registry 与 GitHub 上游实时数据）：

| 维度 | 结论 |
|---|---|
| 来源 | 上游 `github.com/mermaid-js/mermaid`，官方 npm `mermaid` 包；候选固定版本 `11.17.2`，实际 vendor 由 `MDV-14` 记录 tarball integrity、SHA-256、上游 tag/commit 与完整 import closure |
| 许可证 | MIT（包内 LICENSE 文件，MIT 全文确认）；允许 vendor 与再分发，无 copyleft 传染 |
| 能力理由 | 官方文档明确 tiny 不支持 Mindmap、Architecture、KaTeX 与 lazy loading；完整版覆盖本项目要求的 `flowchart`、`sequenceDiagram`、`mindmap`、`architecture-beta`、`classDiagram`、`stateDiagram-v2`、`erDiagram`，并按 diagram chunk 懒加载 |
| 包体策略 | 不把 npm 的源码、文档、测试和开发依赖全部塞进应用；vendor 经审计的**完整浏览器运行时闭包**（ESM 入口 + 所有可达 diagram/layout chunks + 必需 CSS/资源），由 manifest 锁 hash/MIME/大小/许可证。不得 tree-shake 删除图类型，也不得改用 tiny |
| 净化安全 | Mermaid 固定 `startOnLoad: false`、`securityLevel: "strict"`；仍将返回 SVG 视为不可信并经过 Browser-owned SVG policy gate，不能只依赖上游内部净化 |
| 跨平台 | 纯 JS/ESM 在 CEF Renderer 内执行；Windows/macOS 共用同一 vendored 资产与共享 runtime，平台层只负责资源打包/读取。HarmonyOS 接线后置且不得用 CEF 证据宣称完成 |
| 运行方式 | `/app.js` 先扫描标准 Mermaid block；仅命中时 `import()` 本地 ESM 入口，具体 diagram chunk 再由 Mermaid 从同 origin manifest 路由加载；无 CDN、无 npm runtime、无公网 fallback |

`MRT-01..04` 先冻结通用扩展节点、编译期 registry 与 loader/lifecycle；`MDV-14` 独立完成 Mermaid 供应链与运行时闭包冻结。`MDV-15..20` 只交付 Mermaid adapter、资源路由、渲染、安全、交互/懒加载、有界缓存与跨平台收口。任何一步失败时普通 Markdown 必须保持可用。

## 15. Markdown Runtime 扩展边界

- 保留现有 C++17 `md4c` 作为 Markdown parser 与普通安全 HTML 生成器，不引入第二套 `markdown-it`/`marked` parser，也不让 Rust Core 重写 Mermaid。
- 通用 Extension Framework 以 [Markdown Runtime v1 契约](markdown-runtime.md) 为事实源，由 `docs/plans/markdown-runtime-roadmap.md` 的 `MRT-01..04` 承接：`browser/shared-ui/markdown` 产出 `safe_html + extension_nodes[]` 的内部 fallback plan；规划中的 `browser/shared-ui/markdown-runtime` 拥有编译期 registry、manifest loader、预算/cache/generation 与错误隔离。节点只含闭合 `kind`、不透明 node ID、经过大小校验的最小源码与 source revision；Runtime 命中后才由 MDV assembly 生成唯一 inert placeholder，页面不按源码/offset 猜目标，也不接受扩展名、URL 或任意模块路径。
- `MDV-15..20` 只向闭合 registry 注册 `mermaid` adapter。Code Highlight 已由 MRT-06 以精确 fence matcher、固定同源资源路由、viewport lazy 和严格 `span`/`hljs-*` token policy 接入；未知/纯文本/失败 fence 保持普通代码块。KaTeX、ECharts、Graphviz 与 Presentation 分别由后续 MRT 原子任务评审后启用；PlantUML、Vega、TV/Cast 和 AI 编辑保持延后或 gap analysis。任何能力都不能以动态模块名、文档 manifest 或配置字符串绕过 registry。
- Mermaid 只属于用户打开的本地 MDV 页面，不进入 `crayon-page-data`、CNT 的确定性页面 Markdown、CAAP/tool registry 或 Agent 文件能力。

## 16. 资源、性能与生命周期门禁

- 构建期生成 manifest，Release 扫描逐文件核对路径、hash、MIME、总字节、许可证与 import closure；运行时路由只接受 manifest 中的精确相对路径，拒绝 query/fragment、`..`、编码分隔符、未知扩展和目录枚举。
- Mermaid 初始化 promise 每个文档页至多一个；block 数、单 block DSL 字节、并发渲染、内存 cache 与错误文本均有命名上限。默认值由 `MDV-19` benchmark 冻结，不在业务代码散落 magic value。
- cache 仅限会话内存，key 至少包含 `source hash + theme + mermaid version + runtime policy version`；文档关闭、导航、Profile 销毁、无痕窗口关闭或内存压力时清除，不新增磁盘缓存或最近文档痕迹。
- 无 Mermaid 文档的启动/首屏不得读取 Mermaid 资产；多图文档使用 viewport lazy render 与有界并发。性能验收必须记录首个普通 Markdown paint、首次 Mermaid import、首图完成、全部可见图完成、CPU/RSS/UI delay 与资源字节。
- 文档切换、编辑、主题切换和退出均推进 generation；上游 `render()` 不可取消时只允许结果失效丢弃，不能让旧 SVG 覆盖新文档，也不能在退出后回调已销毁页面。
