# CEF：Desktop 浏览器壳 Roadmap

状态：`CEF-01A DONE`，`CEF-01B READY`；用户切换到 SDK 源码接入后本任务退回待领取，尚未创建 `browser/engine-api` 生产代码。当前目标平台为 Windows、macOS；Linux 不在当前范围。每项以目标路径、测试 ID 和证据作为验收，不以单平台截图替代。

## 原子任务

| ID | 状态 | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|---|
| CEF-01A | DONE | FND-08 | `cmake/cef`、`tests/contracts`、第三方文档 | 固定 CEF Standard revision、四平台官方 hash、许可和本地缓存/离线根契约 | 确定性 contract test；错误平台/缺失根/错误版本/校验失败；Windows 包实下载校验 | S1 |
| CEF-01B | READY | CEF-01A | `browser/engine-api` | 不含 CEF 类型和产品策略的 C++17 `BrowserEngineAdapter` 最小接口、独立 Fake 与 compile contract | 接口 contract；Fake 生命周期/错误/重复释放；Harmony 可实现性说明 | S1 |
| CEF-01C | TODO | CEF-01B | 根 `CMakeLists.txt`、`CMakePresets.json`、`cmake/cef` | 共享 CMake/preset、离线 CEF root 接入和最小 test target | preset/schema、无网络 configure、错误 root、RG-005 | S1 |
| CEF-01D | TODO | CEF-01C | `browser/cef-shell` Windows 构建文件、验证记录 | Windows x64 CEF 最小壳 configure/build 与 Debug/Release 资源装配证据 | VS2022 configure/build；启动前依赖 smoke；包不入 Git | S2 |
| CEF-01E | TODO | CEF-01D | macOS 构建文件、CI/验证记录 | macOS x64/arm64 configure/build 门禁并收口 bootstrap Review | 两个 macOS 架构 CI/configure/build；P0/P1=0；未实机项显式记录 | S2 |
| CEF-02 | TODO | CEF-01E | `browser/cef-shell/src/process` | Browser/render/GPU 子进程入口与 sandbox 开关；正式构建强制 sandbox | Windows/macOS 启动/退出；sandbox smoke；无业务代码在 main | S3 |
| CEF-03 | TODO | CEF-02 | `src/browser/window` | 单窗口/标签生命周期、导航、前后退、刷新、停止、缩放 | BR-001、重复关闭、崩溃恢复；资源无泄漏 | S3 |
| CEF-04 | TODO | CEF-03 | `src/browser/context` | 临时/持久 `CefRequestContext` factory，Profile ID 不用名称作路径 | BR-002、PV-001、PV-004 基础；context 隔离 | S3 |
| CEF-05 | TODO | CEF-04 | `src/browser/permission` | 摄像头/麦克风/通知/定位/剪贴板/下载按站点控制 | allow/deny/remember/session tests；默认最小权限 | S3 |
| CEF-06 | TODO | CEF-02,FND-08 | `src/ipc`、`crayon-ipc-schema` | length-prefixed IPC、session secret、schema/大小/进程校验 | RG-007；畸形/超大/错误 secret/旧版本 | S2 |
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

### CEF-01B 跨引擎接口冻结（待领取）

- 状态：`READY`；依赖 `CEF-01A DONE`，尚无生产代码改动。
- 单一目标：冻结桌面 native 编排可消费、CEF 后端可实现且能映射到 ArkWeb 的最小 C++17 浏览器引擎契约；本任务不创建可运行浏览器。
- 输入：`docs/current/architecture.md` 的依赖方向与状态所有权、FND-08 的强类型 ID/Core 错误语义、BR-001/BR-013 与 PV-001/PV-004 的后续调用需求，以及 CEF/ArkWeb 均可表达的导航、标签、Profile、权限、可信输入事实和 observation 订阅能力。
- 输出与允许修改：`browser/engine-api/include/crayon/browser_engine/` 的纯抽象接口/强类型值对象，`browser/engine-api/README.md` 的线程、所有权、错误和 Harmony 语义映射，`browser/engine-api/tests/` 的独立 Fake/contract，以及仅供该模块独立编译测试的 `browser/engine-api/CMakeLists.txt`。
- 禁止修改：根 `CMakeLists.txt`/preset、`browser/cef-shell`、`browser/harmony-shell`、Rust Core/schema、产品 UI、Profile 实现、媒体策略、Cast-SDK 与平台 adapter；生产 include/source 不得包含 Fake、Mock、CEF、ArkWeb、OS header 或站点规则。
- 接口边界：命令只表示已接收或稳定拒绝，异步结果只经有所有权说明的 event sink 交付；类型覆盖 adapter 生命周期、标签/导航、Profile 上下文、权限决定、可信输入事实和 observation 订阅。不得携带 Cookie、Authorization、响应正文、接收端命令、投屏模式决策或 CEF handle。
- 错误与生命周期：无效/空 ID、非法 URL/zoom/权限值 fail closed；重复 close/destroy/unsubscribe 幂等且结果稳定；subscription/adapter 释放后不得回调；接口本身不启动线程、计时器、IO 或等待，因此取消/超时由后续具体 operation owner 定义，不能在本任务伪造异步成功。
- 验收：所有 public header 可独立包含；Fake 完整实现每个纯虚方法；contract 覆盖正常命令、无效输入、重复调用、事件顺序、unsubscribe 后无回调和 adapter 销毁；扫描证明 public/production 文件不含 CEF/ArkWeb/OS/Cast/relay/测试类型；Harmony 说明逐项给出 ArkWeb 可实现、需 native bridge 或后续 capability 降级，不把桌面结果冒充 Harmony 真机证据。
- 测试命令：先以缺少 public header 的 compile contract 记录失败；再运行 `cmake -S browser/engine-api -B .cache/build/engine-api -G Ninja -DCRAYON_ENGINE_API_BUILD_TESTS=ON`、`cmake --build .cache/build/engine-api`、`ctest --test-dir .cache/build/engine-api --output-on-failure`、`scripts/check.ps1 fast`、`scripts/check.ps1 security` 和 `git diff --check`。
- 完成证据：编译器/CMake/Ninja 版本、测试数量与结果、public include 扫描、Code Review 结论和未覆盖项；只有实现、测试、Review 均完成后才转 `DONE` 并解锁 `CEF-01C`。

### CEF-01C～CEF-01E 边界

- `CEF-01C` 只建立共享构建图；输入为 01A/01B，输出限 CMake/preset/test target，不实现平台进程或产品行为。
- `CEF-01D` 只负责 Windows x64 bootstrap；不改公共接口，不用单平台结果代替 macOS。
- `CEF-01E` 只补齐 macOS x64/arm64 门禁并 Review；没有对应 runner 时必须保留为 `BLOCKED/VERIFIED`，不得伪造 S2 证据。

## 接口冻结

`BrowserEngineAdapter` 首次冻结只包含导航、标签、Profile、权限、输入事实和 observation 订阅；不得暴露 CEF 对象给 UI/Core。后续 `CNT-02` 以独立 contract 扩展有界 page snapshot stream/cancel，`ACT-04/ACT-07` 扩展能力受限的 semantic discovery/action；`AGT-15` 只把正式 ACT 能力接入 CAAP。禁止为了 Agent 暴露 raw DOM/CDP/selector/JavaScript。每次新增接口必须先写 contract test 和 Harmony 可实现性说明。

## 每项通用验证

- C++ format/static analysis、目标 test target、目标平台 build。
- 变更 renderer/browser IPC 时执行畸形消息、大小上限、旧 navigation 和 secret 泄漏测试。
- Windows/macOS 实现允许分任务完成，但共同接口变更由一个 owner 先合并，平台实现不得各自修改 schema。
