# MDV：本地 Markdown 查看器 Roadmap

状态：`MDV-01 DONE`（契约已冻结为 `docs/current/markdown-viewer.md` v1.0），`MDV-02 READY`，`MDV-03..07 TODO`。本 Roadmap 承接“浏览器内查看本地 Markdown 文档、渲染预览与分栏编辑”的产品增量（PRD v0.8 §4.1）：`crayon://mdv` 内置查看页复用 `crayon://newtab` 的自定义 scheme、内存资源与严格 CSP 模式；本地 `.md` 只经用户手势的受控入口打开，保存走原子写。MDV 是纯用户能力，不进入 CAAP tool registry；Agent 侧任意文件访问禁令不变。排期属于 V1（Windows 优先、BUX 主线之后）。

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
| MDV-05 | TODO | MDV-04 | `browser/shared-ui/mdv` | 分栏编辑与实时预览：编辑器状态机、dirty 跟踪、关闭/切换确认 | MD-004、MD-005 |
| MDV-06 | TODO | MDV-05 | `browser/shared-ui/mdv`,`browser/cef-shell` | 保存语义：写回/另存为、原子写、外部修改冲突检测、失败显式报告 | MD-006 |
| MDV-07 | TODO | MDV-01..06 | `docs/current`,`docs/plans` | Windows 实机收口与模块总 Review（macOS 对齐后置，不得用 Windows 证据完成 macOS） | MD-007；Review P0/P1=0 |

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
