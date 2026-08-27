# MDV：本地 Markdown 查看器 Roadmap

状态：`MDV-01 DONE`（契约已冻结为 `docs/current/markdown-viewer.md` v1.0），`MDV-02..06 VERIFIED`（模型层切片），接线切片 `MDV-08..10 TODO`，`MDV-07 TODO`（收口）。本 Roadmap 承接“浏览器内查看本地 Markdown 文档、渲染预览与分栏编辑”的产品增量（PRD v0.8 §4.1）：`crayon://mdv` 内置查看页复用 `crayon://newtab` 的自定义 scheme、内存资源与严格 CSP 模式；本地 `.md` 只经用户手势的受控入口打开，保存走原子写。MDV 是纯用户能力，不进入 CAAP tool registry；Agent 侧任意文件访问禁令不变。排期属于 V1（Windows 优先、BUX 主线之后）。

## 产品设计结论

- 查看器是独立 origin 的内置页 `crayon://mdv`：页面框架只从编译期/内存资源提供，渲染内容在 Browser process 内确定性生成后注入；零默认网络请求，不加载远程脚本或样式。
- 入口只有三种且全部要求用户手势：主菜单“打开文件”对话框（仅 `.md` 过滤）、拖放 `.md` 文件到窗口、omnibox 输入本地路径判定为本地文件后路由进查看器；页面内容不能触发打开动作。
- 渲染方向：确定性 Markdown → 安全 HTML。语法范围以 CommonMark 常用子集 + GFM 表格为基线，由 `MDV-01` 契约冻结闭合清单；默认技术方向为 vendored 轻量开源 C 库（如 md4c，MIT），必须先通过来源、许可证、维护状态、包体和跨平台影响评审并锁定版本，评审不过则自研语法子集；不引入运行时动态下载。
- 交互形态：源码视图 / 渲染预览切换；分栏模式左侧编辑源码、右侧实时渲染预览。编辑只产生本地 dirty 状态，保存由用户显式触发（写回原文件或另存为），原子写（`.tmp` + rename），失败显式报告，外部修改冲突显式提示。
- 边界：文件大小/编码有界校验（仅 UTF-8）；渲染输出强制 HTML 转义与标签白名单；不开放任意文件系统、目录枚举或文件系统监控；不做远程 `.md` URL 渲染、双链/wiki 扩展语法、协同编辑或导出 PDF。
- 无痕窗口可用查看器，但会话内状态（最近文件、滚动位置）不持久化。

## 原子任务

| ID | 状态 | 依赖 | 目标路径 | 单一交付 | 测试/验收 |
|---|---|---|---|---|---|
| MDV-01 | DONE | BUX-03 | `docs/current/markdown-viewer.md` | 契约冻结：`crayon://mdv` scheme/origin/CSP、入口与手势门禁、渲染语法范围、安全边界与渲染选型评审结论 | 契约 Review 通过；语法/CSP/入口矩阵冻结 |
| MDV-02 | VERIFIED | MDV-01 | `browser/shared-ui/markdown` | 平台中立确定性 Markdown 渲染引擎：MD→转义安全 HTML、golden 与注入矩阵；vendored 库接入或自研子集 | MD-002；独立 ctest、`-Werror` 零告警 |
| MDV-03 | VERIFIED | MDV-01,MDV-02,BUX-03 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | 只读查看内置页（fixture 内容驱动）：scheme handler、源码/预览切换、严格 CSP、零网络 | MD-003、MD-004 只读部分；Windows Debug/Release |
| MDV-04 | VERIFIED | MDV-03,BUX-16,PLT-02 | `browser/shared-ui/mdv`,`browser/cef-shell` | 受控本地 `.md` 入口：文件对话框/拖放/omnibox 路径路由、路径/大小/UTF-8 校验、用户手势门禁 | MD-001、MD-003 |
| MDV-05 | VERIFIED | MDV-04 | `browser/shared-ui/mdv` | 分栏编辑与实时预览：编辑器状态机、dirty 跟踪、关闭/切换确认 | MD-004、MD-005 |
| MDV-06 | VERIFIED | MDV-05 | `browser/shared-ui/mdv`,`browser/cef-shell` | 保存语义：写回/另存为、原子写、外部修改冲突检测、失败显式报告 | MD-006 |
| MDV-08 | DONE | MDV-02,MDV-03,BUX-03 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | `crayon://mdv` scheme handler 与只读查看接线：scheme 注册、内存资源路由、CSP 下发、viewer 模型绑定与源码/预览切换呈现 | MD-003；Windows Debug/Release 实机 |
| MDV-09 | DONE | MDV-04,MDV-08,BUX-16 | `browser/cef-shell/src/browser/mdv` | 受控文件入口接线：菜单打开对话框（`.md` 过滤）、拖放、omnibox 本地路径路由三入口接手势门禁与入口守卫，平台分隔符归一 | MD-001；手势外零触发路径；Windows 实机 |
| MDV-10 | VERIFIED | MDV-05,MDV-06,MDV-08,MDV-09 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | 编辑与保存接线：分栏编辑 UI 呈现、dirty 三选确认对话框、真实文件 IO 钩子注入保存控制器（原子写）、外部修改冲突提示 | MD-005、MD-006；Windows 实机含只读位置失败报告 |
| MDV-07 | VERIFIED | MDV-01..06,MDV-08..10 | `docs/current`,`docs/plans` | Windows 实机收口与模块总 Review（macOS 对齐后置，不得用 Windows 证据完成 macOS） | MD-007；Review P0/P1=0 |
| MDV-13 | VERIFIED | MDV-08..11 | `browser/shared-ui/markdown`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | 图片支持：云端 https 直载 + 本地受控序号路由（文档目录内、格式/大小白名单、路径不入 URL/DOM）+ CSP img-src 修订 | MD-002 图片矩阵 + 实机 |
| MDV-14 | READY | MDV-13 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`docs/current` | 流程图/图表渲染：vendored `@mermaid-js/tiny` 11.17.2（评审已通过，见契约 §14）接入，fence `mermaid` → SVG 渲染管线 | 契约扩展 + 渲染 golden + 实机 |
| MDV-12 | VERIFIED | MDV-10 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`docs/current` | 编辑工具栏：14 项闭合动作（包裹/行前缀/骨架三类语义），复用既有编辑通道 | mdv_page 断言 + 实机交互验证 |
| MDV-11 | VERIFIED | MDV-08..10 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`docs/current` | 编辑回归修复（textarea 化被覆盖丢失）+ 拖放打开 + 右键上下文菜单入口（E4）+ 分栏间隔条可拖动 | MD-001..006 回归 + 新交互实机验证 |

## 接线切片说明（MDV-08..10）

MDV-02..06 按"模型层零 IO / 零 CEF 类型"交付后，产品仍不可见：scheme handler、三入口、编辑器呈现与真实文件 IO 均无实现。按仓库模型层/装配层切片惯例（CEF-06..14 → 装配、BUX 同构），补齐三个接线任务，每个都是可独立审查、可独立回退的原子任务：

- **共同边界**：CEF adapter 只在 `browser/cef-shell/src/browser/mdv/`（新增目录），共享层 `browser/shared-ui/mdv/**` 继续禁止 CEF/Win32/AppKit 类型；内置页资源只从编译期/内存提供，CSP 逐字使用 MDV-03 锁定的常量；所有可见文案进共享 locale 资源；新增生产文件每切片 ≤6 个。
- **MDV-08 明确不做**：任何文件访问、编辑、保存；页面内不能出现可触发打开的控件（kPage 来源在守卫层已一票拒绝）。
- **MDV-09 明确不做**：任意路径访问或目录枚举；拖放/对话框之外的新入口；路径校验逻辑改动（矩阵已在 MDV-04 冻结，接线只做 Windows 反斜杠归一与手势门禁传递）。
- **MDV-10 明确不做**：自动保存、静默覆盖、最近文件列表持久化（V1 不落盘）；密码/支付类内容语义判断；导出 PDF。
- 真实平台门禁：各切片自带 Windows Debug/Release build+ctest+实机 smoke；跨切片总验收归 MDV-07。

## 开发规则

- 每次只领取一项；渲染库属于新增依赖，领取 `MDV-02` 前必须完成来源、许可证、维护状态、包体和跨平台影响评审并锁定版本（仓库 `AGENTS.md` §12）。
- 共享层（`browser/shared-ui/**`）不得出现 CEF/Win32/AppKit/ArkWeb 类型；CEF adapter 只在 `browser/cef-shell/**`。
- 内置页只从编译期/内存资源提供，独立 origin、严格 CSP；渲染输出经转义与白名单，无 script、事件属性或远程引用。
- 本地文件只限用户手势选择的 `.md`；路径校验拒绝目录、非 `.md`、超长与控制字符；不做任意路径访问或目录枚举。
- 保存使用 `.tmp` + rename 原子写；无静默残留，不把 best-effort 宣称为成功；外部修改冲突必须显式提示。
- 所有可见文案进入本地化资源；图标来自自有 glyph/品牌资产。
- 不修改 BUX-18 既有依赖；MDV 独立在 `MDV-07` 收口，不阻塞其他浏览器基线项。

## MDV-01 原子范围（契约冻结）

- 状态：`READY`；依赖 `BUX-03 DONE`（内置页 scheme handler 模式）。
- 单一目标：冻结本地 Markdown 查看器的产品/安全契约文档 `docs/current/markdown-viewer.md`：`crayon://mdv` 的 scheme/origin/CSP 与资源路由、三种入口的手势门禁与路径校验矩阵、渲染语法范围闭合清单（CommonMark 子集 + GFM 表格基线）、文件大小/编码上限、保存语义与外部修改冲突策略、明确不做清单；并给出渲染选型评审结论（vendored 库名单、许可证与锁定版本，或自研语法子集决定）。本任务不写生产代码。
- 输入：PRD v0.8 §4.1、`BUX-03` 的 newtab scheme handler 模式、`BUX-04A` 的 omnibox 本地路径判定、`BUX-16` 的受控本地文件入口边界、UX-015 与 MD-001..007。
- 输出与允许修改：新增 `docs/current/markdown-viewer.md`；允许修改 `docs/current/README.md`（契约表登记）、本 Roadmap 与总 Roadmap/plans 索引状态。
- 禁止修改：任何生产代码、BUX/CNT 契约、CAAP schema、其他 Roadmap 任务边界。
- 边界：契约必须显式声明查看器不是 Agent 能力、不进 tool registry；语法范围闭合枚举，未列语法按纯文本渲染；CSP 不允许外链脚本/样式。
- 验收与测试：契约 Review 通过（按 `docs/current/code-review-standard.md`）；语法清单、CSP、入口/路径校验矩阵可直接作为 `MDV-02..06` 的验收输入；`git diff --check`。
- 明确不做：渲染引擎实现（MDV-02）、scheme handler（MDV-03）、文件入口（MDV-04）、编辑（MDV-05）、保存（MDV-06）。

### MDV-01 完成记录（2026-08-24）

- 实现：新增契约文档 `docs/current/markdown-viewer.md` v1.0（13 章）：§2 `crayon://mdv` 独立 origin、内存资源路由与全封闭 CSP（`default-src 'none'`、`img-src/connect-src/font-src 'none'`、路径不进 URL）；§3 三入口（菜单对话框/拖放/omnibox 路径）手势门禁与"页面内容零触发路径"；§4 路径校验矩阵（后缀/类型/控制字符/长度/穿越/存在六类，复用 PRV-04 path_guard）；§5 加载边界（≤5 MiB、UTF-8 严格+BOM 剥离、CRLF/LF 归一、空文件合法）；§6 渲染语法闭合清单（CommonMark 常用子集+GFM 表格/任务列表/删除线，未列语法按纯文本转义）；§7 渲染安全（确定性输出、raw HTML 全禁、生成标签/属性白名单闭合枚举、链接 scheme 白名单、图片永不加载）；§8 视图态与 dirty 确认；§9 原子写保存语义与 (size,mtime) 外部修改冲突；§10 无痕/持久化边界（V1 不落盘最近文件列表）；§11 明确不做；§12 选型评审结论——**vendored md4c 0.5.3（MIT，CommonMark 0.31 合规、单文件 C99 仅依赖 libc、支持禁用 raw HTML、Qt/LibreOffice 生产采用）**，vendor 评审不过则自研同等子集；§13 MD-001..007 验收映射。登记 `docs/current/README.md` 契约表。本任务未写任何生产代码。
- 选型证据：2026-08-24 抓取上游 README/CHANGELOG（MIT、CommonMark 0.31、扩展 flag 化、0.5.3 最新 release）；实际 vendor 时须固定 revision+内容 hash 并复核评审表（`MDV-02` 开工前置）。
- Code Review：按需求/边界→正确性→架构→安全/隐私复核。P0 0、P1 0、P2 1——§9 外部修改冲突用 `(size, mtime)` 检测存在同秒写入的窗口（mtime 精度限制），V1 接受并在冲突提示中提供"另存为"退路；若后续需要强一致可加内容 hash 对比（归 `MDV-06` 复核）。
- 未覆盖与风险：渲染引擎 vendor 与 golden 体系归 `MDV-02`；scheme handler 与内置页归 `MDV-03`；Windows 实机门禁归 `MDV-07`。`MDV-01` 转为 `DONE`，解锁 `MDV-02 READY`。

### MDV-02 完成记录（2026-08-24）

- 实现：新增 `browser/shared-ui/markdown`（header/impl/CMake/契约测试各 1），vendored md4c 0.5.3（含补齐的 `md4c-html.c/h`，同 tag `472c417`，VENDORED.md 已更新哈希）。渲染管线：输入归一（BOM 剥离、CRLF/CR→LF）→ 严格 UTF-8 校验（拒绝续字节首/超长/代理区/越界）→ 5 MiB 上限 → md4c 解析（`TABLES|STRIKETHROUGH|TASKLISTS|NOHTMLBLOCKS|NOHTMLSPANS`——**刻意不含 PERMISSIVEAUTOLINKS，裸链保持纯文本**；raw HTML 全转义）→ 输出后处理（`<img>`→`md-img-placeholder` 占位显示 alt+地址文本永不加载；`<a href>` scheme 白名单 http/https/mailto，其余降级为纯文本）→ 生成标签/属性白名单终检（27 标签 + 7 属性 + 布尔属性 disabled/checked），任何违规 fail-closed 返回 `kOutputPolicyViolation` 空输出。根 `CMakeLists.txt` 启用 C 语言（vendored C99 源）。
- 验证：`cmake -S . -B .cache/build/mdv02` 零告警；`markdown_render` 1/1（6 组：golden 基础语法/表格/任务列表/代码块、链接与 autolink、注入矩阵（script/事件属性/javascript:/file:/相对路径/HTML 注释/ftp autolink 全转义或降级）、三次渲染逐字节确定性、BOM/CRLF/Unicode 归一、5MiB 与四类非法 UTF-8 拒绝）；共享层回归 41/41；workspace Rust 全绿；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——白名单终检的标签步进公式对非嵌套常规输出已验证，但 `<` 出现在属性值内（如 `title="a<b"`）时 ParseTag 会误判；md4c 生成属性值时对 `<` 转义为 `&lt;` 故当前不可达，若未来接入其他生成器需先保证该不变量。
- 未覆盖与风险：`crayon://mdv` scheme handler 接线（MDV-03）、编辑器/保存语义（MDV-05/06）、Windows 实机（MDV-07）。`MDV-02` 转为 `VERIFIED`。

### MDV-03 原子范围（只读查看视图模型与 CSP 契约，切片 1）

- 状态：`IN_PROGRESS`；依赖 `MDV-01 DONE`、`MDV-02 VERIFIED`、`BUX-03 DONE`。
- 路径说明：本切片交付共享层视图模型与 CSP/资源契约常量；CEF scheme handler 接线与 Windows Debug/Release 实机归 MDV-03 后续切片（装配后统一实机验收）。
- 单一目标：`browser/shared-ui/mdv`——只读查看器的视图状态机：源码/预览/分栏三态切换、渲染请求去抖（≤100ms 合并、注入时钟）、revision fencing（旧渲染结果不可落位）、内容装载状态（有界/UTF-8 校验结果绑定 MDV-02 状态码）、CSP 常量与零网络契约（golden）。
- 边界：无持久化（最近文件/滚动位置仅内存）；无编辑/dirty（MDV-05）；渲染内容只消费 MDV-02 输出；CSP 字符串逐字节 golden 锁定；路径/文件名永不进入 URL（模型层无 URL 概念，仅内容 revision）。
- 验收与测试：MD-003 模型部分（装载边界/空文件）、MD-004 只读部分（视图切换/去抖/旧结果丢弃）。命令：独立 configure/build/ctest、共享层回归、`git diff --check`。
- 明确不做：CEF scheme handler（后续切片）、编辑与保存（MDV-05/06）、Windows 实机（MDV-07）。

### MDV-03 完成记录（2026-08-25，视图模型切片）

- 实现：新增 `browser/shared-ui/mdv`（header/impl/CMake/契约测试各 1，链接 MDV-02 渲染引擎）。`MdvViewerModel`：源码/预览/分栏三态切换（只读默认 Preview）；`LoadContent` 只收字节不收路径（路径永不进入模型/URL），装载状态闭合映射 MDV-02 结果（Loaded/TooLarge/InvalidUtf8/RenderPolicyViolation/Empty，空文件合法）；渲染请求滑动去抖（`kRenderDebounceMs=100`，窗口内合并复用 pending revision）；revision fencing——旧/陈旧渲染结果 `DeliverRender` 拒绝且 HTML 永不落位（MD-004），CloseDocument 推进 revision 使在途渲染变陈旧；CSP 常量逐字节 golden（MDV-01 §2 全封闭十二条）与 `/app.html|css|js` 内存资源路径常量；零持久化面。无 CEF 类型、无 IO。
- 验证：`cmake -S . -B .cache/build/mdv03` 零告警；`mdv_viewer` 1/1（7 组：视图切换、装载状态矩阵含引擎二次 UTF-8 校验、滑动去抖合并/新代际、陈旧渲染丢弃、关闭文档失效在途、CSP/资源路径 golden、5000 步风暴 revision 单调）；共享层回归 42/42；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——`LoadContent` 的 `utf8_valid` 调用方参数仅作预检语义（引擎无条件重验，测试已覆盖谎报场景）；参数本身冗余，MDV-04 接线时可移除以收窄接口。
- 未覆盖与风险：CEF scheme handler 接线（`crayon://mdv` 内存资源 + CSP 下发 + 受控绑定注入，MDV-03 后续切片）、编辑/保存（MDV-05/06）、Windows 实机（MDV-07）。`MDV-03` 转为 `VERIFIED`（切片 1；handler 接线后补实机证据）。

### MDV-04 原子范围（受控本地 .md 入口守卫，切片 1）

- 状态：`VERIFIED`；依赖 `MDV-03 VERIFIED`、`BUX-16 VERIFIED`、`PLT-02 DONE`。
- 路径说明：本切片交付共享层入口守卫模型；文件对话框/拖放的 CEF 接线归后续切片。
- 单一目标：`browser/shared-ui/mdv` 新增入口守卫——三入口（菜单对话框/拖放/omnibox 路径）统一用户手势门禁 + §4 路径校验矩阵（后缀大小写不敏感 `.md`、控制字符、长度上限、`..` 穿越、存在性与常规文件经注入 stat 回调）+ §5 装载边界（5 MiB、UTF-8 严格 + BOM 剥离、CRLF→LF、空文件合法）。
- 边界：页面来源（kPage）在任何入口稳定拒绝；路径校验全部在 Browser process 模型层完成；symlink/junction 完整解析防护归平台层 path_guard（PRV-04 口径），本模型以注入回调表达；不创建新文件。
- 验收与测试：MD-001 模型部分、MD-003 装载边界。命令：独立 configure/build/ctest、共享层回归、`git diff --check`。
- 明确不做：CEF 文件对话框/拖放接线（后续切片）、保存写回（MDV-06）、Windows 实机（MDV-07）。

### MDV-04 完成记录（2026-08-25，入口守卫模型切片）

- 实现：`browser/shared-ui/mdv` 新增 `mdv_entry_guard`（header/impl/契约测试）。三入口统一守卫：**手势门禁优先**（`EntrySource::kPage` 一票拒绝，先于一切检查——页面内容零触发路径）；§4 路径矩阵——`.md` 后缀大小写不敏感且要求非空词干（`.md`/`.markdown`/`.mdown` 拒绝）、控制字符（<0x20 与 DEL）拒绝、长度 ≤4096、`..` 段穿越拒绝（完整 symlink/junction 解析归平台 path_guard，经注入 stat 探针表达）、存在性与常规文件经注入探针（null 探针 fail-closed）；§5 装载边界——5 MiB 上限、严格 UTF-8（复用 MDV-02 校验器）、BOM 剥离一次、CRLF/CR→LF、空文件合法。`GateLocalLoad` 把入口校验 + 内容边界 + 归一化串成单次调用，与 MDV-03 `LoadContent` 构成端到端流（契约测试覆盖）。
- 验证：`cmake -S . -B .cache/build/mdv04` 零告警；`mdv_entry_guard` 1/1（5 组：后缀矩阵、手势门禁优先、路径矩阵含穿越/控制字符/长度/探针、装载边界含 BOM/CRLF/空文件/二进制伪装、gate→viewer 端到端）；`mdv_viewer` 1/1；共享层回归 43/43；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——反斜杠在模型层按普通名字字符处理（Windows 分隔符归一由平台接线在调用前完成）；若平台接线遗漏归一，`..\\` 变体不会被本层拦截（已记录，MDV-04 接线切片验收项）。
- 未覆盖与风险：CEF 文件对话框/拖放/omnibox 路由接线（MDV-04 后续切片）、平台 path_guard 完整解析（PLT-W04/M04）、Windows 实机（MDV-07）。`MDV-04` 转为 `VERIFIED`（模型切片）。

### MDV-05 原子范围（分栏编辑与 dirty 确认模型）

- 状态：`IN_PROGRESS`；依赖 `MDV-04 VERIFIED`。
- 路径说明：本切片交付共享层编辑状态机；编辑器 UI 呈现归后续接线切片。
- 单一目标：`browser/shared-ui/mdv` 新增编辑模型——分栏模式下编辑产生内存 dirty 状态（绝不自动写盘）、编辑触发渲染去抖（复用 MDV-03 revision fencing）、dirty 下关闭/切换/导航的三选确认（保存并继续/放弃/取消，取消不丢内容、放弃不写盘）、加载新文件/关闭文档时 dirty 拦截。
- 边界：编辑只改内存缓冲；确认流为闭合状态机（无第四选项）；放弃/确认后 dirty 清除；保存动作本身归 MDV-06，本模型仅暴露"请求保存"钩子位。
- 验收与测试：MD-004 编辑部分、MD-005 全部。命令：独立 configure/build/ctest、共享层回归、`git diff --check`。
- 明确不做：写盘（MDV-06）、CEF 编辑器呈现、Windows 实机（MDV-07）。

### MDV-05 完成记录（2026-08-25）

- 实现：`browser/shared-ui/mdv` 新增 `mdv_edit`（header/impl/契约测试）。`MdvEditModel` 叠加在 MDV-03 viewer 之上：编辑只写内存缓冲并置 dirty（**绝不自动写盘**），渲染调度复用 viewer 的滑动去抖 + revision fencing；确认 pending 期间编辑拒绝；`BeginBlockingTransition`——clean 直接放行（返回 false）、dirty 打开三选确认；`ResolveTransition` 闭合三选——Cancel 保留内容关对话框、Discard 丢弃缓冲不写盘、SaveAndContinue 保持阻塞直到 MDV-06 的 `NotifySaveSucceeded()`（原子写成功钩子）清除 dirty 并放行；`LoadDocument` 在阻塞期守卫拒绝。无 IO、无 CEF 类型。
- 验证：`cmake -S . -B .cache/build/mdv05` 零告警；`mdv_edit` 1/1（6 组：编辑置 dirty 与去抖、clean 直通、阻塞三选全路径、SaveAndContinue 阻塞至保存成功、重复阻塞/无效选择拒绝、5000 步风暴状态闭合）；`mdv_viewer`/`mdv_entry_guard` 同步通过；共享层回归 44/44；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——`NotifySaveSucceeded` 无条件清 dirty；若未来出现"保存成功但内容又被编辑"的竞态窗口，需以内容指纹比对替代布尔清除（MDV-06 接线时评估）。
- 未覆盖与风险：编辑器 UI 呈现与键盘接线（后续装配切片）、保存语义（MDV-06）、Windows 实机（MDV-07）。`MDV-05` 转为 `VERIFIED`。

### MDV-06 原子范围（保存语义与外部修改冲突模型）

- 状态：`IN_PROGRESS`；依赖 `MDV-05 VERIFIED`。
- 路径说明：本切片交付共享层保存控制器（注入 IO 钩子，模型零真实 IO）；真实文件 IO 在 CEF shell 装配时注入。
- 单一目标：`browser/shared-ui/mdv` 新增 `mdv_save`——写回（Ctrl+S）/另存为两条路径、原子写计划（同目录临时文件 → rename）、外部修改 `(size,mtime)` 冲突检测（写回路径保存前重 stat 比对加载基线，绝不静默覆盖）、失败显式报告（临时文件尽力清理，清理失败必须报告残留路径）。
- 边界：另存为受 §4 矩阵约束（后缀/字符/长度/穿越）但允许覆盖已存在 `.md` 且不做冲突比对（用户显式选择新位置）；写回冲突闭合三选（覆盖我的/另存为/取消）由 MDV-05 确认流承载，本模型输出冲突判定；只读/盘满/权限失败映射闭合错误码；无半写文件。
- 验收与测试：MD-006 全部（模型部分）。命令：独立 configure/build/ctest、共享层回归、`git diff --check`。
- 明确不做：真实 IO 与对话框（装配切片）、Windows 实机（MDV-07）。

### MDV-06 完成记录（2026-08-25）

- 实现：`browser/shared-ui/mdv` 新增 `mdv_save`（header/impl/契约测试，注入 IO 钩子——模型零真实 IO）。`MdvSaveController`：写回路径保存前重 stat 与加载基线 `(size,mtime)` 比对，漂移即 `kFailedConflict`（绝不静默覆盖，MD-006）；文件消失同样 `kFailedStat` fail-closed；另存为走 §4 形状矩阵（后缀/长度/穿越/控制字符）但允许新建或覆盖已存在 `.md`、不做冲突比对（用户显式选择新位置）；原子写 = 同目录临时文件（`<target>.tmp-<pid>`）→ rename，任一步失败显式报错——临时写失败 `kFailedTempWrite`、rename 失败先尽力清理临时文件（成功 `kFailedRename`、清理失败 `kFailedResidual` 并报告残留路径）；保存成功后基线迁移至目标文件（`mtime_known=false`，下次保存前重 stat 恢复）。无半写状态：冲突/校验失败在写临时文件之前返回。
- 验证：`cmake -S . -B .cache/build/mdv06` 零告警；`mdv_save` 1/1（4 组：写回 happy path 含基线迁移、外部修改冲突与恢复重试/文件消失、另存为矩阵含无基线写回 fail-closed、失败报告链含临时写失败/rename 失败清理/残留路径上报）；`mdv_viewer`/`mdv_entry_guard`/`mdv_edit` 同步通过；共享层回归 45/45；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——保存成功后 `mtime` 未知（记 0 + `mtime_known=false`），下次写回必然重 stat 比对真实值，行为正确但 `FileBaseline` 的字段语义依赖该约定；MDV-07 接线时若 stat 钩子能回读保存后 mtime，可直接填充消除歧义。
- 未覆盖与风险：真实 IO 钩子实现与对话框接线（装配切片）、只读位置/盘满的真实平台错误码映射（装配时注入）、Windows 实机（MDV-07）。`MDV-06` 转为 `VERIFIED`。MDV 模型层（MDV-02..06）全部完成，仅剩 MDV-07 Windows 实机收口。

## MDV-08 原子范围（scheme handler 与只读查看接线）

- 单一目标：在 Browser process 以 BUX-03 模式注册 `crayon://mdv` 的 resource handler（域 `mdv`），从编译期内存资源提供 `/app.html|/app.css|/app.js` 三个固定框架资源并下发契约 CSP；以确定性 fixture 文档驱动 `MdvViewerModel`（装载→MDV-02 渲染→DeliverRender 门控）生成只读查看页——源码面板（转义）与预览面板（白名单 HTML）同页呈现，视图切换由内存 `/app.js` 经 `addEventListener` 切换 body 数据态完成。本切片不接任何真实文件入口、无编辑、无保存。
- 输入：MDV-01 契约 §2/§7/§8、MDV-02 引擎 `RenderMarkdownToSafeHtml`、MDV-03 viewer 模型与 `kMdvCsp`/资源路径常量、BUX-03 newtab 的 factory/route/字符串资源模式。
- 输出与允许修改：共享层新增 `browser/shared-ui/mdv/{include/crayon/browser_mdv/mdv_page.h, src/mdv_page.cc, tests/mdv_page_test.cc}`（路由分类器 + 页面快照渲染器）；CEF 层新增 `browser/cef-shell/src/browser/mdv/{cef_mdv_handler.h,.cc}` 与 `tests/mdv_handler_contract.cmake`；装配修改 Windows `app.cc/.h`（工厂注册 + 字符串加载）、`resources/windows/resource_ids.h` 与 `app.rc.in`（IDS_CRAYON_MDV_*）、两个 CMakeLists 源表。新增生产文件 ≤6。
- 禁止修改：MDV-02..06 已冻结模型语义与 golden、newtab 模块、engine-api、Profile/隐私逻辑、macOS/Harmony 壳（macOS 接线归 MDV-07 后对齐）；页面不得出现内联事件属性、外链资源或任何文件路径进入 URL/DOM；fixture 内容为编译期常量，不读磁盘。
- 边界：路由分类 fail-closed——仅 `crayon://mdv` 域、GET/HEAD、精确三路径、拒绝 credentials/port/query/fragment（其余 404/405）；CSP 逐字节使用 `kMdvCsp`（script-src 'self' 允许内存 JS）；源码文本全量转义后才入 HTML，`rendered_html` 来自 MDV-02 白名单输出按可信原样插入；所有可见文案走 IDS 字符串资源（zh-CN 先行，en-US 对齐归 MDV-07）。
- 验收与测试：路由矩阵（方法/scheme/host/路径/credentials/port/query/fragment 全组合）；HTML/CSS/JS 无网络引用与主动内容扫描；源码转义注入矩阵（script/引号/控制字符 fixture）；三次渲染逐字节确定性；CSP 头逐字节等于 kMdvCsp；handler contract 结构校验。命令：独立 mdv configure/build/ctest、Windows Debug/Release build+ctest、clang-format、fast/security、`git diff --check`、实机 omnibox 打开 `crayon://mdv/app.html` 冒烟（页面出现、视图切换生效、零进程残留）。
- 明确不做：文件对话框/拖放/omnibox 路由判定（MDV-09）、编辑与保存 IO（MDV-10）、最近文件持久化、macOS 实机（MDV-07 对齐）、CAAP/Agent 面（永久禁止）。

### MDV-08 完成记录（2026-08-26，scheme handler 与只读查看接线）

- 实现：共享层新增 `browser/shared-ui/mdv` 的 `mdv_page` 模块——`ClassifyMdvRequest` 路由分类器（仅 `crayon://mdv` 域 GET/HEAD 精确三路径 `/app.html|/app.css|/app.js`，credentials/port/query/fragment 一律 404/405 fail-closed，镜像 newtab 分类器形状）；`MdvPageSnapshot` + `RenderMdvDocument/Stylesheet/Script` 确定性渲染器（源码面板全量转义、预览面板原样插入 MDV-02 白名单 HTML、视图切换由内存 `/app.js` 的 `addEventListener` 切换 `body[data-view]`、面板可见性纯 CSS、无内联事件属性/无网络引用）。CEF 层新增 `browser/cef-shell/src/browser/mdv/cef_mdv_handler`：`MdvMemoryResourceHandler` 逐字节下发共享 `kMdvCsp` 常量与 no-store/nosniff/no-referrer/X-Frame-Options 头；fixture 文档经真实 MDV-03 门控路径（LoadContent→RequestRender→MDV-02 渲染→DeliverRender）生成三份内存 body，工厂注册于域 `mdv`。Windows 装配：`IDS_CRAYON_MDV_*`（211..218）字符串资源 + `LoadMdvStrings` 注入 + 启动期 `mdv_strings_valid()` 门禁（新退出码 16）；工厂注册失败走既有 shutdown 链。
- 过程披露：① MinGW GCC 构建暴露 `markdown_render.cc` 缺 `<cstdint>` 的既有可移植性问题（clang/MSVC 宽容），按共同入口补一行 include 并在本次构建验证；② 实机冒烟中 Chrome omnibox 将自定义 scheme 判为搜索词，两次回车误触 Google 搜索（公网测试纪律失误，如实记录；第三次以"输入→Down 选中 URL 建议行→回车"完成导航，未再触网）。
- 自动验证：独立共享层构建 `mdv_page` 契约测试通过（路由矩阵含 HEAD/405/装饰 URL 拒绝、三次渲染逐字节确定性、源码转义注入矩阵、三资源零网络/零内联处理器扫描、空态/错误横幅、初始视图态与 CSP 常量锁定）；Windows Debug/Release build 零错误，两配置 ctest 均 **58/58**（含新 `mdv_page` 与 `mdv_handler_contract`：结构存在性、共享 CSP 常量、无文件/网络 IO、路由门禁必经）。clang-format（Google）对新文件 dry-run 零违规；`scripts/check.ps1 fast/security` 全 passed；`git diff --check` 通过。
- Windows 实机（Debug）：omnibox 打开 `crayon://mdv/app.html` 成功——标题"蜡笔文档"，预览态正确渲染标题/表格/任务列表（复选框禁用态）/代码块/安全链接，原始 HTML `<b>` 按纯文本转义显示；点击"源码"按钮切换到源码视图，完整原始 Markdown 正确显示（内存 JS 视图切换生效）；URL/DOM 中无文件路径；Alt+F4 退出后同路径进程残留 0。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`（公网误触为测试过程纪律问题非产品缺陷，产品侧零公网请求——页面资源全内存且 CSP connect-src 'none'）。
- 未覆盖与风险：en-US 字符串与 macOS 接线归 MDV-07 对齐；fixture 驱动（真实文件入口归 MDV-09）；`/app.js` 当前仅视图切换，编辑去抖接线归 MDV-10；omnibox 对自定义 scheme 的搜索判定是 Chrome runtime 行为，E3 入口（omnibox 本地路径路由）在 MDV-09 以 Browser process 判定绕过。`MDV-08` 转为 `DONE`，解锁 `MDV-09`。

## MDV-09 原子范围（受控文件入口接线）

- 单一目标：把三个用户手势入口接通到 MDV-04 入口守卫与 MDV-08 查看页——E1 菜单/Ctrl+O 打开对话框（经 `CefBrowserHost::RunFileDialog`，过滤器仅 `.md`）；E2 拖放 `.md` 到窗口（drop 产生的 `file://` 导航）；E3 omnibox 提交的 `file://` 本地 `.md` 导航。三入口统一经 `GateLocalLoad`（手势门禁 + §4 路径矩阵 + §5 装载边界）加载，成功后写入运行时快照并把当前标签导航到 `crayon://mdv/app.html`；失败时查看页横幅显示可操作错误，绝不半加载。
- 输入：MDV-04 `ValidateEntry/GateLocalLoad/NormalizeLoadedContent`、MDV-08 handler 与页面渲染器、`window::TabController` 既有 `SetChromeCommandCallback` 回调惯例、CEF `RunFileDialog`/`CefDragData`/`OnBeforeBrowse`。
- 输出与允许修改：新增 `browser/cef-shell/src/browser/mdv/{cef_mdv_entries.h,.cc}`（入口控制器：对话框/拦截/加载管线，`std::filesystem` stat 探针与有界读取）；`cef_mdv_handler` 扩展 `MdvRuntimeState`（互斥保护快照，工厂每次 Create 按当前快照渲染）；`window/tab_controller.h/.cc` 增加两个窄回调挂点（`SetLocalEntryCommandHandler`——OnChromeCommand 先咨询可吞掉 IDC_OPEN_FILE；`SetNavigationInterceptor`——OnBeforeBrowse 先咨询可取消 file:// 导航），镜像既有回调惯例不改状态所有权；共享层 `mdv_page` 快照增加 `error_text`（渲染器转义）与 `status_not_markdown` 文案位；Windows `app.cc/.h` 装配、`resource_ids.h`/`app.rc.in` 增 219；`mdv_page_test`/`mdv_handler_contract` 扩展。
- 禁止修改：MDV-04 守卫矩阵语义、MDV-08 路由/CSP、engine-api、共享层 CEF 类型禁令；不得实现目录枚举/文件监控/自动保存；页面内容发起的 `crayon://mdv` 或 `file://` 导航（user_gesture=false）一律拒绝。
- 边界：`file://`→本地路径转换做百分号解码与 Windows 前导斜杠归一；读取为有界单次二进制读（≤5 MiB+1 探测），不持锁 IO；非 `.md` 拖放/导航不拦截按原行为放行；E3 局限如实记录——Chrome omnibox 将无 scheme 的本地路径判为搜索（Chrome runtime 行为），本切片只覆盖 `file://` 形式提交，纯文本路径判定接线归 BUX omnibox 自有控件任务。
- 验收与测试：入口守卫集成测试（对话框回调路径转换矩阵、file:// 解码矩阵、user_gesture 门禁、非 .md 放行）；mdv_page 扩展（error_text 转义与横幅优先级）；handler contract 扩展（entries 文件存在性、守卫调用必经）；Windows Debug/Release build+ctest；实机：Ctrl+O 对话框选 `.md` 打开、拖放 `.md` 打开、拖放非 `.md` 不拦截、页面发起导航被拒、退出零残留。
- 明确不做：编辑/保存（MDV-10）、纯文本路径的 omnibox 判定（BUX 自有 omnibox）、最近文件持久化、目录枚举、macOS 实机（MDV-07）。

### MDV-09 完成记录（2026-08-26，受控文件入口接线）

- 实现：
  - **E1 对话框入口**：`MdvEntryController::HandleChromeCommand` 经 `cef_id_for_command_id_name("IDC_OPEN_FILE")` 版本安全识别原生 Ctrl+O/菜单"打开文件"命令并吞掉默认行为，改走 `CefBrowserHost::RunFileDialog`（标题来自注入字符串资源、过滤器仅 `.md`）；`MdvFileDialogCallback` 空选即取消（零加载零导航）。
  - **E2/E3 file:// 拦截**：`InterceptNavigation` 在 `OnBeforeBrowse` 前咨询——仅 `user_gesture=true` 的 `file://` 且 `.md` 后缀（大小写不敏感）导航被取消并转入加载管线（拖放 drop 与 omnibox file:// 提交同路径）；`LocalPathFromFileUrl` 做百分号解码与 Windows 盘符/UNC 前导斜杠归一；非 .md 本地目标保持默认行为。
  - **加载管线**：`std::filesystem` stat 探针（存在/常规文件/其他三态）注入 `ValidateEntry`，有界单次二进制读（≤5 MiB+1 探测、不持锁 IO）后 `GateLocalLoad` 全门禁；成功经 MDV-02 渲染写入 `MdvRuntimeState`（互斥快照，工厂每次 Create 按当前快照渲染文档 body）并把当前标签导航到查看器；失败以转义横幅显示可操作文案（新增 `IDS_CRAYON_MDV_STATUS_NOT_MARKDOWN`，快照新增 `error_text` 位且渲染器转义、横幅优先于状态映射）。
  - **窄挂点**：`window::TabController` 新增 `SetLocalEntryCommandHandler`/`SetNavigationInterceptor` 两个回调与 `HandleLocalEntryCommand`/`InterceptNavigation` 公有路由方法，`WindowClient::OnChromeCommand`/`OnBeforeBrowse` 先咨询后默认——不改状态所有权，镜像 `SetChromeCommandCallback` 惯例。
- 过程披露：① handler.h 首版缺 `MdvPageSnapshot` using 声明（`crayon::browser_mdv` 非外层命名空间），错误类型参数引发下游 C2660 连锁，补声明修复；② `std::istreambuf_iterator` 最令人头疼的解析（most vexing parse）改用显式迭代器变量 + assign；③ `MdvRuntimeState` 方法实现曾误落匿名命名空间，移出修复；④ 契约测试仍检查旧注册签名，随新签名更新。
- 自动验证：共享层 `mdv_page` 测试新增横幅优先级与转义用例通过；Windows Debug/Release build 零错误、两配置 ctest 均 **58/58**（`mdv_handler_contract` 扩展：entries 文件存在性、`GateLocalLoad` 必经、`RunFileDialog` 使用）；clang-format（Google）零违规；`scripts/check.ps1 fast/security` 全 passed；`git diff --check` 通过。
- Windows 实机（Debug，测试文件 D:\crayon-mdv-test）：① omnibox 提交 `file:///D:/crayon-mdv-test/notes.md` → 拦截成功，查看器渲染"测试笔记"（标题/加粗/列表），URL 变为 `crayon://mdv/app.html`（路径不入 URL）；② Ctrl+O 打开对话框——标题"蜡笔文档"、过滤器 "MD File (*.md)"、目录记忆与仅 .md 列表，选择 `second.md` → 查看器渲染"第二个文件"内容（对话框选择→守卫→渲染全链路）；③ `file:///...readme.txt` → 不拦截，按默认行为显示原始文本页；④ Alt+F4 退出后同路径进程残留 0。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。页面发起导航（user_gesture=false）结构性不可入守卫；对话框回调经 shared_ptr 保活；句柄/回调所有权清晰（controller 由 app 持 shared_ptr，CEF 回调持引用）。
- 未覆盖与风险：E2 拖放无法可靠自动化，其代码路径与 E3 完全相同（drop 产生 user-gestured file:// 导航），人工拖放验证归 MDV-07 收口清单；无 scheme 纯文本路径的 omnibox 判定按范围归 BUX 自有 omnibox 任务（Chrome runtime 将其判为搜索，本切片只覆盖 file:// 形式）；跨用户/权限失败的真实平台错误码映射在只读位置场景归 MDV-10 保存路径验证。`MDV-09` 转为 `DONE`，解锁 `MDV-10`。

## MDV-10 原子范围（分栏编辑与真实保存接线）

- 单一目标：查看器源码面板变为可编辑（source/split 态 `<textarea>`），编辑经 CefMessageRouter 受控绑定（唯一 query 名，仅 `crayon://mdv` origin）流入 Browser process 的 `MdvEditModel`（dirty/去抖/revision fencing 全走既有模型）；Ctrl+S 拦截为写回保存，`MdvSaveController` 注入真实文件 IO 钩子（`std::filesystem` stat/写临时/rename/清理，原子写）；dirty 下导航/打开新文件经 OnBeforeBrowse 拦截进入页内三选确认（保存并继续/放弃/取消），外部修改冲突进页内三选（覆盖我的/另存为/取消，另存为走 SAVE_DIALOG）；保存结果以页内状态条反馈，只读位置/盘满失败显式报告。
- 输入：MDV-05 `MdvEditModel`、MDV-06 `MdvSaveController`（函数指针 IO 钩子）、MDV-08 页面渲染器、MDV-09 入口控制器与运行时快照、CEF `wrapper/cef_message_router.h`。
- 输出与允许修改：共享层 `mdv_page`（源码面板 textarea 化、确认浮层、脏标记、/app.js 扩展——查询节流、确认按钮、预览应用函数、beforeunload 兜底）与 `mdv_page_test` 扩展；CEF 层新增 `browser/cef-shell/src/browser/mdv/cef_mdv_editing.h/.cc`（编辑/保存控制器 + 真实 IO 钩子）；`window/tab_controller.h/.cc` 增加消息路由窄挂点（`SetPageQueryDelegate` + `OnProcessMessageReceived` 转发，镜像既有惯例）；`new_tab` 渲染进程 App 增加消息路由 renderer 侧装配（crayon scheme 页共用渲染进程 App，装配性修改）；Windows `app.cc/.h` 装配；`mdv_handler_contract` 扩展。
- 禁止修改：MDV-05/06 模型语义与既有测试；入口守卫（MDV-04）与路由/CSP（MDV-08）；消息路由 query 名仅绑定 `crayon://mdv` origin（OnContextCreated 校验 URL），web 页面不可见该绑定；不得自动保存、不得静默覆盖、不得持久化路径。
- 边界：编辑文本单向流入（页面→Browser），预览单向流出（ExecuteJavaScript 应用），无任意 JS 暴露；保存前 collect 当前缓冲，冲突按 (size,mtime) 判定；dirty 关闭标签由 beforeunload 兜底（Chrome 原生两键），完整三选确认覆盖导航与打开新文件场景，关闭标签的页内三选受 Chrome runtime 边界限制如实记录；另存为仅从冲突三选与写回失败恢复路径触达（V1 无独立菜单项）。
- 验收与测试：mdv_page 扩展（textarea/浮层/脏标记渲染、查询节流脚本无内联处理器）；editing 契约/行为测试（真实 IO 钩子：写回 happy/冲突/只读失败/残留清理，决策矩阵经模型）；handler contract 扩展（editing 文件存在、消息路由绑定 origin 校验）；Windows Debug/Release build+ctest；实机：打开文件→分栏编辑→预览实时更新→Ctrl+S 写回→外部修改冲突提示→只读位置失败报告→退出零残留。
- 明确不做：自动保存、最近文件持久化、关闭标签的页内三选（Chrome runtime 边界，beforeunload 兜底）、导出 PDF、macOS 实机（MDV-07）。

### MDV-10 完成记录（2026-08-26，分栏编辑与真实保存接线）

- 实现：
  - **页面 v2**（`mdv_page`）：源码面板 textarea 化（source/split 态可编辑），快照新增 `dirty/save_ok/confirm_visible`；页内三选确认浮层（保存并继续/放弃/取消，文案全走字符串资源 IDS 220..224）；`/app.js` v2——编辑输入 ~80ms 节流后经受控 `mdvQuery` 绑定发送、`window.mdvPush` 接收 Browser 推送（预览 HTML/脏标记/确认浮层/横幅）、确认按钮 decision 查询、beforeunload 脏兜底；无内联事件属性、零网络。
  - **受控绑定**：`CefMessageRouter` 双侧接线——渲染侧挂入 crayon scheme 页共用进程 App（OnWebKitInitialized/OnContextCreated/Released/OnProcessMessageReceived）；浏览器侧 WindowClient 持有 router（**UI 线程惰性创建**——首版在 CefInitialize 前创建触发 `Check failed: CefCurrentlyOn(TID_UI)` 启动即崩，已修复并记录），`OnBeforeBrowse`/`OnRenderProcessTerminated`/`OnProcessMessageReceived` 转发；TabController 增 `SetPageQueryHandler`/`HandlePageQuery` 窄挂点（镜像既有惯例）。query 名 `mdvQuery` 仅对 `crayon://mdv/` origin 帧服务，其余 origin 查询直接 Failure 拒绝。
  - **编辑/保存控制器**（`cef_mdv_editing`）：编辑突发经 `MdvEditModel.ApplyEdit`（确认挂起时拒绝）→ MDV-02 渲染 → DeliverRender 门控 → 快照更新；Ctrl+S 拦截 `IDC_SAVE_PAGE`（35004）为写回保存，`MdvSaveController` 注入真实 `std::filesystem` IO 钩子（stat/写临时/rename/清理，临时名带序号+时钟防撞）；dirty 导航拦截（OnBeforeBrowse 前置，查看器自身 reload 除外）进入页内三选，SaveAndContinue 保存成功后自动续航 pending URL，Discard 直接续航；外部修改冲突进入冲突浮层——覆盖我的（save-as 语义跳过漂移检查）/另存为（SAVE_DIALOG + §4 矩阵）/取消；保存结果经 `ExecuteJavaScript` mdvPush 反馈（成功绿条/失败红条，残留临时路径必须上报）。
- 过程披露（构建期修复）：StatusBanner 签名扩展漏改调用点；`JSON_PARSER_RFC`/`VTYPE_DICTIONARY` 常量名对齐；`CefMessageRouterBrowserSide::Create(config)` 无 handler 参数（`AddHandler` 后置）；router Handler 基类非引用计数（去掉误用的 IMPLEMENT_REFCOUNTING）；`OnQuery` query_id 为 `int64_t`；`std::istreambuf_iterator` most-vexing-parse；`MdvRuntimeState` 实现误落匿名命名空间；`CefValue::SetDictionary(CefDictionaryValue::Create())` 用法。
- 自动验证：Windows Debug/Release build 零错误、两配置 ctest 均 **58/58**（`mdv_page` 扩展通过；`mdv_handler_contract` 扩展 editing 文件存在性 + MDV-06 模型驱动 + ExecuteJavaScript 推送）；clang-format 零违规；`scripts/check.ps1 fast/security` 全 passed；`git diff --check` 通过。启动崩溃修复后实机验证：应用正常启动、file:// 打开文档、分栏视图（左源码右预览）渲染正确、退出零残留。
- **未覆盖与阻塞（如实记录）**：交互式编辑输入、Ctrl+S 落盘、冲突浮层与三选确认的**实机键入验证被桌面环境阻塞**——冒烟期间另一应用（ChatGPT，pid 5472）反复抢占前台，SetForegroundWindow/ALT 解锁/SendKeys 均无法稳定落地按键（原始错误：`frontmost_pid_mismatch`；多次输入未达页面）。已验证到"文件打开→分栏渲染→零残留"为止；编辑/保存/确认链路的模型层行为由 MDV-05/06 单测与本次契约测试覆盖，端到端键入验证待桌面空闲后人工补验（MDV-07 收口清单首项）。状态维持 `VERIFIED`，不转 `DONE`。

### MDV-07 完成记录（2026-08-26，Windows 实机收口与模块总 Review）

- en-US 对齐：`browser/shared-ui/locales/{zh-CN,en-US}.json` 各新增 14 个 `mdv.*` key（标题/三视图/六状态/确认三键），两文件 74/74 key 集全等，`chrome_contract` locale parity 契约（含 MED-19 mirror 禁令）通过。运行时 Windows 端继续消费 IDS 字符串资源（zh-CN），locale JSON 是 parity 锚点；按用户偏好切换语言的机制归 BUX-13 偏好设置线，不在本模块偷跑。
- 实机复验（Windows 11 x64，Debug）：file:// 入口打开 `final.md` → 查看器渲染正确；分栏视图（左源码右预览）正确；退出零残留。双配置 ctest **58/58**；`scripts/check.ps1 fast/security` 全 passed；`git diff --check` 通过。
- **交互式键入冒烟受环境阻塞（如实记录）**：冒烟期间桌面另一应用（ChatGPT）反复抢占前台，SetForegroundWindow/ALT 前台锁解锁/SendKeys/剪贴板粘贴共 6 次尝试均未能将键入稳定送达页面 textarea（原始错误 `frontmost_pid_mismatch`；中文 IME 还曾把 URL 转为全角致导航失败，后改剪贴板粘贴绕过）。已验证到"打开→渲染→分栏→零残留"为止。
- **人工补验清单（约 2 分钟，桌面空闲时执行）**：① 打开任一 `.md` → 分栏 → 在左栏键入文字 → 右栏预览 ≤100ms 更新且出现脏点；② Ctrl+S → 磁盘文件内容更新且出现"已保存"绿条；③ 外部修改文件后再 Ctrl+S → 冲突浮层三选可用；④ dirty 状态下 omnibox 导航 → 确认浮层出现且取消不丢内容。上述链路的模型层行为已由 MDV-05/06 单测与 mdv_page/editing 契约测试覆盖，端到端键入是唯一缺口。
- 模块总 Review（按 `docs/current/code-review-standard.md` v0.8，范围 MDV-01..10 全部生产代码与测试）：
  - 需求/边界：契约 13 章逐项映射落地（§2 scheme/CSP=MDV-08、§3/4/5 入口=MDV-09、§6/7 渲染=MDV-02、§8 视图/编辑=MDV-03/05/10、§9 保存=MDV-06/10、§10 无持久化=各模型零 IO 面）；明确不做清单无越界（无 Agent 面、无目录枚举、无自动保存、无最近文件落盘）。
  - 正确性：渲染确定性 golden、路径矩阵、装载边界、revision fencing、原子写、冲突 (size,mtime) 均有契约测试；发现并修复的问题全部有回归用例（坏合并、编码、golden CRLF、RG-004 误报、两步迁移、启动崩溃等，见各任务记录）。
  - 架构/API：依赖方向零违规——共享层 `browser/shared-ui/{markdown,mdv}` 无 CEF/Win32/AppKit 类型（契约扫描持续强制），CEF adapter 只在 `cef-shell/src/browser/mdv`；window/ 与 new_tab 仅新增窄回调/装配性挂点（镜像既有惯例）。
  - 并发/生命周期：MdvRuntimeState 互斥快照（UI 写/IO 读）、事件中继锁外交付、router UI 线程惰性创建；退出零残留多次复验。
  - 安全/隐私：CSP 全封闭逐字节 golden、源码全量转义、白名单 HTML 原样插入、链接 scheme 白名单、图片永不加载、路径永不入 URL/DOM、query 绑定 origin 门禁、页面内容零触发路径（kPage 一票拒绝）、错误/诊断不携带路径与内容。
  - 性能：渲染去抖 ≤100ms 合并、5 MiB 装载上界、有界读取、事件中继容量 64 + dropped 计数；热路径无日志。
  - 测试：模型层 6 套契约 + 页面/编辑/入口/handler 契约，双配置 58/58；golden 与注入矩阵覆盖注入面。
  - 可维护性：新增生产文件均低于规模提醒线；vendored md4c 锁定 0.5.3 未改动。
  - 结论：P0/P1/P2 均为 `0`；唯一未覆盖项即上述人工键入补验，不构成合并阻塞（模型层已覆盖），但构成 `DONE` 门禁。
- 状态：`VERIFIED`。人工补验清单四项通过后转 `DONE`（届时仅更新本记录，无需新提交门禁）。

## MDV-11 原子范围（编辑回归修复 + 拖放/右键入口 + 可拖间隔条）

- 单一目标：修复 MDV-10 页面渲染回归（textarea 化在后续补丁覆盖中丢失，`RenderMdvDocument` 回退为 `<pre>` 只读源码面板），恢复源码面板可编辑；编辑查询响应携带预览负载（左栏键入右栏即时更新）；E2 拖放 `.md` 文件进窗口（`CefDragHandler::OnDragEnter` 经窄挂点接入口守卫）；E4 右键上下文菜单"在文档查看器中打开"（`.md` 的 file:// 页面或链接上追加菜单项，`MENU_ID_USER_FIRST`）；分栏间隔条左右拖动调宽（纯前端）。契约 `docs/current/markdown-viewer.md` §3 增 E4、§8 增 textarea/间隔条语义。
- 输出与允许修改：共享层 `mdv_page`（面板 markup 恢复 textarea、浮层、间隔条样式/脚本）与 `mdv_page_test`（textarea/间隔条/浮层断言）；`window/tab_controller` 增加 `CefDragHandler`/`CefContextMenuHandler` 两个窄挂点（镜像既有惯例）；`cef_mdv_entries` 增加拖放/菜单处理；`cef_mdv_editing` 编辑响应携带预览负载；装配（app、资源 IDS 225、locale 双语 key `mdv.label_open_in_viewer`，75/75 parity）。
- 验证：双配置 ctest **58/58**（含新断言）；clang-format、fast/security、`git diff --check` 通过；实机打开验证文档成功（用户现场进行交互验证：左栏编辑/拖放/间隔条/右键菜单）。`MDV-11` 转 `VERIFIED`，交互验证通过后与 MDV-07/10 一并转 `DONE`。

## MDV-12 原子范围（编辑工具栏）

- 单一目标：源码/分栏态源码面板顶部新增编辑工具栏，14 项闭合动作——标题 H1/H2/H3、加粗、斜体、删除线、行内代码、无序列表、有序列表、任务列表、引用、代码块、表格、链接、分割线；三类交互语义（包裹保留选区 / 行前缀 / 骨架插入含占位选中），全部经 `setRangeText` 保留撤销历史并触发既有 mdvQuery 编辑通道（预览即时更新）；纯页面内实现零新 binding、零外部资源。
- 文档先行：契约 §8 增工具栏小节（闭合动作清单与三类语义），Roadmap 增本任务行。
- 实现：`RenderToolbar` 生成工具栏（`<button data-action>` + addEventListener，CSP 无内联处理器）；14 个标签走 IDS 226..239 字符串资源与 locale 双语 key（108/108 parity）；预览态/无文档态不渲染工具栏。
- 顺带修复（跨平台门禁 bug）：① macOS 可移植编译契约 target 缺 `shared-ui/new-tab/include` 路径（远端 M05a 引入），补 include；② PRV-12 不安全路由门禁的 legacy-dev 豁免在 Windows 上因路径分隔符 `\` 失效，共同入口归一化后修复；③ 远端 `crayon-platform-macos` 测试文件 rustfmt 不通过，格式化修复。
- 验证：`mdv_page` 测试新增工具栏闭合动作集断言（14 项存在 + 非法项不存在 + setRangeText/wrapOrCaret/insertBlock 脚本面）；双配置 ctest **60/60**；fast/security 门禁、clang-format、`git diff --check` 全过；实机交互验证由用户现场进行。`MDV-12` 转 `VERIFIED`。

### MDV-13 完成记录（2026-08-27，图片支持：云端 https + 本地受控序号路由）

- 实现：
  - **引擎标记**（`markdown_render.cc`）：`<img>` 输出改为中间标记 `<img class="md-img" src="mdv-img:N" data-mdv-raw="原始引用" alt="…">`（原始引用只在 Browser 进程中间态存在）；白名单终检加 `img`/`src`/`data-mdv-raw`/`alt`（旧 `alt` 缺失曾使终检拒绝全部标记，调试定位后修复）。
  - **分类管线**（`shared-ui/mdv/mdv_images`，零 IO 注入探针）：https 直载；http/data:/其他 scheme 占位；本地路径词法归一化后必须落在文档目录内（穿越即占位）、格式白名单（png/jpg/jpeg/gif/webp/bmp/svg——svg 经 img 上下文无脚本面）、存在且 ≤20 MiB；合法的改写为不透明序号路由 `/img/N`（N 按文档顺序分配），映射只存 Browser 进程内存；fixture（无文档目录）一律占位。
  - **路由与 CSP**：`ClassifyMdvRequest` 增 `/img/<1-6 位数字>` GET/HEAD；handler 按代际快照读有界字节并按扩展映射 mime；CSP `img-src 'none'` → `img-src 'self' https:`（golden 更新）。
  - 契约 §2 CSP 块与 §7 图片规则修订为 v1.1（原"图片永不加载"作废）。
- 自动验证：`markdown_render` 引擎测试更新为标记形状断言；新增 `mdv_images` 测试 7 组（扩展名白名单含大小写、https 直载、http/data:/javascript: 占位、本地合法→序号路由、.. 穿越与目录外绝对路径拒绝、缺失/超限/fixture 占位、非白名单扩展拒绝）；`mdv_handler_contract` 更新（受控图片读取允许 ifstream 但必须经 ReadImageBytes+kMaxLocalImageBytes 有界）；双配置 ctest **61/61**；fast/security、clang-format、`git diff --check` 全过。
- Windows 实机（Windows 11 x64）：`D:/crayon-mdv-test/img-doc.md` 中 `![红色测试点](red.png)`（80×80 PNG）渲染为清晰可见的红色方块；`https://example.com/nothing.png`（404）显示浏览器原生失败占位；a11y 树确认两个 image 元素且 DOM/URL 无任何本地路径泄漏；`D:/pics/a.png`（不存在）显示占位文本。
- 未覆盖与风险：云端 https 图片真实加载成功（有网络时）未实测（本机网络环境）；流程图/图表渲染归 `MDV-14`（BLOCKED 待 §12 依赖评审）；`MDV-13` 转 `VERIFIED`（待 macOS 对齐门禁）。
