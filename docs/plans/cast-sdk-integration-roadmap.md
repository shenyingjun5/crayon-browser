# SDK：Cast-SDK 集成 Roadmap

状态：`FND-08 DONE`；`SDK-01..07 DONE`；`SDK-08 READY`；`SDK-15/16` 为 Partner/TV Cast Manifest 的后续外部依赖任务。任务数 16。源码固定为 Cast-SDK `44c3a99871aa1e68cbda71eacefbb41d23a747a8`，通过 `third_party/cast-sdk` submodule 接入；不得依赖开发者本机源码路径。

## 边界

只通过稳定 facade 复用发现、投屏码、连接、能力、远程媒体投送、播控和会话监督。网页、Cookie、relay recipe、CEF 与产品 UI 不进入 Cast-SDK。Partner/TV Cast Manifest 的签名、设备能力、字幕/队列和结果回报属于 Cast-SDK/接收端协议；浏览器只做缺口分析并消费正式发布的 facade。

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
| SDK-15 | SDK-14,HUB-16 | `docs/plans/**`、Cast-SDK API proposal | 对 Partner/TV signed manifest、能力协商、字幕/队列/结果回报做浏览器侧缺口分析；形成外部 Cast-SDK/receiver 独立 Roadmap/API 提案，不改外部仓库 | `CS-011`; 所有字段有 owner/trust/compat/失败语义；浏览器无临时协议 | X1 |
| SDK-16 | SDK-15，且外部 API 已获批、发布并固定 revision | `crayon-cast-adapter/**`,`crayon-app-runtime/cast_usecase/**` | 仅通过正式 facade 消费 Partner/TV Cast Manifest 能力和事件 | `CS-012`; 签名/版本/能力/字幕/队列/结果；无 raw manifest/控制 URL/协议复制 | X1 |

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

## SDK-05 完成记录（2026-08-11）

- 改动：`crates/crayon-cast-adapter/src/service.rs`（`SenderCastFacade` + `SenderCastFacadeConfig`，实现 `CastFacade` 全部 21 个方法）、`src/service_tests.rs`（22 项）、`src/lib.rs`（导出与模块说明）、`Cargo.toml`（新增锁定 submodule 内 `cast-sender-session` path 依赖，RG-008 passed）。设计要点：SDK 服务是唯一状态所有者，adapter 不镜像设备/会话状态；唯一一把 `Mutex<Option<SenderCommandService>>` 只在 clone/take Arc 句柄期间持有，从不跨 SDK 调用、callback 或 join；`shutdown`/Drop 幂等并按逆序释放（监督会话内存态终止 → disconnect（媒体活跃时至多一次有界 SOAP stop）→ stop_discovery join worker → drop service 由 SDK hub/server 各自 Drop join/关闭），SDK runtime 失败全部 best-effort 吞掉、不 panic；shutdown 后所有可失败调用 fail closed 为 `InvalidState`，无返回值读降级为空。会话桥接：`subscribe_cast_session` → 纯转换 `SessionBridge`，SDK hub 只在 strictly-newer 时发布且按 listener 过滤，旧 generation 事件不到达产品 listener；转换丢弃 health/ownership/receiver 字段与 `error_code`，`playback_position` 在边界丢弃 `track_uri`（RL-014）。播控 fencing 与 Fake 逐条对齐：无会话→`NoActiveSession`、旧 generation→`StaleSessionGeneration`、同/新 generation 未知身份→`NoActiveSession`、终态仅 stop 幂等成功；fence 判定实时重读 SDK supervisor，竞态由 SDK 内部 `CAST_SESSION_STALE_GENERATION` 二次 fencing 兜底。`cast_media` 先 fail-closed 校验连接设备再投送，成功后从 `current_cast_session()` 取真实 handle（含 media_kind）组装 `CastSessionRef`。设备 ID 用 `stable_device_key()`；`disconnect` 先经监督终止活动会话再 `disconnect_device`。
- 决策：`restart` = 新实例（全部端口 ephemeral loopback，状态不跨实例携带，文档化）；`PresentingStatic` 无产品对应态，防御性映射 `Unknown`（facade 永不投图片）；投屏码解码失败当前经通用映射得 `InvalidInput`，语境化重映射 `InvalidCastCode` 与取消缺口留给 SDK-07（结构未堵死：错误映射集中在调用点一行）；生命周期 CI 测试不调用 `start_discovery`/`resolve_device_by_cast_code`（LAN 组播/发包），改用"构造 + 不 start 的调用 + SDK `add_mock_device` loopback 设备 + `begin_platform_self_receiver_session`（仅绑定 ephemeral loopback 控制服务）"驱动确定性监督会话。
- 验证：`cargo test -p crayon-cast-adapter` 53/53 PASS（lib 26 = 4 既有 error + 22 新增：构造无副作用/幂等 stop-discovery/disconnect/shutdown fail-closed/drop+restart/无会话 fencing/cast_media fail-closed/loopback 不可达映射 NetworkUnavailable/connect-disconnect 往返/assess 委托/桥接全流程+generation 单调+stop 幂等/退订不再投递+notify_immediately/callback 内重入 facade 不死锁/并发调用+shutdown 不 panic/转换映射穷尽钉扎/设备 DTO 无定位键）；`cargo test --workspace` 66 suites / 308 passed / 0 failed PASS；`cargo clippy --workspace --all-targets -- -D warnings` PASS；`cargo fmt --all -- --check` PASS；`cargo run -p repo-guard -- scan --root .` PASS（RG-003/004 仅 `app/` 既有 warning，无新增）；`bash scripts/check.sh fast` PASS；`git diff --check` PASS。
- Code Review：按 current 标准逐维度审查（需求边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试、可维护性），P0/P1 = 0；P2 一项：Drop/shutdown 中 `stop_discovery` join discovery worker 的最坏耗时 ≈ 一个在途 SSDP 周期（默认 `discovery_timeout_ms` 10s，可配置）——有界但不可忽略，SDK 未提供更细粒度取消，复核跟踪 SDK-14。
- 未覆盖：`start_discovery`/`refresh_discovery` 的 LAN 组播路径与 `resolve_device_by_cast_code` 真实发包验证为手工项（未运行）；`cast_media` 成功路径需要可应答 SOAP 的接收端（loopback 只覆盖失败映射），由 SDK-13 Harness 负责；orphan 线程依赖代码审查与 SDK hub/server 的 Drop join 保证（CI 无跨平台线程枚举断言）；macOS 构建与真机（SDK-13）。

## SDK-06 完成记录（2026-08-11）

- 改动：`crates/crayon-cast-adapter/src/facade.rs`（discovery 契约注释定稿快照语义，trait 签名不变，Fake/契约测试无需同步签名）、`src/service.rs`（新增 `device_snapshot_of`：按稳定 `DeviceId` 折叠 UDN 冲突与投屏码+SSDP 重复注册，代表取最小 SDK id——与 SDK registry `HashMap` 迭代顺序无关的确定性规则；输出重排为 (friendly_name, device_id) 确定性全序，消除同名设备的随机哈希序；模块文档把 SDK-06 移出 out-of-scope）、`src/service_tests.rs`（新增 6 项真实实现确定性测试，全部走 SDK `add_mock_device` registry 入口，不触 LAN）、`test-support/src/cast_facade.rs`（Fake `list_devices` 过滤非 `Ready` 状态并按同一全序排序；registry 内部保留非 Ready 设备以支撑 `connect` 的 `RouteLost` 分支）、`test-support/tests/cast_facade.rs`（新增 1 项 Fake 对齐测试）。
- 快照语义定稿（CS-001/CS-002）：① 快照只含当前可连接接收端——设备老化（Stale/Offline）、未解析或占位名即从快照消失而非降级展示，重新解析后以同一稳定 `DeviceId` 回归；② `stop_discovery` 不清空快照（pinned SDK 只停 worker/翻 flag，registry 保留），重复 stop 幂等；③ 同一逻辑接收端（同名、UDN 冲突、多网卡/IP 漂移、投屏码+SSDP 双注册）在快照中只出现一次，ID 不含 IP；④ 快照有确定性全序（friendly name，再 device id）；⑤ `refresh_discovery` 在未运行时等同启动（pinned SDK 行为，文档化）。
- 增量事件评审结论：不需要增量通道，契约不扩张。依据：pinned SDK 的 `DiscoveryCycleResult.events` 只在 `discover_devices_cycle*` 返回值中、由内部 discovery worker 消费，公开 facade 无订阅入口（暴露增量须先走 SDK 缺口流程）；CS-001 明确 UI 只消费设备快照，快照轮询 + `refresh_discovery` 触发已满足 CS-001/002 全部验收。
- SDK-04 P2 关闭：确认 pinned SDK `stop_discovery` 不清空设备 registry（`cast-sender-core` `stop_discovery` 仅翻 `discovery_running` flag；service 层 join worker），Fake 语义与真实实现一致并各有测试钉死（真实：`snapshot_survives_stop_and_lists_in_deterministic_order`；Fake：`discovery_snapshot_hides_non_ready_orders_deterministically_and_survives_stop`）。
- 验证：`cargo test -p crayon-cast-adapter` 59/59 PASS（lib 32 = 既有 26 + 新增 6：stop 保留+全序、同名不同 ID、IP 漂移单条目同 ID、UDN 冲突折叠、老化不降级/不可见/再解析回归、无控制 URL/占位名不可见；契约 16；链接 2；CS 场景 9）；`cargo test -p test-support --test cast_facade` 24/24 PASS；`cargo test --workspace` 66 suites / 315 passed / 0 failed PASS；`cargo clippy --workspace --all-targets -- -D warnings` PASS；`cargo fmt --all -- --check` PASS；`cargo run -p repo-guard -- scan --root .` PASS（RG-003/004 仅既有 warning——含 SDK-05 遗留的 `session_bridge_flow_fencing_and_stop_idempotency` ~122 行函数提醒，HEAD 已存在，无新增）；`bash scripts/check.sh fast` PASS；`git diff --check` PASS。
- Code Review：按 current 标准逐维度审查（需求边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试、可维护性），P0/P1 = 0；P2 一项：Fake `connect` 对 Stale/Offline 设备报 `RouteLost`，真实实现因老化设备不可见而在 `find_sdk_device_id` 报 `DeviceNotFound`——两者都是契约内稳定的“先重新发现”错误，但行为不完全同形；真实 `RouteLost`（registry Ready 而 route 过期）路径需要可过期 route 的环境，延期 SDK-07（connection owner）评估是否对齐 Fake，真机证据 SDK-13。
- 未覆盖：`start_discovery`/`refresh_discovery` 的 LAN 组播路径与 worker 驱动的老化/合并为手工项（未运行，CI 不发组播）；`refresh` 未运行时启动 discovery 的 SDK 行为仅文档化、CI 未覆盖；macOS 构建与真机（SDK-13）。

## SDK-07 完成记录（2026-08-11）

- 改动：`crates/crayon-cast-adapter/src/service.rs`（`resolve_device_by_cast_code` 调用点新增 `map_cast_code_error`：DTO 只放行 ASCII alphanumeric 超集，该调用唯一的 `InvalidInput` 来源是 pinned codec 拒绝精确字母表/取值范围/校验和，故语境化重映射为 `InvalidCastCode`，其余错误仍走 CS-008 通用表；`find_sdk_device_id` 由"取 list 首个匹配"改为"取最小 SDK id"，与快照代表规则一致——`list_devices` 同名条目是 `HashMap` 随机序，原实现对双注册设备的 connect 目标不确定；模块文档定稿连接/投屏码语义并移出 out-of-scope）、`src/facade.rs`（契约注释定稿：resolve 稳定结果集、取消语义、connect 幂等/切换/DeviceNotFound/RouteLost、disconnect 幂等与重连；trait 签名未变）、`src/service_tests.rs`（新增 4 项真实实现确定性测试，全部走 `add_mock_device` 或 codec 预解码拒绝，不触 LAN）、`test-support/src/cast_facade.rs`（Fake `connect` 对齐：非 Ready 设备报 `DeviceNotFound`；`RouteLost` 分支改由 `fail_next_connect` 一次性脚本编排，模块文档同步）、`test-support/tests/cast_facade.rs`（2 项既有断言改对齐 + 新增 2 项：RouteLost 一次性脚本/失败不掉线、幂等重复连接/切换/断开后重连）、`crates/crayon-cast-adapter/tests/fake_facade.rs`（CS-003 场景扩展：码过期=DeviceNotFound、取消语义定稿注释；新增 `cs_003_connect_disconnect_state_mapping` 覆盖未知/老化/重复/切换/route lost/断开后重连）。
- 状态映射定稿（CS-003）：成功 Ok；码格式/字母表/校验和错误 `InvalidCastCode`（DTO 入界 + codec 调用点重映射双层）；码过期与无人应答同形 `DeviceNotFound`；LAN/route 失败 `NetworkUnavailable`/`ReceiverUnreachable`/`RouteLost`（CS-008 表）；connect 对同一设备幂等、连接他机切换、快照外（未知/老化）设备 `DeviceNotFound`、可见但 route 过期 `RouteLost`；disconnect 幂等 no-op，断开后重连是普通新 connect。
- 取消语义定稿：pinned SDK `resolve_device_by_cast_code` 无协作式取消 API，调用有界（每候选描述路由一个 discovery timeout × SDK 固定候选端口集）；取消=调用方放弃该有界调用，迟到结果被丢弃，迟到成功只等价于一次新 resolve 的设备注册，facade 不产生任何"已取消"错误。CS-003 取消分支以一次性脚本钉死"任何中止上报只能是稳定码、绝不透传 SDK 字符串"。**SDK 缺口记录**（按「SDK 缺口处理」第 1 条）：调用场景为 UI 投屏码输入后用户取消等待；需要 SDK 提供可取消的 resolve API 才能提前终止在途发包，提案与修订需 Cast-SDK 外部仓库授权，复核点 SDK-14。
- SDK-06 P2 关闭：Fake `connect` 对 Stale/Offline 设备改报 `DeviceNotFound`，与真实实现（老化设备不在快照 → `find_sdk_device_id` fail closed）同形；真实 `RouteLost`（可见但已验证 route 过期）需要可过期 route 环境，pinned SDK 只在 LAN 解析路径建 route record、无公开注入入口，CI 不可确定性构造——Fake 以脚本覆盖该分支，真机证据归 SDK-13。
- 验证：`cargo test -p crayon-cast-adapter` 64/64 PASS（lib 36 = 既有 32 + 新增 4：codec 三类拒绝重映射 InvalidCastCode 且通用 InvalidInput 不受影响、幂等/切换/断开后重连、双注册 connect 命中快照代表条目、老化设备 DeviceNotFound；契约 16；链接 2；CS 场景 10）；`cargo test -p test-support --test cast_facade` 26/26 PASS；`cargo test --workspace` 66 suites / 322 passed / 0 failed PASS；`cargo clippy --workspace --all-targets -- -D warnings` PASS；`cargo fmt --all -- --check` PASS；`cargo run -p repo-guard -- scan --root .` PASS（RG-003/004 仅 `app/` 与既有提醒，无新增）；`bash scripts/check.sh fast` PASS；`git diff --check` PASS。
- Code Review：按 current 标准逐维度审查（需求边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试、可维护性），P0/P1 = 0；P2 一项（延期）：真实 `RouteLost` 路径（visible-but-route-expired）CI 不可构造，如上由 Fake 脚本 + SDK-13 真机 Harness 覆盖，跟踪 SDK-13。
- 未覆盖：投屏码成功/码过期/无人应答的真实 LAN 发包路径为手工项（未运行，CI 不发包）；真实协作式取消依赖 SDK 新 API（缺口已记录）；macOS 构建与真机（SDK-13）。

## SDK 缺口处理

若固定 revision 的公开 facade 未提供浏览器所需能力：

1. 在本 Roadmap 记录缺口和调用场景。
2. 在 Cast-SDK 建独立 Roadmap、测试和公共 API 评审。
3. SDK 合入并推送新 commit 后执行 `SDK-01/02/09` revision 升级验证。
4. 浏览器仓库不得调用 private crate、复制 SOAP/CastExtension、缓存设备 IP 或建立临时协议分支。
5. 修改 Cast-SDK/接收端外部仓库、推送或发布仍需用户明确授权；`SDK-15` 本身只产出缺口和 API 提案。

## Review 专项

- callback 是否在锁外；session generation 是否贯穿所有控制。
- Cast-SDK diagnostics 不得携带网页 URL、Cookie、relay secret。
- SDK runtime 失败不能阻塞浏览器退出或 Profile 清理。
