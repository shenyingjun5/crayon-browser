# LOC：桌面浏览器三语言本地化 Roadmap

- 版本：`desktop-localization-v1`
- 日期：2026-09-02
- 状态：`LOC-01/03/04/05W/06W DONE`；`LOC-02 VERIFIED`（待独立繁体语言审校）；`LOC-07W BLOCKED`；`LOC-08M VERIFIED`（macOS arm64 双配置/完整 CTest/本地 artifact 已补证）；其余任务按依赖 `TODO`
- 任务数：10
- 当前发布顺序：Windows 10/11 x64 先完成 `LOC-01..07W` 并进入第一期候选；macOS `LOC-08M/09M` 后续验证，不能用 Windows 证据替代
- 支持语言：简体中文 `zh-CN`、繁体中文 `zh-TW`、英文 `en-US`

## 1. 目标与当前事实

本 Roadmap 把三语言本地化作为第一期三大闭环的横切发布质量，而不是第四条业务闭环。目标是在同一 Windows x64 候选包中，使 CEF/Chromium 原生 UI、蜡笔自有页面、平台原生窗口与可访问名称使用同一进程级语言快照，并按用户首选系统 UI 语言在下次启动时自动选择；macOS 共享实现和三套资源同时保持可接入，平台特有真机门禁按当前 Windows-first 决策后置。

当前代码事实：

- `browser/shared-ui/locales/{en-US,zh-CN,zh-TW}.json` 已具备 155-key parity，并由确定性 generator 生成共享 C++ catalog、Windows RC 与 macOS strings；繁体语言人工审校仍待 LOC-07W。
- Windows Browser process 已在 `CefInitialize` 前读取用户首选 UI 语言，并以同一不可变 snapshot 设置 `CefSettings.locale`/`accept_language_list` 及自有产品 catalog；Release 已闭合为三套支持语言及必要 gender pak。
- macOS Browser process 已接入 `CFLocaleCopyPreferredLanguages` adapter，并以同一共享 snapshot 设置 CEF locale/Accept-Language 和产品 catalog；bundle 构建只消费 generator 的 `en/zh-Hans/zh-Hant.lproj`。macOS arm64 双配置构建/完整 CTest/本地 ad-hoc artifact 已于 LOC-08M 补证，真实系统语言与正式签名包仍待 LOC-09M/QAR。
- CEF Windows Release 产物已闭合为三套支持语言及必要 gender pak；Debug 保留完整上游 locale 以便诊断。
- BUX-13 preference schema v1 没有语言键。本期只实现“跟随系统”，不修改已完成的 BUX-13 历史结论或持久化 schema。

## 2. 冻结语言契约

### 2.1 支持集合与协商

系统语言定义为**用户首选 UI 语言列表**，不是区域格式、时区、键盘布局或 IME。平台 adapter 提供有序 BCP-47 tag，平台无关 resolver 选择第一个受支持项：

| 系统 tag | `AppLocale` | CEF locale | HTML `lang` |
|---|---|---|---|
| `zh-CN`、`zh-Hans-*`、`zh-SG` | `kZhCn` | `zh-CN` | `zh-CN` |
| `zh-TW`、`zh-Hant-*`、`zh-HK`、`zh-MO` | `kZhTw` | `zh-TW` | `zh-TW` |
| `en-*` | `kEnUs` | `en-US` | `en-US` |
| 裸 `zh` | `kZhCn` | `zh-CN` | `zh-CN` |
| 不支持、空、非法、平台 API 失败 | `kEnUs` | `en-US` | `en-US` |

- tag 比较 ASCII 大小写不敏感并接受 `_` 输入归一为 `-`；单 tag ≤64 字节、列表 ≤32 项/4096 字节，超界稳定忽略并最终回退英文。
- 只在 Browser process 启动、`CefInitialize` 之前解析一次，形成不可变 `LocaleSnapshot`；运行中不监听系统语言变化、不热切换 CEF pak。系统语言变化在下次完整重启自动生效。
- Renderer/page/Agent 输入、URL/query、Profile 数据和环境变量不能选择或覆盖产品语言；Release 不增加 `--locale-for-test`、远程调试或页面 binding。
- 本期不增加手动语言设置。未来需要 `system/zh-CN/zh-TW/en-US` 覆盖时建立独立 `LOC-11`，升级 preference schema 并明确重启生效。

### 2.2 UI locale 与网页语言

UI locale 和网页可观察语言由同一闭合选择结果派生，但不泄露完整系统语言列表：

| `AppLocale` | `CefSettings.locale` | `accept_language_list` |
|---|---|---|
| `kZhCn` | `zh-CN` | `zh-CN,zh,en-US,en` |
| `kZhTw` | `zh-TW` | `zh-TW,zh,en-US,en` |
| `kEnUs` | `en-US` | `en-US,en` |

- `accept_language_list` 只影响标准 `Accept-Language` 和 `navigator.language(s)`；不得携带原始系统列表、地区格式或 Profile 特征。
- 本地 fixture 必须逐字核对 header 与 JS language；变更属于网络可观察隐私面，进入 `LOC-07W`/`PRV-13AW` Review。
- 自有 `crayon://` 页面逐页输出同一 `html lang`，但继续保持 `no-store`、CSP、零公网和现有转义/安全策略。

### 2.3 文案与术语

- `browser/shared-ui/locales/{en-US,zh-CN,zh-TW}.json` 是产品文案唯一手写事实源。key 稳定、只增不改；三份必须 key、值类型、占位符集合和 accelerator 语义完全一致。
- 缺 key、重复 key、空值、无效 UTF-8、控制字符、占位符不一致或生成结果过期必须在构建/检查阶段失败；运行时不得因资源缺失静默混用另一语言。
- `zh-TW` 使用人工审校的繁体中文和冻结术语表，不做运行时简繁转换。正式品牌词继续遵守当前 RNM/BRD 契约；若需改变“蜡笔”品牌写法，先走品牌/命名决策，不能由翻译任务擅改。
- 不翻译网页内容、用户文件、网页标题、URL、设备名称、协议字段、稳定错误码、日志 key、CAAP/MHV1 wire 值。Markdown、CEF、Agent 等术语按词汇表保持一致。
- 动态文本使用有界 typed placeholder；禁止句子片段拼接。日期、数字和复数超出当前三语言闭合需求时另建格式化契约，不直接把 Chromium ICU 类型泄漏到共享层。

## 3. 架构与资源所有权

新增平台中立 `browser/shared-ui/localization`，拥有：

- 闭合 `AppLocale`、`LocaleSnapshot`、BCP-47 归一/协商和固定 Accept-Language 派生。
- 编译期只读 `LocaleCatalog` 与 key lookup；无 CEF/Win32/AppKit 类型、无运行时 JSON/文件/网络 IO、无可变全局状态。
- 由调用方在启动时构造一次并按值/只读引用注入 new-tab、MDV、page Markdown、Cast chrome 和后续产品 surface；各业务模块不重复判断平台/tag。

平台所有权：

- Windows adapter 只使用 `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` 读取有序用户 UI 语言；API 失败或返回非法/超界结果时向 resolver 提供空列表并稳定回退 `en-US`，不得改读区域格式 locale。Browser process 在构造产品 owner 和 `CefSettings` 前取得同一 snapshot。
- macOS adapter 使用 `CFLocaleCopyPreferredLanguages`，按同一 resolver 选择；`en/zh-Hans/zh-Hant.lproj` 仅承接系统原生 bundle metadata/菜单资源。
- CEF child process 通过正常 Browser 启动参数和 locale pak 继承语言，不建立第二套检测或页面可控覆盖。

资源生成：

- 新增确定性 `tools/locales/generate.mjs` 与 `--check`，从三份 JSON/manifest 生成 C++ catalog、Windows 原生资源片段和 macOS `.strings`；生成结果提交仓库并由 CMake 消费，普通产品构建不要求运行 Node。
- 生成器本身、manifest、生成输出与平台资源映射必须可审查；两次生成 SHA-256 集合一致。平台手写资源只保留 icon/manifest 等非文案资产。
- Release packaging 只从固定 CEF distribution 复制 `en-US/zh-CN/zh-TW` 及其所需 gender pak；不删除上游缓存，不修改 CEF vendor 内容。

## 4. 发布可见范围

`LOC-04` 的清单必须覆盖所有第一期 Release 可达文案和可访问名称：

- CEF/Chromium 菜单、上下文菜单、错误页与原生按钮。
- 应用/窗口标题、任务栏/App bundle 名称、文件对话框自有标题和产品确认框。
- `crayon://newtab`、`crayon://mdv`、Mermaid/Highlight/KaTeX 状态、编辑工具栏、tooltip 与 `aria-label`。
- 网页 Markdown 预览/复制/另存为/状态，Cast picker、投屏码、Direct/Relay/停止/播控/拒绝/外部交接。
- 导航、omnibox、标签、书签、历史、下载、设置、Profile/无痕、站点控制、权限、安全反馈与地址自动填充中已产品可达的 surface。
- 第二期默认关闭 surface 的现有 locale key 继续三语言 parity，但不因翻译存在而宣称对应能力已启用。

## 5. 原子任务

| ID | 状态 | 依赖 | 单一交付目标 | 主要路径 | 验收映射 |
|---|---|---|---|---|---|
| LOC-01 | DONE | BUX-18,REL-05,current 契约 | 冻结三语言、系统协商、术语、UI/网页语言、资源 owner、支持/非目标和文案清单 | `docs/current/localization.md`,`docs/plans/localization-roadmap.md` | UX-001/002/004/013/016/018；Review P0/P1=0 |
| LOC-02 | VERIFIED | LOC-01 | 建立三语言事实源、manifest、确定性 generator 与 key/placeholder/生成结果门禁 | `browser/shared-ui/locales`,`tools/locales`,`browser/shared-ui/localization/generated` | 三语言 parity；正/负向 generator contract；重复生成一致；语言审校归 LOC-07W |
| LOC-03 | DONE | LOC-01 | 实现平台无关 `AppLocale`/`LocaleSnapshot` resolver 和 bounded BCP-47/Accept-Language 契约 | `browser/shared-ui/localization` | tag/列表/非法/超界/顺序/回退/LCG 不变量；零 OS/CEF 类型 |
| LOC-04 | DONE | LOC-02 VERIFIED,LOC-03 DONE | 把共享产品 surface 迁移到统一只读 Catalog，清除产品可见硬编码和分散 locale 判断 | `browser/shared-ui/**`,`browser/cef-shell/src/browser/**` 的窄 string DTO | surface/key/html-lang/escape/a11y parity；无行为/权限变化 |
| LOC-05W | DONE | LOC-04,CEF-15,PLT-W04 | Windows 系统 UI 语言探测、CEF locale/Accept-Language 与自有 UI 使用同一 snapshot | `browser/cef-shell/src/process/windows`,`browser/cef-shell/src/windows` | Debug/Release build、完整 CTest、CEF/产品语言 contract |
| LOC-06W | DONE | LOC-05W | Windows 三语言 native/CEF 资源装配与 Release 语言包闭包，不夹带其他 locale | `browser/cef-shell/resources/windows`,`browser/cef-shell/CMakeLists.txt`,package contract | 三套 pak 必备/额外 pak 拒绝、artifact scan、大小/hash/NOTICE |
| LOC-07W | BLOCKED | LOC-06W,CNT-20W2,MDV-25W,PLT-W05b | Windows 10/11 x64 三语言与非支持语言回退英文的真实 CEF、可访问性、IME/DPI 和包体验收 | `tests/e2e/desktop`,`tests/e2e/platform`,LOC/REL/QAR 证据 | UX-001/002/004/013/016/018；三语言真机；Review P0/P1=0 |
| LOC-08M | VERIFIED | LOC-04,CEF-02M | macOS resolver adapter、CEF locale 与 `en/zh-Hans/zh-Hant.lproj` 产品装配 | `browser/cef-shell/src/macos`,`browser/cef-shell/src/process/macos`,`browser/shared-ui/localization/generated/macos` | arm64 Debug/Release build/contract；不冒充签名/真机通过 |
| LOC-09M | TODO | LOC-08M,macOS 候选环境 | macOS 三语言真实 CEF、VoiceOver/IME/缩放与 package/signature 语言闭包 | `tests/e2e/desktop`,`tests/e2e/platform`,LOC/QAR 证据 | UX-016/018；三语言真机；签名包；原生 x64 边界如实记录 |
| LOC-10 | TODO | LOC-07W,LOC-09M | 跨平台本地化总 Review、文档/支持矩阵/残余风险与后续手动覆盖决策 | current/LOC/REL/QAR 文档 | P0/P1=0；Windows/macOS 证据隔离；APPROVE |

`LOC-01..10` 复用现有 `UX-001/002/004/013/016/018` 与 `RG-006`，本 Roadmap 不新增唯一测试 ID，当前权威测试总数仍为 212。

## 6. 每任务边界与验收命令

### LOC-01 契约与清单

- 允许：新增 `docs/current/localization.md`，更新 LOC/总/REL/current/plan 索引；只读盘点生产文案、CEF settings、平台资源和 package。
- 禁止：生产/测试/CMake/locale 资源行为改动，不修改 BUX-13 schema，不运行真机后声称功能完成。
- 验收：链接/任务计数/依赖一致；`cargo run --quiet -p repo-guard -- scan --root .`、`git diff --check`；v0.9 文档 Review。
- 不做：翻译、generator、resolver 或平台接线。

### LOC-02 资源事实源与生成

- 输入：LOC-01 key/术语/placeholder 契约和现有 136-key 双语资源。
- 允许：三份 locale JSON、独立 locale manifest、`tools/locales/**`、generated 输出、对应 contract/CMake check target。
- 禁止：CEF/平台启动代码、BUX/MDV/Cast 行为、第三方翻译/runtime 依赖；生成器不得访问网络。
- 验收：`node tools/locales/generate.mjs --check`；独立 locale contract 正向通过，缺 key/重复/空值/坏 UTF-8/placeholder 漂移/过期生成结果负向拒绝；两次生成 hash 一致；Format、fast、`git diff --check`。
- 不做：运行时选择语言或迁移 UI 调用方。

### LOC-03 resolver

- 输入：LOC-01 的闭合语言表和预算。
- 允许：`browser/shared-ui/localization` 的非生成生产/测试文件、根/模块 CMake。
- 禁止：Win32/AppKit/CEF 类型、运行时 JSON/IO、环境变量/页面/Profile override、Preference schema。
- 验收：独立 C++17 `/W4 /WX` 与 `-Wall -Wextra -Wpedantic -Werror` build；resolver contract 覆盖大小写/脚本/地区/下划线/裸 zh/有序列表/不支持/非法/超界/API failure 投影与 5000 步 LCG；共享层回归、fast、`git diff --check`。
- 不做：平台 API 和可见文案迁移。

### LOC-04 共享 surface 迁移

- 输入：完整 generated catalog 与不可变 `LocaleSnapshot`。
- 允许：共享 UI string DTO/renderer、CEF browser adapter 的窄注入点、相关 contract；按 surface 小提交迁移但不改变业务状态机。
- 禁止：Cast-SDK/Relay/CAAP/schema、权限/路由/投屏策略、文件行为、平台系统语言 API；不得把 JSON parser 放进生产图。
- 验收：三语言全部已发布 surface key/readback、HTML `lang`、转义、placeholder、tooltip/accessible name、窄/宽伪本地化布局；硬编码产品文案扫描；受影响 target build/CTest、fast/security、`git diff --check`。
- 不做：Windows/macOS 系统探测和 Release pak 装配。

### LOC-05W Windows 运行时装配

- 输入：Windows 有界首选 UI 语言列表与 LOC-03/04。
- 允许：Windows bootstrap/app adapter、必要 source/package contract 和 CMake target 接线。
- 禁止：修改全局系统语言、测试专用生产开关、远程控制、Profile 持久化、macOS 行为。
- 验收：注入 tag 的纯 adapter contract 覆盖 API success/failure/超界；真实当前系统启动 smoke；`CefSettings.locale`、最小 Accept-Language、自有 catalog 与 child process 同源；Windows x64 Debug/Release `ALL_BUILD`、完整 CTest、fast/security、`git diff --check`。
- 不做：裁剪 Release pak 或三套系统语言真机结论。

### LOC-06W Windows Release 资源闭包

- 输入：固定 CEF 150 distribution 和 LOC-02 生成资源。
- 允许：Windows RC/generated resources、CEF copy allowlist、package/source contract、Release staging 证据。
- 禁止：删除/修改 `.cache` 或上游 CEF vendor、改变 CEF revision、引入在线语言包、夹带测试 locale/生成器到 Release。
- 验收：Debug/Release package contract；`en-US/zh-CN/zh-TW` 及必要 gender pak 全部存在，任一缺失或其他主 locale pak 出现稳定失败；实际 staging 执行 `repo-guard --artifact-path`，记录路径/大小/SHA-256/NOTICE；完整 CTest、fast/security、`git diff --check`。
- 不做：系统语言切换真机与安装器最终 Go/NoGo。

### LOC-07W Windows 真实矩阵

- 环境：Windows 10/11 x64 的 `zh-CN`、`zh-TW`、`en-US` 真实用户 UI 语言，以及至少一个不支持语言；使用可恢复 VM/user profile，修改系统语言前记录并恢复原配置。
- 语言质量：由简体中文、繁体中文和英文熟练使用者逐 surface 复核术语、语气、歧义、截断和安全确认含义；在此证据完成前 `LOC-02` 最高 `VERIFIED`。
- 验收：每种语言从干净 Profile 启动，检查 Chromium menu/context/error page、newtab、MDV、page Markdown、Cast、窗口/原生对话框、`html lang`、`navigator.language(s)` 和本地 fixture 捕获的 `Accept-Language`；系统语言改变后仅在完整重启生效；浅/深、窄/宽、100%/200% DPI、键盘、Narrator、简繁中文/英文 IME；Debug/Release 完整 CTest、Release artifact scan、安装/升级/回滚适用回归。
- 禁止：把注入 tag、Mock、截图文本识别或单配置 smoke 冒充系统语言真机；不得访问公共网络。
- 结果：Windows 全矩阵通过才可 `DONE` 并解锁 REL-03；macOS 保持独立 `TODO/NOT_RUN`。

### LOC-08M/09M macOS 后置

- LOC-08M 本次原子范围（2026-09-03，用户要求开始 macOS 验证）：依赖 `LOC-04 DONE` 与 `CEF-02M VERIFIED`；以 `3fd7804` 干净工作区为基线，只补 macOS arm64 本地化装配的 Debug/Release 构建、完整适用 CTest 和 bundle contract 证据，并修复此接线直接造成的构建/契约问题。
- 允许路径：`browser/cef-shell` 的 macOS locale 接线、对应 CMake/package/source contract、LOC Roadmap 与计划索引；共享 locale/catalog 只读。首轮产品构建发现共享窗口策略依赖与媒体 transport 头文件遗漏，范围补充为仅修复这两处 Mac 产品构建接线并加回归门禁，不改其行为。禁止修改 Cast-SDK、公共协议、业务权限、系统语言/输入法偏好和用户 Profile；不访问真实 Keychain、不使用证书凭证、不公证上传或发布。
- 验收命令：离线固定 CEF 下 `cmake --preset macos-arm64-cef-debug` 与 Release configure；两个构建目录分别 `cmake --build`、`ctest --output-on-failure`；`node tools/locales/generate.mjs --check`、`cargo run --quiet -p repo-guard -- scan --root .`、`git diff --check`。测试覆盖支持/不支持/非法/超界/API 失败回退；locale 初始化无后台任务，取消/超时由 CEF Harness 与退出释放覆盖。
- 明确不做：LOC-09M 三语言系统切换、VoiceOver/IME/缩放、Developer ID/公证/安装升级、真实投屏与长稳；不以本次 macOS 补证改变 Windows 首发策略。
- 首轮完整 CTest 补充范围：修复 `tests/e2e/desktop/browser/run_page_snapshot_fixture.py` 将 Windows 专用 `media-cast-ui-win` 误派给 Mac 导致的超时，只调整测试平台分派并新增独立回归测试；保留 Mac `media-cast-ui` 与 Windows 可信物理输入门禁，不修改产品或扩大输入授权。
- LOC-08M 只完成共享实现消费、`zh-Hant.lproj`、CEF/bundle 资源和 arm64 构建/contract；没有真实系统语言/VoiceOver/IME/package evidence 最高 `VERIFIED`。
- LOC-09M 在真实 macOS arm64 完成三语言、VoiceOver、中文/英文 IME、100%/200% scaling、签名包；原生 x64 只有原生硬件才能给长稳结论，Rosetta 只允许短 smoke。
- Windows 证据不得升级 LOC-08M/09M 状态；macOS 特有失败也不回写已完成的 Windows LOC-07W。

### LOC-10 总 Review

- 按 v0.9 顺序复核需求/边界、resolver 正确性、owner/依赖、CEF 生命周期、语言隐私面、启动/渲染性能、三语言测试、生成供应链和包体。
- 固定 commit/range，列 Windows/macOS 各自最高状态；P0/P1 归零，P2 延期必须有后续 ID。
- 同步 current 支持矩阵、REL/QAR 门禁和已知限制；不把手动覆盖、更多语言、RTL 或在线翻译夹入 v1。

## 7. Windows 首发矩阵

| 维度 | 必须覆盖 |
|---|---|
| OS/架构 | Windows 10 x64、Windows 11 x64 |
| 语言 | `zh-CN`、`zh-TW`、`en-US`、不支持语言→`en-US` |
| 构建 | Debug/Release `ALL_BUILD` + 完整 CTest |
| 原生 CEF | chrome/menu/context/error、locale pak、child process |
| 自有页面 | newtab、MDV、page Markdown、Cast、全部 tooltip/a11y |
| Web 可观察 | `html lang`、`navigator.language(s)`、本地 fixture `Accept-Language` |
| UI | light/dark、narrow/wide、100%/200% DPI、文本增长/截断 |
| 输入/辅助 | 键盘、Narrator、简体/繁体/英文 IME |
| 生命周期 | clean/既有 Profile、重启切换、崩溃恢复、退出零残留 |
| Release | 仅三语言 pak、artifact scan、大小/hash/NOTICE、安装/升级/回滚 |

## 8. 非目标与风险

非目标：

- 本期不支持第四种语言、RTL、在线翻译、网页内容翻译、用户自定义翻译包或运行时插件。
- 不做运行中热切换，不修改 BUX-13 schema，不增加页面/Agent/CLI 语言控制能力。
- 不改变业务错误码、协议、Cast route、授权、Profile 或内容安全行为。

主要风险与控制：

- CEF 与产品 UI 混合语言：必须共用一个 pre-initialize snapshot，LOC-05W/08M 逐层 readback。
- 三份手写资源漂移：只手写 JSON，平台输出由 generator 生成并 `--check`。
- 翻译改变安全含义：权限、删除、外部交接和投屏拒绝文案进入术语/语义 Review，不能弱化确认或失败。
- `Accept-Language` 指纹扩张：只发送闭合最小列表，不记录/发送原始偏好；本地 fixture 和隐私 Review 锁定。
- 包体或资源缺失：LOC-06W/09M 对真实 artifact 扫描，缺支持 pak 与夹带额外主 locale 均失败。
- 伪本地化不能替代真机：布局 automation 只发现截断；LOC-07W/09M 才能关闭平台状态。

## 9. 领取顺序

## LOC-01 完成记录（2026-09-02）

- 交付：新增 [桌面三语言本地化契约](../current/localization.md)，冻结 `en-US/zh-CN/zh-TW` 协商、进程级 snapshot、最小 Accept-Language、资源/生成 owner、术语、第一期 surface、可访问性、隐私、平台证据和非目标；同步 current/LOC/REL/总索引。
- 验证：`cargo run --quiet -p repo-guard -- scan --root .` 退出码 0、`passed=true`；RG-003/RG-004 仅为未修改生产文件既有 warning。`git diff --check` 退出码 0；六份受影响 Markdown 相对链接检查 `MARKDOWN_LINKS=PASS`。
- Code Review：按 v0.9 顺序复核范围、协商确定性、owner/依赖、CEF 初始化时序、Accept-Language 隐私、资源供应链、平台证据真实性；P0/P1/P2/P3=`0/0/0/0`，`APPROVE`。
- 未覆盖与风险：本任务不实现翻译、generator、resolver 或平台接线；现有双语/硬编码事实仍由 LOC-02+ 关闭。

## LOC-02 验证记录（2026-09-02）

- 交付：三份 155-key JSON、locale manifest、LF/hash lock、C++ catalog、三语言 Windows RC 与 macOS `Localizable/InfoPlist.strings` 生成物；产品构建只消费提交生成物，Node 仅用于测试/check。
- 正/负向验证：`node --test tools/locales/generate.test.mjs` 5/5 PASS，覆盖真实三语言确定性、重复 key、非法 UTF-8、空值、控制字符、非 string、缺 key、顺序、placeholder、stale/missing/extra output；`node tools/locales/generate.mjs --check` PASS（3 locales、155 keys、9 files）。
- CMake：`cmake -S . -B target/localization-cmake -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` 成功；定向 CTest `localization_generated_check`、`localization_generator_contract` 2/2 PASS。
- Code Review：复核路径约束、无网络、strict UTF-8/JSON、跨 locale parity、RC/macOS/C++ escaping、LF/CRLF、hash 与额外生成物拒绝；P0/P1=0，P2=1（独立繁体语言审校，转 `LOC-07W`），`APPROVE_WITH_FOLLOWUP`。
- 未覆盖与风险：尚未由繁体中文熟练使用者审校，也未接入产品 runtime/platform；因此不标 `DONE`，不宣称 UI 已支持三语言。

## LOC-03 完成记录（2026-09-02）

- 交付：新增纯 C++17 `AppLocale`/`LocaleSnapshot` resolver，固定三套 CEF/HTML/Accept-Language 投影；大小写/下划线归一、非法/不支持 tag 忽略、首选顺序、64-byte tag、32 项/4096-byte list 和英文 fail-closed 均有行为测试。
- GCC：`target/localization-cmake` 以 `-Wall -Wextra -Wpedantic -Werror` 构建 resolver/public-header，三项 `browser_localization_*` CTest 3/3 PASS。
- MSVC：VS 2022 x64 Debug/Release 以 `/W4 /WX /permissive-` 构建；Debug 与 Release 三项 contract 均 3/3 PASS。resolver 测试含 5000 步确定性 LCG；public header 独立编译；source boundary 自动拒绝 Win32/AppKit/CEF 类型和运行时文件 IO。
- Code Review：初版 boundary pattern 对允许的 `cef_locale` 字段产生误报，已收窄为真实 CEF 类型/头边界并以 fail-fast 命令复验；最终 P0/P1/P2/P3=`0/0/0/0`，`APPROVE`。
- 未覆盖与风险：没有平台 API、CEF 初始化或产品 surface 接线，分别由 LOC-04/05W/08M 承接。

## LOC-04 完成记录（2026-09-03）

- 交付：新增只读 `LocaleCatalog` 与统一 `ProductStrings` DTO；new-tab、MDV、网页 Markdown 和 Cast chrome 的 155-key 三语言资源均由同一 `LocaleSnapshot` 构造，缺 key 直接失败；Windows 产品 owner 已从平台 `LoadString` 分散装配迁移到该共享 bundle，未改变业务状态机、权限或投屏策略。
- 契约：`browser_product_strings_contract` 覆盖三 locale、Windows/macOS shortcut 投影、完整字段、繁中/英文锚点，并实际渲染 new-tab/MDV 逐字检查 `html lang`；generator 的 key/type/placeholder/accelerator/escaping 负向门禁继续生效。
- 构建：VS 2022 x64 `/W4 /WX /permissive-` Debug/Release 共享 target 与 6 项 localization contract 全部 PASS；固定 CEF `150.0.10+g8042e43+chromium-150.0.7871.101` Windows x64 Debug `crayon_browser` 完整产品链接 PASS。CEF 自带 delay-load/ATL warning 保留，不是本任务新增。
- 回归：`scripts/check.ps1 fast` 首轮在未改动的 `stalled_probe_times_out_to_inconclusive_without_retry` 偶发失败，单测复验 PASS，完整 fast 重跑 PASS；`scripts/check.ps1 security` PASS；`repo-guard` PASS（仅既有 RG-003/RG-004 warning），`git diff --check` PASS。
- Code Review：按 v0.9 复核 catalog owner、缺 key fail-closed、UTF-8 转换、共享/平台依赖、CEF 生命周期、HTML 转义与安全边界；P0/P1/P2/P3=`0/0/0/0`，`APPROVE`。
- 未覆盖与风险：Windows 系统 API 与 CEF settings 尚未接线，当前 bootstrap 临时固定 `zh-CN` 仅用于迁移期保持原行为，由 LOC-05W 立即替换；繁体语言人工审校仍归 LOC-07W。

## LOC-05W 完成记录（2026-09-03）

- 交付：新增 Windows `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` 有界 adapter；Browser process 在 `CefInitialize` 前只解析一次 `LocaleSnapshot`，并用同一 snapshot 设置 `CefSettings.locale`、闭合最小 `accept_language_list` 与统一 `ProductStrings`。API 失败、空/非法 UTF-16、单 tag、总字节和列表超界均稳定回退 `en-US`；未增加页面、Profile、环境变量或测试专用生产覆盖入口。
- 构建/契约：固定 CEF `150.0.10+g8042e43+chromium-150.0.7871.101`、VS 2022 Windows x64 Debug/Release `ALL_BUILD` 均退出 0；Release 完整 CTest 92/92 PASS（323.92 秒）。Debug 完整 CTest 执行 92 项时 90/92 PASS（717.78 秒），失败的 #79/#80 均发生在业务 `terminal=completed` 后且伴随宿主孤儿只读 Git 从 2 增至 86；精确清理后 #79 `page_snapshot_cef_integration_windows` 1/1 PASS（146.93 秒）、#80 `cast_cef_integration_windows` 1/1 PASS（11.31 秒），因此同一 Debug 构建的 92 项已全覆盖通过，但不记作单次 92/92。
- 真机 smoke：Windows 11 x64 当前用户 `zh-CN` 真实启动 Debug `CrayonBrowser.exe`；UI Automation readback 显示窗口标题“蜡笔浏览器 - Chromium”、Chromium 原生“返回/前进/重新加载/查看网站信息/地址和搜索栏/新标签页”及 `crayon://newtab` 自有中文标题/正文；正常关闭后 `CrayonBrowser.exe`、`crayon-content-host.exe`、`crayon-media-host.exe` 残留 0。
- 回归：`cargo test --workspace` 重跑退出 0（127.4 秒）；fast 其余 `guard/format/brand-assets-unit/brand-assets/legacy-unit` 五阶段逐项退出 0；`scripts/check.ps1 security` 退出 0；`git diff --check` 退出 0。整段 `scripts/check.ps1 fast` 首轮被已确认的 `crayon-agent-gateway` 偶发零 CPU 挂起拖至 900 秒 TIMEOUT，终止本次超时测试树后单 crate 101/101 PASS、全 workspace 重跑 PASS。
- Code Review：按 v0.9 复核系统语言语义、边界/回退、snapshot 唯一 owner、CEF 初始化时序、Accept-Language 隐私、child process 继承、生命周期与证据真实性；P0/P1/P2/P3=`0/0/0/0`，`APPROVE`。
- 未覆盖与风险：尚未裁剪/扫描 Release locale pak，也未执行三套系统语言与不支持语言的 Windows 10/11 真机矩阵；分别归 LOC-06W/07W。宿主 Codex Git 状态轮询可产生无父进程的只读 `git.exe`，长门禁需在不触碰活跃/写入 Git 的前提下清理并保留分段证据。

## LOC-06W 完成记录（2026-09-03）

- 交付：Windows `app.rc` 改为消费 LOC-02 确定性生成的三语言 STRINGTABLE；产品 CMake 从 CEF 通用资源复制中移除整个 `locales`，Debug 保留完整上游语言集供开发诊断，Release 仅装配 `en-US/zh-CN/zh-TW` 主 pak 与三类必要 gender pak，共 12 个文件。staging 脚本在递归清理前拒绝缺失/根目录/符号链接输出、符号链接目标和源目标同址。
- 契约：`windows_cef_shell_package_contract` 验证三语言原生 product-name resource、支持 pak 必备、Release 额外 `.pak` 拒绝，并用临时负向 fixture 锁定缺包/多包行为；`mdv_handler_contract` 改为核对生成 RC 和共享本地化 bundle owner。Debug/Release 定向 package contract 均 1/1 PASS，受影响 contract 双配置均 9/9 PASS。
- 构建与回归：固定 CEF `150.0.10+g8042e43+chromium-150.0.7871.101`、VS 2022 Windows x64 Debug/Release `ALL_BUILD` 均退出 0；完整 CTest Debug 92/92 PASS（459.78 秒）、Release 92/92 PASS（345.05 秒）；安全补强后双配置增量构建退出 0，package contract 再次各 1/1 PASS。`scripts/check.ps1 fast` 退出 0（204.2 秒），`scripts/check.ps1 security` 退出 0，`node tools/locales/generate.mjs --check` 为 3 locales/155 keys/9 files PASS，`git diff --check` 退出 0。
- Release staging：`D:/crayon-browser/target/localization-release-staging` 共 35 个文件、372,512,715 字节、12 个 locale pak；`CrayonBrowser.exe` SHA-256=`EAB5D939293A666B210B8F5FAEC191324A017D6105485CFC45150863607BD367`，其与 DLL、content/media host、runtime manifest 均和最新 Release 输出逐项哈希一致。`THIRD_PARTY_NOTICES.md`、`mermaid.spdx.json`、`mermaid-manifest.json` 已生成，显式 `repo-guard --artifact-path` 退出 0，RG-006/RG-009 PASS。
- Code Review：按 v0.9 复核需求边界、RC/CEF 资源正确性、CMake owner、路径与递归清理安全、Release 包体、负向测试、NOTICE/SBOM 和证据真实性；路径边界问题已在 Review 中修复并复验，最终 P0/P1/P2/P3=`0/0/0/0`，`APPROVE`。
- 未覆盖与风险：尚未执行 `zh-CN/zh-TW/en-US` 与不支持系统语言的真实用户 UI 语言切换、Narrator、IME、100%/200% DPI、安装/升级/回滚矩阵；这些只由 LOC-07W 关闭。staging 为本地候选证据目录，不等同签名安装包或 REL-03 Go/NoGo。

## LOC-07W 进展记录（2026-09-03，Windows 11 x64）

- 文案预审：逐项复核三套 155-key 事实源的产品语义，修正繁体中文 `task` 术语漂移、Workflow 页面操作范围和客户端请求状态，并统一三语言的无效固定入口、外部客户端请求中、非 Markdown 文件错误文案；重新生成 9 个平台资源。`node tools/locales/generate.mjs --check` PASS，generator 正/负向测试 5/5 PASS。该 Agent 编辑预审不能代替简体/繁体/英文熟练使用者的独立语言审校，`LOC-02` 仍保持 `VERIFIED`。
- 新增证据工具：`tests/e2e/desktop/browser/run_localization_probe.py` 只监听随机 `127.0.0.1` 端口，每次使用高熵临时 route token，并以 `no-store` 和闭合 CSP 显示、回传原始 `Accept-Language`、`navigator.language(s)` 与 document language；报告体、语言项、长度、超时均有界，错误 route 与畸形报告均拒绝。`browser_localization_probe_selfcheck` 已接入 desktop E2E CTest，直接 selfcheck 与双配置 CTest 均 1/1 PASS；探针自测逐字得到 `zh-CN,zh;q=0.9`、`zh-CN`、`[zh-CN,zh]` 的预期 fixture 数据，但此自测不作为真实 CEF 语言证据。
- 当前系统事实：Windows 11 专业版 x64 `10.0.26200.7309`，当前系统 UI/Culture 为 `zh-CN`，用户首选 UI 语言列表只有 `zh-Hans-CN`，DISM 只报告已安装 `zh-CN`（UI fallback `en-US`），当前缩放为 100%。因此 `en-US`、`zh-TW`、不支持语言回退和 200% DPI 尚不具备真实环境证据。
- 当前 `zh-CN` 产品观察：真实 Debug CEF 启动显示“蜡笔浏览器 - Chromium”，UI Automation 读回 Chromium 原生“返回/前进/重新加载/查看网站信息/地址和搜索栏/新标签页”以及 `crayon://newtab` 的简体标题、说明、地址栏提示和空固定入口；在导航本地语言探针前，Release 首次路径触发 Windows 防火墙对 `crayon-media-host.exe` 的权限提示。自动化按安全规则未处理该系统安全提示，测试产品与探针已精确清理、残留为 0。
- 构建/自动化：文案与探针变更后 VS 2022 Windows x64 Debug/Release `ALL_BUILD` 均退出 0，双配置 localization/product-strings/newtab/MDV/package 受影响契约各 17/17 PASS。Release 完整 CTest 为 92/93 PASS（270.51 秒），唯一 `cast_cef_integration_windows` 在无人进行真实物理点击时稳定复现 `actual_media=0`；单项复跑同样失败。该既有门禁故意由低级鼠标钩子拒绝 `LLMHF_INJECTED`，不得为使自动化变绿而放宽。Debug 排除该物理点击项的其余 92/92 PASS（482.98 秒）。`scripts/check.ps1 fast`、`scripts/check.ps1 security`、`git diff --check` 均退出 0。
- 候选 artifact：三语言文字重新链接后更新既有 Release staging；35 个文件、372,512,715 字节、12 个 locale pak，`CrayonBrowser.dll` SHA-256=`78E267C5E7FE3F38FE5BCA56586EF5CB1B68650CEE5BA4DB68242C1D38B72FB1`。NOTICE/SPDX/manifest 重新生成，显式 `repo-guard --artifact-path` 退出 0，RG-006/RG-009 PASS。
- 待闭合：用户需手动处理 Windows 防火墙提示并在 Cast CEF 测试窗口执行真实物理点击；还需安装/切换 `en-US`、`zh-TW` 和一种不支持的用户 UI 语言，从干净 Profile 完整重启并运行 header/JS/html-lang 与全 surface 矩阵；Narrator、简繁中文/英文 IME、浅/深、窄/宽、100%/200% DPI、安装/升级/回滚及三语言人工审校均为 `NOT_RUN`。这些外部条件连续阻塞后，LOC-07W 转为 `BLOCKED`；不得以自动化或注入输入冒充关闭。

## LOC-08M 实现记录（2026-09-03，Windows 11 x64 可移植证据）

- 实现：新增 macOS 用户首选 UI 语言 adapter；Browser process 在 `CefInitialize` 前只解析一次共享 `LocaleSnapshot`，同一快照设置 `CefSettings.locale`、`accept_language_list` 与完整 `ProductStrings`。移除新标签、MDV、网页 Markdown、Cast 的硬编码/`CFBundleCopyLocalizedString` 分散路径，非法、空、超界或 API 失败统一回退英文。
- Bundle：macOS target 改为只消费 generator 提交的 `en/zh-Hans/zh-Hant.lproj`，不再维护 `resources/macos` 手写文案副本；package contract 要求三套 `.lproj` 恰好闭合，缺失或额外目录均失败。
- Windows 可移植验证：VS 2022 x64 Debug/Release `ALL_BUILD` 均成功；两配置 LOC 定向 CTest 各 8/8 PASS，覆盖三语言/排序/不支持 tag、API 失败和容量边界、生成物一致性、完整产品 surface 及 macOS source contract。Release 排除可信物理点击项的完整 CTest 首轮为 92/93，`page_snapshot_cef_integration_windows` 因 20 次性能样本 p95 超过既有 500ms 门槛失败但进程/Profile 残留均为 0，原配置单项复跑 1/1 PASS（37.72 秒）；不把首轮失败改写为全绿。`node tools/locales/generate.mjs --check` PASS（3 locales/155 keys/9 files）；`fast` 首轮在未改动的 `crayon-platform-windows --lib` 非稳定失败，crate 单项 27/27 PASS 后独立重跑 `fast` 退出 0；独立 `security` 与 `git diff --check` 退出 0。
- Code Review：按 v0.9 顺序复核 resolver/CEF 单快照、资源 owner、失败回退、容量边界、包闭包和跨平台证据真实性；修正测试误用固定偏好项上限，并关闭 CoreFoundation UTF-8 保守容量导致合法 tag 被误拒绝的 P2，最终 P0/P1/P2/P3=`0/0/0/0`，`APPROVE`。
- 未覆盖与风险：当前是 Windows 环境，macOS arm64 Debug/Release configure/build/完整 CTest、真实 App 启停、三系统语言、CEF header/JS/html-lang、VoiceOver/IME/缩放、签名与 package contract 均为 `NOT_RUN`；因此状态只到 `IMPLEMENTED`，不得升级为 `VERIFIED/DONE`。

## LOC-08M macOS 补证记录（2026-09-03）

- 基线：`main@3fd7804`，开始时工作区干净、固定 Cast-SDK 未改；macOS 26.6.2（25G83）arm64，固定 CEF `150.0.10+g8042e43+chromium-150.0.7871.101_macosarm64`。本次只修改 Mac 产品/测试构建接线、平台测试分派与本任务文档。
- 构建回归：首轮 Debug 产品编译失败于 `popup_policy.h file not found` 与 `macos::MediaHostProcess` 未声明；补齐生产与集成测试 target 的共享窗口策略依赖及具体 transport 头文件。新增 source contract 在修复前分别拒绝产品/集成 target 缺依赖，修复后通过；没有修改 popup/Cast 业务行为。
- 配置：`cmake --preset macos-arm64-cef-debug -DCRAYON_CEF_ROOT="$PWD/.cache/cef/cef_binary_150.0.10+g8042e43+chromium-150.0.7871.101_macosarm64"` 与同命令追加 `-B .cache/build/macos-arm64-cef-release -DCMAKE_BUILD_TYPE=Release` 均 exit 0。修复后 `/usr/bin/time -p cmake --build .cache/build/macos-arm64-cef-debug --parallel 6` exit 0（最终增量确认 3.11 秒）；Release 对应命令 exit 0（最终重建 15.46 秒）。Release Ninja 报既有 `premature end of file; recovering` 并重建，未手动删除缓存。
- 首轮完整 CTest：Debug 89/90、317.47 秒；Release 89/90、275.08 秒，均 exit 8。唯一失败为 Mac 执行 Windows 专用 `media-cast-ui-win` 后超时，既有 Mac `media-cast-ui` 已通过。首轮曾并行运行双配置，Release 后续增量重建与测试时段重叠，因此不作为最终发布/性能证据；修复后按配置串行完整重跑。
- Harness 修复：以闭合平台映射选择 native UI 场景；Mac 保留全部共享场景和 `media-cast-ui`，Windows 保留其可信物理输入 `media-cast-ui-win`，显式错误平台请求在启动前拒绝。独立 `python3 tests/e2e/desktop/browser/page_snapshot_fixture_test.py` 4/4 PASS、exit 0，并接入 CTest；不是删除安全用例或放宽输入门禁。
- 最终 Debug：`ctest --test-dir .cache/build/macos-arm64-cef-debug --output-on-failure --timeout 240` exit 0，91/91 PASS（146.16 秒）。真实 CEF Harness 23 类场景、含 20 次性能样本，`complete_p95_ms=37`、`max_tick_delay_ms=29`、`residue=0`；测试注入的可信播放事实只证明 Harness 路径，不替代真实用户点击/ADB 接收端 Direct 上屏。
- 最终 Release：`ctest --test-dir .cache/build/macos-arm64-cef-release --output-on-failure --timeout 240` exit 0，91/91 PASS（147.06 秒），CEF 集成矩阵 99.10 秒；与 Debug 串行且没有构建重叠。最终两配置均为单次完整通过，不用定向复跑拼接覆盖首轮失败。
- 静态验证：`python3 -m py_compile tests/e2e/desktop/browser/run_page_snapshot_fixture.py tests/e2e/desktop/browser/page_snapshot_fixture_test.py`、`cargo fmt --all -- --check`、`node tools/locales/generate.mjs --check`（3 locales/155 keys/9 files）、两份改动文档相对链接检查、`git diff --check` 均 exit 0。`cargo run --quiet -p repo-guard -- scan --root .` exit 0（复跑 4.11 秒），仅既有 RG-003/004 warning；未安装独立 Python/CMake formatter/linter，未宣称对应全量检查通过。
- 本地 Release artifact：以 APFS clone 将本次 Release App 复制至 `.cache/loc08m-release-zexW1b/CrayonBrowser.app`，`repo-guard mermaid-metadata --root . --output-dir .cache/loc08m-release-zexW1b` 生成 NOTICE/SPDX/manifest；目录共 265 个常规文件、336,586,881 字节。主程序 6,646,048 字节、SHA-256 `fac4ac76f3449855883dad9a11ba943174042a5d988a4547a0af3a5090703fec`，与构建目录一致。`codesign --verify --deep .cache/loc08m-release-zexW1b/CrayonBrowser.app` exit 0；`cargo run --quiet -p repo-guard -- scan --root . --artifact-path .cache/loc08m-release-zexW1b` exit 0（20.52 秒，RG-006/009 PASS）。这只是本地 ad-hoc App 与资源闭包证据，不是 Developer ID、Apple 公证、签名安装器或 Go/NoGo。
- 环境观察：首轮 locale 单测与 repo-guard 一度停留 `_dyld_start`，签名校验有效且随后自行恢复；未关闭系统安全服务、删除 provenance 或改 Keychain。computer-use 返回 Mac 锁屏、无法自动解锁，已请求用户手动解锁；没有执行系统语言/输入法/辅助功能设置变更。
- 未覆盖：LOC-09M 的真实产品三语言/header/JS/html-lang、VoiceOver/IME/原生缩放、真实产品启停；Developer ID/公证/安装升级；手机 Direct/Relay、100 次稳定性与长稳。Windows 原生本次 `NOT_RUN`；真实 SecureStore Keychain 仍按用户决策最后验证。
- Code Review（v0.9，`3fd7804` 上本次 9 文件工作区 diff）：独立复核单一构建目标、共享窗口策略的平台中立性、media transport owner、测试平台映射与早期拒绝、未改变输入授权/CEF 生命周期/锁序/热路径，以及源/产物隔离与证据真实性；P0/P1/P2/P3=`0/0/0/0`，`APPROVE`。未改动 Rust/SDK/schema 或 locale 事实源，无新增生产函数/规模提醒；全 workspace fast/security 本次未运行，不能用 CTest 替代其结论。最高状态 `VERIFIED`，LOC-09M 实际系统语言和用户交互门禁仍未关闭。

LOC 模块当前无 `IN_PROGRESS` 项，`LOC-08M VERIFIED` 已补 macOS arm64 构建/契约/本地 artifact 证据，下一项为 `LOC-09M`（需解锁 Mac 并取得真实系统语言/交互证据）；`LOC-07W BLOCKED` 等待 Windows 人工/系统环境。后续严格按：

`LOC-01 -> (LOC-02 || LOC-03) -> LOC-04 -> LOC-05W -> LOC-06W -> LOC-07W`

macOS 在 Windows 候选资源允许时执行：

`LOC-04 -> LOC-08M -> LOC-09M -> LOC-10`

`LOC-07W` 完成即可作为 Windows REL-03 输入，不等待 `LOC-09M/10`；LOC 模块整体只有跨平台总 Review 完成后才可全部 `DONE`。
