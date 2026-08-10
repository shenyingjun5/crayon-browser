# 当前开发事实入口

本目录只保存当前有效的产品、架构、测试和交付契约。历史方案不得覆盖这里的结论。

## 必读文档

| 文档 | 作用 |
|---|---|
| [`../crayon-private-cast-browser-prd.md`](../crayon-private-cast-browser-prd.md) | 产品范围、用户体验、隐私和合规门禁 |
| [`architecture.md`](architecture.md) | 目标架构、目录、依赖方向和 Cast-SDK 边界 |
| [`testing-standard.md`](testing-standard.md) | 测试层级、fixture、平台矩阵和证据要求 |
| [`test-cases.md`](test-cases.md) | 可执行测试用例目录 |
| [`code-review-standard.md`](code-review-standard.md) | Code Review 维度、等级和合并条件 |
| [`../crayon-private-cast-browser-technical-design.md`](../crayon-private-cast-browser-technical-design.md) | 完整技术背景与协议设计 |
| [`../plans/README.md`](../plans/README.md) | 活跃 Roadmap 和下一批可领取任务 |

## 当前基线（2026-08-09）

- 根 crate：`get-video 0.1.0`，Rust 2021；正式 workspace 另含 domain、ipc、runtime、media-probe、media-observer、cast-policy、legacy-adapter 和 repo-guard；Tauri `app/demo` 被显式排除并独立保留。
- 当前 UI：Tauri 2，属于迁移源，不是目标 CEF 架构。
- `app/src/main.rs` 为 1231 行，混合窗口、注入脚本、候选、relay、诊断和 CLI，必须按基础迁移 Roadmap 拆分。
- 9 个生产文件的内联测试已迁入同目录 `*_tests.rs`；生产文件只保留条件测试模块声明。
- 当前 relay 暴露任意 URL `/proxy`、`/api/extract`、测试页面，并可监听 `0.0.0.0`；正式产品必须替换为 session/resource 路由。
- 正式注入观察器不再包含自动播放、广告识别/跳过或 seek；旧通用 relay 只存在于显式 `legacy-dev` 编译路径。
- `cargo test --workspace`：65 passed；`legacy-dev` 包为 92 passed、13 ignored；最近一次 `scripts/check.ps1 all` 为 49.9 秒。
- PRD、技术方案和 Roadmap 当前均为新增文档，开发前仍需检查真实 Git 状态。

## FND-01 遗留契约基线（2026-08-09）

基线提交为 `cb10436`。下表只证明旧行为当前存在，全部属于迁移红线；“基线测试通过”不表示目标产品合规。FND-04 移除或隔离旧行为时，必须把对应测试改成正式构建的禁止性断言。

| 用例 | 当前被检测的遗留行为 | 目标方向 |
|---|---|---|
| BR-009 | 原基线含广告域过滤、广告容器和跳过选择器；FND-04 已移除 | 正式构建不得识别或跳过广告 |
| BR-010 | 原基线含无手势 `play()`、自动点击、广告 seek；FND-04 已移除 | 只观察；投屏必须由用户主动触发 |
| RG-004 | 固定 `127.0.0.1:8377` Beacon、`0.0.0.0:8321` Relay | 端口动态分配；网络暴露按能力和会话授权 |
| RL-001 | 任意 URL `/proxy`、`/api/extract`、player/probe 页面已隔离到 `legacy-dev` | 正式 LAN 仅暴露 session/resource 路由 |

基线规模：

| 文件 | 行数 |
|---|---:|
| `app/src/main.rs` | 1231 |
| `src/relay/mod.rs` | 252 |
| `src/lib.rs` | 57 |
| `tests/fixtures.rs` | 1124 |
| `tests/online.rs` | 422 |

可复现命令和结果：

```text
cargo test --test legacy_contract  # 5 passed
cargo test -p get-video --no-default-features --features legacy-dev --lib  # 53 passed
cargo fmt --all -- --check         # passed
git diff --check                   # FND-01 交付前检查
```

## FND-02 测试物理隔离（2026-08-09）

- `codec/drm/probe/lib/extract/rules/sites/static_parse/relay-proxy` 的测试正文已迁到同目录独立文件，非测试源码前缀与基线逐字一致。
- `tests/fixtures.rs` 从 1124 行降至 496 行；测试按 `static_parse/hls/mp4/security/dash/sites` 拆成 6 个子文件，最大子文件 175 行。
- 迁移前后测试/helper 函数集合一致，fixture 的 84 个 `assert*` 断言保持不变；未扩大生产公共 API。
- FND-02 迁移时的 legacy 基线为 86 passed、13 ignored、0 failed；FND-04 后由显式 `legacy-dev` 命令保持该回归面。

## FND-03 Repo Guard 与检查入口（2026-08-09）

- `tools/repo-guard` 输出 schema v1 JSON，覆盖 RG-001～RG-008；错误阻断，规模和遗留可配置字面量以 warning 报告，缺少 Release 产物/schema/Cast-SDK 时显式 `not_applicable`。
- 16 个工具测试覆盖生产测试泄漏、测试依赖、3000 行测试文件、凭证/绝对路径、依赖环、domain/runtime/legacy 边界、Cast-SDK revision、Release 文件名/字节资产、schema vector、误报和临时目录清理。
- `scripts/check.ps1` 与 `scripts/check.sh` 提供 `fast/core/security/all`；成功或失败都输出逐步骤结构化 summary。
- 热缓存 `fast` 实测 16.9 秒；`core`、`security`、`all` 和 Git Bash `fast` 已通过。

## FND-04 Formal/Legacy 红线隔离（2026-08-09）

- 默认 `formal-product` 与显式 `legacy-dev` 互斥；旧模块、CLI、fixture、online、Tauri app 和 demo 只在 `legacy-dev` 下编译。
- 注入观察器已删除广告域/DOM 识别、跳过选择器、自动 `play()`、自动 click、seek 和 UI-test 自动选片；网络与 DOM 观察保留。
- formal Release rlib 已扫描，不含 `/api/extract`、任意 `/proxy`、player/probe、广告跳过、自动 seek 或 remote-debug 标记。
- FND-04 收口时 formal 包 6 条、legacy 包 87 条通过；FND-05 增加兼容导出测试后当前分别为 7 条和 88 条，13 条公网测试 ignored；安全门禁通过。
- Tauri app/demo 已明确标注 legacy；其 Windows 编译仍被仓库既有缺失 `app/icons/icon.ico`、`demo/icons/icon.ico` 阻断，未把该阻塞误报为 feature 回归。

## FND-05 正式 Workspace 与领域骨架（2026-08-09）

- 统一 workspace edition、MSRV、lint 和 dev/release profile；`unsafe`、`dbg!`、`todo!`、`unimplemented!` 进入常驻 lint 门禁。
- `crayon-domain` 提供经校验的产品身份和 formal/legacy 模式；`crayon-ipc-schema` 提供非零 schema version 与启动协商；`crayon-app-runtime` 组装一致的 formal runtime descriptor；`crayon-legacy-adapter` 对 formal 调用 fail closed。
- 根包兼容 re-export 仅覆盖 formal 的 domain/runtime/ipc；`crayon-legacy-adapter` 仅由被排除的 `get-video-app` 依赖，正式依赖图不携带它。
- 4 个 crate 共 8 条独立行为测试；repo-guard RG-005 自动拒绝 domain 的网络/平台依赖、runtime 的具体平台依赖和非 legacy app 对 adapter 的引用。
- `cargo metadata`、`cargo check --workspace --all-targets`、`cargo test --workspace`、严格 Clippy、`scripts/check.ps1 all` 和 `git diff --check` 已通过；RG-007 在 FND-08 创建 current/previous schema vectors 前保持显式 `not_applicable`。

## FND-06 纯媒体能力与规划门禁（2026-08-09）

- `crayon-media-probe` 承载 codec/container、HLS/DASH protection 和 frame verdict 纯函数，共 26 条独立测试；crate 依赖树只有自身。
- 根 `codec/drm/probe` 保持 legacy 兼容入口和原测试面；HTTP 检查、站点名单、WebView 能力假设和中文原因没有进入正式 crate。
- `crayon-media-observer` 用强类型保存 PageReported/BrowserVerified、用户激活和播放推进事实；不包含 URL、站点 extractor、浏览器对象或投屏决策。
- `crayon-cast-policy` 仅依赖 observer；页面自报、缺少用户激活、播放未推进或 capability 未就绪一律拒绝进入 planning。
- 正式 workspace 65 条与 legacy 92 条回归通过；全 workspace Clippy、repo guard、依赖树、Release 字节扫描和 diff 检查通过。

## FND-07A Legacy 嗅探资源边界（2026-08-10）

- legacy 嗅探脚本从 `app/src/main.rs` 逐字迁至独立 JS 资源，由薄 Rust loader 使用 `include_str!` 编译期加载；运行入口和注入时机不变。
- 架构契约以 FNV-1a 64 `63ca75fa11950408` 锁定资源字节，并同时扫描 Rust/JS，禁止静默加入广告识别或 click/play/seek 自动操作。
- `app/src/main.rs` 从 1178 行降至 952 行；正式 workspace 66 条与 legacy 93 条离线回归通过，13 条公网测试仍显式 ignored；严格 Clippy、repo guard、format 和 diff 检查通过。
- Tauri app 缺失的可复现构建资源仍由 FND-07E 负责，本任务没有修改端口、路由、状态、命令、Relay 或 CLI 行为。

## FND-07B Legacy 共享模型与状态所有权（2026-08-10）

- `Sniff*`/`Probe*` 数据模型逐字迁至 `app/src/models.rs`，`AppState` 与去重写入 `push_hit` 逐字迁至 `app/src/runtime.rs`；锁所有权在模块 doc 显式（beacon 写入、sniff/probe 读取、`_relay` 装配后只读），字段、日志与同步语义不变（字段集合机器比对一致）。
- 新增 `models_tests.rs`（序列化 golden 2 条）与 `runtime_tests.rs`（去重 2 条）；`app/src/main.rs` 从 952 行降至 881 行。
- legacy 回归 58 条、legacy_contract 6 条、`scripts/check.sh all` 通过；app 整体编译与 Clippy 受本机 WebKitGTK 2.38 < 2.40 阻断（FND-07E 既定范围），新测试经 `#[path]` 原样挂载的独立 harness 运行通过。

## FND-07C Legacy Beacon 与网络地址拆分（2026-08-10）

- loopback Beacon（`/sniff`、`/diag`、`/probe-report`）逐字迁至 `app/src/legacy_beacon.rs`，生命周期显式拆为 `beacon_router` + `start_beacon_server`；LAN 地址选择逐字迁至 `app/src/legacy_network.rs`；固定端口、route 与返回字节不变（gif 机器比对一致）。
- 新增 beacon route 契约/query 边界测试 3 条与 LAN helper 不变量测试 2 条（dev-only `tower` 依赖，不进入生产构建图）；`app/src/main.rs` 从 881 行降至 757 行。
- RG-004 契约改锁 `legacy_beacon.rs` 固定端口基线；legacy 回归 58 条、legacy_contract 6 条、`scripts/check.sh all` 通过；app 整体编译/Clippy 受本机 WebKitGTK 2.38 阻断，新测试经 `#[path]` harness 运行。

## FND-07D Legacy 命令与探测编排拆分（2026-08-10）

- Tauri commands 与提取编排迁至 `app/src/commands.rs`，嗅探编排迁至 `legacy_sniff.rs`，解码探针迁至 `legacy_probe.rs`，登录窗口/Cookie 读取迁至 `login.rs`；探针候选筛选为纯函数，入 `models.rs`。handler 名称、注册顺序与序列化输出不变（函数集合机器比对一致）。
- `app/src/main.rs` 从 757 行降至 242 行，只含装配入口；legacy_contract 新增命令面/模块接线/登录窗口契约，BR-009/BR-010 红线扫描覆盖全部 app 模块。
- app 侧 13 条测试经 `#[path]` harness 通过；legacy 回归 58 条、legacy_contract 7 条、`scripts/check.sh all` 通过；app 整体编译/Clippy 仍受 WebKitGTK 2.38 阻断（FND-07E 收口）。

## FND-07E 启动、Relay 与 CLI 装配收口（2026-08-10）

- relay 启动迁至 `app/src/legacy_relay.rs`，CLI/UI-test 编排迁至 `app/src/cli.rs`，setup 装配迁至 `app/src/app.rs`；`app/src/main.rs` 降至 61 行，只含命令注册与装配入口。端口、route、线程、CLI marker 契约锁定不变（函数集合机器比对无丢失）。
- 补齐 `app/icons/icon.ico` 与 `demo/icons/icon.ico`（由 64x64 `icon.png` 确定性生成），解除 FND-04 记录的 Windows 构建阻断；提交 `app/Cargo.lock` 保证 app 独立构建可复现。
- legacy_contract 8 条、legacy 回归 58 条、`scripts/check.sh all`/`security` 通过；`cargo check --manifest-path app/Cargo.toml` 在本机被系统 WebKitGTK 2.38.2 < 2.40 阻断（openEuler 24.03，未绕过版本检查），需在达标环境补跑后 FND-07E 转 VERIFIED。

## 事实更新规则

- 完成模块 Roadmap 后，把稳定结论收敛到本目录，再归档实施过程。
- 这里只记录已经确认并会持续影响开发的事实，不记录临时日志、猜测、凭证和每日进度。
- 代码与本文冲突时先停止开发，确认是实现缺陷还是契约已变；不得静默选择方便的一方。
