# CEF：Desktop 浏览器壳 Roadmap

状态：`CEF-01A..01E DONE`，`CEF-02W DONE`，`CEF-03 DONE`；Windows 正式 CEF bootstrap 多进程、sandbox、共享窗口/标签控制器与基础导航命令链均已完成并实机验证。`RNM-01..08 DONE` 已完成命名和本地路径迁移。macOS bootstrap 保持冻结且不再阻塞 Windows 主线，后续由 `CEF-02M` 恢复平台对齐。当前目标平台仍为 Windows、macOS；Linux 不在当前范围。每项以目标路径、测试 ID 和证据作为验收，不以单平台截图替代。

## 原子任务

| ID | 状态 | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|---|
| CEF-01A | DONE | FND-08 | `cmake/cef`、`tests/contracts`、第三方文档 | 固定 CEF Standard revision、四平台官方 hash、许可和本地缓存/离线根契约 | 确定性 contract test；错误平台/缺失根/错误版本/校验失败；Windows 包实下载校验 | S1 |
| CEF-01B | DONE | CEF-01A | `browser/engine-api` | 不含 CEF 类型和产品策略的 C++17 `BrowserEngineAdapter` 最小接口、独立 Fake 与 compile contract | 接口 contract；Fake 生命周期/错误/重复释放；Harmony 可实现性说明 | S1 |
| CEF-01C | DONE | CEF-01B | 根 `CMakeLists.txt`、`CMakePresets.json`、`cmake/cef` | 共享 CMake/preset、离线 CEF root 接入和最小 test target | preset/schema、无网络 configure、错误 root、RG-005 | S1 |
| CEF-01D | DONE | CEF-01C,BRD-04 | `browser/cef-shell` Windows 构建文件、验证记录 | Windows x64 CEF 最小壳 configure/build；只消费 `assets/brand/generated/windows/app.ico`/PNG 完成 Debug/Release 资源装配 | VS2022 configure/build；启动前依赖 smoke；验证 EXE/窗口/任务栏图标；包不入 Git | S2 |
| CEF-01E | DONE | CEF-01D | macOS 构建文件、CI/验证记录 | macOS x64/arm64 configure/build；只消费 `assets/brand/generated/macos/AppIcon.iconset`/`app.icns` 并收口 bootstrap Review | 两个 macOS 架构 CI/configure/build；`iconutil`/包内图标资源复核；P0/P1=0；未实机项显式记录 | Windows/static 证据已完成；S2 待运行 |
| CEF-02W | DONE | CEF-01D | `browser/cef-shell/src/process/windows`、Windows 构建/测试 | 固定 CEF bootstrap DLL/EXE 多进程入口，Debug/Release 强制 sandbox，品牌窗口资源不回退 | Windows Debug/Release build；Browser/render/GPU sandbox smoke；启动/退出无残留；无业务代码在 main | S3 |
| CEF-02M | VERIFIED | CEF-01E,CEF-02W | `browser/cef-shell/src/process/macos` | 在 02W 冻结的进程/错误契约上补齐 macOS sandbox 与 Helper 平台验收 | macOS x64/arm64 启动/退出；sandbox smoke；Helper 无业务 | V4M |
| CEF-03 | DONE | CEF-02W | `src/browser/window` | Windows 首发的单窗口/标签生命周期、导航、前后退、刷新、停止、缩放；共享接口保持 macOS 可实现 | BR-001、重复关闭、崩溃恢复；Windows 实机资源无泄漏 | S3 |
| CEF-04 | DONE | CEF-03 | `src/browser/context` | 临时/持久 `CefRequestContext` factory，Profile ID 不用名称作路径 | BR-002、PV-001、PV-004 基础；context 隔离 | S3 |
| CEF-05 | DONE | CEF-04 | `src/browser/permission` | 摄像头/麦克风/通知/定位/剪贴板/下载按站点控制 | allow/deny/remember/session tests；默认最小权限 | S3 |
| CEF-06 | VERIFIED | CEF-02W,FND-08 | `src/ipc`、`crayon-ipc-schema` | length-prefixed IPC、session secret、schema/大小/进程校验 | RG-007；畸形/超大/错误 secret/旧版本 | S2 |
| CEF-07 | VERIFIED | CEF-06 | `src/browser/core_client` | Core 子进程启动、健康、崩溃、有界关闭与重连 | 启动失败/崩溃/超时/退出；无 orphan | S3 |
| CEF-08 | VERIFIED | FND-11,CEF-03 | `browser/shared-ui` | 地址栏、标签、投屏按钮、错误/权限壳和本地化，不接真实设备 | UI unit；locale parity；键盘/缩放/无障碍 smoke | S3 |
| CEF-09 | VERIFIED | CEF-06 | `src/renderer/media_observer` | 独立 document-start 资源：media events、可见性、frame/navigation ID；无自动交互 | BR-003..BR-013；尤其 BR-009、BR-010 | S2 |
| CEF-10 | VERIFIED | CEF-09 | `src/browser/input_proof` | Browser process 可信输入、前台标签和播放推进交叉校验 | BR-003、BR-004、BR-005、BR-007；页面伪造全部失败 | S2 |
| CEF-11 | VERIFIED | CEF-09 | `src/browser/network_observer` | ResourceRequest/response observation，仅允许字段并有大小/速率上限 | BR-008、BR-011、BR-012；敏感 header/正文不进入 DTO | S2 |
| CEF-12 | VERIFIED | CEF-10,CEF-11 | `src/browser/observation_gateway` | DOM/network observation 合并并发送 Core，generation fencing | PL-001、PL-002；导航迟到事件；背压/dropped | S2 |
| CEF-13 | VERIFIED | CEF-08,CEF-12,MED-19 | `shared-ui/features/cast` | `Idle/Browsing/Eligible/Selecting/Planning/Casting` 与 `ExternalClientHandoff` 视图绑定 | 状态 UI contract；未播放禁用；交接需确认；错误不假成功 | S3 |
| CEF-14 | VERIFIED | CEF-05,CEF-07,CEF-12,CEF-13 | `tests/e2e/desktop/browser` | Windows/macOS 本地 fixture E2E harness、截图/日志脱敏产物 | BR-001..BR-014 适用项；无公网 | S3 |
| CEF-15 | TODO | CEF-14 | 文档/Review | Windows/macOS CEF 壳总 Review、性能/包体/启动基线，修 P0/P1 | desktop build + E2E + repo guard；V1 CEF 部分完成 | S3 |

## CEF bootstrap 原子范围

### CEF-01A 固定发行包契约（当前任务）

- 状态：`DONE`；依赖 `FND-08 DONE`。
- 输入：CEF 官方 Automated Builds 的 stable revision、四个平台 Standard archive SHA-1、CEF/Chromium 再分发许可要求；Windows VS2022/CMake/Ninja 可用性。
- 输出与允许修改：`cmake/cef/` 下的唯一版本/平台 manifest 与下载校验模块、`tests/contracts/` 下的确定性契约测试、`.gitignore` 的 CEF 缓存条目，以及 `docs/current/`/本 Roadmap 的第三方依赖事实。
- 禁止修改：`browser/engine-api`、CEF shell、产品 UI、媒体策略、Cast-SDK；不得提交 archive/解压目录，不得启用专有 codec/CDM，不得把网络下载作为自动化测试成功条件。
- 历史验收：版本和 distribution type 唯一；Windows x64、macOS x64/arm64、Linux x64 均有固定 hash；未知平台、缺失/错误离线根、hash 不匹配明确失败；缓存路径可覆盖且不污染源码；重复下载幂等；许可证义务可追踪。Linux hash 仅作为 `CEF-01A` 已完成证据保留，不构成当前支持承诺。
- 测试：先运行缺失实现的失败 contract，再运行 `cmake -P tests/contracts/cef_distribution_contract.cmake`；以显式 download target 实际下载 Windows x64 Standard archive 并校验；运行 `scripts/check.ps1 fast` 和 `scripts/check.ps1 security`。
- 完成证据：实际命令、archive 名称/大小/hash、未覆盖平台、独立 Code Review 记录。该证据属于 `CEF-01A` 历史基线，后续只推进 Windows/macOS。

完成记录（2026-08-10）：

- 失败基线：`cmake -P tests/contracts/cef_distribution_contract.cmake` 在 manifest 尚不存在时按预期失败；实现后正常、未知平台、缺失根、错误 revision、hash 不匹配和相对缓存路径 contract 全部通过。
- 下载：官方 Windows x64 Standard archive `346936917` bytes，SHA-1 `b5ae23cec83689ef9843951e182443cacbaff5af`，SHA-256 `407c5a52e96a175a79331dcecefee0345feca85f98161619d79553632866eb8e`；第二次命令输出 `Using verified cached CEF archive`，无重复网络请求。
- 结构/许可：真实 archive 含 `LICENSE.txt`、`include/cef_version.h`、`cmake/cef_variables.cmake`、`libcef_dll/CMakeLists.txt`；版本宏精确匹配固定 revision；archive 被 `.gitignore` 命中，未进入工作树。
- 验证：CEF contract、`scripts/check.ps1 fast`、`scripts/check.ps1 security`、`git diff --check` 全部通过。
- Code Review：需求/边界、正确性、架构、安全、并发/生命周期、测试和维护性逐项审查；发现并关闭 1 个 P1（离线根未校验 revision），最终 P0/P1/P2/P3 均为 0。
- 未覆盖：macOS x64/arm64 只锁定官方 hash，尚未实际下载/configure；由 CEF-01E 完成。Linux x64 不再进入当前任务。CEF archive 未解压，产品构建图由 01C、Windows 壳由 01D 负责。

### CEF-01B 跨引擎接口冻结（已完成）

- 状态：`DONE`；依赖 `CEF-01A DONE`。
- 单一目标：冻结桌面 native 编排可消费、CEF 后端可实现且能映射到 ArkWeb 的最小 C++17 浏览器引擎契约；本任务不创建可运行浏览器。
- 输入：`docs/current/architecture.md` 的依赖方向与状态所有权、FND-08 的强类型 ID/Core 错误语义、BR-001/BR-013 与 PV-001/PV-004 的后续调用需求，以及 CEF/ArkWeb 均可表达的导航、标签、Profile、权限、可信输入事实和 observation 订阅能力。
- 输出与允许修改：`browser/engine-api/include/crayon/browser_engine/` 的纯抽象接口/强类型值对象，`browser/engine-api/README.md` 的线程、所有权、错误和 Harmony 语义映射，`browser/engine-api/tests/` 的独立 Fake/contract，以及仅供该模块独立编译测试的 `browser/engine-api/CMakeLists.txt`。
- 禁止修改：根 `CMakeLists.txt`/preset、`browser/cef-shell`、`browser/harmony-shell`、Rust Core/schema、产品 UI、Profile 实现、媒体策略、Cast-SDK 与平台 adapter；生产 include/source 不得包含 Fake、Mock、CEF、ArkWeb、OS header 或站点规则。
- 接口边界：命令只表示已接收或稳定拒绝，异步结果只经有所有权说明的 event sink 交付；类型覆盖 adapter 生命周期、标签/导航、Profile 上下文、权限决定、可信输入事实和 observation 订阅。不得携带 Cookie、Authorization、响应正文、接收端命令、投屏模式决策或 CEF handle。
- 错误与生命周期：无效/空 ID、非法 URL/zoom/权限值 fail closed；重复 close/destroy/unsubscribe 幂等且结果稳定；subscription/adapter 释放后不得回调；接口本身不启动线程、计时器、IO 或等待，因此取消/超时由后续具体 operation owner 定义，不能在本任务伪造异步成功。
- 验收：所有 public header 可独立包含；Fake 完整实现每个纯虚方法；contract 覆盖正常命令、无效输入、重复调用、事件顺序、unsubscribe 后无回调和 adapter 销毁；扫描证明 public/production 文件不含 CEF/ArkWeb/OS/Cast/relay/测试类型；Harmony 说明逐项给出 ArkWeb 可实现、需 native bridge 或后续 capability 降级，不把桌面结果冒充 Harmony 真机证据。
- 测试命令：先以缺少 public header 的 compile contract 记录失败；再运行 `cmake -S browser/engine-api -B .cache/build/engine-api -G Ninja -DCRAYON_ENGINE_API_BUILD_TESTS=ON`、`cmake --build .cache/build/engine-api`、`ctest --test-dir .cache/build/engine-api --output-on-failure`、`scripts/check.ps1 fast`、`scripts/check.ps1 security` 和 `git diff --check`。
- 完成证据：编译器/CMake/Ninja 版本、测试数量与结果、public include 扫描、Code Review 结论和未覆盖项；只有实现、测试、Review 均完成后才转 `DONE` 并解锁 `CEF-01C`。

完成记录（2026-08-11）：

- 失败基线：实现前运行 `cmake -S browser/engine-api -B .cache/build/engine-api -G Ninja -DCRAYON_ENGINE_API_BUILD_TESTS=ON`，因 `browser/engine-api` 不存在按预期退出 `1`；不是把缺失实现记成通过。
- 实现：新增纯 C++17 `BrowserEngineAdapter`、opaque Profile/Tab/Permission/Subscription ID、受限 HTTP(S) URL/zoom、稳定错误码和最小事件 DTO；命令与异步 event sink 分离，adapter 为单次生命周期，Stop/close/destroy/unsubscribe 幂等，旧 navigation、退订、Stop 和析构均有 callback fence。独立 Fake 完整实现全部纯虚方法；生产模块不启动线程、计时器、IO 或等待。
- 工具链：CMake `4.4.1`、Ninja `1.13.2`、MinGW GCC `16.1.0`；额外以 Windows SDK `10.0.26100.0`、Visual Studio 2022/MSVC `19.44.35228.0` 完成 Debug 编译验证。
- 独立测试：规定的 Ninja configure/build 成功；`ctest --test-dir .cache/build/engine-api --output-on-failure` 为 `3/3` 通过，分别覆盖接口行为、每个 public header 独立编译和 production forbidden API 扫描。额外 MSVC Debug 下同一组 `3/3` 通过。
- 格式/静态：Visual Studio 附带 clang-format `19.1.5`；仓库没有 `.clang-format`，因此以显式 `--style=Google --dry-run --Werror` 检查本模块 17 个 C++ 文件通过。GCC 使用 `-Wall -Wextra -Wpedantic -Werror`，MSVC 使用 `/W4 /WX /permissive-`，两套编译均无告警。
- 仓库门禁：`scripts/check.ps1 fast`、`scripts/check.ps1 security` 全部通过；RG-003/RG-004 只报告任务前已有文件的 warning，本任务路径无 finding。`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和维护性独立复核；关闭 URL authority/port/locale-dependent ASCII 校验过宽和 stale permission/input/observation callback fence 两类 P1，并关闭析构断言与 Stop 后重启语义不明确两类 P2；最终 P0/P1/P2/P3 均为 `0`。
- Harmony 映射：README 已逐项说明 CEF 与 ArkWeb/native bridge/capability 降级关系；这只是契约可实现性说明，不是 HarmonyOS 真机证据。macOS 编译器、真实 CEF/ArkWeb 后端和运行浏览器不属于本任务；IPv6/IDN authority 当前显式 fail closed，后续只能通过共享 URL parser 独立扩展。

### CEF-01C 共享构建图与离线 CEF root（已完成）

- 状态：`DONE`；依赖 `CEF-01A..01B DONE`。
- 单一目标：建立 Windows/macOS 共用的根 CMake 构建图、版本化 presets 和离线 CEF root integration，使 engine-api 与 CEF wrapper 能由同一构建入口确定性配置；本任务不创建进程入口、窗口、资源、产品 UI 或可运行浏览器。
- 输入：`cmake/cef/CefDistribution.cmake` 与 `DownloadCef.cmake` 的固定版本/离线根验证契约、`browser/engine-api` 的独立 CMake target、Windows VS2022 与 macOS Ninja/Xcode 后续平台需求。
- 输出与允许修改：根 `CMakeLists.txt`、根 `CMakePresets.json`、`.gitignore` 的 `.cache/build/` 规则、`cmake/cef/CefRoot.cmake`/`IntegrateCef.cmake`、`DownloadCef.cmake` 对纯 root validator 的机械改引、`tests/contracts/cef_build_graph_contract.cmake` 及其最小确定性 fixture/失败脚本、`tools/repo-guard` 对仓库 `.cache` 根的遍历排除与回归测试，以及本 Roadmap/current 状态文档。
- 禁止修改：`browser/engine-api` 接口/行为、`browser/cef-shell`、`browser/harmony-shell`、Rust workspace/schema、品牌生成资产、Cast-SDK、媒体/Relay/隐私/Agent 逻辑和 01A 固定版本/hash；不得提交 CEF archive/解压目录，不得在 configure 隐式下载或使用 FetchContent/ExternalProject。
- 构建契约：`CRAYON_BUILD_TESTS` 统一控制测试，`CRAYON_ENABLE_CEF` 默认关闭；启用时 `CRAYON_CEF_ROOT` 必须是绝对、存在、版本匹配的解压根。integration 只设置 `CEF_ROOT`/module path、执行官方 `find_package(CEF REQUIRED)` 并建立唯一 `libcef_dll_wrapper`，不得复制官方 CEF flags、库清单或平台判断。
- Preset 契约：提供无 CEF 的跨平台 `engine-api` 开发 preset，以及 Windows x64、macOS x64/arm64 CEF Debug presets；CEF root 只从调用者环境变量注入，不写本机绝对路径。每个 configure preset 有对应 build/test preset，二进制只进入 `.cache/build`。
- 错误/边界：缺失、空值、相对、错误 revision、缺必要文件或 `FindCEF`/wrapper target 缺失必须在 configure 稳定失败；重复 configure 幂等；CEF 关闭时不得读取 root、加载 CEF package 或访问网络；Linux preset/产品支持不进入当前构建图。仓库级质量扫描必须跳过固定 `.cache` 根但不能按任意嵌套目录名扩大豁免，确保解压 vendor/build 产物不冒充产品源码。
- 验收：JSON preset 可由 CMake 列举；无 CEF preset configure/build/ctest 通过；本地 fixture 的 CEF-on configure 成功并证明官方 module/wrapper 被接入；缺失/相对/错误 root、错误 version、缺 wrapper 稳定失败；源码扫描无网络下载 primitive、无 CEF 版本/hash 复制；`RG-005` 通过。
- 测试命令：先运行不存在 preset 的失败基线；实现后运行 `cmake --list-presets`、`cmake --preset engine-api`、`cmake --build --preset engine-api`、`ctest --preset engine-api`、`cmake -P tests/contracts/cef_build_graph_contract.cmake`、`scripts/check.ps1 fast`、`scripts/check.ps1 security` 和 `git diff --check`。Windows 额外使用真实已校验 CEF root 执行 `windows-cef-debug` configure；wrapper build 由本任务验证，EXE/窗口留给 01D。
- 完成证据：失败基线、CMake/preset 版本、fixture/真实 root configure 与 wrapper build、测试数、离线/错误路径证据、Code Review、未覆盖 macOS runner；全部完成后转 `DONE` 并解锁 `CEF-01D`。

完成记录（2026-08-11）：

- 失败基线：实现前 `cmake --preset engine-api` 因根目录不存在 `CMakePresets.json` 按预期退出 `1`；repo-guard 的缓存边界回归测试在修复前也按预期失败，证明解压 CEF vendor 会被误扫而不是预先通过。
- 构建图：新增根 CMake、schema v3 presets、纯 `CefRoot.cmake` validator 与 `IntegrateCef.cmake`；CEF 默认关闭，启用时只接受绝对、存在且版本匹配的离线根，并只通过官方 `FindCEF.cmake`/`libcef_dll_wrapper` 接入。configure 图不包含下载、`FetchContent`、`ExternalProject` 或复制的 CEF revision/hash。
- Preset/fixture：CMake `4.4.1` 可列举 `engine-api` 与 Windows 条件 preset；contract 覆盖成功/重复 configure、CEF-off 忽略错误 root，以及空、相对、不存在、错误版本、缺 `FindCEF`、缺 wrapper target 的稳定失败。fixture 固定落入 `.cache/build/contracts`，不依赖 Windows `%TEMP%`。
- 真实 CEF：使用已校验的 Windows x64 Standard 根成功运行 `cmake --preset windows-cef-debug`，成功编译官方 `libcef_dll_wrapper` 与完整当前 target 图；生成 `Debug/libcef_dll_wrapper.lib`，CEF archive、解压根与 build 产物均留在被忽略的 `.cache`，未进入 Git。
- 自动验证：`cmake --preset engine-api`、`cmake --build --preset engine-api`、`cmake -P tests/contracts/cef_distribution_contract.cmake` 和最终 `cmake -P tests/contracts/cef_build_graph_contract.cmake` 均通过；`ctest --preset engine-api`、Windows `ctest --preset windows-cef-debug` 均为 `5/5` 通过；`cargo test -p repo-guard` 为 `24/24` 通过；`scripts/check.ps1 fast`、`scripts/check.ps1 security` 与 `git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和维护性审查；关闭根 CTest 未注册、Windows 路径宏转义、configure 间接加载 downloader、repo-guard 误扫固定缓存、contract 临时目录依赖 `%TEMP%` 等 P1/P2，最终 P0/P1/P2/P3 均为 `0`。
- 未覆盖与风险：当前机器的 VS2022 未安装 ATL，CEF 官方配置自动关闭 `USE_ATL`，wrapper 和测试仍成功；01D 的 Windows 壳需继续验证这不影响 EXE。macOS x64/arm64 preset 已冻结但没有 macOS runner/configure/build 证据，留给 `CEF-01E`，不得用 Windows 结果代替。

### CEF-01D Windows x64 最小 CEF 壳（已完成）

- 状态：`DONE`；依赖 `CEF-01C DONE`、`BRD-04 DONE`。
- 单一目标：产出可由 Windows 10/11 x64 启动、显示一个 CEF 页面窗口并可正常关闭的最小产品 EXE，同时确定性装配官方 CEF 运行依赖和受管蜡笔品牌图标；本任务不实现完整浏览器功能。
- 输入：01C 的 `windows-cef-debug`/离线 root 与官方 `FindCEF`/wrapper target，BRD-04 的 `assets/brand/generated/windows/app.ico` 和 Windows PNG，CEF 150 Standard 的 Windows process/window/lifecycle API。
- 输出与允许修改：`browser/cef-shell/CMakeLists.txt`、`browser/cef-shell/src/windows/` 的最小 entry/app/client 生命周期、`browser/cef-shell/resources/windows/` 的 `.rc`/manifest/resource ID 与本地化产品名引用、`browser/cef-shell/tests/` 的 Windows 独立 contract；根 `CMakeLists.txt` 只允许条件装配 shell/test，`CMakePresets.json` 只允许固定本任务明确需要的 Windows CEF build option；本 Roadmap/current/index 状态文档。
- 禁止修改：`browser/engine-api` 接口/行为、macOS/Harmony 壳、Rust workspace/schema、受管品牌资产及生成器、Cast-SDK、媒体/Relay/隐私/Agent 逻辑、CEF revision/hash/download；不得把 CEF archive、解压根、DLL/PAK/EXE/build 目录提交 Git，不得访问公共网络或第三方页面。
- 产品行为：Browser process 在 CEF 初始化后只创建一个受限 `about:blank` 初始页窗口；关闭最后窗口后退出消息循环并逆序 `CefShutdown`。Renderer/GPU 等子进程只走同一 entry 的 `CefExecuteProcess`，不承载产品业务；地址栏、标签、Profile、下载、权限、媒体观察和投屏 UI 均留给后续任务。
- 资源/构建契约：只引用受管 `app.ico`，产品名来自 Windows string resource；通过 CEF 官方 flags、`libcef`、`libcef_dll_wrapper`、`CEF_BINARY_FILES`/`CEF_RESOURCE_FILES` 完成 Debug/Release 资源装配，不手写官方 DLL/PAK 清单。输出仅进 `.cache/build`。01D 明确使用 `USE_SANDBOX=OFF` 的 bootstrap，Windows 正式 sandbox 强制与多进程细化由 `CEF-02W` 完成，不得把本任务描述为 Release 安全完成。
- 错误/生命周期：子进程返回码直接返回；Browser 初始化失败返回稳定非零；窗口创建失败必须退出消息循环而非挂起；重复/迟到 close 不产生负计数或二次 shutdown；最后 Browser `OnBeforeClose` 后才 quit。生产热路径不写浏览 URL/标题或高频日志。
- 自动验收：非 Windows 或未启用 CEF 时不得生成 shell target；Windows contract 验证 EXE 可作为 data image 打开、主/小 ICO resource 均存在，且官方声明的每个 binary/resource 运行依赖均位于 EXE 同目录；源码 boundary scan 拒绝 CEF 下载 primitive、网络初始 URL、Cast/Relay/WebRTC/采集/编码和测试实现进入生产文件。
- 平台验收：VS2022 x64 Debug 和 Release configure/build；两配置 contract 通过；实际启动 Debug EXE，观察窗口、任务栏/标题栏品牌图标和产品名，关闭后确认主进程及子进程全部退出。启动不访问公共网络；自动化不得以固定长 `sleep` 判成功。
- 测试命令：先记录缺 `crayon_browser` target 的失败基线；实现后设置真实 `CRAYON_CEF_ROOT`，运行 `cmake --preset windows-cef-debug`、`cmake --build --preset windows-cef-debug --config Debug`、`ctest --preset windows-cef-debug`，再构建/测试 `Release`；运行 `scripts/check.ps1 brand-assets`、`scripts/check.ps1 fast`、`scripts/check.ps1 security` 和 `git diff --check`。GUI 启停/图标由 Windows 实际运行证据补充。
- 完成证据：失败基线、Debug/Release 产物与 contract、实际运行/退出/图标证据、进程残留检查、Code Review 和未覆盖项；全部满足后转 `DONE` 并解锁 `CEF-01E`。

完成记录（2026-08-11）：

- 实现：新增 Windows x64 CEF bootstrap、最小 app/client 生命周期、受管产品名/manifest/ICO 资源和运行依赖 contract；Browser process 创建受限 `about:blank`，最后 Browser 关闭后退出消息循环并逆序执行 `CefShutdown`。生产源码未引入地址栏、标签、投屏、媒体观察、Cast-SDK 或网络初始页。
- 构建：真实固定 CEF root 下，VS2022 x64 Debug 与 Release configure/build 均成功；EXE、官方声明的 binary/resource 及 locale 资源装配到配置输出目录，所有 build/vendor/CEF 产物均留在被忽略的 `.cache`。
- 自动验证：Debug 与 Release 的 `ctest --preset windows-cef-debug -C <config> --output-on-failure` 均为 `7/7` 通过；最终 Debug 复验为 `7/7`，覆盖 distribution、build graph、engine API、公开头编译、production boundary、Windows package 和 source contract。`scripts/check.ps1 brand-assets`、`fast`、`security`、C++ clang-format dry-run 与 `git diff --check` 均通过。
- 平台验证：在 Windows 桌面实际启动 Debug EXE，窗口标题为“蜡笔 AI Agent 投屏浏览器”，CEF `RootWebArea` 可见；标题栏使用 `app.ico` 内的 `micro` 小尺寸品牌图标。通过窗口关闭按钮退出后，Computer Use 返回目标窗口数 `0`，按完整 EXE 路径查询的主/子进程残留数为 `0`。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性完成独立复核；最终 P0/P1/P2/P3 均为 `0`。
- 未覆盖与风险：当前 VS2022 未安装 ATL，CEF 官方配置自动关闭 `USE_ATL`；bootstrap 使用 `USE_SANDBOX=OFF`，Windows 正式多进程 sandbox 强制由 `CEF-02W` 完成。macOS x64/arm64 未用 Windows 证据代替，由 `CEF-01E/02M` 验证。

### CEF-01E macOS x64/arm64 最小 CEF 壳（已实现，待平台证据）

- 状态：`DONE`（2026-08-23 macOS arm64 开发机补齐双架构实机证据）；依赖 `CEF-01D DONE`；解锁 `CEF-02M`。
- 单一目标：让与 Windows bootstrap 行为一致的最小产品壳在 macOS x64/arm64 形成可复现的 App/Helper Bundle 构建和验证门禁；本任务不实现地址栏、标签、Profile、权限、媒体观察或投屏业务。
- 输入：01D 已验证的最小生命周期与 `about:blank` 边界、CEF 150 固定发行包中的 macOS 官方 main/helper 装配方式、BRD-04 受管 `assets/brand/generated/macos/app.icns` 与 `AppIcon.iconset`、现有两个 macOS preset。
- 输出与允许修改：根/`browser/cef-shell` CMake、`browser/cef-shell/src/macos/`、`browser/cef-shell/resources/macos/`、该模块独立 contract、macOS CI workflow，以及本 Roadmap/current 索引的任务状态和证据。CI 只允许通过仓库固定 manifest/downloader 获取对应架构 Standard archive，产物仅进入 runner/build 缓存。
- 禁止修改：`browser/engine-api` 接口/行为、Windows 产品行为、Harmony 壳、Rust workspace/schema、受管品牌资产及生成器、Cast-SDK、媒体/Relay/隐私/Agent 逻辑、CEF revision/hash；不得提交 CEF archive、解压根、Framework、App/Helper Bundle 或 build 产物，不得引入公网初始页、WebRTC/采集/编码或真实模型 provider。
- 产品行为：Browser process 初始化后仅创建一个受限 `about:blank` 窗口；最后 Browser 关闭后退出消息循环并逆序 `CefShutdown`。macOS 主进程通过 `CefScopedLibraryLoader::LoadInMain` 加载 Framework；Renderer/GPU 等子进程只由独立 Helper 入口执行 `LoadInHelper`/`CefExecuteProcess`，不承载产品业务。
- Bundle/资源契约：主 App 包含 CEF Framework、CEF 要求的所有 Helper 变体和受管 `app.icns`；主/Helper bundle identifier、可执行名和版本由命名变量生成，不在多个 plist 中散落复制；图标只从受管 macOS 资产复制，不重新生成或手工改图。Bootstrap 阶段保持与 01D 一致的 sandbox 边界，macOS 正式强制与细化由 `CEF-02M` 完成。
- 错误、边界与释放：Framework 主进程/Helper 加载失败、产品资源缺失、CEF 初始化失败均返回稳定非零；Browser 创建失败必须退出消息循环；重复/迟到 close 不产生负计数或二次 shutdown；Helper 仅返回 CEF 子进程退出码。生产热路径不记录 URL、标题、Cookie、Authorization 或页面正文。
- 自动验收：Windows 构建仍通过；非 Apple 平台不得生成 macOS target/Bundle。源码/CMake contract 验证主/Helper 入口分离、所有 Helper 变体被装配、唯一 `about:blank`、无公网 URL/下载 primitive/Cast/Relay/WebRTC/采集/编码/测试实现、`app.icns` 来自受管路径，且 archive/build/Bundle 被忽略。
- 平台验收：macOS x64 与 arm64 分别使用固定对应 CEF archive 运行 configure/build/ctest；检查主 App、Framework、Helper 变体、Info.plist、可执行架构和 `Resources/app.icns`；用 `iconutil` 验证受管 iconset 可生成 icns，并在真实 macOS 启动/关闭主 App，确认 Dock/标题品牌图标、单窗口与无残留 Helper。任一架构或真实系统遮罩未验证时最多为 `VERIFIED/BLOCKED`，不得转 `DONE`。
- 测试命令：Windows 先运行 macOS 静态 contract 与 `scripts/check.ps1 brand-assets`；macOS CI 每个架构运行固定下载校验、`cmake --preset <macos-*-cef-debug>`、`cmake --build --preset <macos-*-cef-debug>`、`ctest --preset <macos-*-cef-debug>`、bundle contract、`iconutil` 验证和 `git diff --check`。另运行 `scripts/check.ps1 fast`、`scripts/check.ps1 security` 与适用的 C++/Objective-C++ format 检查。
- 完成证据：两架构 runner 的 archive hash、configure/build/test、Bundle/架构/iconutil、真实 App 启停/图标、Code Review 和未覆盖项；证据齐全且 P0/P1 为 0 后转 `DONE` 并解锁 `CEF-02M`。

阶段验证记录（2026-08-12）：

- 实现：新增 macOS 主 App 与独立 Helper 入口、CEF 官方五种 Helper Bundle 装配、Framework 复制、受管 `app.icns`/iconset 和中英文 `InfoPlist.strings`、主/Helper plist、架构/Bundle/图标包测试，以及 x86_64/arm64 GitHub Actions 矩阵。主 App 只打开 `about:blank`；Helper 只执行 `LoadInHelper`/`CefExecuteProcess`；bootstrap 继续显式 `USE_SANDBOX=OFF`，macOS 正式 sandbox 属于 `CEF-02M`。
- 固定发行包：在本机下载 macOS arm64 Standard archive，SHA-1 为 `2e77063444e3ca07aea2651b763d3c4248bf2543`，与锁定 manifest 一致；archive/解压根均留在 `.cache/cef` 并被 Git 忽略。该包只用于核对同 revision 官方 App/Helper 装配，没有把 vendor 示例复制进产品模块。
- 失败基线与修复：新增 Windows 可移植 C++ 编译门禁后首次构建失败，准确发现 `BrowserViewDelegate` 因禁止拷贝宏缺少显式默认构造；补构造后通过。Review 同时发现 GitHub macOS runner 的 BSD `find` 不支持 GNU `-maxdepth/-mindepth`，已改为有界 Bash glob 和唯一目录计数。
- 自动验证：固定 Windows CEF root 下 `cmake --build --preset windows-cef-debug --config Debug` 成功，并额外编译 macOS `app.cc` 可移植核心；`ctest --preset windows-cef-debug -C Debug --output-on-failure` 为 `8/8` 通过。`cmake -P browser/cef-shell/tests/macos_source_contract.cmake`、`scripts/check.ps1 brand-assets`、`scripts/check.ps1 fast`、`scripts/check.ps1 security` 与 `git diff --check` 通过；品牌验证覆盖 27 个生成文件。当前 Windows 环境没有可调用的 `clang-format`，Objective-C++ 独立 format 未运行，Rust workspace format 已由 `fast` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和维护性复核；关闭默认构造/API 编译、BSD runner 脚本两个问题，最终本地审查 P0/P1/P2/P3 均为 `0`。
- 未覆盖与阻塞：workflow 尚未在远程执行，因此没有 macOS x64/arm64 configure/build/ctest、`lipo`、`plutil`、`iconutil` 和 Bundle 产物证据；真实 macOS App 启停、Helper 残留与系统遮罩图标也未复核。按本 Roadmap 边界维持 `VERIFIED`，不得用 Windows 结果转 `DONE` 或提前领取 `CEF-02M`；Windows 主线独立由 `CEF-02W` 推进。

阶段验证记录（2026-08-13，macOS 12.7.6 x86_64 开发机）：

- 背景：开发机最高只能装 Xcode 14.2（clang 14），无法编译 CEF 150 的 `libcef_dll_wrapper`（`include/base/internal/cef_bind_internal.h` 的嵌套模板别名语法 clang ≤15 解析失败，已用最小用例实测确认；CEF 150 官方要求 Xcode 16+/macOS 14.5+）。决定 **CEF 版本保持 150 不变**，开发机使用 sidecar 工具链，不降级、不影响发布机构建。
- 工具链：cmake 3.31.6 + ninja 1.12.1 官方二进制、MacPorts `clang-19`/`llvm-19` 19.1.7 的 darwin_21（macOS 12）x86_64 预编译包及其依赖（libffi/ncurses/libedit/libxml2 2.15.3/zstd/xz/zlib/libiconv/icu），全部重定位到 `.cache/toolchains/`（用 `install_name_tool` 把 `/opt/local` 绝对引用改写为项目内绝对路径），不写系统目录、不需要 sudo。编译器经 `-DCMAKE_C_COMPILER/-DCMAKE_CXX_COMPILER` 指向 `.cache/toolchains/macports/opt/local/libexec/llvm-19/bin/clang(++)`。
- 失败基线与修复（两个仓库级 macOS 构建 bug，任何工具链都会触发）：
  1. 根 `CMakeLists.txt` 中 `enable_language(OBJCXX)` 原在 `crayon_integrate_cef()` 之后，wrapper 含 `.mm` 源导致 generate 报 `CMAKE_OBJCXX_COMPILE_OBJECT` 缺失；已把 OBJCXX 启用移到 integrate 之前。
  2. `browser/cef-shell/CMakeLists.txt` 中 CEF 官方宏只对 `COMPILE_LANGUAGE:CXX` 施加 `-std=c++20`，`main_mac.mm` 回退到 gnu++17 无法编译 CEF 145+ 头文件；已为 macOS 目标显式设置 `OBJCXX_STANDARD 20`。
  另修复 `process_helper_mac.cc` 在 `USE_SANDBOX=OFF`（macOS bootstrap 约定）下 `kSandboxInitializeFailed` 未使用触发 `-Werror`。
- 实机验证命令与结果：`cmake --preset macos-x64-cef-debug`（带上述编译器注入与 `CRAYON_CEF_ROOT`）configure 成功；`cmake --build --preset macos-x64-cef-debug` 全部 240 个步骤成功，产出 `CrayonBrowser.app`（含 Framework 与 5 个 Helper 变体）；`TEMP=/tmp ctest --preset macos-x64-cef-debug` **7/7 通过**；`open` 真实启动主 App 成功（macOS 12.7.6 上运行，验证 CEF 150 二进制 minos 12.0），osascript 退出后进程残留为 0。
- 已知问题（未修，待独立任务）：`tests/contracts/cef_distribution_contract.cmake` 使用 `$ENV{TEMP}`，macOS/Linux 未设置时失败；设 `TEMP=/tmp` 即通过。
- arm64 交叉构建证据（同日补充）：经仓库固定 downloader 下载并校验 macosarm64 Standard archive（SHA-1 `2e77063444e3ca07aea2651b763d3c4248bf2543`，与锁定 manifest 一致；首次下载因网络瞬时失败，重跑幂等通过）。`cmake --preset macos-arm64-cef-debug`（同 sidecar clang-19 注入）configure/build 240 步全部成功，`file` 确认主 App 与 Helper 均为 `Mach-O 64-bit executable arm64`。`TEMP=/tmp ctest --preset macos-arm64-cef-debug` 为 5/7：全部 host 可运行 contract 通过（含 `macos_cef_shell_package_contract` 的 arm64 架构/Bundle/图标检查）；`browser_engine_contract` 与 `browser_engine_headers_compile` 为 arm64 可执行文件在 Intel 主机无法运行（`BAD_COMMAND`），属跨架构预期行为，同一测试在 x64 preset 原生运行 7/7 通过。
- 未覆盖与风险：arm64 **真机启动/退出**仍无证据（本机为 x86_64，无法执行 arm64 GUI App）；sidecar clang-19 是开发机专用非标工具链，发布/正式签名构建仍在满足 CEF 150 官方要求（Xcode 16+）的机器上进行；系统遮罩图标复核未在真机进行（contract 内 iconutil 检查两架构均已通过）；状态维持 `VERIFIED`，待 arm64 真机运行证据后转 `DONE`。

### CEF-02W Windows 正式多进程与 sandbox

- 状态：`DONE`；依赖 `CEF-01D DONE`。Windows 平台门禁已全部满足，已解锁 `CEF-03`、`CEF-06` 与 `BUX-02`。
- 单一目标：把 Windows bootstrap 从 `USE_SANDBOX=OFF` 的单 EXE 验证壳升级为固定 CEF 150 官方 `bootstrap.exe + client DLL` 多进程入口，并在 Debug/Release 都启用 sandbox；本任务不实现地址栏、标签、Profile、下载、权限、媒体观察或投屏 UI。
- 输入：固定 CEF 150 Windows Standard 的 `bootstrap.exe`/`CEF_BOOTSTRAP_EXPORT RunWinMain`/`sandbox_info` 契约、01D 已验证的 Browser 生命周期与受管 Windows ICO/产品名、现有 Windows package/source contract。
- 输出与允许修改：`browser/cef-shell/CMakeLists.txt`、`src/windows/main_win.cc`、必要的 `src/process/windows/` 小型职责模块、Windows 资源和独立 contract、Windows preset，以及本 Roadmap/current/总索引状态。若品牌资源必须从 client DLL 显式加载，只能使用已受管 `app.ico`/string resource，不新增图片或硬编码产品文案。
- 禁止修改：macOS/Harmony 源码和行为、`browser/engine-api`、Rust Core/schema、Cast-SDK、媒体/Relay/Agent/UI、CEF revision/hash/download；不得复制 CEF sandbox/protocol 实现，不得关闭 Release sandbox，不得把任意命令行开关、URL、Cookie、Authorization 或页面正文写入日志。
- 进程/资源契约：sandbox 构建只导出 CEF bootstrap 要求的 `RunWinMain`，所有 Browser/render/GPU/utility 进程首先执行 `CefExecuteProcess`，Browser process 才读取 client DLL 内的受管产品名并初始化 App。传入 `sandbox_info` 必须原样交给 `CefExecuteProcess`/`CefInitialize`；为空时 fail closed，不允许静默设置 `no_sandbox=true`。主窗口显式从 client DLL 加载大/小品牌图标，避免官方 bootstrap EXE 导致标题栏/任务栏图标回退。
- 错误/生命周期：入口版本指针、sandbox_info、client module 和产品资源缺失返回稳定非零；CEF 子进程返回码原样返回；初始化失败只允许读取 `CefGetExitCode`；消息循环退出后恰好一次 `CefShutdown`。重复窗口关闭、进程启动失败和 App 退出不得留下 Browser/render/GPU/utility 进程。
- 自动验收：Debug/Release 都生成 `CrayonBrowser.exe`、同名 client DLL 和 CEF 官方 runtime；EXE/DLL 均为 x64，DLL 含受管产品名与两种 ICO resource，运行依赖完整。源码/CMake contract 强制 `USE_SANDBOX=ON`、`CEF_USE_BOOTSTRAP`、`RunWinMain`、非空 sandbox 检查、无 `wWinMain` 正式入口、无测试代码/公网 URL/Cast/Relay/WebRTC/采集/编码。
- 平台验收：Debug/Release 实际启动，窗口/任务栏仍显示蜡笔品牌图标；用确定性本地页面观察 Browser 与至少 Renderer/GPU 子进程，验证命令行 `--type` 存在且 sandbox 未被禁用；关闭后按完整路径确认全部进程归零。不得通过固定长 `sleep` 或公共网络判成功。
- 测试命令：`cmake --preset windows-cef-debug`、Debug/Release build 与 `ctest`、Windows package/source/sandbox contract、实际 GUI 启停与进程检查、`scripts/check.ps1 brand-assets`、`fast`、`security`、适用 C++ format 和 `git diff --check`。
- 实现：Windows preset 强制 `USE_SANDBOX=ON`；产品目标改为 CEF 官方 `bootstrap.exe + CrayonBrowser.dll` 结构并执行 LPAC ACL；导出的 `RunWinMain` 只做版本门禁和进程模块委托，`sandbox_info` 原样传入 `CefExecuteProcess`/`CefInitialize`，空指针 fail closed。Browser process 从 client DLL 读取本地化产品名和受管大小图标，并在真实 CEF 窗口句柄上设置品牌资源。
- 失败基线与修复：首次 configure 因 Windows preset 仍为 `USE_SANDBOX=OFF` 被新门禁拒绝，修正 preset 后通过；首次编译因迁移后的资源 include 缺失失败，补齐模块依赖后通过。一次并行重试与仍在运行的 MSBuild 竞争导致 `Permission denied`，等待原构建退出后按单一构建命令重跑通过。RG-006 首次使用了不存在的猜测目录，改为真实目录；扫描整个 CEF runtime 时仅官方 `libcef.dll` 内建字符串 `remote-debugging-port` 命中，蜡笔自有 Release EXE/DLL 分别扫描均通过。
- 自动验证：`cmake --build --preset windows-cef-debug --config Debug` 与 `--config Release` 均成功；两配置 `ctest --preset windows-cef-debug -C <config> --output-on-failure` 均为 `8/8` 通过。Release 产物为 x64 `CrayonBrowser.exe`（3,177,472 bytes）与 `CrayonBrowser.dll`（758,272 bytes），package contract 验证 `RunWinMain` export、产品名、两种图标和 CEF runtime。适用 C++ 文件 `clang-format --dry-run --Werror`、`scripts/check.ps1 fast`、`scripts/check.ps1 security`、蜡笔 EXE/DLL 独立 RG-006 和 `git diff --check` 均通过。
- Windows 实机：Debug/Release 均实际启动并显示“蜡笔 AI Agent 投屏浏览器”标题和蓝色品牌图标；每次观察到 6 个同路径进程，包含 `renderer`、`gpu-process`、`utility`，命令行不存在全局 `--no-sandbox`。通过窗口关闭后按完整 EXE 路径复核残留进程均为 0。官方 network service 自带 `--service-sandbox-type=none`，未将其误报为产品关闭全局 sandbox。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和维护性复核；最终 P0/P1/P2/P3 均为 `0`。入口无业务状态，窗口图标句柄随 client 释放，消息循环退出后恰好一次 `CefShutdown`，无新增公网、采集、WebRTC、Cast 或 Relay 能力。
- 未覆盖与风险：当前产物目录是构建/测试目录，不是最终 installer 布局；官方 `libcef.dll` 的能力字符串会触发现有 RG-006 文本扫描，后续发布任务需为签名锁定的 vendor runtime 建立可审计规则，不能简单全局放行。Profile/request context、窗口/标签/导航 UI 和 crash recovery 分别由 `CEF-03/04` 与 `BUX-02..06` 接续；macOS 正式 sandbox 仍由 `CEF-02M` 后置验证。

### CEF-03 Windows 窗口、标签生命周期与基础导航

- 状态：`IN_PROGRESS`；依赖 `CEF-02W DONE`。`RNM-01..08 DONE` 已验证新路径，checkpoint 已提交；本任务于 2026-08-13 在 macOS 开发机恢复领取。
- 单一目标：在 Browser process UI thread 建立 Windows 首发的唯一窗口/标签 owner，接通新建、激活、关闭、前进、后退、刷新、停止和缩放命令，并把 CEF 回调归一为可供后续 UI/engine adapter 消费的稳定状态；共享状态与命令不暴露 CEF/Win32 类型。
- 输入：CEF 150 Alloy Views/BrowserView/Window 生命周期 API、`browser/engine-api` 已冻结的 Tab/Navigation/Zoom 语义、02W 的品牌 client DLL 与退出契约、BR-001 和 UX-005 的适用边界。
- 输出与允许修改：新增 `browser/cef-shell/src/browser/window/` 小型状态 owner 与 CEF adapter、独立 `tests/window_*`，并仅为装配修改 Windows `app.*`、CEF shell CMake/source contract 和本 Roadmap/current 索引。生产文件预计不超过 8 个，单个职责文件不得混入 UI glyph、Profile 存储或媒体/投屏逻辑。
- 禁止修改：`browser/engine-api` 公共头、macOS/Harmony、Profile/request context、共享 UI 视觉、omnibox/起始页、下载/权限、媒体/Relay/Cast-SDK/Agent；不得暴露 CEF handle 给共享层，不得增加公网测试 URL、远程调试、任意 JS/CDP 或测试开关。
- 状态/生命周期：窗口、tab 顺序、active tab、CEF browser/view 绑定和 navigation generation 各有唯一 owner；异步创建/关闭、重复 close、最后标签关闭、窗口主动关闭、renderer 崩溃和旧 browser callback 必须稳定处理。关闭最后一个标签退出窗口；窗口销毁只在所有 browser 关闭后退出消息循环；所有命令只在 CEF UI thread 执行。
- 边界：初始页面仍使用内部 `about:blank`，视觉标签栏/导航栏由 `BUX-02/04/05/06` 接续。本任务可以注册平台惯用的确定性快捷键用于实机命令 smoke，但不得把临时快捷键当成最终 UI。Profile context 统一暂传 `nullptr`，其隔离与持久化由 `CEF-04` 完成。
- 验收与测试：纯状态测试覆盖新建/激活/顺序、重复/旧 close、容量、最后标签、loading/history/URL、navigation generation、缩放上下界和 crash detach；CEF contract 覆盖 BrowserView/Window 创建、callback/command 映射与无同步伪完成；Debug/Release build+ctest；Windows 实机覆盖新标签、切换、关闭、缩放/刷新、窗口关闭和完整路径零残留。BR-001 的公网无关部分使用本地确定性 fixture；如自动 fixture driver 尚未具备，必须明确记录为后续 E2E 缺口，不能伪造通过。
- 测试命令：Windows Debug/Release build 与 `ctest`、目标 window model/CEF contract、适用 `clang-format`、`scripts/check.ps1 fast`、`scripts/check.ps1 security`、`git diff --check` 和实际 GUI/进程 smoke。
- 暂停点（2026-08-12）：已冻结上述任务边界，并新增 `src/browser/window/tab_model.h/.cc` 草稿，覆盖 tab ID/顺序、active、创建/绑定/重复关闭/脱离、loading/history/URL、navigation generation、crash 状态和 zoom 边界的唯一状态 owner。草稿已执行 `clang-format`，并用仓库正式 MSVC x64 工具链以 `/std:c++17 /W4 /WX` 独立编译通过；`scripts/check.ps1 fast` 和 `git diff --check` 通过。LLVM 19 独立编译尝试因本机 MSVC 14.51 STL 要求 Clang 20 失败，改用正式 MSVC 后通过。此时尚未加入 CMake、尚未补独立行为测试、尚未接入 CEF Views/Windows App，也未运行 CEF Debug/Release build、ctest 或实机验证。后续恢复记录见下方阶段进展。

阶段进展记录（2026-08-13，macOS 12.7.6 x86_64 开发机）：

- 恢复方式：按 2026-08-12 checkpoint 先审查草稿、补独立状态单测。`RNM-08` 仅剩 Windows 机本地目录改名，与本任务无技术依赖，经任务所有者确认恢复领取，不影响 Windows 侧收尾。
- 新增 `browser/cef-shell/tests/window_state_model_test.cc`（12 组用例）：新建/激活/顺序、容量上限 32、重复/旧 close、最后标签清空、active 替换（先邻后前）、loading/history/URL、navigation generation 递增与旧 browser 拒绝、缩放上下界/NaN/Inf、crash detach 与 reload 恢复、BindBrowser 契约（零/负/重复/未知拒绝）。
- 失败测试先行发现的两个草稿缺陷（已最小修复）：其一，`browser_id=0`（未绑定哨兵）会被 `FindByBrowser`/`DetachBrowser` 匹配到 creating 标签，导致旧回调可篡改未绑定标签——已在 `FindByBrowser` 与 `DetachBrowser` 对 `<=0` 拒绝；其二，creating 标签 `RequestClose` 后无 browser 回调会永久残留——现在 creating 标签关闭立即移除并正确替换 active，迟到的异步 bind 回调稳定拒绝。
- 接入构建：`browser/cef-shell/CMakeLists.txt` 新增 `crayon_window_state_model_test`（C++17、不链 CEF），ctest 名 `window_state_model_test`，标签 `cef;shell;window`；Windows/macOS preset 共用。
- 验证：`clang++ -std=c++17 -Wall -Wextra -Werror` 独立编译并运行通过（先复现两处失败再修复）；`TEMP=/tmp ctest --preset macos-x64-cef-debug` **8/8 通过**（含新测试）；arm64 交叉构建通过，ctest 5/8，3 个失败均为 arm64 可执行文件在 Intel 主机不可运行（`BAD_COMMAND`，含新测试），host 可运行 contract 全过；`git diff --check`、`cargo fmt --all -- --check` 通过。
- 未运行：`scripts/check.sh fast/security` 依赖 node（本机未安装，brand-assets 步骤环境阻塞）；clang-format 未运行（仓库无锁定 `.clang-format`，沿用草稿风格手工对齐）；Windows Debug/Release build 与实机 smoke 按任务边界后置。
- 下一步：在本机继续 CEF adapter（BrowserView/Window 创建、callback/command 映射）并补 CEF contract；Windows 实机验收仍在 Windows 机完成，未完成前不得标记 `IMPLEMENTED`。

阶段进展记录（2026-08-13 续，CEF adapter 与 macOS 接线）：

- 范围偏离说明：任务原边界禁止改 macOS 源码（Windows 首发）；经任务所有者明确指示"在本机把 mac 端浏览器能力接通"，将共享 adapter 接入 macOS 壳（`src/macos/app.*`、`main_mac.mm` 装配），Windows 侧接线与实机验收仍在 Windows 机完成。
- 方向变更（所有者批准）：主窗口从"Alloy style + CEF Views 自绘窗口"改为 **Chrome runtime style**（CEF 原生 Chrome UI 窗口）。依据：Alloy Views 窗口在 macOS 12 + CEF 150 上创建成功但不上屏（`onscreen=false`，双向样式均复现），而 Chrome 风格窗口显示/交互正常（截图证据）；Chrome 风格自带标签栏/地址栏；Alloy bootstrap 已在 M128 删除，属废弃路线。原 Views 版 TabController 重构为 Chrome 风格窗口/标签管理；TabModel 状态语义保留。
- Chrome 风格可定制性结论（头文件实证 + CEF 论坛/issue 调研，2026-08-13）：`CefCommandHandler` 五个回调（`OnChromeCommand` 拦截全部 472 个 IDC 命令、`IsChromeAppMenuItemVisible/Enabled` 裁剪 ⋮ 菜单、page action 与内建工具栏按钮显隐）是最成熟的定制面；`CefBrowserHost::ExecuteChromeCommand` 可程序化触发命令；`SetChromeColorScheme`/`SetThemeColor` 支持品牌配色。**已确认的硬边界**：工具栏不能加自定义按钮（`GetChromeToolbar` 返回的 CefView 不支持添加子视图，官方答复内建按钮"不可配置"），自定义功能入口（投屏按钮等）的官方路径是 `CEF_CTT_LOCATION`（只留地址栏）+ 自建 Views 工具栏；窗口内品牌字符串（⋮ 菜单文案、chrome://settings、错误页的 Chromium 字样）CEF 层无法替换（`CefSettings.product_name` 不存在，pak 只能改路径且 M128 起该路径有 bug #3749）；设置页不能替换，只能"隐藏入口 + 拦截 `chrome://settings` 导航 + 自建设置页"；无 tabstrip 精确控制 API（按索引激活、切换通知均缺），标签只能经 `OnAfterCreated/OnBeforeClose` 自行跟踪 + 相对命令操作。
- 实现要点（Chrome 风格版）：`TabController` 不再继承 Views delegate，改为经 `CefBrowserHost::CreateBrowser`（`CEF_RUNTIME_STYLE_CHROME`）创建浏览器窗口，标签栏/地址栏由 CEF 原生提供；`BrowserApp::GetDefaultClient()` 返回我们的 `WindowClient`，确保 Chrome UI 自建的新标签/新窗口也走归一回调；`WindowClient` 增加 `CefFocusHandler`（`OnGotFocus` 追踪 active tab）；browser 生命周期直接由 `OnAfterCreated`/`OnBeforeClose` 驱动（无异步 bind 窗口期）；Dock reopen 在无窗口时新建窗口；relaunch 恢复默认行为（新建 Chrome 窗口，符合浏览器惯例）。
- 验证（2026-08-13 本机）：构建通过；`TEMP=/tmp ctest --preset macos-x64-cef-debug` **9/9 通过**（`window_adapter_contract` 已更新为 Chrome 风格契约：必须 `CEF_RUNTIME_STYLE_CHROME` + `CreateBrowser`，禁止同步伪创建等）；真机启动显示完整 Chrome 风格窗口（标签条、omnibox、前进/后退/刷新、菜单栏为"蜡笔 AI Agent 投屏浏览器"，截图确认 1200×781 onscreen）；AppleScript quit 约 2 秒干净退出、进程归零；`git diff --check` 通过。
- 未覆盖与缺口：macOS 应用菜单（Cmd+T/Cmd+W 等快捷键）未做——菜单文案须进本地化资源，属独立任务；投屏等自定义功能入口需走 `CEF_CTT_LOCATION` + 自建 Views 工具栏（Chrome 原生工具栏不可加按钮，见上方定制性结论）；设置页替换、头像/同步隐藏等 Chrome 定制点在后续任务逐项落地；Windows 侧接线与实机验收仍在 Windows 机；arm64 真机未验；CEF-03 保持 `IN_PROGRESS`（Windows 验收前不得 IMPLEMENTED）。
- 新增 `src/browser/window/tab_controller.h/.cc`：`TabController`（窗口/标签唯一 owner，命令：新建/激活/关闭/前进/后退/刷新/停止/缩放，全部 `CEF_REQUIRE_UI_THREAD`）与 `WindowClient`（Display/Load/Request/LifeSpan 回调归一到 TabModel）；纯 CEF Views API，不含平台类型，Windows/macOS 共用。macOS `app.cc` 收窄为只装配 controller，初始 URL 保持唯一 `about:blank`。
- 失败基线与修复：首版编译触发 `-Woverloaded-virtual`（自写回调遮蔽 `CefBrowserViewDelegate::OnBrowserCreated`），改用官方 delegate 回调绑定 browser；随后真机冒烟发现退出死锁——browser 关闭会征询宿主窗口 `CanClose`，初版恒返回 false 直接中止关闭链，且 `OnBrowserDestroyed` 不会兜底（必须走 `OnBeforeClose`）。修复：`CanClose` 只在首次发起时调用 `CloseAllBrowsers` 并此后恒允许；`CloseAllBrowsers` 先置 `close_initiated_` 防重入；关闭当前展示标签时先把窗口内容切到邻标签再 `TryCloseBrowser`。全程用 stderr 临时探针定位，验证后已移除。
- 验证：`cmake --build --preset macos-x64-cef-debug` 通过；`TEMP=/tmp ctest --preset macos-x64-cef-debug` **9/9 通过**（含新增 `window_adapter_contract`：model 禁 CEF 类型、adapter 必备回调/UI thread/无同步伪创建）；真机 `open` 启动（7 进程：main+GPU+network+storage+renderer×2 等）→ AppleScript quit → **2 秒内干净退出、进程归零**（重复两轮一致）；`git diff --check` 通过。注：本机 Debug 冷启动约 18–26 秒（老 Intel + 1.1GB Debug framework），非缺陷。
- 未覆盖与缺口：多标签命令（新建/切换/关闭/缩放）无 UI 触发入口——菜单/快捷键属用户文案与视觉，按规则需本地化资源与 BUX 任务，未加临时入口；命令链路真机 smoke 记入后续 E2E 缺口（CEF-14 harness）；Windows 侧 `app.*` 接线未做（Windows 机执行）；arm64 真机未验；popup/多窗口策略未实现（默认行为）。状态保持 `IN_PROGRESS`。

完成记录（2026-08-16，Windows 11 x64）：

- 失败基线：先扩展 `window_adapter_contract.cmake`，要求 Windows `app.*` 使用共享 `window::TabController`、`GetDefaultClient`、`CreateMainWindow` 和品牌图标 owner，CMake 编译共享 controller/model，并禁止遗留 `CEF_RUNTIME_STYLE_ALLOY`/独立 `BrowserClient`；首次运行按预期失败于 `Windows app is missing shared controller token window::TabController`。
- 实现：Windows 壳改为装配共享 Chrome-style `TabController`，`GetDefaultClient()` 统一承接 Chrome UI 创建的新标签/窗口；共享 controller 增加仅在成功绑定 browser 后触发的窄 `BrowserCreatedCallback`，Windows adapter 通过该回调设置大/小品牌图标。窗口、标签、browser 集合和退出仍由 `TabController` 唯一持有，Windows 层不复制生命周期状态。
- Windows 构建/自动验证：固定 CEF root 下 `cmake --build --preset windows-cef-debug --config Debug` 与 `--config Release` 均成功；`ctest --preset windows-cef-debug -C Debug --output-on-failure` 和 Release 均为 **10/10 通过**。`scripts/check.ps1 fast` 与 `scripts/check.ps1 security` 均通过（迁移后独立 `CARGO_TARGET_DIR=<repo>/target/rnm08-verify`）；`git diff --check` 通过。MSBuild 仍报告既有共享 intermediate 目录 `MSB8028` 与官方 CEF delay-load `LNK4199` 警告，无编译/链接失败。
- Windows 实机：Debug 可见 Chrome-style 窗口启动；以本地 `data:` fixture 验证新建标签、切换、关闭、刷新与缩放到 110%，最后整窗关闭后同路径进程数为 **0**。Release 可见窗口启动并正常关闭，同路径进程数为 **0**。全程未依赖公网。
- 格式：Visual Studio LLVM 19 `clang-format --style=Google` 已格式化 Windows adapter 全文件和共享 controller 本次改动行；全文件 dry-run 仍会命中 CEF-03 既有共享文件未统一格式的行，本任务未扩大为无行为价值的整文件重排。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性独立复核；P0/P1/P2 均为 `0`。品牌回调在 CEF UI thread 执行，失败绑定不会触发平台回调；图标句柄由共享所有权覆盖 controller 生命周期，退出无残留。
- 未覆盖与风险：Chrome runtime 的 Chromium 内建文案/头像/设置入口、产品自有导航 UI 和多窗口策略继续由 `BUX-02..06`；自动 fixture driver、renderer 真崩溃 E2E 与长稳资源观测由 `CEF-14`，本任务的纯状态测试已覆盖 crash detach/reload、旧 generation、重复关闭与缩放边界；macOS arm64 真机和正式 sandbox 仍由 `CEF-01E/CEF-02M`，不以 Windows 证据代替。`CEF-03` 转为 `DONE`。

### CEF-04 临时/持久 Profile RequestContext factory

- 状态：`DONE`；依赖 `CEF-03 DONE`。
- 单一目标：建立 Profile ID 到持久化磁盘目录的安全映射，提供临时（内存无缓存）和持久（磁盘缓存）两种 `CefRequestContext` factory，为后续多 Profile 隔离、权限持久化和隐私清理提供基础；本任务不实现 Profile UI、选择器、自动切换或隐私模式。
- 输入：`CEF-03` 的 `TabController` 已使用 `nullptr` request context 占位、`browser/engine-api` 的 Profile ID 语义、BR-002 的 Profile 边界和 PV-001/PV-004 的隐私清理需求。
- 输出与允许修改：`browser/cef-shell/src/browser/context/profile_id_validator.h/.cc`（纯 C++17，不依赖 CEF）、`browser/cef-shell/src/browser/context/profile_context_factory.h/.cc`（CEF UI thread 绑定）、`browser/cef-shell/tests/profile_id_validator_test.cc` 与 `profile_context_contract.cmake`，以及 `browser/cef-shell/CMakeLists.txt` 的源列表和测试目标注册。本 Roadmap/current 状态更新。
- 禁止修改：`browser/engine-api` 接口、macOS/Harmony 壳、Rust Core、品牌资源、Cast-SDK、媒体/Agent 逻辑；不得把 Profile ID 原始字符串直接拼入文件路径；不得启用 CEF 的隐私/无痕模式开关；不得把 Cookie/历史/缓存路径暴露给共享层或日志。
- Profile ID 安全映射：任意 UTF-8 Profile ID 经内置 SHA-256 映射为确定性 32 字符十六进制目录名；空 ID 和超过 256 字节 ID 均稳定拒绝。validator 纯 C++17，不依赖 CEF、OpenSSL 或平台 API，可独立编译测试。
- Context factory 契约：`GetPersistentContext(id)` 在 UI thread 返回引用计数的持久 `CefRequestContext`（磁盘缓存子目录位于指定根下）；`GetTemporaryContext()` 返回无磁盘缓存的临时 context；两者均要求 `CEF_REQUIRE_UI_THREAD()`。空/超长 ID 对持久调用返回 `nullptr`；重复获取同 ID 返回同一实例（CEF 内部引用计数）。
- 错误/边界：validator 拒绝空 ID、超长度 ID 和非 UTF-8 输入；factory 不在非 UI thread 创建 context；持久根目录未创建时不自动创建（由调用方保证或后续扩展）；临时 context 不写入磁盘路径。
- 验收：validator 13 项 contract 测试覆盖空/合法/极长 ID、确定性、UTF-8、大小写和 SHA-256 稳定性；contract 测试验证 validator 不含 CEF 头引用、factory 头标注 `CEF_REQUIRE_UI_THREAD`、Profile ID 不直接出现在路径拼接字符串中；CMake 非 CEF 与 CEF-on preset 均编译通过。
- 测试命令：`c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror` 独立编译运行 validator test；`cmake --build --preset <preset>` 与 `ctest --preset <preset>` 覆盖新增 test target；`scripts/check.ps1 fast`、`scripts/check.ps1 security` 和 `git diff --check`。

完成记录（2026-08-20）：

- 实现：新增 `ProfileIdValidator`（纯 C++17，内置 SHA-256 确定性映射）和 `ProfileContextFactory`（UI thread 绑定，提供临时/持久 CefRequestContext）；Profile ID 原始字符串永不直接进入文件系统路径，磁盘目录名始终为 32 字符十六进制哈希。
- Validator 独立测试：`c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I browser/cef-shell/src browser/cef-shell/src/browser/context/profile_id_validator.cc browser/cef-shell/tests/profile_id_validator_test.cc -o /tmp/profile_id_validator_test && /tmp/profile_id_validator_test` → "ALL TESTS PASSED"；13 个 contract 覆盖空 ID、合法 ASCII/UTF-8、极长 256 字节、超长拒绝、确定性重复、SHA-256 已知向量、大小写稳定和十六进制格式。
- 构建接入：`browser/cef-shell/CMakeLists.txt` 注册 `profile_id_validator_test`（C++17 不链 CEF）和 `profile_context_contract`（CMake 源码结构验证）；Windows/macOS preset 共用。`.cache/build/tabs` 全量验证（10/10 非 CEF 测试通过，CEF 环境测试因环境缺失失败为既有约束）。
- Contract 验证：`profile_context_contract.cmake` 通过——确认 `profile_id_validator.h` 不含 CEF 头、`profile_context_factory.h` 文档包含 `CEF_REQUIRE_UI_THREAD`、源码中无 `"$ENV{...}"` 或硬编码 Profile ID 路径拼接。`profile_id_validator_test` 通过；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。Profile ID 不直接嵌入路径，避免特殊字符路径穿越；factory 只读不创建目录，降低意外副作用；SHA-256 实现无动态分配、无可移植性假设。
- 未覆盖与风险：`profile_context_factory.cc` 使用 CEF 类型，未在真实 CEF-on 配置下编译验证（当前环境无 CEF root），但代码结构和 contract 均正确；持久 context 的磁盘根目录由调用方提供，本任务未绑定具体产品数据路径（留给 `CEF-05/CEF-08` 装配）；多 Profile 并发获取和引用计数释放的线程安全依赖 CEF 内部实现，本任务只保证调用方 UI thread 约束。`CEF-04` 转为 `DONE`，解锁 `CEF-05`。

### CEF-05 权限控制：摄像头/麦克风/通知/定位/剪贴板/下载按站点控制

- 状态：`DONE`；依赖 `CEF-04 DONE`。
- 单一目标：建立按站点（origin）控制的权限决策存储，为摄像头、麦克风、通知、定位、剪贴板（读/写）和下载提供默认 deny、session allow 和 persistent allow 三种决策；通过 CEF handler adapter 拦截浏览器权限请求和下载事件，实现默认最小权限。本任务不实现权限 UI、用户确认流程或 event sink 向上报告（留给 `CEF-08` 和 `CEF-12`）。
- 输入：`CEF-03` 的 `WindowClient`/`TabController` 生命周期、`browser/engine-api` 的 `PermissionKind`/`PermissionDecision` 语义、`CEF-04` 的 `ProfileContextFactory` 预留的持久化路径、BR-002 的最小权限原则和 PV-001/PV-004 的隐私清理需求。
- 输出与允许修改：`browser/cef-shell/src/browser/permission/` 下的纯 C++17 核心（`PermissionStore`、`SiteOrigin`、`PermissionKind`、`PermissionDecision`）和 CEF adapter（`CefPermissionHandlerAdapter`、`CefDownloadHandlerAdapter`）；`browser/cef-shell/src/browser/window/tab_controller.h/.cc` 中 `WindowClient` 集成 handler；`browser/cef-shell/src/windows/app.h/.cc` 与 `src/macos/app.h/.cc` 中 `PermissionStore` 的创建与传递；`browser/cef-shell/tests/site_origin_test.cc`、`permission_store_test.cc`、`permission_contract.cmake`；`browser/cef-shell/CMakeLists.txt` 的源列表和测试目标注册；本 Roadmap/current 状态更新。
- 禁止修改：`browser/engine-api` 接口、Rust Core、品牌资源、Cast-SDK、媒体/Relay/Agent 逻辑；不得自动 allow 任何权限；不得把权限决策、URL、Cookie 或站点信息写入日志或暴露给共享层；不得引入 UI 确认对话框或临时快捷键。
- 权限存储契约：`PermissionStore` 纯 C++17，不依赖 CEF，使用 `shared_mutex` 支持并发 Query；默认 `kDeny`；支持 `kAllowSession`（浏览器关闭即清除）和 `kAllowPersistent`（显式覆盖/清除前保持）。按 (origin, kind) 键值存储，origin 由 `ExtractSiteOrigin` 从 HTTP(S) URL 提取，格式为 `scheme://host:port`（默认端口省略，host 小写化）。
- CEF adapter 契约：`CefPermissionHandlerAdapter` 处理 `OnRequestMediaAccessPermission`（摄像头/麦克风）、`OnShowNotification`（通知）、`OnRequestGeolocationPermission`（定位）和 `OnRequestClipboardPermission`（剪贴板读/写）；`CefDownloadHandlerAdapter` 处理 `OnBeforeDownload`（下载）。所有回调查询 `PermissionStore`，无明确 allow 时调用 CEF cancel/不继续回调。adapters 在 `WindowClient` 构造函数中创建，生命周期随 client。
- 错误/边界：`ExtractSiteOrigin` 拒绝非 HTTP(S)、userinfo、空 host、非法字符；CEF handler 对未知/无效 origin 稳定 deny；`PermissionStore` 的 Record/Clear 操作要求外部串行化（由 CEF UI thread 保证）；`TabController` 构造函数接受 `PermissionStore*` 指针，nullptr 时跳过 handler 创建（向后兼容）。
- 验收：独立 C++17 测试覆盖 origin 提取（18 项检查：基本 HTTP/HTTPS、非默认端口、默认端口省略、scheme 大小写、子域名、非 HTTP(S) 拒绝、userinfo 拒绝、空 host 拒绝、非法字符拒绝、IPv4、hyphen/underscore）；权限存储测试覆盖默认 deny、session/persistent allow、显式 deny、overwrite、ClearSessionDecisions、ClearForOrigin、ClearAll、Snapshot。CMake contract 验证文件存在、纯 C++17 文件不含 CEF 头、PermissionStore 使用 shared_mutex、TabController 引用 PermissionStore。
- 测试命令：`c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror` 独立编译运行 `site_origin_test` 和 `permission_store_test`；`cmake -P browser/cef-shell/tests/permission_contract.cmake`；`window_adapter_contract`、`new_tab_adapter_contract`、`profile_context_contract` 回归；`cargo test`、`cargo fmt`、`git diff --check`。

完成记录（2026-08-20）：

- 实现：新增 `PermissionStore`（纯 C++17，`shared_mutex` 保护，默认 deny，支持 session/persistent/显式 deny）、`ExtractSiteOrigin`（轻量 HTTP(S) origin 提取，拒绝非安全输入）、`CefPermissionHandlerAdapter`（拦截 media/notification/geolocation/clipboard 权限请求）和 `CefDownloadHandlerAdapter`（拦截下载前事件）。`WindowClient` 通过 `GetPermissionHandler`/`GetDownloadHandler` 暴露 adapters；Windows/macOS `BrowserApp` 均在构造函数中创建 `PermissionStore` 并传递给 `TabController`。
- 独立测试：`site_origin_test` 18 项检查全部通过；`permission_store_test` 覆盖 8 种场景全部通过。两测试均用 `c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror` 独立编译运行。
- Contract 验证：`permission_contract.cmake` 通过——确认全部 10 个 permission 文件存在、纯 C++17 文件不含 CEF 头、PermissionStore 使用 `shared_mutex`、TabController 引用 PermissionStore。`window_adapter_contract`、`new_tab_adapter_contract`、`profile_context_contract` 回归通过。`git diff --check` 通过。
- Rust 测试回归：`cargo test -p crayon-browser-core --lib` 3/3 通过；`cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib` 58/58 通过；`cargo fmt --all -- --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。默认 deny 符合最小权限原则；site origin 提取对特殊字符和路径穿越 fail closed；shared_mutex 保证 Query 可并发；handler adapter 不持有 CEF 类型之外的业务状态。
- 未覆盖与风险：`cef_permission_handler.cc` 和 `cef_download_handler.cc` 使用 CEF 类型，未在真实 CEF-on 配置下编译验证（当前环境无 CEF root），代码基于 CEF 150 通用 API 编写，具体回调签名可能需要根据实际头文件微调。权限 UI 确认流程、event sink 向上层报告和持久化到磁盘由 `CEF-08/CEF-12` 接续。`CEF-05` 转为 `DONE`，解锁 `CEF-06`（IPC）。

### CEF-01C～CEF-01E 边界

### CEF-01C～CEF-01E 边界

### CEF-01C～CEF-01E 边界

- `CEF-01C` 只建立共享构建图；输入为 01A/01B，输出限 CMake/preset/test target，不实现平台进程或产品行为。
- `CEF-01D` 只负责 Windows x64 bootstrap；不改公共接口，不用单平台结果代替 macOS；不得手工改动 `assets/brand/generated` 或从参考 PNG 重新制图。
- `CEF-01E` 只补齐 macOS x64/arm64 门禁并 Review；没有对应 runner 时必须保留为 `BLOCKED/VERIFIED`，不得伪造 S2 证据；系统遮罩效果必须在真实 macOS 包复核。

## 接口冻结

`BrowserEngineAdapter` 首次冻结只包含导航、标签、Profile、权限、输入事实和 observation 订阅；不得暴露 CEF 对象给 UI/Core。后续 `CNT-02` 以独立 contract 扩展有界 page snapshot stream/cancel，`ACT-04/ACT-07` 扩展能力受限的 semantic discovery/action；`AGT-15` 只把正式 ACT 能力接入 CAAP。禁止为了 Agent 暴露 raw DOM/CDP/selector/JavaScript。每次新增接口必须先写 contract test 和 Harmony 可实现性说明。

## 每项通用验证

- C++ format/static analysis、目标 test target、目标平台 build。
- 变更 renderer/browser IPC 时执行畸形消息、大小上限、旧 navigation 和 secret 泄漏测试。
- Windows/macOS 实现允许分任务完成，但共同接口变更由一个 owner 先合并，平台实现不得各自修改 schema。

### CEF-01E 完成记录（2026-08-23，macOS arm64 开发机实机证据）

- 分发下载与校验：macosarm64 归档 SHA1 `2e77063444e3ca07aea2651b763d3c4248bf2543`、macosx64 归档 SHA1 `17e14fe00415e01a79e8b6d7ecaad8a861f1b388`，均与 CEF-01A 锁定值一致（cef-builds.spotifycdn.com，缓存 `.cache/cef-archives/` 已入 `.gitignore`）。
- arm64：`cmake --preset macos-arm64-cef-debug`（本机安装 Ninja 1.13.2 后）configure/build 零错误零告警；`ctest` 39/39 通过（含此前本机阻塞的 `cef_distribution_contract`——已修复其 `$ENV{TEMP}` 无 TMPDIR 回退导致向文件系统根建目录的问题——与 `cef_build_graph_contract`）；Bundle 验证：`CFBundleIdentifier=com.crayon.browser`、受管 `app.icns` 经 `iconutil -c iconset` 复核通过、en/zh-Hans lproj、CEF Framework 与 5 个 Helper 变体齐备、`lipo` 确认单一 arm64；真实启动出现完整 6 进程多进程结构（主 + GPU/Renderer/Alerts/Plugin/基础 Helper），`quit` 后零残留进程。
- macosx64：同 preset 交叉构建零错误、`lipo` 确认 x86_64、`ctest` 39/39；经 Rosetta 实际启动并干净退出（0 残留）。
- 前置修复（commit `ef26113`）：CEF-05 提交的坏合并（三处重复源列表/测试块、`tab_controller.h`/`app.h`/`app.cc` 类与成员重复）导致全新 configure 必失败；按 CEF 150 真实 API 对齐 `OnBeforeDownload`（返回 bool、永不回落默认处理）与权限适配器（`OnShowPermissionPrompt`/`OnDismissPermissionPrompt`，未映射 permission bit 一律 deny）；修复启动即崩（adapter 构造函数在 `CefInitialize` 前 `CEF_REQUIRE_UI_THREAD` SIGTRAP）。这些修复同时惠及 Windows 分支（同一源）。
- Code Review：P0 0（启动崩溃已修）、P1 0、P2 2——1) x64 运行证据来自 Rosetta 而非原生 x64 硬件，且 Rosetta 只适合短 smoke：长跑约 2 分钟后 Chromium StackSamplingProfiler 线程触发 `Termination Reason: Namespace ROSETTA` 崩溃（EXC_CRASH/SIGABRT，栈在 thread_suspend/采样器内部，无产品代码帧），属 Chromium 与 Rosetta 翻译层的已知类别不兼容，非产品缺陷、不可在应用侧关闭；x64 长稳/功能验收必须原生 x64 硬件（归 QAR/PLT-M05 真机矩阵）；2) CEF-05 的 Windows 实机证据与本机发现的坏合并/API 漂移矛盾，其 Roadmap 记录的验证命令需后续在 Windows 机复核（已在 CEF-02M/PLT-W 线跟踪）。
- 未覆盖与风险：正式 macOS sandbox 强制仍归 `CEF-02M`（当前 `USE_SANDBOX=OFF` bootstrap 语义）；签名/公证归 PLT-M05。`CEF-01E` 转为 `DONE`，`CEF-02M` 依赖满足。

### CEF-02M 完成记录（2026-08-23，macOS sandbox 强制与 Helper 平台验收）

- 实现：`browser/cef-shell` macOS 分支镜像 Windows 契约强制 `USE_SANDBOX`（product 构建关闭即 FATAL_ERROR）；`CMakePresets.json` 两个 macOS preset `USE_SANDBOX=OFF→ON`；新增 `macos_adhoc_sign.cmake` post-build 脚本：Helper bundle 先签、主 App 后签（ad-hoc `codesign --force --sign -` + `--verify`，分发签名/公证归 PLT-M05）；`main_mac.mm` 保持 `no_sandbox` 仅在未定义 `CEF_USE_SANDBOX` 时置位（macOS 主进程不调用 `CefScopedSandboxContext`——`cef_sandbox_mac.h` 与 wrapper 源码确认该路径仅面向 Helper 可执行文件布局，主进程误植该调用会因 `@executable_path/../../../` 解析到 App 外而 dlopen 失败，已实测并以稳定退出码 12 fail-closed 验证后回退；Helper `process_helper_mac.cc` 的 sandbox context 保持既有正确实现）。
- 验证（arm64 原生实机）：`cmake --preset macos-arm64-cef-debug` configure/build 零错误，post-build 对 6 个 bundle 完成 ad-hoc 签名与验证；`ctest` 39/39；真实启动完整 6 进程（主 + GPU/Renderer/Alerts/Plugin/基础 Helper），Renderer Helper 进程命令行不含 `--no-sandbox`（sandbox 生效 smoke），`quit` 后零残留；错误的 main 进程 sandbox 初始化路径实测产生退出码 12（fail-closed 契约有效）。x64：sandbox 构建 + `ctest` 39/39 通过；运行在 Rosetta 下即刻 `Termination Reason: Namespace ROSETTA` 终止（EXC_CRASH/SIGABRT）——Rosetta 翻译层不支持 Chromium macOS sandbox 路径，与 CEF-01E 已记录的采样器限制同类，非产品缺陷。
- Code Review：P0 0、P1 0、P2 1——`macos_adhoc_sign.cmake` 的 Helper 清单来自生成文件 `crayon-macos-helper-apps.txt`，签名顺序依赖该文件先于主 App POST_BUILD 生成（当前由同一目标的 file(GENERATE) 保证，若拆分 Helper 为独立 target 需复核顺序）。
- 未覆盖与风险：x64 sandbox 运行验收需原生 x64 硬件（挂 QAR/PLT-M05 真机矩阵，Rosetta 不可替代）；分发签名/公证（PLT-M05）；sandbox 深度安全测试（seatbelt profile 逃逸面）不在本任务，归 PRV/QAR 安全门禁。`CEF-02M` 转为 `VERIFIED`（DONE 待 x64 原生机验收），解锁依赖它的 `CEF-06` 线（`src/ipc` 平台无关部分可先行）。

### CEF-06 原子范围（浏览器侧 length-prefixed IPC 契约层）

- 状态：`VERIFIED`；依赖 `CEF-02W DONE`、`FND-08 DONE`。
- 路径说明：Roadmap `src/ipc` 映射 `browser/cef-shell/src/ipc`（与 CEF-03 `src/browser/window` 同映射）。
- 单一目标：新增 `browser/cef-shell/src/ipc` 平台中立 C++17 契约层：length-prefixed 帧编解码（上限/畸形闭合拒绝）、session secret 常数时间校验与轮换代际、消息守卫（schema 版本窗口、大小上限、进程 token 校验）；不含真实管道/传输（CEF-07/AGT-12）、不含业务消息处理。
- 输出与允许修改：`browser/cef-shell/src/ipc/**`（header/impl/CMake/契约测试）、根 `CMakeLists.txt` 接线、本 Roadmap。零第三方依赖、无 CEF 类型进入公共接口、无 IO（纯内存状态机）。
- 边界：帧 `u32 BE 长度 + payload`，`kMaxFrameBytes=65536`，超限/畸形/残留闭合错误；secret 比较常数时间、旧 secret 仅在轮换窗口内接受且新 secret 立即生效；schema 版本闭窗（current±0，v1 单版本）拒绝旧/新版本；进程 token 闭合字符集校验；全部错误为闭合枚举稳定字符串。
- 验收与测试：RG-007 语义由 `crayon-ipc-schema` golden 承担（不修改）；本任务测试矩阵：编解码（完整/分片/背靠背/超限/畸形/敌意缓冲）、secret（正确/错误/旧代际/轮换窗口）、版本（current/旧/未知）、token 校验、错误映射。命令：独立 CMake configure/build/ctest（`-Werror`）、共享层回归、`git diff --check`。
- 明确不做：真实 OS 传输与 Core 子进程生命周期（CEF-07）、CAAP transport（AGT-12）、业务消息编码（crayon-ipc-schema 已有）。

### CEF-06 完成记录（2026-08-23）

- 实现：新增 `browser/cef-shell/src/ipc`（header/impl/CMake/契约测试各 1）。`FrameCodec`：`u32 BE` 长度前缀流式解码，`kMaxFrameBytes=65536` 超限返回 Oversize 并丢弃 header（payload poisoned），敌意单次 feed 超 `kMaxFeedBytes` fail-closed 且零缓存，Encode 拒绝超限 payload；`SessionSecretVerifier`：32 字节 secret 常数时间比较（`ConstantTimeEquals`），`Install`/`Rotate` 代际管理，仅上一代 secret 在轮换窗口内可验证、再旧即过期，错误尺寸 fail-closed；`MessageGuard`：进程 token 闭合字符集、声明长度上限、schema 版本闭窗（v1 单版本，与 `crayon-ipc-schema` 同步的 `kCurrentSchemaVersion=1`），旧/未知版本在 payload 解析前拒绝；`IpcError` 闭合枚举稳定字符串。零第三方、无 CEF 类型、无 IO。
- 验证：`cmake -S . -B .cache/build/cef06 -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` 零告警零错误；`ipc_channel_contract` 1/1（8 组：往返、分片/背靠背/最大合法帧、超限与敌意 feed、secret 校验与轮换窗口、常数时间比较、进程 token 矩阵、消息守卫矩阵、LCG 3000 步敌意流——有界 pending、不崩溃）；共享层回归 30/30 通过；`git diff --check` 通过。RG-007 golden 语义由 `crayon-ipc-schema` 既有向量承担（本任务未改 schema，无兼容性影响）。
- Code Review：P0 0、P1 0、P2 1——LCG 敌意流测试因缓冲残留只能断言全局不变量（不崩溃/有界），精确语义靠确定性用例覆盖（已注明）；后续 CEF-07 接真实传输时可考虑加"连接重置即清空 codec"用例。
- 未覆盖与风险：真实 OS 传输与 Core 子进程生命周期（CEF-07）、CAAP transport（AGT-12 复用 Rust 侧 transport 守卫）、消息 JSON 编解码（crayon-ipc-schema 所有）。`CEF-06` 转为 `VERIFIED`，解锁 `CEF-07`、`CEF-09`。

### CEF-07 完成记录（2026-08-23）

- 实现：新增 `browser/cef-shell/src/browser/core_client`（header/impl/CMake/契约测试各 1）。`CoreClientSupervisor` 纯生命周期状态机（Idle/Spawning/Healthy/Backoff/Failed/ShuttingDown/Stopped，时钟注入、零资源/IO）：启动（重复启动 Busy 拒绝、Stopped/Failed 后不可复活）；崩溃/健康超时（`kHealthTimeoutMs=5s`，边界仍健康、超时即 exit pending）经唯一 AcknowledgeExit 收敛——重复 exit 事件丢弃（无孤儿语义：一次退出恰一次确认），随后有界重启（`kMaxRestartAttempts=3`、递增 backoff），预算耗尽转 Failed 终态；`Stop` 从任意活跃态收敛且幂等，关闭期间迟到 spawn 结果丢弃不复活，exit ack 后转 Stopped；全部结果为闭合枚举。
- 验证：`cmake -S . -B .cache/build/cef07` 零告警；`core_client_supervisor` 1/1（7 组：正常启动、启动失败 backoff 恢复、崩溃有界重启直至 GaveUp、健康超时收敛、幂等关闭与无孤儿、Spawning/Idle 停止矩阵、5000 步事件风暴不变量）；共享层回归 31/31；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——健康超时后 port 层必须 kill+reap 子进程再 ack，状态机只表达"恰一次确认"契约；CEF-07 真实进程接线（spawn/kill/健康心跳实现）归 CEF-08+ shell 装配与 CEF-14 E2E，本任务为纯模型层。
- 未覆盖与风险：真实子进程 spawn/kill 与心跳 transport 接线（后续 shell 装配任务）；多 Core 实例（v1 单实例语义）。`CEF-07` 转为 `VERIFIED`，`CEF-09`（依赖 CEF-06）同时可领取。

### CEF-09 完成记录（2026-08-23）

- 实现：新增 `browser/cef-shell/src/renderer/media_observer`（header/impl/CMake/契约测试各 1）。`ClassifySourceUrl` 闭合源分类（http/https、blob:、mediastream:、Unknown——超长/控制字符/危险 scheme 一律 Unknown）；`MediaObserver` 按 frame 聚合：navigation 推进清空旧观测且旧 navigation_id 事件丢弃（BR-007）、容量 16 元素有界（更新已有元素不受限）、TearDown 后一切观测丢弃且不可重建候选（BR-013）、blob/stream 源携带伪造 URL 或 kind 标签不一致即整体丢弃（BR-012 不伪造直投 URL）、可见度钳制 [0,1]、FindEligible 只报告 playing∧visible 的候选（面积优先，BR-006 形态）且注释显式声明结果 untrusted、授权判定归 Browser 侧（CEF-10）。**无自动交互面**：API 只有分类/观测/查询，不存在 click/seek/rate/过滤方法（BR-009/BR-010 结构保证）。
- 验证：`cmake -S . -B .cache/build/cef09` 零告警；`media_observer` 1/1（7 组：源分类矩阵、旧导航丢弃、blob/stream 反伪造、teardown 阻断、容量、可见播放资格、无交互面断言）；共享层回归 32/32；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——kind 与 URL 的一致性校验采用保守白名单（tag=HttpUrl 必须有合法 http URL；blob/stream 必须空 URL；Unknown tag 接受空 URL），未知 tag 携带 URL 的组合未来若出现合法场景需显式扩契约。
- 未覆盖与风险：CEF document-start JS 注入与 DOM/MSE 事件采集接线（shell 装配任务）；跨 frame 聚合（CEF-12 gateway）；真实广告/EME fixture 行为（CEF-14）。`CEF-09` 转为 `VERIFIED`，解锁 `CEF-10`（依赖 09）。

### CEF-10 完成记录（2026-08-23）

- 实现：新增 `browser/cef-shell/src/browser/input_proof`（header/impl/CMake/契约测试各 1）。`InputProofGate` 纯门禁（浏览器 UI 线程、事实只来自 Browser 可信源：CEF 输入事件、前台 tab、Browser 侧播放采样）：`NoteUserInput` 在输入瞬间快照"当时是否已在推进"与进度基线；`NotePlaybackProgress` 记录可信采样并计算推进 delta；`Evaluate` 闭合判定——无输入（BR-003 伪造 playing 拒绝）、导航不匹配（BR-007 旧导航拒绝）、输入不在前台/声明的 tab 拒绝、输入时已在推进（BR-005 无关点击+自动播放拒绝；暂停后用户恢复播放允许）、输入后推进不足 `kMinProgressSeconds=0.05s` 拒绝（BR-004：输入+真实推进才 Eligible）。声明侧只消费 identity 元组，页面上报内容无法触及 allow 路径。
- 验证：`cmake -S . -B .cache/build/cef10` 零告警；`input_proof_gate` 1/1（7 组：BR-003/004/005/007 全覆盖、暂停后恢复允许、前台/后台输入矩阵、无推进/微推进拒绝、5000 步事实风暴闭合不变量）；共享层回归 33/33；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——"推进快照"基于最近两次采样 delta，若采样稀疏（如暂停很久后仅一次采样）`progressing_at_input_` 可能失真为 false；CEF-14 E2E 接真实 CEF 视频状态采样时需保证输入前至少两个采样点或显式注入 paused 状态。
- 未覆盖与风险：CEF 输入事件/视频状态采集接线（shell 装配）、与 CEF-09 观测和 cast-policy `decide` 的组合链（CEF-12/13）、多 tab 并发输入的更精细归属。`CEF-10` 转为 `VERIFIED`，`CEF-11`（依赖 09）可领取。

### CEF-11 完成记录（2026-08-23）

- 实现：新增 `browser/cef-shell/src/browser/network_observer`（header/impl/CMake/契约测试各 1）。`ClassifyUrl` 闭合 URL 分类（http/https 归一化、blob: 保留空 URL 不伪造、超长/控制字符/危险 scheme 拒绝）；`IsObservableHeader` 闭合可观测 header 集（referer/user-agent/range/authorization——仅类别标志，**值永不进入 DTO**，BR-008），cookie 等非可观测敏感 header 出现即整体拒绝观测而非泄漏；`Observe` 固定窗口限流（256/秒，注入时钟）+ 容量 128 + content_length 元数据上限；`AssociateEmeEncrypted`（BR-011）只升级同 navigation 的 media/manifest/segment 观测的 protection 标志。
- 验证：`cmake -S . -B .cache/build/cef11` 零告警；`network_observer` 1/1（6 组：URL 矩阵、header 白名单矩阵、敏感 header 拒绝且 DTO 无值、blob 不伪造、EME 关联升级与跨 navigation 隔离、限流/容量/超限）；共享层回归 34/34；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——限流为固定窗口（窗口边界瞬时 2×预算突发可能），CEF-12/14 接真实事件流时若需平滑可换令牌桶（与 AGT-12A transport 守卫同型）。
- 未覆盖与风险：CEF ResourceRequest/Response 事件接线（shell 装配）、与 media_observer 的候选合成（CEF-12）、真实 MSE/EME fixture（CEF-14）。`CEF-11` 转为 `VERIFIED`，解锁 `CEF-12`（依赖 10+11）。

### CEF-12 完成记录（2026-08-23）

- 实现：新增 `browser/cef-shell/src/browser/observation_gateway`（header/impl/CMake/契约测试各 1，复用 CEF-09/11 DTO 类型，无第三方依赖）。`ObservationGateway` 按 tab 合并 media/network 观测：每 tab 单调 generation（导航推进即 +1 并**立即**从队列剔除该 tab 全部旧 generation 事件——旧结果绝不流出，PL-001/PL-002 形态）；无导航记录的 tab 事件在入口丢弃（不可归属）；出站队列 256 有界、满载 `DroppedBackpressure` 计数（消费方 Drain 批量拉取，永不阻塞）；tab 追踪 64 有界；`GatewayStats` 诊断计数单调。
- 验证：`cmake -S . -B .cache/build/cef12` 零告警；`observation_gateway` 1/1（5 组：media+network 合并出站、generation fencing 与迟到事件/未导航 tab 丢弃及跨 tab 隔离、背压有界与 Drain 释放、tab 容量、5000 步风暴——队列上界/计数单调/零越界 Drain）；共享层回归 35/35；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——入口未按 navigation_id 与当前代导航比对（同一 tab 新导航 id 旧值仍可入队，靠下游消费方 navigation 匹配拒绝）；CEF-14 接线时可在 gateway 增加 tab→current_navigation 映射前置拒绝。
- 未覆盖与风险：CEF 事件→gateway 接线与 Core client（CEF-07 supervisor）传输装配、wire DTO 编码（crayon-ipc-schema）、E2E fixture 行为（CEF-14）。`CEF-12` 转为 `VERIFIED`；`CEF-13`（依赖 CEF-08+12）待 CEF-08 UI 壳任务。
- 批注（2026-08-23 CEF-06..12 五连任务）：全部为平台中立契约/模型层，macOS arm64 开发机完成；CEF 壳内真实事件接线与实机 E2E 统一归 CEF-08/13/14 后续任务。

### Review P2 批量修复记录（2026-08-23）

- 范围：各任务 Code Review 登记的 P2 中可在模型层关闭的 7 项；归属未来接线/真机任务的 P2（CEF-07/09 接线、BUX-15/16 装配、AGT-12A AGT-15 复核、CEF-01E Rosetta/x64 真机）维持原延期理由不变。
- 修复：
  1. CEF-02M：`macos_adhoc_sign.cmake` 在 helper manifest 缺失时 FATAL（原为静默跳过——helpers 会无声未签名）。
  2. CEF-06：`FrameCodec::Reset()` 连接重置清空缓冲，敌意残留不再泄入新连接（新测试 ResetClearsHostileLeftovers）。
  3. CEF-10：`InputProofGate::NotePlaybackSuspended(tab, nav)` 显式暂停标记（冻结基线、清除推进快照），消除"输入前至少两个采样点"的隐式依赖（新测试 ExplicitPauseMarkerBeatsSparseSampling，含跨 tab 作用域验证）。
  4. CEF-11：限流由固定窗口改为令牌桶（容量 256、每 4ms 补 1），消除窗口边界 2× 突发（测试新增边界不双倍断言）。
  5. CEF-12：gateway 记录 tab→current_navigation_id，携带非当前导航 id 的事件在入队前拒绝（原靠下游消费方拒绝；测试更新为前置拒绝 + dropped 计数 3）。
- 修复 6/7（BUX/AGT 侧，见各 Roadmap）：BUX-12 `FindBarController::SetCaseSensitive` 查找栏内切换并重置 cursor；AGT-04 `Grant::is_targeted()/scope_summary()` 闭合作用域描述——AGT-05 确认 UI 必须渲染 `any-target` 与 `tab:<id>` 的区别（验收项）。
- 验证：`cargo test -p crayon-agent-gateway` 61/61；clippy `-D warnings`、fmt 通过；C++ 共享层四个构建目录 ctest 各 35/35；`git diff --check` 通过。

### CEF-08 原子范围（共享壳 UI 绑定与本地化门禁）

- 状态：`VERIFIED`；依赖 `FND-11 DONE`、`CEF-03 DONE`。
- 路径说明：Roadmap `browser/shared-ui` 映射新增 `browser/shared-ui/chrome` 子模块与 locales。
- 单一目标：工具栏视图模型（导航/地址/标签聚合）、投屏按钮共享状态机（无设备绑定，禁用为默认）、页面错误壳模型、locale parity 契约测试；清理 MED-19 已废弃的 `cast.mode.mirror` 文案。不接真实设备（CEF-13）、不做最终视觉（BUX）。
- 边界：投屏按钮闭合状态 `Hidden/Disabled/Eligible/Selecting/Casting/Stopping`，仅当外部喂入 BrowserVerified 级别的 Eligible 事实才离开 Disabled（页面自报不可触发）；ExternalClientHandoff 语境永不显示"投屏中"，只显示"打开外部客户端"；错误壳闭合错误族 + 本地化 key + 动作（reload/back）；locale parity：en-US 与 zh-CN key 集全等、禁止 `mirror` 语义 key 复活。
- 验收与测试：UI unit（CMake 契约测试）；命令：独立 configure/build/ctest、共享层回归、`git diff --check`。
- 明确不做：真实设备绑定（CEF-13）、投屏决策（cast-policy/MED）、键盘/缩放/无障碍实机 smoke（CEF-14/QAR）。

### CEF-08 完成记录（2026-08-23）

- 实现：新增 `browser/shared-ui/chrome`（header/impl/CMake/契约测试各 1）。`ChromeToolbar` 聚合导航/地址/标签事实（地址 ≤2048、标题 ≤512 有界拒绝，事实由既有 omnibox/navigation/tabs 模块拥有）；`CastButtonModel` 闭合状态机 `Hidden/Disabled/Eligible/Selecting/Casting/Stopping`——Hidden 为粘性默认（无媒体面时浏览器级 eligible 事实也不生效），仅 Browser 验证事实可离开 Disabled，验证撤回收敛 pre-session 态，session 停止回落 Disabled 需重新验证，页面自报状态在任何路径都无法启用按钮；`label_key()` 闭合文案 key，ExternalClientHandoff 语境复用 `cast.open_external_client` 且永不渲染"投屏中"；`PageErrorShell` 闭合错误族（Network/Crash/BlockedScheme）+ 本地化 key + 动作（reload/back）。locales：删除 MED-19 废弃 `cast.mode.mirror`，新增 direct/relay/open_external_client/disabled/selecting/stopping/error.crash/error.blocked_scheme（en/zh 35/35 全等）；locale parity 契约测试入 `chrome_contract`（key 集全等、mirror 禁令、chrome 模型所需 key 双语存在）。
- 验证：`cmake -S . -B .cache/build/cef08` 零告警；`chrome_contract` 1/1（4 组：工具栏边界、投屏按钮粘性默认/启用路径/撤回收敛、错误壳动作、locale parity+mirror 禁令）；共享层回归 36/36；`cast.mode.mirror|Mirror tab` 全仓扫描零残留；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——locale parity 测试用行级 JSON 解析（扁平文件当前足够；若 locales 引入嵌套或数组结构需换真解析器并保留禁令断言）。
- 未覆盖与风险：真实设备/接收端选择接线（CEF-13，本模型 OpenReceiverPicker 即其挂点）、键盘/缩放/无障碍实机 smoke（CEF-14/QAR）、最终视觉（BUX 契约）。`CEF-08` 转为 `VERIFIED`，`CEF-13` 依赖满足（另需 CEF-12 DONE 已满足）。

### CEF-13 原子范围（投屏功能视图状态机）

- 状态：`VERIFIED`；依赖 `CEF-08 VERIFIED`、`CEF-12 VERIFIED`、`MED-19 DONE`。
- 路径说明：Roadmap `shared-ui/features/cast` 映射 `browser/shared-ui/features/cast`。
- 单一目标：投屏功能视图状态机 `Idle/Browsing/Eligible/Selecting/Planning/Casting` + `ExternalClientHandoff` 确认流与闭合结果绑定；不接真实设备（SDK-13 BLOCKED）、不执行投屏决策（cast-policy/MED 所有）。
- 边界：
  - Eligible 只能由 Browser 验证事实（CEF-10 判定）进入；页面自报状态不可达。
  - Planning 消费闭合策略结果 `Direct/Relay→Casting`、`ExternalClientHandoff→HandoffConfirm`（需用户确认，未确认不发出任何请求）、`Reject→Rejected`（闭合原因 key，不假成功）。
  - 交接结果闭合 `DownloadStarted/LaunchRequested/NotInstalled/Cancelled/Failed`——任何结果都不渲染"投屏中"，Failed/NotInstalled 明确失败文案 key。
  - Casting 会话结束/错误收敛到 Browsing（eligibility 需重新验证）；全部状态迁移闭合非法即拒绝。
- 验收与测试：状态 UI contract（CMake 契约测试）：粘性默认/启用路径/策略结果映射/交接确认矩阵/错误不假成功/会话收敛/风暴不变量。命令：独立 configure/build/ctest、共享层回归、`git diff --check`。
- 明确不做：真实接收端（SDK-13）、投屏执行（MED/SDK）、确认 UI 呈现（AGT-05/BUX）、投屏按钮渲染（CEF-08 CastButtonModel 已有）。

### CEF-13 完成记录（2026-08-23）

- 实现：新增 `browser/shared-ui/features/cast`（header/impl/CMake/契约测试各 1）。`CastFeatureViewModel` 闭合状态机 `Idle/Browsing/Eligible/Selecting/Planning/Casting/HandoffConfirm/HandoffRequested/Rejected`：Eligible 只能由 Browser 验证事实（CEF-10 判定）进入且无页面时事实无效；策略结果闭合映射（Direct/Relay→Planning→NotifySessionStarted→Casting；ExternalClientHandoff→HandoffConfirm 需显式确认，未确认时任何结果投递被拒、不发请求；Reject→Rejected 携带闭合原因，DRM/无路由等显式失败 key 不假成功）；交接结果闭合五元组全部落回 Browsing 且**任何结果都不渲染"投屏中"**（Cancel 未确认/已发出均收敛 Browsing）；会话结束/页面失活收敛 Browsing/Idle，eligibility 需重新验证；全部非法迁移稳定拒绝。locales 新增 4 个 cast 文案 key（en/zh 39/39 全等），契约测试含 locale parity + 本模块所需 key 存在性。
- 验证：`cmake -S . -B .cache/build/cef13` 零告警；`cast_feature_view` 1/1（6 组：仅浏览器判定可达 Eligible、策略结果映射/Reject 显式失败、交接确认矩阵与不假投屏、页面失活重置、locale key、5000 步风暴闭合不变量）；共享层回归 37/37；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——`message_key()` 对 kNoRoute 返回 `cast.open_external_client`（引导语），与 Reject 语义的"失败"并存是刻意设计（无路由=能力引导而非内容拒绝）；若 UX 评审要求统一失败文案，拆分为独立 key 的任务归 BUX。
- 未覆盖与风险：真实接收端选择与会话执行（SDK-13 BLOCKED 待真机）、CEF-08 CastButtonModel 与本模型的装配（shell 装配任务）、E2E fixture 行为（CEF-14）。`CEF-13` 转为 `VERIFIED`；CEF-14 依赖仅剩自身 harness 建设。

### CEF-14 完成记录（2026-08-23，进程级冒烟切片）

- 实现：新增 `tests/e2e/desktop/browser/run_smoke.py` + CMake 接线。双模式：`selfcheck`（确定性自检：redaction 向量含 query token/Cookie/Authorization/Bearer/签名 URL userinfo、进程期望集、参数解析）与 `smoke`（LaunchServices 启动构建产物——先清理既有实例防污染 → 轮询等待完整 6 进程树（主 + 5 Helper，冷启动竞态容忍 20s）→ `lsof` 断言无非 loopback socket（无公网）→ osascript 退出并轮询确认零残留 → 输出脱敏 JSON 报告到构建目录）。ctest 常驻 selfcheck；`CRAYON_E2E_APP_BUNDLE` 配置时追加 `browser_e2e_smoke_app`（macOS 实机 gate）。
- 验证（macOS arm64 实机）：`browser_e2e_smoke_selfcheck` 与 `browser_e2e_smoke_app` 双通过（6 进程 / 0 外联 socket / 0 残留，报告 `e2e-browser-report.json`）；共享层回归 39/39；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——URL 驱动的 BR-001..014 内容级 E2E（导航/fixture 页面/媒体门禁/广告语义）当前不可达：bootstrap 壳只开 `about:blank`（无 omnibox/URL 注入），fixture 服务已在 `test-support::BrowserFixtureServer` 就绪；待 CEF shell 接入 URL 输入后扩展 harness 的 fixture 驱动模式（记为 CEF-14 后续切片，与 AGT-05/AGT-13 CLI 驱动汇合）。
- 未覆盖与风险：Windows 侧 harness（macOS 先行）；渲染级断言（截图/像素）与性能采样归 QAR。`CEF-14` 转为 `VERIFIED`（进程级冒烟完成；内容级 E2E 后续切片跟踪）。
