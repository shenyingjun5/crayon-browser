# MDV：本地 Markdown 查看器 Roadmap

状态：`MDV-01 READY`（依赖 `BUX-03 DONE` 已满足），`MDV-02..07 TODO`。本 Roadmap 承接“浏览器内查看本地 Markdown 文档、渲染预览与分栏编辑”的产品增量（PRD v0.8 §4.1）：`crayon://mdv` 内置查看页复用 `crayon://newtab` 的自定义 scheme、内存资源与严格 CSP 模式；本地 `.md` 只经用户手势的受控入口打开，保存走原子写。MDV 是纯用户能力，不进入 CAAP tool registry；Agent 侧任意文件访问禁令不变。排期属于 V1（Windows 优先、BUX 主线之后），`MDV-04` 起依赖 `BUX-16` 的受控本地文件入口。

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
| MDV-01 | READY | BUX-03 | `docs/current/markdown-viewer.md` | 契约冻结：`crayon://mdv` scheme/origin/CSP、入口与手势门禁、渲染语法范围、安全边界与渲染选型评审结论 | 契约 Review 通过；语法/CSP/入口矩阵冻结 |
| MDV-02 | TODO | MDV-01 | `browser/shared-ui/markdown` | 平台中立确定性 Markdown 渲染引擎：MD→转义安全 HTML、golden 与注入矩阵；vendored 库接入或自研子集 | MD-002；独立 ctest、`-Werror` 零告警 |
| MDV-03 | TODO | MDV-01,MDV-02,BUX-03 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | 只读查看内置页（fixture 内容驱动）：scheme handler、源码/预览切换、严格 CSP、零网络 | MD-003、MD-004 只读部分；Windows Debug/Release |
| MDV-04 | TODO | MDV-03,BUX-16,PLT-02 | `browser/shared-ui/mdv`,`browser/cef-shell` | 受控本地 `.md` 入口：文件对话框/拖放/omnibox 路径路由、路径/大小/UTF-8 校验、用户手势门禁 | MD-001、MD-003 |
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
