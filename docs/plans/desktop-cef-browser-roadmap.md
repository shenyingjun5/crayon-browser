# CEF：Desktop 浏览器壳 Roadmap

状态：`CEF-01A..01D DONE`，`CEF-01E VERIFIED`，`CEF-02W DONE`，`CEF-03 IN_PROGRESS`；Windows 正式 CEF bootstrap 多进程与 sandbox 已完成并实机验证，当前实现 Windows 窗口/标签生命周期与基础导航。macOS bootstrap 保持冻结且不再阻塞 Windows 主线，后续由 `CEF-02M` 恢复平台对齐。当前目标平台仍为 Windows、macOS；Linux 不在当前范围。每项以目标路径、测试 ID 和证据作为验收，不以单平台截图替代。

## 原子任务

| ID | 状态 | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|---|
| CEF-01A | DONE | FND-08 | `cmake/cef`、`tests/contracts`、第三方文档 | 固定 CEF Standard revision、四平台官方 hash、许可和本地缓存/离线根契约 | 确定性 contract test；错误平台/缺失根/错误版本/校验失败；Windows 包实下载校验 | S1 |
| CEF-01B | DONE | CEF-01A | `browser/engine-api` | 不含 CEF 类型和产品策略的 C++17 `BrowserEngineAdapter` 最小接口、独立 Fake 与 compile contract | 接口 contract；Fake 生命周期/错误/重复释放；Harmony 可实现性说明 | S1 |
| CEF-01C | DONE | CEF-01B | 根 `CMakeLists.txt`、`CMakePresets.json`、`cmake/cef` | 共享 CMake/preset、离线 CEF root 接入和最小 test target | preset/schema、无网络 configure、错误 root、RG-005 | S1 |
| CEF-01D | DONE | CEF-01C,BRD-04 | `browser/cef-shell` Windows 构建文件、验证记录 | Windows x64 CEF 最小壳 configure/build；只消费 `assets/brand/generated/windows/app.ico`/PNG 完成 Debug/Release 资源装配 | VS2022 configure/build；启动前依赖 smoke；验证 EXE/窗口/任务栏图标；包不入 Git | S2 |
| CEF-01E | VERIFIED | CEF-01D | macOS 构建文件、CI/验证记录 | macOS x64/arm64 configure/build；只消费 `assets/brand/generated/macos/AppIcon.iconset`/`app.icns` 并收口 bootstrap Review | 两个 macOS 架构 CI/configure/build；`iconutil`/包内图标资源复核；P0/P1=0；未实机项显式记录 | Windows/static 证据已完成；S2 待运行 |
| CEF-02W | DONE | CEF-01D | `browser/cef-shell/src/process/windows`、Windows 构建/测试 | 固定 CEF bootstrap DLL/EXE 多进程入口，Debug/Release 强制 sandbox，品牌窗口资源不回退 | Windows Debug/Release build；Browser/render/GPU sandbox smoke；启动/退出无残留；无业务代码在 main | S3 |
| CEF-02M | TODO | CEF-01E,CEF-02W | `browser/cef-shell/src/process/macos` | 在 02W 冻结的进程/错误契约上补齐 macOS sandbox 与 Helper 平台验收 | macOS x64/arm64 启动/退出；sandbox smoke；Helper 无业务 | V4M |
| CEF-03 | IN_PROGRESS | CEF-02W | `src/browser/window` | Windows 首发的单窗口/标签生命周期、导航、前后退、刷新、停止、缩放；共享接口保持 macOS 可实现 | BR-001、重复关闭、崩溃恢复；Windows 实机资源无泄漏 | S3 |
| CEF-04 | TODO | CEF-03 | `src/browser/context` | 临时/持久 `CefRequestContext` factory，Profile ID 不用名称作路径 | BR-002、PV-001、PV-004 基础；context 隔离 | S3 |
| CEF-05 | TODO | CEF-04 | `src/browser/permission` | 摄像头/麦克风/通知/定位/剪贴板/下载按站点控制 | allow/deny/remember/session tests；默认最小权限 | S3 |
| CEF-06 | TODO | CEF-02W,FND-08 | `src/ipc`、`crayon-ipc-schema` | length-prefixed IPC、session secret、schema/大小/进程校验 | RG-007；畸形/超大/错误 secret/旧版本 | S2 |
| CEF-07 | TODO | CEF-06 | `src/browser/core_client` | Core 子进程启动、健康、崩溃、有界关闭与重连 | 启动失败/崩溃/超时/退出；无 orphan | S3 |
| CEF-08 | TODO | FND-11,CEF-03 | `browser/shared-ui` | 地址栏、标签、投屏按钮、错误/权限壳和本地化，不接真实设备 | UI unit；locale parity；键盘/缩放/无障碍 smoke | S3 |
| CEF-09 | TODO | CEF-06 | `src/renderer/media_observer` | 独立 document-start 资源：media events、可见性、frame/navigation ID；无自动交互 | BR-003..BR-013；尤其 BR-009、BR-010 | S2 |
| CEF-10 | TODO | CEF-09 | `src/browser/input_proof` | Browser process 可信输入、前台标签和播放推进交叉校验 | BR-003、BR-004、BR-005、BR-007；页面伪造全部失败 | S2 |
| CEF-11 | TODO | CEF-09 | `src/browser/network_observer` | ResourceRequest/response observation，仅允许字段并有大小/速率上限 | BR-008、BR-011、BR-012；敏感 header/正文不进入 DTO | S2 |
| CEF-12 | TODO | CEF-10,CEF-11 | `src/browser/observation_gateway` | DOM/network observation 合并并发送 Core，generation fencing | PL-001、PL-002；导航迟到事件；背压/dropped | S2 |
| CEF-13 | TODO | CEF-08,CEF-12,MED-19 | `shared-ui/features/cast` | `Idle/Browsing/Eligible/Selecting/Planning/Casting` 与 `ExternalClientHandoff` 视图绑定 | 状态 UI contract；未播放禁用；交接需确认；错误不假成功 | S3 |
| CEF-14 | TODO | CEF-05,CEF-07,CEF-12,CEF-13 | `tests/e2e/desktop/browser` | Windows/macOS 本地 fixture E2E harness、截图/日志脱敏产物 | BR-001..BR-014 适用项；无公网 | S3 |
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

- 状态：`VERIFIED`；依赖 `CEF-01D DONE`；未达到 `DONE`，因此尚未解锁 macOS 对齐任务 `CEF-02M`，但不阻塞 Windows 任务 `CEF-02W`。
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

### CEF-03 Windows 窗口、标签生命周期与基础导航（当前任务）

- 状态：`IN_PROGRESS`；依赖 `CEF-02W DONE`。一次只推进本任务，`BUX-02 READY` 暂不领取。
- 单一目标：在 Browser process UI thread 建立 Windows 首发的唯一窗口/标签 owner，接通新建、激活、关闭、前进、后退、刷新、停止和缩放命令，并把 CEF 回调归一为可供后续 UI/engine adapter 消费的稳定状态；共享状态与命令不暴露 CEF/Win32 类型。
- 输入：CEF 150 Alloy Views/BrowserView/Window 生命周期 API、`browser/engine-api` 已冻结的 Tab/Navigation/Zoom 语义、02W 的品牌 client DLL 与退出契约、BR-001 和 UX-005 的适用边界。
- 输出与允许修改：新增 `browser/cef-shell/src/browser/window/` 小型状态 owner 与 CEF adapter、独立 `tests/window_*`，并仅为装配修改 Windows `app.*`、CEF shell CMake/source contract 和本 Roadmap/current 索引。生产文件预计不超过 8 个，单个职责文件不得混入 UI glyph、Profile 存储或媒体/投屏逻辑。
- 禁止修改：`browser/engine-api` 公共头、macOS/Harmony、Profile/request context、共享 UI 视觉、omnibox/起始页、下载/权限、媒体/Relay/Cast-SDK/Agent；不得暴露 CEF handle 给共享层，不得增加公网测试 URL、远程调试、任意 JS/CDP 或测试开关。
- 状态/生命周期：窗口、tab 顺序、active tab、CEF browser/view 绑定和 navigation generation 各有唯一 owner；异步创建/关闭、重复 close、最后标签关闭、窗口主动关闭、renderer 崩溃和旧 browser callback 必须稳定处理。关闭最后一个标签退出窗口；窗口销毁只在所有 browser 关闭后退出消息循环；所有命令只在 CEF UI thread 执行。
- 边界：初始页面仍使用内部 `about:blank`，视觉标签栏/导航栏由 `BUX-02/04/05/06` 接续。本任务可以注册平台惯用的确定性快捷键用于实机命令 smoke，但不得把临时快捷键当成最终 UI。Profile context 统一暂传 `nullptr`，其隔离与持久化由 `CEF-04` 完成。
- 验收与测试：纯状态测试覆盖新建/激活/顺序、重复/旧 close、容量、最后标签、loading/history/URL、navigation generation、缩放上下界和 crash detach；CEF contract 覆盖 BrowserView/Window 创建、callback/command 映射与无同步伪完成；Debug/Release build+ctest；Windows 实机覆盖新标签、切换、关闭、缩放/刷新、窗口关闭和完整路径零残留。BR-001 的公网无关部分使用本地确定性 fixture；如自动 fixture driver 尚未具备，必须明确记录为后续 E2E 缺口，不能伪造通过。
- 测试命令：Windows Debug/Release build 与 `ctest`、目标 window model/CEF contract、适用 `clang-format`、`scripts/check.ps1 fast`、`scripts/check.ps1 security`、`git diff --check` 和实际 GUI/进程 smoke。
- 暂停点（2026-08-12）：已冻结上述任务边界，并新增 `src/browser/window/tab_model.h/.cc` 草稿，覆盖 tab ID/顺序、active、创建/绑定/重复关闭/脱离、loading/history/URL、navigation generation、crash 状态和 zoom 边界的唯一状态 owner。草稿已执行 `clang-format`，并用仓库正式 MSVC x64 工具链以 `/std:c++17 /W4 /WX` 独立编译通过；`scripts/check.ps1 fast` 和 `git diff --check` 通过。LLVM 19 独立编译尝试因本机 MSVC 14.51 STL 要求 Clang 20 失败，改用正式 MSVC 后通过。当前尚未加入 CMake、尚未补独立行为测试、尚未接入 CEF Views/Windows App，也未运行 CEF Debug/Release build、ctest 或实机验证；因此状态严格保持 `IN_PROGRESS`。恢复时先审查该草稿并补状态单测，测试通过后再接 CEF adapter，不得直接跳到 `BUX-02` 或标记 `IMPLEMENTED`。

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
