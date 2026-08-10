# FND：基础工程与 Legacy 迁移 Roadmap

状态：`FND-01/02/03/04/05/06/07A DONE`，`FND-07B IN_PROGRESS`。

## 目标

在不先删除当前 Tauri/提取能力的前提下，建立可执行测试基线、生产/测试隔离、目标 workspace 和 legacy 边界；正式产品构建不再携带自动广告操作或通用 LAN 代理。

## 不变量

- 现有 core 53 项测试不减少。
- 特征测试、纯移动、行为修复分开提交。
- CEF 正式 E2E 未通过前保留 Tauri 回归入口，但禁止新增产品功能。
- 不在本阶段接入真实 Cast-SDK、CEF 或平台采集。

## 原子任务

### FND-01：冻结基线与红线特征测试

- 状态：`DONE`
- 依赖：无。
- 修改：`Cargo.toml`、`tests/architecture/legacy_contract.rs`、`docs/current/README.md`；不改生产行为。
- 工作：为 `app/src/main.rs` 的自动点击/广告 seek、固定 beacon、`0.0.0.0` relay，以及 `src/relay` 的 `/api/extract`、任意 URL `/proxy`、player route 建源码/Router 特征测试；记录文件行数、测试数和基线命令。
- 用例：`BR-009`、`BR-010`、`RL-001`、`RG-004` 的 legacy baseline 版本。
- 验证：`cargo test -p get-video --lib`；特征测试定向命令；`git diff --check`。
- 验收：测试稳定区分“legacy 当前存在”与“正式目标禁止”，不误把当前行为写成合规。
- 证据：S1。
- 完成证据（2026-08-09）：`cargo test --test legacy_contract` 4/4；`cargo test -p get-video --lib` 53/53；`cargo fmt --all -- --check`、`git diff --check` 通过；任务级 Code Review 无 P0～P3 问题。

### FND-02：迁移内联测试并拆分测试大文件

- 状态：`DONE`
- 依赖：FND-01。
- 修改：`src/*_tests.rs`、`src/extract/*_tests.rs`、`src/relay/*_tests.rs`、`tests/fixtures/`；生产文件只保留模块声明。
- 工作：逐模块纯移动 `codec/drm/probe/extract/rules/sites/static_parse/relay/lib` 内联测试；把 1124 行 `tests/fixtures.rs` 按 mp4/hls/dash/security 拆分，不改断言。
- 用例：RG-001、RG-002、RG-003。
- 验证：core 53 项不少于基线；`cargo test -p get-video --lib`；现有 integration tests。
- 验收：生产文件无测试正文，单测试文件 <2000 行，无公共 API 扩大。
- 证据：S2。
- 完成证据（2026-08-09）：9 个内联模块纯移动至 `*_tests.rs`；`tests/fixtures.rs` 从 1124 行拆为 496 行入口和 6 个领域文件；`cargo test -p get-video` 默认 86/86，通过且 13 个公网用例保持 ignored；生产前缀、函数集合和 fixture 84 个断言机器比对一致；Code Review 无 P0～P3 问题。

### FND-03：建立 repo guard 与分层检查入口

- 状态：`DONE`
- 依赖：FND-02。
- 创建：`tools/repo-guard/`、`scripts/check.ps1`、`scripts/check.sh`、对应工具测试。
- 工作：实现生产/测试隔离、文件/函数规模报告、敏感硬编码、依赖边界、Release 测试资产检查；建立 fast/core/security/all 命令并输出结构化 summary。
- 用例：RG-001～RG-008。
- 验证：repo-guard 自测；`scripts/check.ps1 fast` 目标 <2 分钟（热缓存）；故意违规 fixture 必须失败。
- 验收：零例外/无基线白名单；错误指向文件/规则；不存在的检查不得假通过。
- 证据：S2。
- 完成证据（2026-08-09）：repo guard 覆盖 RG-001～008，12/12 工具测试及 Clippy 通过；真实仓库无 error，遗留规模/硬编码显式 warning，未接入项显式 not_applicable；PowerShell fast 16.9 秒，core/security/all 与 Git Bash fast 均通过；成功和失败均输出 schema v1 JSON；Code Review 无 P0～P3 问题。

### FND-04：隔离并修正 Legacy 合规/安全红线

- 状态：`DONE`
- 依赖：FND-01、FND-03。
- 修改：`app/src/main.rs`、`src/relay/mod.rs`、构建 feature；新增独立资源文件和回归测试。
- 工作：删除/禁用自动广告点击、广告 seek、跳过选择器和无用户手势自动播放；把通用 `/api/extract`、`/proxy`、player/probe route 限定到显式 `legacy-dev` feature，Release/正式 feature 编译时不存在；不在本任务实现新 relay。
- 用例：BR-009、BR-010、RL-001、RG-006。
- 验证：正式 feature 特征测试通过；legacy-dev 现有回归仍可运行；Release 产物扫描。
- 验收：正式构建零违规能力；legacy 标签和文档清晰，不能被正式 App 引用。
- 证据：S2。
- 完成证据（2026-08-09）：BR-009/010、RL-001、RG-006 共 5 条正式契约通过；formal 默认包与 `legacy-dev` 完整 53+29 回归通过；all/security 门禁通过；formal Release rlib 字节扫描无旧 route/自动操作/测试调试标记；两个 feature 同开编译失败；Tauri app/demo 已显式 legacy，但独立检查被既有缺失 `icons/icon.ico` 阻断；Code Review 无 P0～P3 问题。

### FND-05：建立 Rust workspace 与领域空壳（只创建可编译模块）

- 状态：`DONE`
- 依赖：FND-03。
- 修改：根 `Cargo.toml`、`crates/crayon-domain`、`crayon-app-runtime`、`crayon-ipc-schema`、`crayon-legacy-adapter`。
- 工作：创建最小可编译 crate、统一 edition/lint/profile；根 crate 保持兼容 re-export；禁止空目录和占位 TODO API。
- 用例：RG-005、RG-007。
- 验证：`cargo metadata`、workspace check/test、依赖图 guard。
- 验收：依赖方向 `runtime -> domain/ipc`，domain 无网络/平台依赖；legacy 只能被 legacy app 依赖。
- 证据：S2。
- 完成证据（2026-08-09）：正式 workspace 建立 4 个可编译 crate 和 8 条独立行为测试；根包只兼容导出 formal API，`app/demo` 作为独立 legacy workspace 保留；RG-005 新增 3 条边界违例测试并通过，RG-007 在 schema vector 创建前显式 N/A；`cargo metadata`、`cargo check --workspace --all-targets`、`cargo test --workspace`（31 passed）、严格 Clippy、`scripts/check.ps1 all`（formal 31；legacy 88 passed/13 ignored）和 `git diff --check` 通过；Code Review P0/P1/P2/P3 均为 0，未发现架构、并发、安全、性能或跨平台阻塞。

### FND-06：迁移纯媒体能力到独立 crate

- 类型：迁移 Epic；完成条件为 `FND-06A`～`FND-06D` 全部 `DONE`。
- 状态：`DONE`
- 依赖：FND-02、FND-05。
- 不做：站点网络、任意 HTML/影视站提取、relay、CEF、Cast-SDK、HTTP client 抽象和 v1 公共 schema。
- 完成证据（2026-08-09）：`FND-06A`～`FND-06D` 全部通过；正式 workspace 65 passed，legacy 包 92 passed/13 ignored；全 workspace Clippy、RG-001～008、依赖树、正式 Release 产物扫描和 `git diff --check` 通过；最终 Code Review P0/P1/P2/P3 均为 0。codec 的 116 行 PAT/PMT parser 为单一有界状态机且迁移前后逐字一致，本次不做行为性拆分。

#### FND-06A：迁移 codec/container 纯解析

- 状态：`DONE`；依赖：FND-05。
- 修改：创建 `crates/crayon-media-probe` 的 codec parser；根 `src/codec.rs` 保留 legacy HTTP 检查并兼容 re-export。
- 工作：逐字迁移 codec token、M3U8 codec/container、TS PAT/PMT、MP4 box 纯解析和原测试；不得把 `reqwest`、headers 或 URL fetch 带入新 crate。
- 验证：原 codec 13 条测试数量/断言不减少；workspace test、legacy package、RG-005、兼容导出测试。
- 验收：新 crate 零网络/平台/SDK 依赖；解析输出逐项相同；根调用方不改行为。
- 证据：S2。
- 完成证据：397 行纯 parser 与迁移前机器比对逐字一致；新 crate 10 条纯解析测试、根 12 条 codec 兼容测试通过；HTTP fetch/URL resolve 仍只在 legacy 根模块。

#### FND-06B：迁移通用 DRM/protection 识别

- 状态：`DONE`；依赖：FND-06A。
- 修改：`crayon-media-probe::protection`、根 `src/drm.rs` 兼容层。
- 工作：迁移 HLS keyformat、MPD ContentProtection 和 AES-128 非 DRM 判定；站点域名判断与 legacy restricted reason 留在根兼容层。
- 验证：通用 protection 测试逐字保真；站点 legacy 测试仍通过；空/大小写/未知 keyformat 边界。
- 验收：通用 crate 无站点名单、网络、UI 或规避 DRM 行为；只返回识别结果。
- 证据：S2。
- 完成证据：通用 HLS/DASH/keyformat 8 条测试与根 legacy 11 条兼容/站点测试通过；Netflix/CCTV 名单、URL 解析和中文原因仅保留在 `legacy-dev` 根模块。

#### FND-06C：迁移画面统计 verdict

- 状态：`DONE`；依赖：FND-06B。
- 修改：`crayon-media-probe::frame`、根 `src/probe.rs` 兼容 re-export。
- 工作：纯移动 `FrameStat`、degenerate 判定、codec 可判断性和 `ProbeVerdict`；命名常量保持语义。
- 验证：现有 9 条 probe 测试与兼容调用测试；空样本、黑场开头、load error。
- 验收：不引入采集、CEF、线程、网络或平台类型；输出与迁移前一致。
- 证据：S2。
- 完成证据：8 条 frame/verdict 测试和 9 条根兼容测试通过；阈值改为语义常量；WebView codec 假设与中文 UI 原因留在 legacy，不进入正式 crate。

#### FND-06D：建立 observer/policy 最小纯边界并收口

- 状态：`DONE`；依赖：FND-06C。
- 修改：创建 `crayon-media-observer`、`crayon-cast-policy` 和根兼容导出；不迁移 legacy 站点 extractor。
- 工作：只建立“观察事实不可直接成为投屏许可”和“未具备冻结 capability/schema 时 fail closed”的真实最小 API；Candidate/Protocol v1 延至 FND-08 冻结。
- 验证：不可信观察拒绝、无用户手势拒绝、依赖方向与 Release 扫描；full workspace/legacy 回归。
- 验收：两个 crate 有真实行为测试且无占位 API；不复制 legacy `static_parse/sites/rules`，不依赖网络/平台/SDK。
- 证据：S2。
- 完成证据：observer 2 条事实/provenance 测试、policy 4 条 admission 测试和根兼容测试通过；页面自报、无用户激活、播放未推进、能力未就绪均 fail closed；Review 发现的多布尔参数已改为强类型枚举。

### FND-07：拆分 Tauri 迁移源

- 类型：迁移 Epic；完成条件为 `FND-07A`～`FND-07E` 全部 `DONE`。
- 状态：`IN_PROGRESS`
- 依赖：FND-04、FND-05。
- 不做：修改嗅探算法、端口/路由、CLI 输出、登录持久化、正式产品功能或修复 legacy 行为。

#### FND-07A：迁移嗅探脚本资源

- 状态：`DONE`；依赖：FND-04。
- 修改：`app/src/scripts/legacy_sniffer.js`、`app/src/legacy_sniffer.rs`、`app/src/main.rs`、legacy contract。
- 工作：把内联 `SNIFF_JS` 逐字迁到独立资源，由 `include_str!` 加载；建立固定 hash 和广告/自动播放红线测试。
- 验证：资源字节保真、contract、legacy unit、repo guard。
- 验收：Rust 生产文件无大段 JS；脚本行为不变且不能静默加入 click/play/seek/ad filtering。
- 证据：S2。
- 完成证据（2026-08-10）：原内联脚本逐字迁至 `app/src/scripts/legacy_sniffer.js`，由 `include_str!` 加载，并以 FNV-1a 64 `63ca75fa11950408` 锁定资源完整性；架构契约同时扫描 Rust 与 JS，持续禁止广告识别/过滤以及 click/play/seek 自动操作；`main.rs` 从 1178 行降至 952 行。`scripts/check.ps1 all` 通过（formal 66 passed；legacy 93 passed/13 ignored），严格 Clippy、repo guard、`cargo fmt --check` 和 `git diff --check` 通过；任务级 Code Review P0/P1/P2/P3 均为 0。Tauri app 独立 build 仍按既定范围留给 FND-07E 补齐构建资源后验证。

#### FND-07B：拆分共享模型与状态所有权

- 状态：`IN_PROGRESS`；依赖：FND-07A。
- 修改：`app/src/runtime.rs`、`app/src/models.rs`、`app/src/main.rs`。
- 工作：纯移动 `Sniff*`、`Probe*`、`AppState` 和去重写入；明确锁所有权，不改字段、日志和同步语义。
- 验证：模型序列化 golden、去重测试、legacy 回归、字段集合机器比对。
- 验收：状态只有 runtime owner；生产测试物理隔离；无公共 API 扩张。
- 证据：S2。

#### FND-07C：拆分 legacy Beacon 与网络地址

- 状态：`TODO`；依赖：FND-07B。
- 修改：`app/src/legacy_beacon.rs`、`app/src/legacy_network.rs`、`app/src/main.rs`。
- 工作：纯移动 loopback Beacon、probe report 和 LAN IP 选择；不改固定端口、route 或返回字节。
- 验证：Router route contract、query 边界、LAN helper 单测、legacy security 回归。
- 验收：服务生命周期和状态引用显式；不得新增监听地址、route、线程或错误吞噬。
- 证据：S2。

#### FND-07D：拆分 legacy 命令与探测编排

- 状态：`TODO`；依赖：FND-07C。
- 修改：`app/src/commands.rs`、`app/src/legacy_sniff.rs`、`app/src/legacy_probe.rs`、`app/src/login.rs`。
- 工作：纯移动 Tauri commands、隐藏窗口嗅探、解码探针、提取和登录窗口；handler 名称与序列化输出不变。
- 验证：command surface contract、候选筛选测试、登录窗口 build、legacy CLI 回归。
- 验收：每模块单一变化原因；无自动播放/点击/seek；Cookie 不进入日志或 formal crate。
- 证据：S2。

#### FND-07E：收口启动、Relay 与 CLI 装配

- 状态：`TODO`；依赖：FND-07D。
- 修改：`app/src/legacy_relay.rs`、`app/src/cli.rs`、`app/src/app.rs`、`app/src/main.rs`、缺失的构建资源。
- 工作：移动 legacy relay 启动和 CLI/UI-test 编排；`main.rs` 只调用装配入口；补齐可复现的 Tauri build 前置资源。
- 验证：`cargo check --manifest-path app/Cargo.toml`、CLI/UI smoke contract、all/security、文件规模。
- 验收：`main.rs <300`；每个生产文件单一职责；app 可编译；端口、route、线程、CLI marker 与迁移前一致。
- 证据：S2。

### FND-08：冻结 Core API v1 与 capability schema

- 状态：`TODO`
- 依赖：FND-05、FND-06。
- 修改：`crayon-domain`、`crayon-ipc-schema/schema/`、`config/feature-schema.json`、contract tests。
- 工作：定义强类型 ID、稳定错误、`PlatformCapabilities`、`ReceiverCapabilities`、`SourceObservation`、`MediaCandidate`、`CastPolicyInput/Decision` 和 session generation；明确 secret 不可序列化字段。
- 用例：RG-007、PL-013、CS-008。
- 验证：schema golden、serde roundtrip、unknown field/version tests、secret field deny tests。
- 验收：v1 当前/前一版本策略写入契约；不含 UI 文案、OS 类型和 Cast-SDK 内部类型。
- 证据：S2。

### FND-09：建立确定性 test-support

- 状态：`TODO`
- 依赖：FND-03、FND-08。
- 创建：`test-support/{clock,upstream,receiver,platform,browser_fixture,leak_scanner}`。
- 工作：实现 ManualClock、MockUpstream、FakeReceiver/FakeCastFacade、PlatformFake、fixture server、LeakScanner；只允许 dev-dependency。
- 用例：testing-standard 第 4 节；RG-001/006。
- 验证：每个 fake 自测、生产依赖图和 Release 扫描。
- 验收：无固定长 sleep、无公共网络、无真实秘密；测试端口使用系统随机端口。
- 证据：S2。

### FND-10：把公网测试降级为手工兼容测试

- 状态：`TODO`
- 依赖：FND-09。
- 修改：`tests/online.rs`、`tests/integration/`、`docs/current/testing-standard.md`。
- 工作：用本地 fixture 替代 CI 公网站点；保留经批准的站点 smoke 为 ignored/manual，要求显式环境变量且不输出 URL/账号。
- 用例：PL/RL 本地 fixture 子集。
- 验证：断网环境 core/integration tests；manual test 默认不执行。
- 验收：fast/core 不访问公网；manual 失败不被误报为产品单测失败。
- 证据：S2。

### FND-11：建立配置加载与本地化资源

- 状态：`TODO`
- 依赖：FND-08。
- 创建：`config/product-defaults.toml`、`browser/shared-ui/locales/zh-CN.json`、`en-US.json`、domain config types。
- 工作：集中端口范围、超时、容量、更新渠道、日志策略；用户文案迁资源；配置校验失败阻止启动并返回稳定错误。
- 用例：RG-004、非法/缺失/边界配置 tests。
- 验证：config unit、locale key parity、secret scan。
- 验收：业务源码不含用户文案和可变 magic value；配置不允许 secrets。
- 证据：S2。

### FND-12：基础迁移收口 Review

- 状态：`TODO`
- 依赖：FND-04、FND-05、FND-06、FND-07、FND-08、FND-09、FND-10、FND-11。
- 修改：Roadmap/current 文档；仅修 Review finding。
- 工作：全量架构/安全/测试 Review；核对依赖图、文件规模、legacy 正式隔离、Core API 和测试入口。
- 验证：fast/core/security、workspace build、Release artifact scan、`git diff --check`。
- 验收：无 P0/P1；P2 有任务；V0 完成；CEF/MED/SDK 前置可解锁。
- 证据：S2。

## 提交顺序

FND-01/02/03 各自独立；FND-06 每个领域至少一个提交；FND-04 的违规行为删除不得混入 FND-07 结构移动。任何历史 bug 单独建任务和回归测试。
