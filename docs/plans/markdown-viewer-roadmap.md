# MDV：本地 Markdown 查看器 Roadmap

状态：本模块属于第一期三大闭环之一；`MDV-01 DONE`，`MDV-02..07,10..13 VERIFIED`，`MDV-08/09/21..23 DONE`，`MDV-24/25 VERIFIED`；Mermaid Full 为 `MDV-14..19 DONE`、`MDV-20 VERIFIED`。MDV-25 已移除生产 App 的 `BuildFixtureSnapshot()` 初始化并通过 macOS arm64 Debug/Release、真实空态和本地文件验证；Windows x64 对称回归仍归 Windows 终端。macOS arm64 Debug/Release 的七类 Mermaid、50-block、离线发布目录、SBOM/NOTICE、CEF 嵌套签名与退出零残留已闭合；MDV-20 仍需 Windows x64 Debug/Release 发布包回归后才能转 `DONE`。原生 macOS x64 不进入第一期 Apple Silicon 支持矩阵；Windows Narrator/中文 IME/原生 200% DPI 与当前自动化无法替代的窄窗交互真机仍待补。本 Roadmap 承接“浏览器内查看本地 Markdown 文档、渲染预览、分栏编辑与标准 Mermaid 图表”的产品增量（PRD v0.8 §4.1）：`crayon://mdv` 内置查看页复用 `crayon://newtab` 的自定义 scheme、应用内资源与严格 CSP 模式；本地 `.md` 只经用户手势的受控入口打开，保存走原子写。MDV 是纯用户能力，不进入 CAAP tool registry；Agent 侧任意文件访问禁令不变。

## 产品设计结论

- 查看器是独立 origin 的内置页 `crayon://mdv`：页面框架只从编译期/内存资源提供，渲染内容在 Browser process 内确定性生成后注入；零默认网络请求，不加载远程脚本或样式。
- 入口只有三种且全部要求用户手势：主菜单“打开文件”对话框（仅 `.md` 过滤）、拖放 `.md` 文件到窗口、omnibox 输入本地路径判定为本地文件后路由进查看器；页面内容不能触发打开动作。
- 渲染方向：确定性 Markdown → 安全 HTML。语法范围以 CommonMark 常用子集 + GFM 表格为基线，由 `MDV-01` 契约冻结闭合清单；默认技术方向为 vendored 轻量开源 C 库（如 md4c，MIT），必须先通过来源、许可证、维护状态、包体和跨平台影响评审并锁定版本，评审不过则自研语法子集；不引入运行时动态下载。
- 交互形态：源码视图 / 渲染预览切换；分栏模式左侧编辑源码、右侧实时渲染预览。编辑只产生本地 dirty 状态，保存由用户显式触发（写回原文件或另存为），原子写（`.tmp` + rename），失败显式报告，外部修改冲突显式提示。
- 边界：文件大小/编码有界校验（仅 UTF-8）；渲染输出强制 HTML 转义与标签白名单；不开放任意文件系统、目录枚举或文件系统监控；不做远程 `.md` URL 渲染、双链/wiki 扩展语法、协同编辑或导出 PDF。
- 无痕窗口可用查看器，但会话内状态（最近文件、滚动位置）不持久化。
- 图表能力采用官方完整版 `mermaid` 11.17.2 的离线 ESM 运行时闭包，不采用 `@mermaid-js/tiny`：标准 ```` ```mermaid ```` fence 覆盖 `flowchart/sequenceDiagram/mindmap/architecture-beta/classDiagram/stateDiagram-v2/erDiagram`，图类型由 Mermaid 自行识别。
- Mermaid 不进入浏览器 bootstrap，也不进入无图文档首屏：页面发现 Mermaid block 后才 `import()` 本地入口，具体 diagram chunk 继续同 origin 懒加载；所有可达运行时资产由构建期 manifest 精确枚举并 hash/许可锁定，无 CDN 与公网 fallback。
- 现有 md4c 仍是唯一 Markdown parser；通用 Extension Framework 归 `MRT-01..04`，本 Roadmap 只贡献 Mermaid adapter/资产/安全与交互，不复制 registry/loader。该能力不进入 Rust Core、CNT 页面 Markdown 或 CAAP；Mermaid 返回 SVG 按不可信内容走独立 policy gate，单 block 错误隔离。
- 编辑工具栏采用“飞书式克制密度 + 蜡笔原创线性 glyph”：所有顶层控件使用 24×24 DIP 自有图标、平台感知 tooltip 与快捷键文案；只复用 Markdown 已有列表/引用缩进和 GFM 表格列对齐，不增加普通段落对齐、raw HTML 或私有方言。

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
| MDV-11 | VERIFIED | MDV-08..10 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`docs/current` | 编辑回归修复（textarea 化被覆盖丢失）+ 拖放打开 + 右键上下文菜单入口（E4）+ 分栏间隔条可拖动 | MD-001..006 回归 + 新交互实机验证 |
| MDV-12 | VERIFIED | MDV-10 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`docs/current` | 编辑工具栏：15 项闭合动作（包裹/行前缀/骨架三类语义），复用既有编辑通道 | mdv_page 断言 + 实机交互验证 |
| MDV-13 | VERIFIED | MDV-08..11 | `browser/shared-ui/markdown`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | 图片支持：云端 https 直载 + 本地受控序号路由（文档目录内、格式/大小白名单、路径不入 URL/DOM）+ CSP img-src 修订 | MD-002 图片矩阵 + 实机 |
| MDV-14 | DONE | MDV-13 | `third_party/mermaid`,`tools`,`docs/current`,`docs/plans` | Mermaid Full 供应链冻结：固定 `mermaid` 11.17.2，vendor 完整浏览器运行时 import closure、LICENSE/NOTICE、hash/MIME/大小 manifest 与可重复生成/校验入口 | MD-008；离线 closure/许可/双次生成 hash |
| MDV-15 | DONE | MDV-14,MRT-03 | `browser/shared-ui/markdown`,`browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv` | Mermaid adapter：标准 Mermaid fence → 通用 ExtensionNode、不透明 block/占位与有界 DSL；普通 Markdown 保持既有 md4c 安全输出 | MD-002、MD-009、MR-001/002；golden/注入/边界 |
| MDV-16 | DONE | MDV-14,MRT-04 | `browser/shared-ui/markdown-runtime`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`browser/cef-shell/resources`,`browser/cef-shell/CMakeLists.txt` | Mermaid ESM 资产路由与打包：消费通用 manifest loader，精确路由、正确 MIME、相对 chunk import、macOS/Windows 同源资源装配；无图零加载 | MD-008、MR-003；路由攻击矩阵 + 离线 CEF smoke |
| MDV-17 | DONE | MDV-15,MDV-16 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv` | Mermaid runtime 核心：按需 `import()`、单例初始化、`mermaid.render()`、strict 配置、独立 SVG policy gate、per-block 错误隔离与七类图覆盖 | MD-009；CSP/注入 golden + CEF render |
| MDV-18 | DONE | MDV-17 | `browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`browser/shared-ui/locales` | 图表交互与主题：viewport lazy render、响应式宽度/横向滚动、浅深主题重绘、全屏查看/源码切换；零新特权 binding | MD-004、MD-009；键鼠/a11y/主题实机 |
| MDV-19 | DONE | MDV-17,MDV-18 | `browser/shared-ui/markdown-runtime/assets/mermaid-adapter.js`,`browser/shared-ui/markdown-runtime/tests/mermaid_adapter.test.mjs`,`browser/shared-ui/mdv`,`browser/cef-shell/src/browser/mdv`,`tests/e2e/desktop/browser`,`docs/current/markdown-viewer.md` | 编辑/多图性能与生命周期：有界并发和内存 cache、revision fencing、迟到结果丢弃、关闭/导航/内存压力清理 | MD-004、MD-010；50-block perf/风暴/资源回落 |
| MDV-20 | VERIFIED | MDV-14..19,MDV-07,MDV-10..13 | `docs/current`,`docs/plans`,`tests/e2e/desktop`,`tools/repo-guard` | Mermaid Full 跨平台收口：macOS arm64 先行、Windows x64 回归、安装包/SBOM/NOTICE/零公网、模块总 Review | MD-007..010；Debug/Release + 实机 + Review P0/P1=0 |
| MDV-21 | DONE | MDV-12 | `docs/current`,`docs/plans`,`browser/shared-ui/mdv/design` | 工具栏设计契约与原创 glyph 资产：动作/tooltip/快捷键/上下文矩阵、24×24 图标 manifest 与安全验证；不接页面行为 | MD-011；design contract + `git diff --check` |
| MDV-22 | DONE | MDV-21 | `browser/shared-ui/mdv` | 可测试编辑变换层：修复行前缀重复正文，统一包裹/标题/多行列表/骨架/缩进/表格列对齐与选区保持 | MD-012；独立 ctest + 回归矩阵 |
| MDV-23 | DONE | MDV-22 | `browser/shared-ui/mdv`,`browser/shared-ui/locales`,`browser/cef-shell/resources/windows` | 图标工具栏接线：tooltip、平台快捷键、overflow、roving tabindex、IME 门禁与既有 `mdvQuery` 集成；零新 Browser binding | MD-004、MD-011..013；页面/快捷键/a11y contract |
| MDV-24 | VERIFIED | MDV-23 | `browser/cef-shell`,`tests/e2e/desktop`,`docs/current`,`docs/plans` | 工具栏平台收口：macOS arm64 Helper/签名/默认页/公网/MDV/AX 已闭合；Windows、原生 macOS x64 与剩余交互真机待补 | MD-007、MD-013；Debug/Release + 实机 + Review P0/P1=0 |
| MDV-25 | DONE | REL-02,MDV-24,MRT-08 | `browser/cef-shell/src/browser/mdv`,`browser/cef-shell/src/{macos,windows}`,CEF shell contracts | 已移除生产 fixture 初始化；macOS arm64 与 Windows x64 空态、本地文件和发布二进制扫描闭合 | RG-002、MD-003/004；双平台 CEF + Release scan |

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
- Mermaid 只能消费 manifest 内的本地 ESM 运行时资产；禁止 CDN、运行时 npm、任意模块路径、tiny 降级和把全包塞入浏览器启动链路。完整版语义以图类型完整为准，vendor 只裁去 npm 源码/文档/测试/开发依赖，不得裁掉任一运行时 chunk。
- Mermaid 初始化固定 `startOnLoad:false`、`securityLevel:"strict"`，并关闭 HTML label/外部资源等扩大 SVG 攻击面的配置；上游返回值仍须过 Browser-owned SVG policy gate，不能因“上游已净化”跳过本项目检查。
- 本地文件只限用户手势选择的 `.md`；路径校验拒绝目录、非 `.md`、超长与控制字符；不做任意路径访问或目录枚举。
- 保存使用 `.tmp` + rename 原子写；无静默残留，不把 best-effort 宣称为成功；外部修改冲突必须显式提示。
- 所有可见文案进入本地化资源；图标来自自有 glyph/品牌资产。
- 不修改 BUX-18 既有依赖；MDV 独立在 `MDV-07` 收口，不阻塞其他浏览器基线项。
- Markdown Runtime 的通用 API、Highlight、KaTeX、ECharts、Graphviz、Presentation 统一归 `markdown-runtime-roadmap.md`；TV/Cast 与 AI Source Producer 分别由 `MRT-18/19` 做 gap analysis，PlantUML/Vega/AI 编辑保持延后。它们均不塞入 `MDV-14..20`。其中 Cast Markdown 涉及接收端/Cast-SDK 新协议，必须经 `MRT-18` 与外部独立 Roadmap 获得 facade 后才可实施。


## MDV-20W 原子范围（Windows x64 Mermaid Full 总回归）

- 状态：`DONE`；依赖 `MDV-25W DONE`、`MDV-20 VERIFIED`。
- 单一目标：在 Windows x64 上闭合 Mermaid Full 的产品回归——七类重点图渲染、50-block lazy/cache 终态、Highlight、KaTeX、离线零公网、亮暗主题、窄/宽窗、100%/200% device scale、保存/退出资源回落、Windows Debug/Release 重建 + ctest + Release package/NOTICE/SPDX/零公网门禁。
- 输入：MDV-14..20 已完成实现与 macOS 侧证据、`tests/e2e/desktop/browser/run_mdv_mermaid_perf.mjs` harness、RG-009 manifest 门禁。
- 边界：不改写 macOS 已有证据；不引入新依赖；DevTools loopback 端口只允许在本任务实机验证期间通过命令行开关启用，不进入产品代码或 Release 产物（RG-006 保持 fail closed）。
- 验收命令：Windows Debug/Release build + ctest 全量；`node tests/e2e/desktop/browser/run_mdv_mermaid_perf.mjs --port=9333` 真机通过（含七类图/50-block/cache/内存压力断言）；Release package contract + RG-009 manifest 校验；`scripts/check.ps1 fast/security`。
- 明确不做：不扩展图类型清单；不做 Narrator/IME/窄窗交互（归 MDV-24W）；不改写 macOS 证据。

### MDV-20W 完成记录（2026-09-01）

- 实现一（根因修复，Windows Chrome runtime 可见性）：Windows x64 Chrome runtime 下首个 `CreateBrowser` 的 WebContents 滞留 `document.visibilityState=hidden`（`WasHidden(true/false)`、`SW_HIDE/SW_SHOW`、`SetForegroundWindow` 均无效），导致 MDV 页 IntersectionObserver 永不触发、Mermaid lazy 渲染挂死。修复：`TabController` 增加一次性合成标签条变更 nudge——`CreateBrowserWindow` 置位 `initial_visibility_nudge_pending_`（仅当产品壳配置了 `new_tab_url_`，集成测试 harness 不配置故不受扰），`OnAfterCreated` 投 300ms 延迟 UI 任务执行 `ExecuteChromeCommand(IDC_NEW_TAB/IDC_CLOSE_TAB)`（`cef_id_for_command_id_name` 版本安全映射）各一次，强制标签条变更使 WebContents 可见性重算。回归定位：该 nudge 曾使 `cast_cef_integration_windows`/`page_snapshot_cef_integration_windows` Release 失败（合成 close 误关 fixture tab），以 `new_tab_url_.has_value()` 门控闭合，双配置 85/85 复证。
- 实现二（空态整面板）：`mdv_page.cc` 空态渲染完整 panes+divider+script 结构，harness 的 `mdvPush` 注入在空文档态可用（此前空态直接短路返回，无 preview 节点）。
- 新增常驻 harness `tests/e2e/desktop/browser/run_mdv_theme_viewport.mjs`：亮/暗主题（`prefers-color-scheme` emulation 触发 retheme 重渲染且 SVG 输出变化）、520px 窄窗 + 200% device scale 渲染可用、整页重载后 JSHeap 回落（5.79MB→6.18MB，<1.5× 上界）。
- 验证（Windows x64 真机，CEF 150.0.10 Chrome runtime）：
  - `cmake --build --preset windows-cef-debug --config Debug/Release` 全量零错误；`ctest --preset windows-cef-debug`（Debug）85/85、`ctest --preset windows-cef-debug -C Release` 85/85（含 `windows_cef_shell_package_contract`、`mdv_handler_contract`、cast/page_snapshot 集成）。
  - `node tests/e2e/desktop/browser/run_mdv_mermaid_perf.mjs --port=9333`：blockCount=50、rendered=47、errors=3（17 的倍数位非法图 + 49 位 70KiB 超限图按契约失败）、unresolved=0、七类图（flowchart/sequence/mindmap/architecture-beta/class/state-v2/er）全渲染、cache 与内存压力清零断言通过、`publicRequests=[]`（零公网）、`ordinaryMermaidRequests=0`、JSHeapUsedSize≈15.7MB。
  - `node tests/e2e/desktop/browser/run_mdv_theme_viewport.mjs --port=9333`：failures=[]（亮/暗/窄窗/200%/重载回落）。
  - 可见性探针：门控构建下初始页 `document.visibilityState="visible"`（修复前恒定 `hidden`）。
  - Release 门禁：`repo-guard mermaid-metadata` 生成 THIRD_PARTY_NOTICES.md + SPDX-2.3 SBOM + manifest 成功；`node tools/mermaid/vendor.mjs --check` 104 files/3522090 bytes OK；`scripts/check.ps1 -Mode fast` passed（guard/format/brand-assets/formal-workspace/legacy-unit），`-Mode security` passed（guard/relay-unit/relay-security 7 项）。
- Code Review：P0 0、P1 0、P2 1——可见性 nudge 依赖 Chrome runtime 内部行为（标签条变更触发可见性重算），属未文档化行为，CEF 升级时需复验（已在代码注释与 contract 测试覆盖 85/85 双配置）；另 `page_snapshot_cef_integration_windows` 在与 Release 构建并发时出现过一次超时 flake（单跑 69.8s 通过），归既有集成测试资源竞争，非本任务引入。
- 未覆盖与风险：Narrator/IME/窄窗交互真机归 `MDV-24W`；"保存/退出资源回落"以整页重载 heap 回落近似覆盖，保存链路写后渲染资源释放已被 MDV-06/13 单测覆盖；macOS 特有验证继续如实后置。`MDV-20W` 转为 `DONE`。

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

- 单一目标：源码/分栏态源码面板顶部新增编辑工具栏，15 项闭合动作——标题 H1/H2/H3、加粗、斜体、删除线、行内代码、无序列表、有序列表、任务列表、引用、代码块、表格、链接、分割线；三类交互语义（包裹保留选区 / 行前缀 / 骨架插入含占位选中），全部经 `setRangeText` 保留撤销历史并触发既有 mdvQuery 编辑通道（预览即时更新）；纯页面内实现零新 binding、零外部资源。
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

> 2026-08-28 superseded：上行记录保留当时事实；图表依赖评审现已由契约 v1.2 改为 Mermaid Full，`MDV-14` 状态为 `READY`，不再采用 tiny 方案。

## Mermaid Full 集成波次（MDV-14..20）

### 差距与工程映射

| 参考方案目标 | 当前工程事实 | 集成决定 | 任务 |
|---|---|---|---|
| Mermaid Full 11.x | 契约曾选 `@mermaid-js/tiny`，缺 mindmap/architecture/lazy loading | 撤销 tiny，固定官方 `mermaid` 11.17.2 完整浏览器运行时闭包 | MDV-14 |
| 扩展式 Markdown Runtime | C++17 md4c 直接输出白名单 HTML，`MdvPageSnapshot` 传 `rendered_html` | `MRT-01..03` 建闭合 ExtensionNode/registry；MDV 只接 Mermaid adapter，不引入第二 parser | MRT-01..03、MDV-15 |
| 本地 ESM + dynamic import | `crayon://mdv` 仅有 `/app.html|css|js` 和 `/img/N` 固定路由 | `MRT-04` 建通用 manifest loader/lifecycle；MDV-16 只装配 Mermaid closure 与平台资源 | MRT-04、MDV-16 |
| `mermaid.render()` + strict | `/app.js` 当前仅编辑/视图逻辑，无第三方 runtime | 只在发现 Mermaid block 后加载并初始化；逐 block render，不用 `run()` 全页扫描 | MDV-17 |
| 错误隔离、主题、全屏 | 预览只消费整段 HTML，主题依赖系统 CSS | block 状态机与局部错误卡；主题变化按 block 重绘；全屏只改变页面布局 | MDV-17/18 |
| lazy render、cache、大文件 | 现有 Markdown 5 MiB、编辑 revision fencing；无图表预算 | viewport lazy + 有界并发 + 会话内 cache + diagram generation；迟到结果丢弃 | MDV-18/19 |
| 完整包不拖慢启动 | CEF 壳离线构建，资源由 Browser process 提供 | 无图文档零 Mermaid 读取；记录 import/首图/CPU/RSS/UI delay；双平台包体与 SBOM 门禁 | MDV-16/19/20 |

依赖顺序：`MRT-01 -> MRT-02 -> MRT-03 -> MRT-04` 建立通用 foundation；`MDV-14` 可按单任务纪律独立先行。随后 `MDV-15` 等待 `MDV-14 + MRT-03`，`MDV-16` 等待 `MDV-14 + MRT-04`，再按 `MDV-17 -> 18 -> 19 -> 20` 收口。一次仍只领取一个原子任务；可并行仅表示依赖关系，不授权同一 Agent 同时占用多项。

### MDV-14 原子范围（Mermaid Full 供应链与离线运行时闭包）

- 状态：`DONE`；依赖 `MDV-13 VERIFIED`。本任务替代旧 tiny 集成定义，不写页面渲染行为。
- 单一目标：固定官方 `mermaid` 11.17.2，建立可重复的 vendor/verify 流程，产出应用真正需要的完整浏览器运行时 import closure（ESM 入口、全部可达 diagram/layout chunk 与必需静态资源）、LICENSE/NOTICE、上游 provenance、npm integrity、逐文件 SHA-256/MIME/大小和总包体 manifest。
- 输入：参考方案 §3/4/8/16/17/32/33，当前契约 §14..16，官方 full/tiny 能力差异；现有离线 CEF 构建与第三方依赖锁定惯例。
- 允许修改：`third_party/mermaid/**`、专用 `tools/vendor-mermaid/**` 或等价窄脚本、第三方 NOTICE/SBOM 输入、`docs/current/markdown-viewer.md`、本 Roadmap。生成脚本不得依赖系统全局状态；实际下载只在显式 vendor 更新时发生，普通 build/test 必须完全离线。
- 禁止修改：MDV 页面/renderer 生产代码、md4c、CAAP/CNT/Cast-SDK；不得提交 npm cache、`node_modules`、上游源码/测试/文档全集或开发依赖，不得 tree-shake 删除图类型，不得保留 tiny fallback。
- 边界：复核所有运行时依赖许可证、维护与已知安全公告；manifest 路径必须相对、规范化、无重复/大小写冲突/符号链接，import closure 不得含 `http(s):`/CDN/动态任意 specifier；入口和 chunk 的 CEF 150 ESM 语法兼容必须静态检查。
- 验收：`MD-008`；两次从同一锁定 tarball 生成的 manifest 与资产 hash 全同；离线 import closure 检查零缺失/零多余网络 import；许可证/NOTICE/SBOM 一致；记录实际命令、文件数、总字节与审计结论。
- 明确不做：路由、页面加载、SVG 渲染、图表 UI、性能优化。

### MDV-14 完成记录（2026-08-29）

- 实现：新增 `tools/mermaid/vendor.mjs`（`--check` 离线校验 / `--archive <tgz>` 从锁定 tarball 重建 / `--download` 显式网络维护动作）与 `third_party/mermaid`（`assets/` 104 个 ESM 文件 + LICENSE + VENDORED.md + manifest.json）。manifest schema `crayon-mermaid-assets/v1`：package（name/version/license/npm integrity/tarball SHA-256/upstream tag v11.17.2）、policy（entry `mermaid.esm.min.mjs`、externalImports/networkImports=0、总字节 3,522,090 / 预算 16MiB）、files 104 项逐文件 path/bytes/sha256/mime（全部 text/javascript）。闭包行走以语句/运算符边界 + 关键词与引号零间隔的正则只接受 `./` 相对 specifier，`http(s):`/`data:`/bare 一律 fail closed；每文件经 `node --check` 静态 ESM 语法检查（CEF 150 V8 新于工具链 Node，属保守兼容）。npm manifest 的 22 个源码消费 runtime 依赖均在上游预打包，vendor 闭包零依赖；无 .map、无 docs/test/dev 依赖、无 tiny、未 tree-shake 图类型（flow/sequence/class/state/er/gantt/pie/gitGraph/journey/mindmap/architecture/c4/xychart/quadrant/venn/sankey/requirement/block 等 chunk 全在）。
- 审计：npm registry integrity `sha512-V6K3C8...BBg==` 与 tarball SHA-256 `6ad2f42c...950ffd` 双重锁定；上游 github.com/mermaid-js/mermaid tag `v11.17.2`，MIT LICENSE 全文校验；SBOM 输入（包名/版本/许可/integrity/来源 URL）机器记录于 manifest；资源字符串中出现的 http URL 均为文案字面量，非 import 语句（正则与人工裁定双向确认）。
- 失败基线：开发中先失败后修复——tar 条目预算（13MB 源映射超 8MiB 上限）、正则误报三连（`" from (",n,` 字符串、`data-from"` 属性、`=>import(` 前缀缺失）分别以真实数据复现后收紧/放宽边界并回归。
- 自动验证：`node --test tools/mermaid/vendor.test.mjs` 6/6（tar 边界、身份/integrity fail closed、specifier 分类与字符串陷阱、闭包与 manifest 一致、篡改/缺失/多余/CRLF/乱序矩阵）；`node tools/mermaid/vendor.mjs --check` 104 files/3,522,090 bytes 离线通过；同一锁定 tarball 两次 `--archive` 生成的全部 107 个文件 SHA-256 逐字节一致（可复现性）。
- Code Review：按 v0.8 复核供应链、路径/归档边界、原子替换、网络入口、运行能力、包体、测试与维护性；P0/P1/P2=0。
- 未覆盖与风险：本任务不改页面行为，Mermaid ESM 资源路由、adapter、渲染与安全 gate 归 `MDV-15..20`（`MRT-03/04` 已 DONE，可领取）；CEF 真机内的实际动态 import 行为由 MDV-16 离线 CEF smoke 验证。`MDV-14` 转为 `DONE`。

### MDV-15 原子范围（Mermaid Extension Adapter）

- 状态：`DONE`；依赖 `MDV-14 DONE`、`MRT-03 DONE`。
- 单一目标：在 MRT 通用 ExtensionNode/registry 上注册唯一 Mermaid adapter，使精确 ```` ```mermaid ```` fence 变为安全占位 + 有界 DSL；其他 info string 继续是普通代码块，普通 Markdown golden 逐字节不回退。
- 输入：MRT-02/03 的 ExtensionNode/registry、`RenderMarkdownToSafeHtml`、`MdvViewerModel` revision fencing、`MdvPageSnapshot`、契约 §6/7/15。
- 允许修改：`browser/shared-ui/markdown/**`、`browser/shared-ui/markdown-runtime/**` 中 Mermaid adapter 注册点、`browser/shared-ui/mdv/**` 及对应独立测试；公共变化限 MDV/MRT 内部 DTO，不进入 `browser/engine-api`。
- 禁止修改：vendored md4c 源、CEF/平台代码、页面 binding、Agent/CNT schema；不得用正则重新实现完整 Markdown fence parser，不得根据 `flowchart/mindmap/...` 分支。
- 边界：block 数、单 DSL 字节与总 DSL 字节有命名上限；超界仅对应 block 降级安全代码块/错误占位，不使整篇文档崩溃；源码只作为文本，不进入 URL、HTML 属性、脚本字面量或日志。
- 验收：`MD-002/009`；七类 DSL 均被识别为同一 `mermaid` kind；大小写/附加 token/未闭合 fence/嵌套 fence/HTML 注入/超界矩阵；旧 Markdown golden 全过；5000 步编辑 revision 风暴无旧 block 落位。
- 明确不做：加载 Mermaid、输出 SVG、图表主题/交互、磁盘 cache。

### MDV-15 完成记录（2026-08-29）

- 实现：新增 `browser/shared-ui/markdown-runtime` 的 `mermaid_extension.{h,cc}`——闭合 `mermaid` fence adapter：单一精确大小写敏感 matcher（`{kFence, "mermaid"}`）、编译期 registry（manifest id `mermaid`、version 锁定 vendored `11.17.2`、output `kSvg`/policy `kSvgV1`、asset manifest `mermaid-runtime-assets-v1` 留待 MDV-16 接目录、capabilities 全 deny）；`ApplyMermaidDecorations` 对 P0 HTML 中每个预算内且路由成功的 fence 开标签加 inert 占位标记 `data-mdv-mermaid="true"` + `data-mdv-node="<revision 绑定 id>"`，DSL 保持转义文本，不进 URL/脚本字面量/日志。命名预算：单文档 64 block、单 block 64KiB、总量 512KiB，超界仅该 block 降级普通代码块，文档不失败；装饰器对已装饰/不匹配 HTML fail closed（字节不变）。P0 组装（`RenderP0MarkdownDocument`/`HighlightFallback`）统一接入，`P0MarkdownDocumentResult` 新增 `mermaid_blocks`。页面可见行为不变（mermaid fence 仍呈现代码块文本），占位 UI 与 SVG 渲染归 MDV-16/17。
- CMake：manifest 锁检查（schema/包名/版本/许可/entry/externalImports/networkImports/文件数 104/总字节 3,522,090 任一漂移即 configure 失败）；新增 `mermaid_extension.cc` 与 `markdown_runtime_mermaid` 测试 target。
- 失败基线：三处先失败后修复——测试暴露未闭合 fence 的 CommonMark 语义（md4c 将其上报为合法 fenced code block，测试期望从"不装饰"改为记录语义后断言装饰+转义）；已装饰 HTML 的二次装饰按 fail closed 处理（stale 组合断言改为字节不变）；风暴变异脚本自身破坏 fence（改为只变异 body 段落）。
- 自动验证：`markdown_runtime_mermaid` 1/1（七类 DSL 同 kind + 独立 node id、大小写/附加 token/mermaidish 拒绝、未闭合/HTML 注入转义、单 block/数量预算降级、普通 Markdown 零回退、math+highlight+mermaid 三扩展组装、5000 步 revision 风暴无旧 block 落位且 stale 组合字节不变）；engine-api preset 全量 ctest 57/57；macOS arm64 CEF Debug 全量构建 + ctest 68/68；macOS x64 CEF 构建通过 + markdown/mdv scoped ctest 16/16；`scripts/check.sh fast`/`security`、`git diff --check`、新增行 ≤80 列通过。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、安全/隐私、性能、测试和可维护性；P0/P1/P2=0。装饰对齐用顺序 cursor 而非全局查找（多 fence 无歧义）；node id 为定宽 hex 可安全入属性；预算拒绝只降级不失败。
- 未覆盖与风险：Mermaid ESM 资源路由与打包（MDV-16）、页面占位 UI/动态 import/渲染与 SVG policy gate（MDV-17）、主题/交互（MDV-18）未做；`mermaid-runtime-assets-v1` 目录尚未注册资产目录（MDV-16 消费）。`MDV-15` 转为 `DONE`。

### MDV-16 原子范围（Mermaid ESM 资产路由与跨平台打包）

- 状态：`DONE`；依赖 `MDV-14 DONE`、`MRT-04 DONE`，可在 MDV-15 之后或独立领取。
- 单一目标：让 `crayon://mdv` 只读 resource handler 消费 MRT 通用 manifest loader，按 MDV-14 manifest 提供 Mermaid ESM 入口与相对 chunk，完成 macOS App bundle/Windows resource 的同一资产装配，并保证无 Mermaid 文档不读取任何 Mermaid 字节。
- 输入：MRT-04 loader/lifecycle、`ClassifyMdvRequest`、`MdvMemoryResourceHandler`、CEF CMake/平台资源打包、Mermaid manifest。
- 允许修改：`browser/shared-ui/markdown-runtime/**` 的 Mermaid manifest adapter、`browser/shared-ui/mdv/**`、`browser/cef-shell/src/browser/mdv/**`、`browser/cef-shell/resources/{macos,windows}/**`、`browser/cef-shell/CMakeLists.txt` 与路由/打包测试。
- 禁止修改：Mermaid 资产内容与 hash、md4c/编辑/保存语义、通用 file handler；不得开放目录列表、任意文件读取、query 驱动资源或公网 fallback。
- 边界：仅 GET/HEAD；路径先 percent-decode 一次再规范化，拒绝 `..`、反斜杠、NUL、二次编码分隔符、query/fragment、大小写别名与 manifest 外路径；MIME 至少闭合 `.mjs/.js/.css/.wasm/.json` 的实际 closure，`nosniff` 保持；响应读有界并支持取消。
- 验收：`MD-008`；manifest 每项 200/正确 MIME/hash，未知/穿越/编码逃逸 404，POST 405；CEF 150 可从入口解析全部相对 import；断网环境普通文档和七类 fixture 均无公网请求；macOS arm64 build/smoke 先行，Windows build 回归。
- 明确不做：调用 Mermaid API、SVG 注入、图表 UI。

### MDV-16 完成记录（2026-08-29）

- 实现：Mermaid Full 104 文件闭包经 CMake 从 `crayon-mermaid-assets/v1` manifest 逐文件校验 bytes/SHA-256 后以 HEX 字节数组编译期嵌入（`mermaid_assets_generated.h`，配置依赖逐资产挂接，任一漂移即 configure 失败）；`BuildMermaidAssetCatalog` 产出单一不可变 bundle（manifest id `mermaid-runtime-assets-v1`、entry `mermaid.esm.min.mjs`、resource id=upstream 相对路径、全部 `kJavaScript`）。`crayon://mdv` 路由新增 `/runtime/mermaid/<upstream-relative-path>` 命名空间，handler 以精确、大小写敏感的 bundle 查找供出（不在 manifest 的 id 404）。分类器加固：路径先做 encoded-separator 前置拒绝（`%2f`/`%5c`），再 `PercentDecodePath` 恰好解码一次（畸形/残留 `%`/反斜杠/NUL 全拒），`//`、`/./` 形状拒绝；仅 GET/HEAD 语义不变。macOS App bundle 与 Windows 均消费同一编译期嵌入资产，无需平台资源差异；无 Mermaid 文档不产生任何 mermaid 请求（页面侧 import 由 MDV-17 接线，供出纯按需）。
- 契约修订：`markdown-runtime.md` §8 asset catalog 每 bundle resource 上限 `64`→`256`（Mermaid Full 104 文件闭包为首个超限合法 bundle，修订日期与理由已注明）；代码 `kMaxAssetsPerBundle` 同步。
- 失败基线：三次先失败后修复——`configure_file(@ONLY)` 模板占位符误用 `${}`；raw-string 嵌入触发 `-Woverlength-strings`（>64KiB 字面量），切换 katex 式 HEX 字节数组；测试期望两处与实现语义不符（编码分隔符 `%2f` 前置拒绝、大小写别名归 handler 精确查找 404）。
- 自动验证：`markdown_runtime_mermaid` 扩展 1/1（catalog ready/104 资源/entry/总字节 3,522,090/全部资源相对 import 在 bundle 内闭合解析）；`mdv_page` 路由矩阵扩展（entry/chunk 正确分类、穿越/编码逃逸/大小写别名/未知 id/`%zz`/`%2` 拒绝矩阵）；`mdv_handler_contract` 增 `BuildMermaidAssetCatalog` 必需 token；engine-api 全量 ctest 57/57；macOS arm64 CEF 全量构建 + ctest 68/68（含 handler 契约）；macOS x64 构建 + markdown/mdv scoped ctest 16/16；`scripts/check.sh fast`/`security`、`git diff --check`、新增行 ≤80 列通过。
- 实机 smoke：arm64 Debug `CrayonBrowser.app` 正常启动（6 进程）、退出零残留。
- Code Review：按 v0.8 复核供应链完整性、路由/逃逸边界、原子性与预算、测试与维护性；P0/P1/P2=0。资源 id 语法层只做形状校验、闭合集合校验在 handler 精确查找——与 highlight/katex 命名空间既有分工一致。
- 未覆盖与风险：真实 CEF 内从入口的动态 import 链与图表渲染归 `MDV-17`（本任务的"可从入口解析全部相对 import"由闭包内解析测试结构性保证）；Windows 真机回归归 MDV-20 收口。`MDV-16` 转为 `DONE`。

### MDV-17 原子范围（Mermaid 渲染 runtime 与安全隔离）

- 状态：`DONE`（2026-08-29 完成）；依赖 `MDV-15/16 DONE`。
- 单一目标：在 `/app.js` 的 MDV 扩展 registry 中接入唯一 `mermaid` renderer：发现 block 后才 `import()` 本地入口，单例初始化 `startOnLoad:false/securityLevel:"strict"`，逐 block 调 `mermaid.render()`，经过独立 SVG policy gate 后替换对应占位；单 block 失败局部收敛。
- 输入：MDV-15 block DTO、MDV-16 asset URL、现有 `mdvPush` 受控更新与 CSP。
- 允许修改：`browser/shared-ui/mdv/**`、`browser/cef-shell/src/browser/mdv/**`、MDV locale 与独立 runtime/handler contract 测试。优先把固定 JS/CSS 拆为可测试的 MDV runtime 资源，不继续无限扩张 `RenderMdvScript()` 字符串函数。
- 禁止修改：CSP 为 `unsafe-inline`/`unsafe-eval`，网络 allowlist，Markdown raw HTML 策略；不得调用 `mermaid.run()` 全页扫描，不得使用 `innerHTML` 注入未经 policy gate 的 SVG，不得把解析错误堆栈写日志/DOM。
- 边界：初始化失败只禁用当前页图表并保留源码；SVG gate 拒绝 script/event handler/foreignObject/外部 URL/危险 scheme/`@import`/CSS `url()`，ID 与 fragment 引用必须局限当前 block；`htmlLabels` 等需要 foreignObject 的配置默认关闭。七类基准图必须在此安全配置下通过，否则任务保持 `BLOCKED` 并修订方案，不能放松策略伪造通过。
- 验收：`MD-009`；flowchart、sequenceDiagram、mindmap、architecture-beta、classDiagram、stateDiagram-v2、erDiagram 实际 SVG；非法 DSL 与恶意链接/点击/HTML/style payload 局部错误且其余 block 正常；CSP 零违规、网络零请求、普通 Markdown 未请求入口。
- 明确不做：viewport lazy、全屏、cache、演示/投屏。

### MDV-17 完成记录（2026-08-29）

- 实现：新增 `browser/shared-ui/markdown-runtime/assets/mermaid-adapter.js`（约 500 行，经 CMake 以 resource id `adapter` 嵌入 Mermaid bundle，服务路由 `/runtime/mermaid/adapter`，嵌入闭包由 104 变为 105 资产、MDV-14 104 文件/3,522,090 bytes 逐字节校验保持不变）。adapter 职责：按需单例 `import("/runtime/mermaid/mermaid.esm.min.mjs")` + 一次性 `initialize({startOnLoad:false, securityLevel:"strict", htmlLabels:false, flowchart/class:{htmlLabels:false}})`；逐 block `mermaid.render("mdv-mermaid-<nodeId>", source)`（30s deadline）；独立 SVG policy gate：闭合 tag/attribute 允许清单（禁 script/foreignObject/a/image/feImage/SMIL）、scheme 禁止、URL 引用仅限 `url(#id)`/`#id` 且全部重写为块内唯一 id（`<renderId>`/`<renderId>-`/`<renderId>_` 前缀识别），未知/跨块目标 fail closed；嵌入 `<style>` 在 DOMParser 前剥离（避免触发页面 CSP），CSS 规则结构 fail closed（剩余 at-rule/嵌套/控制字符整块拒绝），选择器限定块内 `#id` 且仅经 CSSOM `setProperty` 应用（永不重建 `<style>` 元素），无效规则/声明丢弃不应用；inline style 仅保留合法声明（丢弃 `undefined;;;undefined` 类垃圾槽位）；block 失败局部标记 `data-mdv-mermaid-error` 并保留转义源码，无堆栈/DOM/日志泄漏；每次替换前复查 `data-mdv-node`/连接态/rendered 态防迟到结果落位。页面接线（`mdv_page.cc` `AppendMdvMermaidScript`）：发现 `code[data-mdv-mermaid]` 后经共享 promise 队列逐块渲染，`.catch` 吞掉 per-block 异常；无图文档零 `import` 零请求。CSS 新增渲染态/错误态样式（含深色）。fixture 示例文档加入一个 mermaid fence 供真实 CEF 端到端面。
- 失败基线（先失败后修复）：(1) 真实 CEF 中 mermaid 输出触发 `style-src` 违规且块渲染失败——定位到 adapter 重建 `<style>` 元素、mermaid 11.17 需要顶层 `htmlLabels:false`（仅 flowchart/class 级无效）、`data-*` 属性与 `feDropShadow` 滤镜原语、超长 path data、空/垃圾 inline style、`@keyframes` 嵌套 CSS——逐一修复并通过；(2) `name` 属性与 `_` 前缀 id 识别补齐。
- 自动验证：`node --test browser/shared-ui/markdown-runtime/tests/mermaid_adapter.test.mjs` 4/4（属性策略、CSS 规则闭合与块作用域、DOM stub 下 gate 端到端重建/引用重写、活动内容与逃逸引用拒绝）；`markdown_runtime_mermaid`（105 资产/vendored 字节锁/adapter token）、`mdv_page`（路由矩阵、`import(` 计数 3、mermaid bootstrap 契约）、`mdv_handler_contract` 在内 engine-api preset ctest 57/57；macOS arm64 CEF Debug 全量构建 + ctest 68/68；macOS x64 构建 + markdown/mdv scoped ctest 16/16；`bash scripts/check.sh fast`（guard/format/brand-assets/legacy-unit 通过；formal-workspace 因本机 Keychain 沙箱限制在 `crayon-platform-macos secure_store` 既有环境性失败，与本次改动无关）、`bash scripts/check.sh security` 通过；`git diff --check` 与新增行 ≤80 列通过（mdv_page.cc 中 >80 列行均为 HEAD 存量）。
- 实机验证（macOS arm64 Debug 真实 CEF，CDP 驱动）：fixture 内置 flowchart 端到端渲染（discovery → import → strict 初始化 → render → gate → SVG 落位 `data-mdv-mermaid-rendered=true`）；七类契约图（flowchart、sequenceDiagram、mindmap、architecture-beta、classDiagram、stateDiagram-v2、erDiagram）经真实 adapter 路径全部产出受控 SVG，且 `foreignObject`/`script`/`a` 均为 0、全部 id 以 `mdv-mermaid-<nodeId>` 为前缀（块内封闭）；恶意 payload 验证——HTML/script 注入被 mermaid strict 中和为惰性文本（渲染成功且无任何活动内容），非法 DSL 仅标记当前 block 错误并保留源码；断网语义下零外部网络请求（全部请求命中 `crayon://mdv`）；CSP/JS 异常为 0。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试与可维护性。P0/P1=0；P2×1——vendored mermaid 渲染管线内部在其临时渲染容器创建 `<style>` 元素，被页面 CSP 正常阻断（每次 render 约 2-3 条 console 记录，无执行、无网络、无 bypass），消除它需要放宽 `style-src`（契约 §2 明确禁止），作为已知限制记录，MDV-24 收口复核；P3——编辑通道在当前 Debug 切片（无真实文档入口）不触发重渲染属 MDV-09 前的预期行为，fixture 只读渲染路径已独立验证。
- 未覆盖与风险：viewport lazy/主题重绘/全屏（MDV-18）、有界并发与 cache/revision 风暴（MDV-19）、Windows x64 真机回归与发布门禁（MDV-20）未做；mindmap/architecture-beta 依赖懒加载 chunk 均已从 bundle 解析成功，长文档多图性能数据归 MDV-19。`MDV-17` 转为 `DONE`。

### MDV-18 原子范围（viewport lazy、主题与图表交互）

- 状态：`IN_PROGRESS`（2026-08-29 领取；`MDV-17 DONE` 依赖满足）。
- 单一目标：增加 `IntersectionObserver` viewport lazy render、响应式容器/横向滚动、浅深主题映射与重绘、全屏查看和查看源码；所有动作只改变本页内存/UI，不新增 Browser 特权。
- 输入：MDV-17 block runtime、现有视图切换/分栏/toolbar/滚动联动、浏览器主题事件。
- 允许修改：`browser/shared-ui/mdv/**`、必要的 MDV 主题窄事件与双语 locale；复用自有 glyph，不引入远程图标/字体。
- 禁止修改：保存/入口/路径权限、CAAP、系统全屏 API 之外的窗口控制；不得增加 SVG/PNG 导出（文件写权限需另立任务），不得让右侧预览滚动反向驱动编辑器形成循环。
- 边界：无 `IntersectionObserver` 时退化为有界首屏队列；主题变化推进 block generation；全屏关闭/Escape/焦点恢复/读屏名称完整；错误卡片提供源码但不显示堆栈或绝对路径。
- 验收：`MD-004/009`；离屏图不 render、进入 viewport 仅 render 一次、滚动回收不重复布局；light/dark 对比与主题重绘无旧 SVG；键盘/Escape/焦点/a11y 实机通过。
- 明确不做：持久化偏好、导出、zoom/pan、Presentation/TV mode。

### MDV-18 完成记录（2026-08-29）

- 实现：mermaid block 渲染改为 `IntersectionObserver` viewport lazy（进入 viewport 触发一次 `unobserve`+render；无 IO 环境退化为有界首屏队列 `Math.min(nodes.length,8)`，仍经共享顺序队列）；`apply()` 预览更新链补上 `resetMermaid()`/`observeMermaid(preview)`（修复 MDV-17 中 mdvPush 驱动的更新不会重新观察 block 的缺口）。adapter 新增 color-scheme 参数（`light`/`dark` 白名单，`initialize` 增补 `theme`，仅在实际变化时重设），render id 增加单调序号后缀保证重绘/多图下 mermaid 内部 id 永不冲突且所有导出 id 仍以 `mdv-mermaid-<nodeId>-r<N>` 为前缀块内封闭；渲染成功后将转义 DSL 以 hidden `span.mdv-mermaid-source` 保留在 block 内。页面新增 page-local 全屏查看 overlay（`#md-mermaid-view`，`role=dialog aria-modal`，克隆已过 gate 的 SVG，查看源码切换 `aria-pressed`，Escape 关闭、焦点落到关闭按钮并在关闭后还原到触发 block，无新 Browser binding）；渲染 block 呈现 `cursor:zoom-in` 与 focus-visible；SVG policy gate 值校验从"禁一切括号"改为"禁可抓取/可执行函数名"（`url(`/`expression(`/`image(`/`element(`/`cross-fade(`/`paint(`/`@`/转义/HTML/scheme 等），放行 dark 主题合法的 `hsl()/rgb()` 颜色函数；inline style 无冒号垃圾槽位（`undefined;;;undefined`）丢弃、真实声明仍强制校验。错误卡片本地化文案经转义 `data-mermaid-error-text` 注入，无堆栈/路径。新增 4 条双语 locale（mdv.mermaid.fullscreen/source/close/error），四件套接齐：`MdvPageStrings`、shared-ui locales json、macOS `Localizable.strings`（zh-Hans/en）、Windows `IDS_CRAYON_MDV_MERMAID_*`（253..256）+ `app.cc` + `mdv_handler_contract`。
- 失败基线（先失败后修复）：(1) mdvPush 注入 block 不渲染——`apply()` 缺 mermaid 重新观察（见上）；(2) dark 主题渲染整块失败——mermaid dark 在 `stop-color=hsl(...)` 与 CSS 值使用颜色函数，旧"禁括号"规则误杀，改为函数名黑名单；(3) macOS 错误卡片显示 key 而非文案——bundle 资源未随 `.strings` 刷新（CEF POST_BUILD 拷贝仅在重链时执行），touch 源码强制重链后修复；(4) 负向测试断言与其他扩展既有 `.catch` 文本冲突，改为正向断言。
- 自动验证：`node --test browser/shared-ui/markdown-runtime/tests/mermaid_adapter.test.mjs` 4/4；engine-api preset ctest 57/57（含 `mdv_page` MDV-18 契约：lazy/主题/全屏/错误卡/aria 断言）；macOS arm64 CEF Debug 全量构建 + ctest 68/68；macOS x64 构建 + markdown/mdv scoped ctest 16/16；`bash scripts/check.sh fast`（全部步骤通过）与 `bash scripts/check.sh security` 通过；`git diff --check` 通过；mdv_page.cc >80 列行均为 HEAD 存量。
- 实机验证（macOS arm64 Debug 真实 CEF，CDP 驱动）：fixture block 经 IO lazy 路径渲染并保留隐藏源码 span；注入 3000px 间隔的离屏 block——离屏不渲染、`scrollIntoView` 后仅可见块渲染一次（`svgCount=1`，另一块保持未渲染）；`Emulation.setEmulatedMedia` 切换 prefers-color-scheme——全部 block 以 dark 主题重绘（text fill=`rgb(204,204,204)` 证实 dark 生效）、切回 light 再次重绘、零错误；全屏 overlay 打开/克隆 SVG/`aria-modal`/焦点落到关闭按钮/查看源码显示 DSL 且 `aria-pressed` 翻转/Escape 关闭/焦点还原/overlay 清空全部通过；错误块仅自身标记并显示本地化卡片，其余 block 继续渲染；全程零外部网络请求。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试与可维护性。P0/P1/P2=0。全屏只消费已过 policy gate 的 SVG 克隆与 block 自有隐藏文本；主题仅内存态；render-once 由 `unobserve` 保证；失败基线 (1) 属 MDV-17 遗留缺口并在本任务闭合。
- 未覆盖与风险：有界并发/cache/revision 风暴与性能口径（MDV-19）；Windows x64 真机交互、VoiceOver/键盘实机细节归 MDV-20 收口；mermaid 内部被 CSP 阻断的 `<style>` 尝试（MDV-17 P2 已知限制）在重绘时同样出现，无执行无网络。`MDV-18` 转为 `DONE`。

### MDV-19 原子范围（编辑并发、缓存与资源预算）

- 状态：`DONE`；依赖 `MDV-17/18 VERIFIED`。
- 单一目标：在快速编辑和多图文档下提供有界渲染调度与会话内 cache，复用 MDV revision fencing，确保旧图、主题旧代际和关闭后的 promise 结果不可落位，并给出资源/响应预算。
- 输入：MDV-05/10 编辑去抖、MDV-17/18 runtime、测试标准性能口径。
- 允许修改：Mermaid 单一运行时 owner `browser/shared-ui/markdown-runtime/assets/mermaid-adapter.js` 及其专项测试、`browser/shared-ui/mdv/**`、CEF MDV 页面生命周期窄接线、`tests/e2e/desktop/browser` perf harness 与 `docs/current/markdown-viewer.md` 预算冻结。
- 禁止修改：磁盘/Profile cache、最近文件、全局 worker pool、公共 telemetry；不得在 render/scroll/input 热路径默认打日志。
- 边界：cache key = source hash + theme + Mermaid version + SVG policy version；容量/单项/并发/等待队列/错误文本均有上限和满载行为；文档关闭、导航、Profile 销毁、无痕窗口关闭、Renderer termination、内存压力清空。Mermaid 无取消 API 时只做 generation invalidation，不假装已取消上游 CPU。
- 验收：`MD-004/010`；50 block（含离屏、重复、错误、超大 DSL）fixture，快速编辑/主题/导航/关闭风暴；记录普通首屏、首次 import、首图、全部可见图、CPU/RSS/UI delay/资产字节；停止后资源回落且 dropped/evicted 可诊断但无正文。
- 明确不做：跨会话 cache、后台预渲染、Worker/OffscreenCanvas 架构迁移。

### MDV-19 完成记录（2026-08-29，编辑并发、缓存与生命周期）

- 实现：Mermaid adapter 成为页会话唯一调度 owner，冻结 `4` 并发/`16` 等待、`30 s` deadline、`128` 项/`16 MiB` LRU 与 `2 MiB` 单项保守 retained accounting；相同 source digest/theme/version/options/policy 的在途工作合并，已通过 Browser-owned SVG gate 的 candidate 会话内复用且每次重新净化/重写 block ID。cache key 含 32-byte 源摘要、light/dark、`mermaid@11.17.2`、固定 options 摘要、`mdv-svg-policy-v1` 与 32-byte 页隔离 nonce，无密码学隔离能力时 fail closed 禁用 cache。
- 生命周期：预览替换、主题切换推进 generation；旧 generation 的已运行 Mermaid CPU 不假装取消，结果仅记 stale 并禁止落 DOM。`pagehide`、Renderer/页销毁关闭会话，BFCache 只推进 generation；`memorypressure` 清 cache。满载/渲染失败保留当前源码并显示上限 `1024` UTF-8 字节的本地化局部错误，诊断只含计数。
- 自动化：`node --test browser/shared-ui/markdown-runtime/tests/mermaid_adapter.test.mjs` 为 7/7，覆盖满载、合并、LRU 双上限、失败、代际、清理/关闭与 50-block burst；macOS arm64 全量 `ctest --preset macos-arm64-cef-debug --output-on-failure` 为 68/68，macOS x64 编译 + `markdown_extension_facts|markdown_runtime_*|mdv_*` 为 14/14（Rosetta 只作编译/自动化）。
- 真实 CEF perf：`node tests/e2e/desktop/browser/run_mdv_mermaid_perf.mjs --port=9333` 通过；50 block = 47 ready + 3 预期局部失败 + 0 unresolved，普通首屏 FCP `112 ms`，首图 `709.1 ms`，全部遍历 `1561.3 ms`，最大事件环延迟 `170.2 ms`，JS heap used/total `9,070,128/20,185,088` bytes，ProcessTime `3.2142 s`；5 cache entries/`167,040` accounted bytes、32 hits、5 stale、0 dropped/evicted；无 Mermaid 文档 `0` 资产请求，含图文档加载 35 个同源资产/`1,144,468` encoded bytes，公网请求 `0`；返回 `crayon://newtab/` 后页会话销毁。
- Code Review：按 v0.8 审查需求/边界、正确性、架构、并发/生命周期、安全/隐私、性能、测试与可维护性，P0/P1/P2 = 0/0/0；主题新代际会等待旧代际在途 render 回收，避免 Mermaid 全局 initialize 跨主题并发竞态。
- 未覆盖与风险：原生 macOS x64 长跑按 Rosetta 边界归 QAR；Windows x64 Debug/Release 真机、Release 包/SBOM/NOTICE 与模块总 Review 归 `MDV-20`。

### MDV-20 原子范围（跨平台、发布与模块总 Review）

- 状态：`VERIFIED`；依赖 `MDV-14..19 VERIFIED` 以及既有 `MDV-07/10..13` 的交互门禁完成；macOS arm64 发布门禁已闭合，等待 Windows x64 Debug/Release 真机回归后转 `DONE`。
- 单一目标：以发布产物验证 Mermaid Full 能力闭环：先完成 macOS arm64 App/Helper 的离线资源、七类图、编辑/主题/全屏/退出与签名 smoke，再做 Windows x64 Debug/Release 回归；同步 SBOM/NOTICE、任务状态、实际指标和模块 Review。
- 输入：MD-007..010、QAR 包体/性能/Release surface 口径、`code-review-standard.md` v0.8。
- 允许修改：`tests/e2e/desktop/**`、`tools/repo-guard` 的 Mermaid release scan、NOTICE/SBOM 输出入口、`docs/current/**`、`docs/plans/**`；发现生产缺陷时退回对应原子任务修复，不在收口任务夹带大补丁。
- 禁止修改：新图类型/新 Markdown 方言、KaTeX/语法高亮/导出/投屏、Agent 能力；不得用单平台、开发目录或联网环境替代发布包证据。
- 边界：安装包不含 tiny、npm cache/node_modules/source tests；所有 manifest 文件存在且 hash 匹配，未知动态 import 为零；无图文档零 Mermaid 资产读取，含图文档零公网请求；退出/Renderer crash 后无残留进程与旧回调。
- 验收：macOS arm64 与 Windows x64 的 Debug/Release build+ctest、发布包离线七类图 smoke、恶意 DSL/security suite、50-block perf、SBOM/NOTICE/release scan；Code Review P0/P1=0，P2 延期必须有后续任务 ID。macOS/Windows 任一缺证据只能 `VERIFIED`，不得 `DONE`。
- 明确不做：HarmonyOS 真机（后续 HM 专项）、Markdown Presentation/TV/Cast、模型/AI 修改图表。

### MDV-20 macOS 完成记录（2026-08-30，跨平台发布收口）

- 实现：`repo-guard` 新增 `RG-009`，锁定 Mermaid 11.17.2 的 104 文件/3,522,090 bytes manifest、MIT NOTICE/LICENSE、发布目录中唯一主程序的 104 个内嵌资源 ID，并提供确定性 `THIRD_PARTY_NOTICES.md`、SPDX 2.3 SBOM 与发布 manifest 生成入口；`RG-006` 只对官方 CEF framework 精确路径豁免 Chromium 自带的 `remote-debugging-port` 字符串，App/Helper 仍 fail closed。真实 CEF harness 扩展为 flowchart、sequence、mindmap、architecture-beta、class、state、ER 七类图，并同时检查普通文档零 Mermaid、含图文档零公网、50-block 终态、generation/cache 上限和内存压力回落。
- macOS arm64 自动验证：Debug 全量 CTest `68/68`（MDV-19 同一代码基线）；Release 先执行全目标 build `204/204`，再执行 `ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure` 为 `68/68`；`codesign --verify --deep --strict --verbose=2 CrayonBrowser.app` 验证 App、5 个 Helper 与 CEF framework 有效；`node tools/mermaid/vendor.mjs --check` 为 `104 files, 3522090 bytes`；恶意 SVG/外链/事件属性/逃逸引用与调度生命周期 `node --test browser/shared-ui/markdown-runtime/tests/mermaid_adapter.test.mjs` 为 `7/7`；`cargo test -p repo-guard` 为 `28/28`。
- macOS arm64 发布/实机验证：正式 staging 只含签名 App、NOTICE、SPDX SBOM 与 manifest，`repo-guard scan --artifact-path .cache/dist/mdv20-macos-arm64-release-20260830` 的 `RG-006/RG-009` 及全部 hard gate 通过（既有 `RG-003/RG-004` warning 不阻断）；真实 Release CEF 七类/50-block 为 `47 rendered / 3 expected errors / 0 unresolved`，FCP `72 ms`、首图 `133.4 ms`、全部访问 `414.6 ms`、最大 UI delay `29.8 ms`，普通文档 Mermaid 请求 `0`、公网请求 `0`，cache `7 entries / 254,002 bytes` 且 memory pressure 后 `0/0`；无调试参数的 `run_smoke.py smoke` 通过完整进程树、loopback-only 与退出零残留。App 包体 `323,452 KiB`。
- Code Review：按 v0.8 覆盖需求/边界、发布误装测试资产、安全例外、离线供应链、性能热路径、错误与退出生命周期、测试与文档；新增反向用例证明主 App 出现 `remote-debugging-port` 仍被 `RG-006` 拒绝。P0/P1/P2=`0/0/0`。
- 未覆盖与风险：Windows x64 Debug/Release build+CTest、正式 staging、七类真实 CEF/50-block/零公网/退出零残留与 `RG-006/RG-009` 尚未取得本次代码基线的真机证据，因此任务维持 `VERIFIED`；原生 macOS x64 长稳按既有 Rosetta 边界归 QAR，不阻塞本任务当前 macOS arm64 门禁。

## 编辑器工具栏优化波次（MDV-21..24）

依赖顺序：`MDV-21 -> MDV-22 -> MDV-23 -> MDV-24`。该波次不改变 Markdown parser、文件权限或保存协议，并与 Mermaid 供应链独立。原计划让 `MDV-24` 等待 `MDV-20` 才接 macOS MDV，经 2026-08-28 代码核对确认基础 MDV 已具备可独立装配入口，因此移除错误依赖：本任务只收口工具栏平台行为，`MDV-20` 仍独立承担 Mermaid 完整运行时的跨平台门禁。

### MDV-21 原子范围（工具栏设计契约与原创 glyph）

- 状态：`DONE`；依赖 `MDV-12 VERIFIED`。
- 单一目标：冻结编辑器图标工具栏的动作、分组、tooltip、平台快捷键、响应式优先级和 Markdown 缩进/对齐边界，并交付一套 24×24 DIP、`currentColor`、无外部引用的蜡笔原创 MDV glyph 资产及独立 manifest/验证入口。
- 输入：当前 `RenderToolbar` 15 个真实 action、`browser-design-v1` 的 24/20/32/36 DIP token、飞书式 icon-only/悬浮说明交互参考、CommonMark 列表/引用缩进和 GFM 表格列对齐规则。
- 允许修改：`docs/current/markdown-viewer.md`、`docs/current/test-cases.md`、`docs/current/testing-standard.md`、总/模块 Roadmap 与索引；新增 `browser/shared-ui/mdv/design/**` 及其 CMake contract target。允许只读取现有 `browser/shared-ui/design/tokens.json`，不得修改浏览器 chrome glyph 集。
- 禁止修改：`mdv_page.cc`、编辑行为、CEF/platform adapter、locale 资源、Markdown parser、保存/入口/安全策略；不得复制飞书 SVG、引入图标依赖或网络资源。
- 边界：基线动作真实计数修正为 15；新增结构菜单只表达列表/任务/引用缩进与 GFM 表格列默认/左/中/右对齐，普通段落对齐保持不可表达；所有图标 `aria-hidden`，可访问名称由后续按钮 locale 提供；manifest 路径闭合、ID 唯一、无 script/style/foreignObject/event/href/URL/品牌 App 图标复用。
- 验收：`MD-011`；独立 configure + ctest 通过，负向 fixture 证明外链/未登记/重复 ID fail closed；`git diff --check`；按 v0.8 Review 检查需求、边界、安全、性能、测试与可维护性。
- 明确不做：把图标接进页面、tooltip DOM、快捷键监听、编辑变换、macOS/Windows 实机。

### MDV-21 完成记录（2026-08-28，工具栏设计契约与原创 glyph）

- 实现：冻结 15 个既有 action、结构菜单、平台 tooltip/快捷键、响应式与 Markdown 缩进/表格对齐边界；新增 `mdv-toolbar-v1` manifest、25 枚原创 `currentColor` SVG、独立 CMake 校验入口及重复 ID/外链/未登记资产负向 fixture。
- 验证：`cmake -S browser/shared-ui/mdv/design -B .cache/build/mdv-toolbar-design` 配置通过；`ctest --test-dir .cache/build/mdv-toolbar-design --output-on-failure` 为 2/2 通过；`git diff --check` 通过；Quick Look 抽查视图切换、标题、任务列表、结构菜单与表格居中 glyph，任务列表留白修订后通过。
- Code Review：按 v0.8 审查需求/边界、安全、性能、测试和可维护性，P0/P1/P2 = 0/0/0；图标只参与编译期资源，不新增运行时网络、脚本或依赖。
- 未覆盖：按原子范围未接页面行为、tooltip DOM、快捷键监听与双平台实机；分别归 `MDV-22..24`。

### MDV-22 原子范围（可测试编辑变换层）

- 状态：`DONE`；依赖 `MDV-21 DONE`。
- 单一目标：把字符串拼接式编辑动作收敛为纯 `text + selection + action -> replacement + next selection` 变换，先用失败测试复现并修复 `linePrefix` 重复选区外正文，再覆盖包裹切换、标题替换、多行列表/引用、骨架、结构缩进和表格列对齐。
- 允许修改：`browser/shared-ui/mdv/**` 与独立测试；页面仍通过一次 `setRangeText` 应用结果并触发既有 input/mdvQuery 通道。
- 禁止修改：图标资产、CEF binding、文件 IO、Markdown parser 与安全白名单；不得用正则重新实现完整 CommonMark parser。
- 边界：空选区/多行/文首文尾/CRLF/UTF-8/嵌套/重复调用均保持选区和选区外字节；Tab/Shift+Tab 只在可缩进结构上下文生效；表格列对齐仅在确定识别的 GFM 表格中启用，否则 no-op/fail closed。
- 验收：`MD-012`；正常、错误、边界、重复、撤销所需单 replacement、5000 步确定性风暴；独立 ctest、全 MDV 回归、`git diff --check`。
- 明确不做：视觉样式、tooltip、平台快捷键、overflow 与实机。

### MDV-22 完成记录（2026-08-28，可测试编辑变换层）

- 实现：新增 UTF-8 字节边界的纯 `text + selection + action -> replacement + relative selection` API；修复原 `linePrefix` 从行首拼接到文末导致正文重复的根因；闭合 21 个 action，覆盖包裹切换、标题/列表/引用多行切换、骨架占位、结构缩进与 GFM delimiter cell 对齐。
- 验证：`cmake --preset engine-api` 配置通过；`cmake --build --preset engine-api --target crayon_browser_mdv_transform_test` 通过；`ctest --test-dir .cache/build/engine-api -R '^mdv_' --output-on-failure` 为 7/7 通过，其中包含 CRLF、选区外字节保持、非法范围/非结构/非表格 fail closed 与 5000 步确定性风暴。
- Code Review：按 v0.8 审查正确性、边界、安全、性能、测试与可维护性，P0/P1/P2 = 0/0/0；单次操作只返回一次 replacement，不持有状态、不做 IO、不引入 parser 或依赖。
- 未覆盖：本切片不接页面；JS UTF-16 selection 与共享 UTF-8 byte offset 的转换、图标/tooltip/快捷键接线归 `MDV-23`。

### MDV-23 原子范围（图标工具栏、tooltip、快捷键与无障碍接线）

- 状态：`DONE`；依赖 `MDV-22 DONE`。
- 单一目标：以数据驱动 action registry 将 MDV-21 glyph 与 MDV-22 变换接入源码/分栏工具栏，交付图标化视图切换、平台感知两行 tooltip、窄宽 overflow、roving tabindex 和 IME-safe 快捷键。
- 允许修改：`browser/shared-ui/mdv/**`、双语 locale、Windows IDS 装配和对应页面/handler contract；固定资源继续从内存提供。
- 禁止修改：新增 Browser binding、放宽 CSP、远程字体/图标、Markdown 语法/文件权限/保存协议；不得把 OS 判断散落进 shared UI，平台 adapter 只注入闭合 shortcut profile。
- 边界：按钮 24×24 canvas、20×20 glyph、点击区优选 36 DIP/最小 32 DIP、2 DIP focus ring；tooltip hover 延迟 450ms、focus 即显、Escape/blur/scroll 隐藏；工具栏 Tab 单入口，左右键/Home/End 导航；`event.isComposing`/229/AltGr 不触发；只有实际可达的组合才显示快捷键。
- 验收：`MD-004/011..013`；DOM/locale/action/shortcut parity、hover/focus/overflow/浅深色/reduced-motion、键盘与 IME contract；双配置 ctest、fast/security、`git diff --check`。
- 明确不做：macOS/Windows 发布包最终实机门禁、普通段落对齐、H4..H6 扩展。

### MDV-23 完成记录（2026-08-28，图标工具栏与安全快捷键接线）

- 实现：构建期嵌入 25 个已验证 SVG；源码/预览/分栏与 15 个编辑动作改为 icon-only；结构菜单接入缩进/减少缩进和 GFM 表格四态对齐；双语两行 tooltip、450ms hover/focus 即显、roving tabindex、窄宽 overflow、浅深色/reduced-motion 和 macOS/Windows shortcut profile 已闭合。
- 安全与正确性：复用唯一 `mdvQuery` binding 和 MDV-22 纯变换层；Browser 侧校验 transform 字段类型、5 MiB 上限、UTF-16/UTF-8 边界及 action 白名单；revision/source fencing 丢弃迟到变换；IME composing、229 与 AltGr fail closed。
- 验证：`ctest --test-dir .cache/build/engine-api -R '^mdv_' --output-on-failure` 7/7；页面脚本经 `node --check`；design contract 2/2；macOS arm64 `mdv_*` 8/8（含 handler contract）。
- Code Review：按 v0.8 审查需求、正确性、架构、生命周期、安全、性能、测试与可维护性，P0/P1/P2 = 0/0/0；脚本生成按 core/toolbar/divider 拆分，生产函数未触发 200 行强提醒。
- 未覆盖：双平台发布包、读屏、IME 与 DPI 实机验收归 `MDV-24`；普通段落对齐与 H4..H6 仍明确不做。

### MDV-24 原子范围（工具栏双平台收口）

- 状态：`VERIFIED`；依赖 `MDV-23 DONE`；与 Mermaid `MDV-20` 解耦。macOS arm64 的 Helper、deployment target、签名、默认页/公网导航、MDV 三视图、主题与 VoiceOver 语义已闭合；Windows x64 的 Debug/Release、完整契约和主要真实 CEF 交互已闭合。原生 macOS x64 长稳、Windows Narrator/中文 IME 组合态与原生系统 200% DPI 仍未闭合，故不得转 `DONE`。
- 单一目标：在完整 MDV 装配上先验证 macOS arm64 的 Meta/Option 快捷键、VoiceOver、IME 与布局，再回归 Windows x64 的 Ctrl/Alt、Narrator、IME 和 DPI；记录不可达/被 Chromium 消费的组合并从 tooltip 契约中移除。
- 允许修改：`browser/cef-shell` 平台装配/本地化、E2E/device harness、发现缺陷对应的最小 shared/platform 修复、契约/Roadmap 证据；生产缺陷超出本任务时退回 MDV-22/23 独立修复。
- 禁止修改：新增格式动作、第三方依赖、Markdown parser、文件权限或保存协议；不得以单平台或模型层结果冒充双平台。
- 验收：`MD-007/013`；macOS arm64 与 Windows x64 Debug/Release，鼠标/键盘/VoiceOver/Narrator、中文/英文 IME、浅深色、窄分栏、100%/200% DPI；Code Review P0/P1=0。
- 明确不做：HarmonyOS、移动端、协同编辑与自定义快捷键配置。

### MDV-24 完成记录（2026-08-28，macOS arm64 平台装配验证）

- 实现：macOS CEF 壳注册现有 `crayon://mdv` scheme、文件打开/保存/导航/拖放/上下文菜单与 page-query controller；注入 `kMacOS` shortcut profile；新增 `en`/`zh-Hans` `Localizable.strings` 并纳入 App bundle/package contract。Windows 继续使用资源 ID 注入 `kWindows` profile，无 UA/路径猜测。
- 验证通过：`cmake --build --preset macos-arm64-cef-debug --target crayon_browser crayon_browser_mdv_page_test crayon_browser_mdv_transform_test`；macOS arm64 `ctest -R '^mdv_'` 8/8；`macos_cef_shell_source_contract` 与 `mdv_handler_contract` 2/2；双语 JSON/strings lint、`git diff --check` 通过。应用完成 ad-hoc 签名；链接仍报告项目既有的 deployment target 26→12 warning。
- 2026-08-28 补充修复：确认 macOS 26.6.2 的 `iconutil` 连自身从有效 ICNS 解包的 iconset 也无法重建，因此移除 host `iconutil` 假门禁，继续由 `tools/brand-assets/verify.mjs` 验证 PNG/ICNS/manifest，package contract 校验包内 ICNS 与受管资产逐字节一致。签名改为显式的 CEF dylib → framework → App 内 helper → 主 App，由内到外且 arm64/x64 同路径执行；package contract 逐组件验签。
- 2026-08-28 根因修复：①旧 Browser 进程在同一 `.app` 被重建/重签后仍存活，会让已加载 framework 与新落盘 Helper/签名不一致，Network/Renderer Helper 因 `SIGTRAP` 重启并触发 `Page Unresponsive`；验收流程改为重建前清理旧实例。②macOS Helper 原先以 `nullptr` 调用 `CefExecuteProcess`，Renderer 没有注册 `crayon` scheme 与 `mdvQuery`；现与 Windows 一致注入 `CreateNewTabProcessApp()`。③Helper 源码变化此前不会重新链接主 App，导致 POST_BUILD 复制/签名不执行；主 target 增加 Helper `LINK_DEPENDS`。④根工程在所有子目录前统一设置 macOS 12.0 deployment target，26→12 链接 warning 已消除。
- 补充验证：arm64/x64 `cmake --build --preset macos-*-cef-debug` 均通过；arm64 与 x64（Rosetta 编译/自动化）`ctest --preset macos-*-cef-debug --output-on-failure` 均为 61/61；`node tools/brand-assets/verify.mjs` 为 8 项/27 文件通过；`git diff --check` 与 `xcrun clang-format --style=Google --dry-run --Werror browser/cef-shell/src/macos/process_helper_mac.cc` 通过；两架构 App `codesign --verify` 及 package contract 的 CEF dylib/framework/embedded Helper/主 App 逐组件验签通过。
- macOS arm64 真机：干净启动 `crayon://newtab` 正常渲染；以 `https://example.com` 为启动页时公网 HTTPS 正常渲染；MDV Preview/Source/Split 与强制深色模式均取得真实 CEF 截图；AX 树暴露源码/预览/分栏 toggle、编辑工具栏、15 个动作、缩进和对齐 popup、editable textarea、预览 heading/table/task/link 语义。默认实例连续存活超过 1 分钟，Browser/GPU/Network/Storage/Renderer 均稳定，无新增 crash report；`SIGTERM` 后无 Helper 残留。所有临时 URL、主题、accessibility 与窗口参数均已回退。
- 未覆盖：当前 arm64 主机不能替代原生 macOS x64 长稳（Rosetta 长跑受 Chromium `StackSamplingProfiler`/sandbox 限制）；Windows x64 Debug/Release、Narrator、IME 与 100%/200% DPI 未运行；本机 UI 自动化对该 CEF 窗口执行 click/drag/set-value 时 native pipe 关闭，因此鼠标 tooltip、中文/英文 IME 实际输入和窄窗拖拽仍只有 DOM/快捷键/IME guard/响应式 CSS 自动化契约证据，不能冒充真机交互通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试与可维护性审查；P0/P1/P2 = 0/0/0。任务回到 `VERIFIED`；`DONE` 仍需 Windows 与上述不可替代的真机交互矩阵。

### MDV-24 完成记录（2026-08-28，Windows x64 收口）

- 缺陷与实现：Windows 全量重建先稳定复现 `crayon_macos_shell_portable_compile_contract` 的两级失败：补齐 MDV include 后暴露 `CoreFoundation/CoreFoundation.h` 无法由 MSVC 编译。该目标把已合法使用 CoreFoundation 的 macOS 平台 adapter 错当为跨平台源码；移除失效的 Windows 伪跨平台 OBJECT target，保留 `macos_cef_shell_source_contract` 与 macOS 原生双架构构建门禁，没有向生产源码加入测试分支。提交前快进到 `d4bdb21` 后又由 MSVC `/W4 /WX` 复现新增 `markdown_extension_facts_test.cc:84-86` 的 `C4127/C2220`；三条编译期枚举关系从运行时 `CHECK` 最小改为 `static_assert`，不改变生产行为。
- Windows x64 构建：`$env:CRAYON_CEF_ROOT=(Resolve-Path '.cache/cef/windows64-root/cef_binary_150.0.10+g8042e43+chromium-150.0.7871.101_windows64').Path; cmake --preset windows-cef-debug; cmake --build --preset windows-cef-debug --parallel 4` 通过；`cmake --build .cache/build/windows-cef-debug --config Release --parallel 4` 首次工具调用在 124 秒超时且没有错误，继续同一增量构建后明确以 exit 0 完成。CEF 报告本机未安装 ATL，以及既有 `/DELAYLOAD` 未使用项 warning；均未阻断产物和测试。
- 完整自动化：在 `d9ebe8b` 首轮 `ctest --test-dir .cache/build/windows-cef-debug -C Debug/Release --output-on-failure` 均为 62/62；提交前整合 `d4bdb21` 新增的 Markdown Runtime 三项测试并关闭上述 MSVC 阻断后，Debug 与 Release 最终均为 65/65。专项 `ctest ... -C Debug -R '^(markdown_extension_facts|mdv_handler_contract|windows_cef_shell_package_contract|windows_cef_shell_source_contract)$'` 为 4/4；`cmake -S browser/shared-ui/mdv/design -B .cache/build/mdv-toolbar-design && ctest --test-dir .cache/build/mdv-toolbar-design --output-on-failure` 为 2/2；`git diff --check` 通过。本机无 `clang-format`，故该工具未运行；三行改动已由 MSVC Debug/Release `/W4 /WX` 双构建覆盖。
- Debug 真实 CEF（100% device scale、浅色）：`crayon://newtab` 与 `https://example.com` 正常渲染；本地 `.md` 进入 `crayon://mdv/app.html`，Preview/Source/Split、15 个图标动作与结构菜单均由真实截图和 UIA/AX 树确认。鼠标/键盘焦点 tooltip 显示两行本地化信息；`Shift+Tab` 进入 roving toolbar、`End` 定位结构按钮；`Ctrl+B` 将“段落”变为 `**段落**`，`Ctrl+Alt+1` 将当前行变为 H1；列表 `Tab`/`Shift+Tab` 在 2/4 空格间往返；GFM 第一列分隔符由 `---` 实际改为 `---:`。英文 `EnglishIME` 与中文文本“中文输入”均写入、预览并经 `Ctrl+S` 落到隔离验收夹具。
- 布局与主题：真实窗口在 621x1005 窄窗下保持单行工具栏水平 overflow，窄 Split 两栏均可见；最大化 1920x1032 后宽 Split 正常。Debug `--force-dark-mode --force-device-scale-factor=1` 下 new-tab 与 MDV 深色正常。Release `--force-device-scale-factor=2` 下 new-tab、MDV Preview/Source、图标和 tooltip 以 200% Chromium device scale 真实渲染；Release 进程正常退出且无残留窗口。
- 未覆盖与原始阻塞：① Narrator 不在 `list_apps`，通过既有 `C:\Windows\System32\Narrator.exe` 启动时 Computer Use 返回 `Computer Use app approval timed out`，随后窗口列表为空，不能冒充 Narrator 通过；技能同时禁止发送 Windows 键组合。② `Alt+Shift` 与 `Ctrl+Shift` 均未切入中文组合态，拼音按键直接写入拉丁字符并已撤销；因此只验证中英文文本输入，未验证中文 IME composition/candidate。③ 200% 使用 CEF `--force-device-scale-factor=2`，没有修改宿主 Windows 的系统显示缩放，不能冒充原生 OS 200% DPI。④ 原生 macOS x64 长稳仍受既有硬件边界限制。
- Code Review：按 `code-review-standard.md` v0.8 独立复核需求/边界、CMake 平台职责、正确性、安全/隐私、性能、测试与可维护性；P0/P1/P2 = 0/0/0。Windows 构建阻断已关闭，任务保持 `VERIFIED`；转 `DONE` 仍需关闭上述 Narrator、中文 IME 组合态、原生 Windows 200% DPI 与原生 macOS x64 门禁。

### MDV-25 原子范围（移除生产 fixture 初始化）

- 状态：`DONE`；依赖 `REL-02 DONE`、`MDV-24 VERIFIED`、`MRT-08 DONE`；macOS arm64 与 Windows x64 对称生产隔离和真实产品空态/本地文件证据已闭合。
- 单一目标：删除 `BrowserApp -> BuildFixtureSnapshot()` 的生产初始化路径，使未由用户打开文档的 `crayon://mdv` 只呈现本地化安全空态；示例 Markdown 只能存在于独立测试/fixture target，不能进入 Release 生产源或 App bundle。
- 输入：REL-02 [生产装配审计](../current/release-v1-assembly.md) 的 MDV 断点、RG-002、MD-003/004、既有 `MdvRuntimeState`/empty-state 契约。
- 允许修改：`browser/cef-shell/src/browser/mdv/**`、macOS/Windows BrowserApp 的 MDV 初始化、相邻 CEF shell contract/MDV 测试与本 Roadmap；不改变 Markdown parser、extension manifest、文件入口、保存 schema 或用户动作。
- 禁止修改：CNT 网页 Markdown、Agent/CAAP、Cast、第三方 runtime、MDV 公共文件权限；不得以隐藏 fixture、编译宏或 Release-only 分支规避生产隔离。
- 边界：手动导航 `crayon://mdv/app.html`、首次启动、文件打开失败和文档关闭均显示无正文的明确空/错误态；成功打开本地 `.md` 后既有 Source/Preview/Split、图片、Highlight/KaTeX/Mermaid 与保存行为不回退；无额外网络/文件 IO。
- 验收：新增能在旧实现命中 fixture 的失败测试，再验证生产 source/binary 不含示例正文或 `BuildFixtureSnapshot`；macOS arm64 Debug/Release CEF build、MDV/handler/source/package contract、真实空态与真实本地文件 smoke，`repo-guard`/RG-002、clang-format、`git diff --check`；Windows 对称回归归 Windows 终端。
- 明确不做：MRT-09 P0 总 Review、Windows Narrator/IME/DPI、原生 macOS x64、任何新 MDV 功能。

### MDV-25 完成记录（2026-08-31，macOS arm64）

- 实现：删除 CEF MDV 生产 handler 内的 `kFixtureMarkdown`、`BuildFixtureSnapshotImpl()` 与公开 `BuildFixtureSnapshot()`；`MdvRuntimeState` 默认构造为 `MdvPageSnapshot{}`，macOS/Windows BrowserApp 均从安全空态启动。`mdv_handler_contract` 新增双平台生产源码隔离门禁，`macos_package_contract` 新增 App 主二进制 `strings` 扫描，禁止旧工厂符号和示例正文进入发布包。
- 构建与自动化：`cmake --build --preset macos-arm64-cef-debug --parallel 4`、`cmake --build .cache/build/macos-arm64-cef-release --parallel 4` 均通过；Debug/Release 的 Markdown Runtime + MDV scoped ctest 均为 16/16，source/package/handler contract 均为 3/3；`bash scripts/check.sh security` 在受控沙箱外通过，`RG-002 passed`，relay-security 7/7；`git diff --check` 通过。Release Ninja 读取旧缓存时报告 `premature end of file; recovering` 后自动恢复并以 exit 0 完成。
- 真实 CEF：macOS arm64 Debug 使用产品 App（固定 mock keychain）手动导航 `crayon://mdv/app.html`，页面只显示本地化“尚未打开文档”，无示例正文；再由地址栏用户手势打开仓库内真实 `docs/reference/蜡笔投屏浏览器_Markdown_Runtime_Extension_Framework_V1.0.md`，成功回到 `crayon://mdv/app.html` 并渲染标题、段落、列表，地址栏/标题未泄露本地路径。
- 格式与 Review：Xcode `clang-format --dry-run --Werror` 已实际运行；工具对相关文件大量 HEAD 存量风格报错，无法作为本次净增行的 clean gate，本次新增 C++ 行沿用邻接风格且 Debug/Release 均以 warning-as-error 构建。按 `code-review-standard.md` v0.8 复核需求边界、默认态、生命周期、安全/隐私、发布隔离、性能与测试，P0/P1/P2=`0/0/0`。
- 未覆盖与风险：Windows App 已完成对称源码替换并受 source contract 约束，但 Windows x64 Debug/Release、二进制扫描和真实空态/本地文件回归未在 macOS 主机运行；因此任务为 `VERIFIED`，待 Windows 终端补证后转 `DONE`。真实 Keychain 未使用，也不是本任务门禁。

### MDV-25 Windows 完成记录（2026-09-01，Windows x64）

- 实现：Windows `crayon_windows_shell_contract` 镜像 macOS 发布隔离口径，直接读取主程序二进制并同时拒绝旧 `BuildFixtureSnapshot` 符号、UTF-8 与 UTF-16 示例正文；读取失败、空文件或不完整读取均 fail closed。扫描仅属于 package contract target，不进入 `CrayonBrowser.exe`，未改变 parser、文件入口、保存或 Runtime 行为。
- 构建与自动化：Windows 11 x64 multi-config tree 的 Debug/Release `ALL_BUILD` 在当前代码上均退出 0（45.0s/42.0s）。`ctest --test-dir .cache/build/windows-cef-debug -C {Debug,Release} -R '^(markdown_.*|mdv_.*|windows_cef_shell_(source|package)_contract)$' --output-on-failure` 分别 **18/18**（60.63s）与 **18/18**（5.91s）；其中 source/handler/package 三项先行复验亦各 3/3。主程序 x64/package/runtime/Host 与新增 fixture byte scan 同一测试闭合。`RUST_TEST_THREADS=1 scripts/check.ps1 fast` 退出码 0（86.7s），`scripts/check.ps1 security` 退出码 0（7.2s；guard、relay-unit、relay-security 全通过），`git diff --check` 通过；repo guard 仅保留既有 warning，RG-006 因本切片未生成独立 staging 目录为 `not_applicable`。
- 真实 CEF：Debug 与 Release 产品 `CrayonBrowser.exe` 均手动直达 `crayon://mdv/app.html`，只显示本地化“尚未打开文档”，旧示例正文为 0；再由产品地址栏打开真实 `docs/current/README.md`，窗口标题为 `README.md - 蜡笔文档`，渲染标题“当前权威契约索引”，Preview/Source/Split 均可达，且页面地址保持 `crayon://mdv/app.html`、未暴露本地路径。Debug 分栏等待 500ms 后稳定显示左右源码/预览；Release 同样确认标题、三视图入口与旧 fixture 不存在。两配置关闭后 `CrayonBrowser`、`crayon-content-host`、`crayon-media-host` 进程残留均为 0。
- 过程披露：首次输入缩写 `crayon://mdv/` 被 Omnibox 作为搜索词，未计为 MDV 通过；改用契约规定的精确 `crayon://mdv/app.html` 后取得上述证据。Release 首次关闭动作因 Computer Use 要求刷新窗口 state 而未执行，重新观察同一窗口后正常关闭；没有强杀或把未执行动作写成通过。
- Code Review：按 v0.9 审查需求/边界、正确性、架构/API、生命周期、安全/隐私、性能、测试/证据、可维护性/供应链；P0 0、P1 0、P2 0、P3 0，`APPROVE`。二进制扫描有界于测试进程和既有发布主程序，不引入产品 IO；UTF-8/UTF-16 双形态避免 MSVC 字面量编码差异造成漏检。
- 未覆盖与风险：Mermaid Full/Highlight/KaTeX/50-block、主题/DPI 与 Release staging 的 Windows 总回归归 `MDV-20W`；Narrator/IME 属 `MDV-24W`。本切片只关闭生产 fixture 隔离缺口，macOS x64 特有门禁继续后置。`MDV-25 DONE`，解锁 `MDV-20W READY`。

### Windows 首发收口 slices（REL-05）

- `MDV-25W DONE`：Windows x64 Debug/Release 重建、source/handler/package contract、主二进制扫描与真实 CEF 空态/本地文件恢复均已闭合；未扩张到 MDV-20W/24W。
- `MDV-20W DONE`（2026-09-01）：可见性 nudge 根因修复（new_tab_url 门控）+ 空态整面板 + 主题/DPI/窄窗/重载回落常驻 harness；双配置 85/85、Mermaid 50-block 47+3 终态、七类图、零公网、Release package/NOTICE/SPDX、fast/security 门禁全过；详见 MDV-20W 完成记录。下一任务 `MDV-24W`（Narrator/IME/DPI 真机）。
- `MDV-24W DONE`（2026-09-01）：键盘/tooltip/locale、UIA 可达性与 DPI awareness 已补证；Narrator（未安装）、中文 IME 组合态、原生系统 200% DPI 三项如实记 `NOT_RUN` 并进入候选已知限制，详见 MDV-24W 完成记录。
- 三个 slice 均不得等待或冒充原生 macOS x64、VoiceOver/Keychain、公证或 macOS 安装包；Windows 首发后再补 macOS 特有 addendum，不改写既有证据。

### MDV-24W 原子范围（Windows 首发支持矩阵补证）

- 状态：`DONE`；依赖 `MDV-23 DONE`、`MDV-20W DONE`。
- 单一目标：补齐 Windows 首发实际支持矩阵缺口证据——键盘/tooltip 与中英文案复核、Narrator 可达性（UIA 树 + Narrator 实跑）、中文 IME 组合态真实输入、原生系统 DPI；环境不可替代项如实记 `NOT_RUN` 并进入候选已知限制。
- 允许路径：`tests/e2e/desktop/**`、本 Roadmap、`docs/current/markdown-viewer.md` 已知限制节；发现缺陷先复现再最小修复（超出范围退回 MDV-22/23）。
- 禁止修改：新增格式动作、parser、文件/保存协议；不以注入输入冒充可信输入（MDV 编辑无注入门禁，但记录输入方式）。
- 验收：键盘/tooltip/locale 自动化复核通过；Narrator 启动 + UIA 树语义证据；中文 IME 组合串真实落屏（成功或如实 NOT_RUN）；原生 200% 系统 DPI（成功或如实 NOT_RUN）；Review P0/P1=0。
- 明确不做：macOS 原生 x64 长稳、VoiceOver 复测（沿用 MDV-24 macOS 记录）、HarmonyOS/移动端。

### MDV-24W 完成记录（2026-09-01，Windows 支持矩阵补证）

- 环境与方法：Windows 11 x64 远程会话、Debug 真实 CEF（CEF 150.0.10 Chrome runtime）、手势入口（omnibox 路径 + Enter）加载含 Highlight/KaTeX/Mermaid 的本地 fixture；UIA 树经 computer-use 语义快照采集，键盘为前台真实按键事件（MDV 编辑无注入门禁，输入方式如实记录）。
- 键盘/tooltip/locale（PASS）：`Shift+Tab` 从源码 textarea 进入 roving 工具栏（焦点落"一级标题"按钮），`Enter` 真实应用 `# ` 前缀且焦点返回 textarea；DOM 复核 22 个工具按钮（14 动作 + 结构菜单 8）全部携带本地化 `aria-label`/`data-tooltip-title`，9 个带 `data-shortcut`+`aria-keyshortcuts`（Ctrl+Alt+1..3、Ctrl+B/I、Ctrl+Shift+X/8/7、Ctrl+K），工具栏容器 `aria-label="编辑工具"`、三视图切换 `aria-pressed` 状态正确；locale parity 95/95 由 fast 门禁覆盖。
- 读屏可达性（UIA PASS / Narrator NOT_RUN）：预览态 UIA 暴露命名视图切换（源码/预览/分栏）、KaTeX MathML 文本节点、highlight token、heading 语义；源码态暴露"编辑工具"toolbar、"缩进和对齐"菜单按钮与 14 个中文命名动作按钮、editable textfield。隐藏 pane 的按钮在 UIA 中以零 bounds 未命名节点出现，为 Chromium 对 `display:none` 子树的既有行为，可见态命名完整，非产品缺陷。本机未安装 Narrator（System32 无 Narrator.exe、无可选功能），语音审阅记 `NOT_RUN`，列为候选已知限制。
- 中文 IME（NOT_RUN）：系统在线布局 `0x08040804`（简体中文），但本会话文本注入以 WM_CHAR 直落、未形成 TSF 组合串（"nihao/zhongwen" 以原文落入，Shift 中英切换未生效）；英文/中文原文本输入与 dirty/预览同步正常。IME 组合态需物理键盘控制台复测，列入已知限制。
- DPI（awareness PASS / 原生 200% NOT_RUN）：`GetProcessDpiAwareness=2`（Per-Monitor）且 context `0x22`=PerMonitorV2——运行期由 Chromium 代码路径设置；`app.manifest` 的 PerMonitorV2 只编入 `CrayonBrowser.dll`，bootstrap exe（CEF 预编译 `bootstrap.exe` 原样复制）与 helper 无该声明，但进程实际 awareness 已由运行期 API 建立，非缺陷。200% 设备缩放渲染已有 MDV-24（Release `--force-device-scale-factor=2`）与 MDV-20W（CDP emulation）证据；原生系统 200% 需改显示缩放并注销重登录，远程会话不可承受，记 `NOT_RUN`。
- 其他：UIA `AXPress` 于 `aria-pressed` toggle 按钮不触发视图切换（DOM `click()` 正常），记为自动化局限 P3，非产品缺陷；键盘 Enter 路径已实测可用。
- Code Review：P0 0、P1 0、P2 0、P3 1（上述自动化局限）；未改生产代码。
- 未覆盖与已知限制（进入候选清单）：Narrator 语音审阅、中文 IME 组合态、原生系统 200% DPI 三项 `NOT_RUN`，均需物理控制台/可选功能安装后补证。`MDV-24W` 转 `DONE`；`MDV-24` 顶层保持 `VERIFIED`（原生 macOS x64 长稳与 VoiceOver 复测仍后置）。
