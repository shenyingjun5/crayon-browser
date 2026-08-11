# SDK：Cast-SDK 集成 Roadmap

状态：`FND-08 DONE`；`SDK-01 DONE`；`SDK-02 DONE`；`SDK-03 DONE`；`SDK-04 DONE`；`SDK-05 READY`。源码固定为 Cast-SDK `44c3a99871aa1e68cbda71eacefbb41d23a747a8`，通过 `third_party/cast-sdk` submodule 接入；不得依赖开发者本机源码路径。

## 边界

只通过稳定 facade 复用发现、投屏码、连接、能力、远程媒体投送、播控和会话监督。网页、Cookie、relay recipe、CEF 与产品 UI 不进入 Cast-SDK。

## 接入决策（2026-08-10）

- 当前阶段不等待 NuGet、SwiftPM 或 OHPM 包，统一从固定 Cast-SDK source revision 构建。
- 本 Roadmap 不处理 Linux；Linux 不作为 SDK-01～SDK-14 的前置、验收或发布阻塞项。
- Cast-SDK 适配只服务同一局域网内的 Direct/Relay、设备发现、投屏码、连接和控制，不承载 WebRTC、浏览器采集或编码。
- 无 Direct/Relay 路由时由产品层执行外部客户端交接；这不是 Cast-SDK session，也不得构造 receiver descriptor。
- Windows、macOS 后续只从 `cast-sender-service::SenderCommandService` 公开 facade 接入；HarmonyOS 从同一 revision 构建 ArkTS/native bridge。
- 浏览器不增加设备身份认证、密钥交换、临时授权或 SDK 使用许可代码；自动发现、投屏码、连接和控制完全沿用 Cast-SDK 现有行为。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| SDK-01 | FND-08 | `.gitmodules`、`third_party/cast-sdk`、`config/cast-sdk-source.toml` | 建立固定 git revision 的源码 submodule；记录 repository、commit、升级/回滚和独立源码边界 | `git submodule status`/gitlink/source lock 一致；干净 checkout 可复现；repo guard 不递归扫描 submodule；无本机 path | S1 |
| SDK-02 | SDK-01 | `crayon-cast-adapter/Cargo.toml` | 只从固定 submodule 引入必要 `cast-sender-service` facade；feature/依赖树/包体基线 | cargo tree；仅 adapter 依赖 SDK；无 tauri/automation 泄漏 | S1 |
| SDK-03 | SDK-02 | `cast-adapter/api` | 产品侧 `CastFacade` trait、强类型 DTO/error；不暴露 SDK 内部类型 | compile/serde/error contract；CS-008 | S1 |
| SDK-04 | SDK-03,FND-09 | `cast-adapter/fake`（test only） | Fake facade 事件/能力/session/generation | CS-001..CS-009 可确定性编排；Release 无 fake | S2 |
| SDK-05 | SDK-03 | `cast-adapter/service` | `SenderCommandService` 生命周期、线程/回调/stop 封装 | start/drop/crash/restart；无 orphan/锁内 callback | S2 |
| SDK-06 | SDK-05 | `cast-adapter/discovery` | start/stop/refresh/list 快照和增量事件 | CS-001、CS-002；多网卡、同名、过期、重复 | S2 |
| SDK-07 | SDK-05,SDK-06 | `cast-adapter/connection` | connect/disconnect/resolve by cast code 状态映射，不增加身份认证或授权协议 | CS-003；成功/错误/取消/route lost | S2 |
| SDK-08 | SDK-05 | `cast-adapter/capability` | receiver assessment -> `ReceiverCapabilities`，TTL/generation | CS-004；未知/变化/旧缓存；policy golden | S2 |
| SDK-09 | SDK-05,SDK-08,MED-19 | `cast-adapter/delivery` | Direct/HLS/Relay URL 只经 facade 投送；外部客户端交接不进入 SDK | CS-005；无 SOAP/URL/WebRTC descriptor 拼接；unsupported 明确 | S2 |
| SDK-10 | SDK-09 | `cast-adapter/control` | handle-bound play/pause/seek/volume/stop | CS-006；非法值、重复、旧 handle、超时 | S2 |
| SDK-11 | SDK-09,SDK-10 | `cast-adapter/session` | listener、自然结束、receiver stop、route lost、替换 | CS-007；旧事件不能停止新会话 | S2 |
| SDK-12 | SDK-07,SDK-08,SDK-10,SDK-11,MED-19 | `crayon-app-runtime/cast_usecase` | UI/runtime 与 SDK 事件编排；撤销 Direct/Relay；外部交接不创建 SDK session | Fake E2E V2；每个终态资源清理；PL-015 | S2 |
| SDK-13 | SDK-12 | 真接收端 Harness | 自动发现、投屏码、能力、投送、控制、终态 | CS-010、E2E-001、E2E-002；记录接收端版本/网络 | S4 |
| SDK-14 | SDK-02,SDK-12,SDK-13 | Review/升级说明 | API contract、source revision、错误映射、并发生命周期 Review | 全 CS；无 P0/P1；锁定 SDK gitlink/revision | S4 |

## SDK-01 完成记录（2026-08-10）

- 改动：新增 `third_party/cast-sdk` git submodule、`.gitmodules` 和 `config/cast-sdk-source.toml`，固定 repository 与完整 commit；递归初始化 SDK 自身的 `SignalLake-SDK` submodule。
- 门禁：repo guard 把嵌套 git submodule 视为独立源码边界，RG-008 校验 lock schema、HTTPS repository、40 位 revision、相对路径、`.gitmodules` 和 checkout HEAD；本仓库存在 source lock 时，后续只接受 adapter 内锁定 submodule 下的源码依赖。
- 验证：`git submodule update --init --recursive --checkout` PASS；`git submodule status --recursive` 显示 Cast-SDK `44c3a99871aa1e68cbda71eacefbb41d23a747a8`、SignalLake-SDK `c9b87b20cba93dcec5b71df8779ce2dee32291b5`；`cargo test -p repo-guard` 22/22 PASS；`cargo run -p repo-guard -- scan --root .` PASS 且 RG-008 passed；`scripts/check.ps1 fast` PASS；`git diff --check` PASS。
- Code Review：按 current 标准检查需求边界、源码锁、路径穿越、submodule 生命周期、测试隔离和后续依赖方向，P0/P1/P2/P3 = 0。
- 未覆盖：本任务没有建立 `crayon-cast-adapter`、没有编译 SDK facade、没有执行平台/真机测试；这些分别由 SDK-02 及后续任务完成。

## SDK-02 完成记录（2026-08-11）

- 改动：新建 `crates/crayon-cast-adapter`（workspace member，doc-only lib + 链接冒烟测试），仅以 path 依赖锁定 submodule 内的 `cast-sender-service` 与 `cast-sender-core`；根 `Cargo.toml` 将 `third_party/cast-sdk` 加入 workspace `exclude`（cargo 会把 workspace 目录内的 path 依赖自动视为成员，并用本仓库根错误解析 SDK 的 workspace 继承；exclude 保持独立源码边界）。产品侧 `CastFacade` 由 SDK-03 定义；本任务不含 WebRTC、外部客户端协议或平台接线。
- 验证：`cargo check -p crayon-cast-adapter` PASS；`cargo test -p crayon-cast-adapter` 2/2 PASS（facade 构造无 discovery/端口/网络副作用，类型独立可构造）；`cargo tree -p crayon-cast-adapter` 基线：11 个 SDK crate + `serde/serde_json/socket2/getrandom/windows-sys` 传递闭包，无 tauri/automation/webview/CDP 命中，无重复版本；debug rlib 基线 `cast-sender-service` 12.8 MB、SDK 合计约 25 MB、adapter 3.6 KB；`cargo run -p repo-guard -- scan --root .` RG-005/RG-008 passed；`cargo clippy --workspace --all-targets -- -D warnings` PASS；`scripts/check.sh fast` PASS；`scripts/check.sh security` PASS；`cargo test --workspace` 63 suites / 231 passed PASS。
- Code Review：按 current 标准审查需求边界、依赖方向（仅 adapter 依赖 SDK，由 RG-005/RG-008 机器强制）、测试隔离（不触网、不起服务）与构造/Drop 生命周期（`SenderCommandService::new` 纯分配，`http_server=None`、discovery 未启动），P0/P1 = 0；P2 一项：Cast-SDK 仓库根 LICENSE 为 GPL-3.0 而 workspace 元数据声明 UNLICENSED，分发前需上游澄清（跟踪：SDK-14、QAR-09）。
- 未覆盖：SDK 平台 image/live/document-render crate 未编译（不在 service 依赖图）；macOS 构建未验证（无 runner）；真机/平台证据由 SDK-13 负责。

## SDK-03 完成记录（2026-08-11）

- 改动：`crates/crayon-cast-adapter` 新增产品侧契约——`src/facade.rs`（`CastFacade` trait + `CastSessionListener`/`CastSessionSubscription`，对象安全、`Send + Sync`）、`src/dto.rs`（`DiscoveredDevice`、`CastCode`、`CastMediaKind`、`ReceiverAssessment`、`CastMediaRequest`/`CastMediaUrl`（不可序列化、Debug 脱敏）、`CastSessionRef`、`Volume`、`PlaybackPosition`、会话监督 DTO 与 `supersedes` fencing）、`src/error.rs`（`CastError` 13 个稳定码 + `SenderErrorKind` 镜像 + `from_sender_error`，只看 kind/稳定 code，不解析自然语言）、`src/error_tests.rs`（对真实 `CastSenderError` 的 CS-008 映射钉扎；穷尽 match 把 `SenderErrorKind` 编译钉到 SDK `ErrorKind`）、`tests/facade_contract.rs`（16 项：serde golden/roundtrip、CS-008 映射表、设备快照无网络定位键、URL Debug 脱敏、fencing 矩阵、trait 对象安全）。依赖新增 `crayon-domain`、`serde`（dev：`serde_json`）。公开签名无任何 `cast_sender_*` 类型；未接真实 `SenderCommandService`（SDK-05）、未写 Fake（SDK-04）。
- 决策：`CastSessionRef` 与 `SessionGrant` 字段同形但语义不同（receiver 会话 fencing vs IPC/relay 授权），单列类型并注明；`PlaybackPosition` 在边界丢弃 SDK `track_uri`（即媒体 URL，RL-014）；设备 ID 约定由 SDK stable device key 派生，DTO 不含 host/IP/location/port/UDN（CS-002/AG-007）；`InvalidCastCode` 不由通用映射产生——`CastCode` 入界校验 + SDK-07 调用点把投屏码解析失败的 `InvalidInput` 语境化重映射；`Image` 类 SDK 错误映射为 `unsupported_by_receiver`（产品无图片投送）；`disconnect` 幂等且无返回；`stop` 对已终态会话幂等成功，只对旧/外来句柄报错。
- 验证：`cargo test -p crayon-cast-adapter` 22/22 PASS（4 单测 + 16 契约 + 2 链接冒烟）；`cargo test --workspace` 64 suites 全 PASS；`cargo clippy --workspace --all-targets -- -D warnings` PASS；`cargo fmt --all -- --check` PASS；`cargo run -p repo-guard -- scan --root .` PASS（RG-003/004 仅 `app/` 既有 warning，无新增）；`bash scripts/check.sh fast` PASS；`git diff --check` PASS。映射表已逐项对照 pinned revision 源码（`DEVICE_NOT_FOUND`/`CAST_CODE_DEVICE_NOT_FOUND`→Device、`SENDER_DEVICE_ROUTE_EXPIRED`/`NETWORK_ROUTE_LOST`/`NETWORK_ROUTE_TEMPORARILY_UNAVAILABLE`→Network、`CAST_SESSION_*`→State）。
- Code Review：按 current 标准逐维度审查（需求边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试、可维护性），P0/P1 = 0；P2 一项：pinned SDK 能力评估只有 media-type 粒度，无 codec/分辨率矩阵，`ReceiverCapabilities` 的保守合成与 TTL/generation 缓存由 SDK-08 负责（已在 `ReceiverAssessment` 文档注明，跟踪 SDK-08/SDK-14）。
- 未覆盖：真实 service 封装与 SDK 回调线程模型（SDK-05）；发现增量事件（SDK-06）；投屏码"取消"语义——pinned SDK 的 `resolve_device_by_cast_code` 无取消 API，SDK-07 需记录缺口或映射超时；macOS 构建与真机（SDK-13）。

## SDK-04 完成记录（2026-08-11）

- 改动：新增 `test-support/src/cast_facade.rs`（`FakeCastFacade`：实现 SDK-03 冻结的 `CastFacade` 全部 21 个方法；编排 API 覆盖设备快照 upsert/remove（同名/UDN 冲突/多网卡合并为稳定 ID）、投屏码绑定与一次性失败脚本（成功/未找到/过期/错误/取消分支）、connect/disconnect（Stale/Offline → `RouteLost`，disconnect 幂等并拆除活动会话）、逐 `(device, media)` capability 编排（默认 `Unknown` 失败关闭，重复设置模拟 capability 变化）、`cast_media` 成功（Starting→Active 双事件、替换旧会话并报 `ReplacedByNewCast`、generation 递增）/失败脚本、播控 play/pause/seek/volume/mute/stop/position（fencing 先行：旧 generation → `StaleSessionGeneration`，外来句柄 → `NoActiveSession`，被 fencing 拒绝的调用不进入记录，stop 对已终态幂等）、终态模拟（自然结束/receiver stop/route lost/其他 controller 替换）与任意快照注入（旧 generation 事件投递给 listener 但不覆盖 `current_session`））、`test-support/tests/cast_facade.rs`（23 项自测：每个编排能力的正常/边界/重复/一次性脚本/旧 generation）、`crates/crayon-cast-adapter/tests/fake_facade.rs`（CS-001..CS-009 各至少一条确定性场景，测试名保留用例 ID）。依赖变化：`test-support` 新增对 `crayon-cast-adapter` 的生产依赖；`crayon-cast-adapter` 新增对 `test-support` 的 dev 依赖。
- 决策：Fake 放在 `test-support` 而非 adapter crate 的 feature-gated 模块——RG-002 把生产源文件中的 `Fake` 标识符判为 Error（`is_test_path` 只豁免 `test-support`、`tests/`、`*_tests.rs`），且 FND-09 先例（`FakeReceiver`/`PlatformFake`/`MockUpstream`）全部集中在 test-support，SDK-12 的 Fake E2E 需要跨 crate 复用；RG-001/RG-006 天然保证其不进生产依赖图与 Release。Roadmap 任务行的 `cast-adapter/fake`（test only）按“cast-adapter 契约的 Fake，test only”解读并落在此。Fake 为全同步内存实现：无网络、无线程、无时钟、无 sleep，所有状态迁移在触发调用内同步完成。listener 回调在锁外派发，锁序固定 `state -> listeners`；订阅句柄用 `Weak` + Drop 退订，facade 先析构也安全。投屏码“取消”分支：pinned SDK 无取消 API（SDK-03 已记录缺口，SDK-07 负责最终映射），Fake 以一次性脚本错误演示该分支。
- 验证：`cargo test -p crayon-cast-adapter` 31/31 PASS（4 单测 + 16 契约 + 2 链接冒烟 + 9 CS 场景）；`cargo test -p test-support --test cast_facade` 23/23 PASS；`cargo test --workspace` 66 suites / 286 passed / 0 failed PASS；`cargo clippy --workspace --all-targets -- -D warnings` PASS（`--all-features` 因根 crate `formal-product` 与 `legacy-dev` 互斥而无法运行，属既有结构约束，与 SDK-02/03 证据口径一致；Fake 无 cargo feature，不存在 feature 组合遗漏）；`cargo fmt --all -- --check` PASS；`cargo run -p repo-guard -- scan --root .` PASS（RG-001/RG-002/RG-005/RG-007/RG-008 passed；RG-003/RG-004 仅 `app/` 既有 warning，无新增；RG-006 not_applicable，Release artifact 扫描需 `--artifact-path`，Fake 所在 test-support 不在任何生产依赖图中）；`bash scripts/check.sh fast` PASS（guard/format/formal-workspace/legacy-unit 全过）；`git diff --check` PASS。
- Code Review：按 current 标准逐维度审查（需求边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试、可维护性），P0/P1 = 0；P2 一项：Fake 的 `list_devices` 不随 discovery 停止清空快照（沿用 SDK 设备 registry 语义“快照是既有事实”），真实 service 的发现快照语义由 SDK-06 定稿时复核（跟踪 SDK-06/SDK-14）。
- 未覆盖：真实 `SenderCommandService` 生命周期/线程模型（SDK-05）；发现增量事件通道——SDK-03 契约只有快照式 `list_devices`，若 SDK-06 需要增量事件需先评审契约；投屏码取消的最终错误映射（SDK-07）；macOS 构建与真机（SDK-13）。

## SDK 缺口处理

若固定 revision 的公开 facade 未提供浏览器所需能力：

1. 在本 Roadmap 记录缺口和调用场景。
2. 在 Cast-SDK 建独立 Roadmap、测试和公共 API 评审。
3. SDK 合入并推送新 commit 后执行 `SDK-01/02/09` revision 升级验证。
4. 浏览器仓库不得调用 private crate、复制 SOAP/CastExtension、缓存设备 IP 或建立临时协议分支。

## Review 专项

- callback 是否在锁外；session generation 是否贯穿所有控制。
- Cast-SDK diagnostics 不得携带网页 URL、Cookie、relay secret。
- SDK runtime 失败不能阻塞浏览器退出或 Profile 清理。
