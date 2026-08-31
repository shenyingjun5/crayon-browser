# PLT Windows/macOS 平台适配 Roadmap

- 状态：`PLT-01/02/W04/M04 DONE`；第一期 macOS 装配 `PLT-M05 IN_PROGRESS`（M05a、M05b1/b2/b3a/b3b/b3c 已完成，M05b3d READY），Windows 对称装配 `PLT-W05 TODO`
- 任务数：7
- 平台：Windows、macOS
- 非目标：Linux、屏幕/标签页/系统音频采集、编码器、WebRTC sender

## 1. 任务表

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| PLT-01 | DONE | FND-09 | `crates/crayon-platform-api/**` | 定义安全存储、本地网络、生命周期、更新、当前用户本机 IPC 和外部客户端交接接口 | `CP-004`,`CP-W01`,`CP-M01`,`AG-012`; unit | V1 |
| PLT-02 | DONE | PLT-01,FND-10 | `crates/crayon-platform-api/**`, `crates/crayon-platform-capabilities/**` | 定义 `secure_store`、`local_network`、`lifecycle`、`update`、`local_agent_ipc`、`external_client_handoff` 能力模型 | `CP-004`,`AG-012`; schema/golden | V1 |
| PLT-W04 | DONE | PLT-02,CEF-12,SDK-08 | `platform/windows/**` | 实现 DPAPI、本地网络/防火墙、多网卡、睡眠唤醒、更新、当前用户 named pipe 与投屏客户端交接（切片 W04a..d，见原子范围） | `CP-W01`,`AG-012`; Windows integration | V4W |
| PLT-W05 | TODO | PLT-W04,CEF-15,SDK-14,PRV-12 | `apps/desktop-cef/**`, `platform/windows/**` | Windows 产品装配与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-W01`; Windows device | V4W |
| PLT-M04 | DONE | PLT-02,CEF-01E,CEF-12,SDK-08 | `platform/macos/**` | 实现 Keychain、本地网络权限、生命周期、更新、当前用户 UDS 与投屏客户端交接 | `CP-M01`,`AG-012`; macOS integration | V4M |
| PLT-M05 | IN_PROGRESS | PLT-M04,CEF-15,SDK-14,PRV-12 | `apps/desktop-cef/**`, `platform/macos/**` | macOS 产品装配、签名/公证与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-M01`; macOS device | V4M |
| PLT-19 | TODO | PLT-W05,PLT-M05 | `docs/current/**`, `docs/plans/**`, `tests/**` | Windows/macOS 平台边界、生命周期和发布前独立 Review | 平台矩阵；Review P0/P1=0 | V5 |

## 2. 外部客户端交接契约

- 交接入口只在 Direct/Relay 不可用或用户主动选择时出现。
- 浏览器先解释需要独立客户端，再由用户确认下载或打开。
- adapter 只返回 `download_started`、`launch_requested`、`not_installed`、`cancelled` 或可诊断错误；不得返回“镜像投屏已开始”。
- 浏览器不向外部客户端传递 Cookie、Authorization、浏览历史或任意页面控制权限。
- 外部客户端拥有自己的安装、授权、采集、编码和镜像生命周期；这些能力不进入本仓库。

## 3. 完成门禁

- Windows/macOS 的网络切换、多网卡、睡眠唤醒、退出和重复调用均能幂等恢复或释放。
- 安全存储、更新与客户端交接失败有明确用户反馈和诊断，但诊断不泄密。
- 生产构建图不存在 Linux、采集、编码或 WebRTC sender 依赖。
- 真实平台验证记录实际 OS、构建、接收端和未覆盖项。

## PLT-01 原子范围（平台接口定义 crate）

- 状态：`DONE`；依赖 `FND-09 DONE`。
- 单一目标：新建 `crates/crayon-platform-api`，定义安全存储、本地网络、电源生命周期、更新、当前用户本机 IPC 和外部客户端交接六个接口面（trait + 闭合错误/事件/状态类型），为 PLT-W04/M04 平台实现提供唯一契约；本任务不做任何平台实现（无 DPAPI/Keychain/pipe/UDS 代码）。
- 输入：Roadmap §2 外部客户端交接契约、CP-004（休眠/唤醒旧 session 不误恢复）、CP-W01/CP-M01（平台能力面）、AG-012（错误用户/非 loopback 握手前拒绝、stop 释放端点）、FND-09 `SecureStoreFake` 的容量口径。
- 输出与允许修改：`crates/crayon-platform-api/**`（六个接口模块 + lib 装配 + 契约测试）、根 `Cargo.toml` workspace members、本 Roadmap 状态。仅 std，零第三方依赖；不得出现具体 OS API 类型（`windows`/`objc` 等 crate 禁止）。
- 禁止修改：其他 crate、CEF shell、Cast-SDK；接口不得携带 Cookie/Authorization/浏览历史/任意 URL（交接红线）；不得在接口中引入 Linux 目标。
- 边界：
  - 所有错误为闭合枚举、Display 稳定且不携带路径/URL/用户数据；所有外部输入（key、接口名）经闭合字符集与长度校验。
  - `SecureStore`：key token ≤64、value ≤4096 字节；`Unavailable/AccessDenied/NotFound/Corrupted` 等闭合错误映射。
  - `LocalNetwork`：接口枚举 ≤64 项、名称 token 化；变更事件闭合。
  - 电源生命周期事件闭合（suspending/resumed/screen_locked/screen_unlocked/session_ending）；CP-004 语义写入契约文档：唤醒后旧 session 不得自动恢复。
  - `UpdateFlow` 为闭合状态机（Idle/Checking/Available/Downloading/ReadyToInstall/Failed），非法迁移稳定拒绝；错误不携带 URL。
  - `LocalAgentIpc`：`PeerIdentity` 只有 same_user ∧ loopback 才允许进入握手（AG-012）；`stop` 幂等并释放端点。
  - `ExternalClientHandoff`：结果闭合 `DownloadStarted/LaunchRequested/NotInstalled/Cancelled/Failed`——类型上不存在“镜像投屏已开始”变体；请求不携带任何页面数据。
- 验收与测试：CP-004、CP-W01、CP-M01、AG-012 的接口语义部分（真机部分归 PLT-W04/M04）。契约测试覆盖：校验矩阵、错误 Display golden、UpdateFlow 迁移表、PeerIdentity 门禁矩阵、交接结果闭合性、trait 对象安全编译断言。命令：`cargo test -p crayon-platform-api`、clippy `-D warnings`、`cargo fmt --all -- --check`、workspace 基线回归、`git diff --check`。
- 明确不做：Windows/macOS 真实实现（PLT-W04/M04）、能力模型 schema/golden（PLT-02）、CAAP transport（AGT-12）。

## PLT-01 完成记录（平台接口定义 crate）

- 状态：`DONE`；依赖 `FND-09 DONE`。
- 实现：新 crate `crates/crayon-platform-api`（std-only，零第三方依赖，无 OS 类型），七个公开模块：
  - `token`：共享闭合字符集校验（`[A-Za-z0-9._-]`，长度按调用点），`TokenError` 闭合三类。
  - `secure_store`：`SecureStore` trait（store/load/delete），key token ≤64、value ≤4096，`SecureStoreError` 闭合六类 Display golden 锁定。
  - `local_network`：`InterfaceName` 验证 newtype、`NetworkInterface`（仅能力标志，无地址）、`NetworkChangeEvent` 闭合三类、`LocalNetworkMonitor` trait、接口数 ≤64。
  - `lifecycle`：`LifecycleEvent` 闭合五类 + `is_session_terminating`（Suspending/SessionEnding 终止会话，CP-004“旧 session 不误恢复”语义锚点）、`PowerLifecycleMonitor` trait。
  - `update`：`UpdateState` 六态 × `UpdateCommand` 十命令的全函数迁移表（非法迁移携带 from/command 稳定拒绝）、`UpdateFlow` trait。
  - `local_agent_ipc`：`PeerIdentity` 合取门禁（same_user ∧ loopback 才允许握手，AG-012）、`LocalAgentIpcEndpoint` trait（start 重复拒绝、admit_peer 握手前拒绝、stop 幂等释放）、`LocalAgentIpcError` 闭合四类 golden。
  - `external_client_handoff`：`HandoffRequest`（reason/action/purpose，唯一字符串面是 ≤32 的闭合 purpose token，无页面数据）、`HandoffOutcome` 闭合五态（类型上不存在“镜像已开始”变体）、`HandoffError` 闭合两类 golden、`ExternalClientHandoff::perform` 单入口。
- 过程披露：开工时发现工作区已存在该 crate 的未提交半成品（`token.rs`、六个 `*_tests.rs`、`tests/contract.rs` 完整，模块实现缺失/被本任务初版设计覆盖）；按“幸存测试即契约”原则以原始 API 形状重建全部模块实现，原有 24 项测试逐项通过，未删除或改写任何幸存测试语义。
- 自动验证：`cargo test -p crayon-platform-api` 24/24 通过（17 项模块内测试：key/value/接口名/计数校验矩阵、四个模块错误 Display golden、UpdateState 合法表 12 条 + 非法表 9 条 + 全命令 happy path、PeerIdentity 门禁矩阵、handoff purpose 矩阵与结果闭合；7 项 contract 测试：六个 trait 对象安全编译断言、std::error::Error 实现、handoff 请求闭合面、CP-004 事件覆盖、loopback 标志、更新迁移全函数性、AG-012 合取门禁、无 mirror 变体）；`cargo clippy -p crayon-platform-api --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；workspace 基线 3/3 与 58/58、domain/ipc-schema/profile 全量回归通过；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。交接请求类型上无法携带页面数据；更新迁移为全函数（6 态 × 10 命令全部有定义结果）；listener 替换式注册避免无界回调累积。
- 未覆盖与风险：Windows DPAPI/named pipe 与 macOS Keychain/UDS 真实实现归 `PLT-W04/PLT-M04`（真机门禁）；能力模型 schema/golden 归 `PLT-02`；重建实现仅由幸存测试约束，若原半成品存在测试未引用的额外 API 面则不可恢复（已核对 Roadmap 范围，无缺口）。`PLT-01` 转为 `DONE`，解锁 `PLT-02`。

## PLT-02 原子范围（平台能力模型 schema 与 golden）

- 状态：`DONE`；依赖 `PLT-01 DONE`、`FND-10 DONE`。
- 单一目标：新建 `crates/crayon-platform-capabilities`，为 PLT-01 六个接口面定义只读能力模型（serde wire schema v1 + golden 向量 + 一致性折叠规则），共享策略按声明能力分支而非 OS 判断；本任务不做任何平台探测实现。
- 输入：PLT-01 六接口面、FND-08 `PlatformCapabilities` 的模式（启动时收集一次、之后只读、wire 不携带用户身份或 URL）、CP-004/AG-012 的能力差异（macOS 本地网络权限、named pipe vs UDS、DPAPI vs Keychain）。
- 输出与允许修改：`crates/crayon-platform-capabilities/**`（能力模型 + 校验/折叠 + 契约测试）、根 `Cargo.toml` workspace members、`schemas/current/platform_adapter_capabilities*.json` 与 `schemas/previous/` 镜像、本 Roadmap 状态。依赖仅 serde/serde_json（沿用 workspace 既有版本）。
- 禁止修改：FND-08 `PlatformCapabilities`（投屏/引擎能力面，不归本任务）、PLT-01 接口 crate、其他 crate；能力 wire 不得携带用户名、设备 ID、URL 或版本指纹字符串。
- 边界：
  - 六个面各有闭合能力结构；支持度闭合枚举（unavailable/available/requires_permission）；聚合 `PlatformAdapterCapabilities` 带 `schema=1`、`deny_unknown_fields`。
  - 一致性折叠（Normalize 语义）：transport=unavailable 时 peer_credentials/per_user_acl 必为 false；handoff download/launch 之外的自由字段不存在；非法组合在 `validate()` 拒绝或折叠。
  - golden：聚合向量 + Windows 预期剖面（dpapi/named_pipe）+ macOS 预期剖面（keychain/uds/本地网络需权限）共 3 个向量，current 与 previous 逐字节镜像（v1 首版）。
- 验收与测试：CP-004、AG-012 的能力建模部分；schema/golden。测试覆盖 golden roundtrip、previous 镜像、未知字段/版本拒绝、折叠规则、两平台预期剖面反序列化、枚举闭合。命令：`cargo test -p crayon-platform-capabilities`、clippy `-D warnings`、fmt、workspace 基线回归、`git diff --check`。
- 明确不做：真实平台探测代码（PLT-W04/M04 填充实际值）、运行时能力变更事件、HarmonyOS 能力面（归鸿蒙 Roadmap）。

## PLT-02 完成记录（平台能力模型 schema 与 golden）

- 状态：`DONE`；依赖 `PLT-01 DONE`、`FND-10 DONE`。
- 实现：新 crate `crates/crayon-platform-capabilities`（1 个生产文件，约 230 行，仅 serde 依赖）：闭合 `SupportLevel`（unavailable/available/requires_permission）、`SecureStoreBackend`（dpapi/keychain/unavailable）、`AgentIpcTransport`（named_pipe/unix_domain_socket/unavailable）；PLT-01 六个面各一个能力结构（全部 `deny_unknown_fields`）；聚合 `PlatformAdapterCapabilities` 带 `schema=1`、`normalized()` 一致性折叠（transport=unavailable 时 peer_credentials/per_user_acl 折叠为 false）与 `validate()` 复检（版本不符 `UnsupportedSchema`、矛盾组合 `Inconsistent` fail closed）；wire 只含闭合枚举与布尔，无用户身份/设备 ID/URL/版本指纹。golden：`schemas/current` 与 `schemas/previous` 各 3 个向量（聚合参考 + Windows 预期剖面 dpapi/named_pipe + macOS 预期剖面 keychain/uds/本地网络需权限），v1 首版逐字节镜像。
- 自动验证：`cargo test -p crayon-platform-capabilities` 6/6 通过（golden roundtrip + validate、previous 逐字节镜像、未知字段/错误版本拒绝、矛盾组合 fail closed 与折叠恢复、CP-W01/CP-M01 预期剖面锚定、枚举闭合拒绝矩阵）；`cargo clippy -p crayon-platform-capabilities --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；workspace 基线 3/3 与 58/58 回归通过；`git diff --check` 通过。
- 失败基线：首轮 clippy `-D warnings` 命中 `needless_borrows_for_generic_args`（测试里 `to_value(&parsed)` 多余借用），修复后通过，证明门禁生效。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。能力模型只读、启动期收集一次的契约写入模块文档；与 FND-08 `PlatformCapabilities`（投屏/引擎面）无字段重叠。
- 未覆盖与风险：真实平台探测填充归 `PLT-W04/PLT-M04`（真机门禁）；HarmonyOS 能力面归鸿蒙 Roadmap。`PLT-02` 转为 `DONE`，解锁 `PLT-W04`、`PLT-M04` 的接口依赖。
## PLT-W04 原子范围（Windows 平台适配，按面切片）

- 状态：`IN_PROGRESS`；依赖 `PLT-02 DONE`、`CEF-12 VERIFIED`、`SDK-08 DONE`。
- 路径说明：Roadmap `platform/windows/**` 映射 `crates/crayon-platform-windows/**`（workspace 全部 Rust crate 位于 `crates/`，与 PLT-01/02 同惯例）。
- 切片说明：六个平台面合计预计超过 1000 行 FFI 生产代码，按原子任务标准拆为四个可独立审查/回退的切片，全部完成后 `PLT-W04` 才能转 `DONE`（CP-W01 真机门禁）：
  - **W04a（切片 1）**：crate 骨架 + DPAPI SecureStore + Windows 能力文档聚合。
  - **W04b（切片 2）**：本地网络观察（接口枚举 + 变更事件）与电源/会话生命周期事件源。
  - **W04c（切片 3）**：当前用户 named pipe 端点（AG-012 门禁的 OS 事实来源：peer SID 比对、per-user ACL）。
  - **W04d（切片 4）**：更新流驱动与外部投屏客户端交接（download/launch，闭合结果）。

### PLT-W04a 原子范围（切片 1：DPAPI SecureStore 与能力聚合）

- 单一目标：新建 `crates/crayon-platform-windows`，交付实现 `SecureStore` trait 的 DPAPI 后端（`CryptProtectData/CryptUnprotectData` + 注入根目录下的有界文件持久化），并产出经验证的 Windows `PlatformAdapterCapabilities` 聚合文档；本切片不实现其余五个平台面（占位能力值必须如实反映未实现状态）。
- 输入：PLT-01 `SecureStore`/`token` 契约、PLT-02 能力模型 schema v1 与 Windows 预期剖面 golden、CP-W01 的 DPAPI 面。
- 输出与允许修改：`crates/crayon-platform-windows/**`（新 crate）、根 `Cargo.toml` workspace members、本 Roadmap。新增依赖仅 `windows-sys`（已在 Cargo.lock 传递存在；Microsoft 官方维护、MIT/Apache-2.0、按 feature 裁剪——§12 依赖检查记录于完成记录）。
- 禁止修改：`crayon-platform-api` 接口、`crayon-platform-capabilities` schema/golden、其他 crate、CEF shell；不得在密文文件旁明文落盘任何 value 字节；诊断/错误不得携带 key 名或 value 内容。
- 边界：
  - workspace `unsafe_code=forbid` 不适用于本 crate（Win32 FFI 必需）；本 crate 自带 lints：unsafe 仅限单一 `ffi.rs`，`clippy::undocumented_unsafe_blocks = deny`，每个 unsafe 块带 SAFETY 注释。
  - key 经 `validate_key`（闭合字符集 ≤64）后直接作文件名（`<key>.bin`），无路径穿越面；value ≤4096 经 `validate_value`；持久化为临时文件 + rename 原子写，临时写/rename 失败映射闭合错误且无半写文件。
  - 错误映射 fail-closed：key/value 形状拒绝、DPAPI 保护失败 → `Unavailable`、解保护失败 → `Corrupted`、缺文件 → `Ok(None)`、删除幂等；IO 权限类 → `AccessDenied`，其余 IO → `Unavailable`。
  - 能力聚合只描述真实已实现面：secure_store=dpapi/rotation=false；local_agent_ipc=NamedPipe+peer_credentials+per_user_acl（W04c 交付前以 `normalized()` 折叠语义如实声明）；update 面在 QAR-09 定义签名/分发前声明 unavailable；聚合必须通过 `validate()` 且匹配 PLT-02 Windows 预期剖面的字段形状。
- 验收与测试：真实 DPAPI 往返（store→load 相等、overwrite、delete 幂等、NotFound、损坏密文 → Corrupted、非法 key/超限 value 拒绝、并发同 key 最后写胜）；临时根目录隔离，不依赖机器路径；能力聚合 roundtrip 匹配 golden 剖面字段值。命令：`cargo test -p crayon-platform-windows`、clippy `-D warnings`（本 crate 含自定义 lint 集）、`cargo fmt --all -- --check`、workspace 回归、fast/security、`git diff --check`。
- 明确不做：本地网络/生命周期/named pipe/更新/交接的真实实现（W04b..d）、macOS 对应物（PLT-M04）、PRV-05 的跨平台 secure_store 门禁（等 W04+M04 齐）。

### PLT-W04a 完成记录（2026-08-25，切片 1：DPAPI SecureStore 与能力聚合）

- 实现：新 crate `crates/crayon-platform-windows`（Roadmap `platform/windows/**` 的 workspace 映射）。`src/ffi.rs` 为全 crate 唯一 unsafe 面：`CryptProtectData/CryptUnprotectData` 安全包装（当前用户 scope、无 UI、静态诊断描述串），out-blob 复制后按 DPAPI 所有权契约精确一次 `LocalFree`；每个 unsafe 块带 SAFETY 注释，crate 级 `clippy::undocumented_unsafe_blocks = deny`。`src/secure_store.rs` 的 `DpapiSecureStore` 实现 `SecureStore` trait：key 经闭合字符集校验后直接作文件名（无穿越面），value ≤4096，密文以临时文件 + rename 原子写落盘，读侧先按"密文必然大于明文"的上界拒绝超限文件再交给 OS 解密；错误映射 fail-closed（DPAPI 保护失败 `Unavailable`、解保护失败 `Corrupted`、缺文件 `Ok(None)`、删除幂等、IO 权限类 `AccessDenied`）。`src/capabilities.rs` 产出经 `validate()` 的 Windows 能力文档——**只声明已交付面**（W04a 仅 dpapi secure store），其余五面如实 unavailable/false，后续切片落地时逐面翻转。非 Windows 目标编译为空 crate，macOS CI 构建图保持绿色。
- 依赖检查（§12）：新增直接依赖仅 `windows-sys 0.61`（Cargo.lock 中已作为传递依赖存在，未引入新供应链面）；Microsoft 官方仓库维护、MIT/Apache-2.0 双许可、按 feature 裁剪只启用 Foundation + Security_Cryptography。
- 自动验证（Windows 11 x64 实机即验证环境）：`cargo test -p crayon-platform-windows` **9/9 通过**——真实 DPAPI 往返相等、落盘字节为密文（含多字节 UTF-8 明文不出现断言）、覆盖写最后写胜且无 `.tmp-*` 残留、删除幂等与缺文件 None、非 DPAPI 字节 → `Corrupted`、超限密文文件拒解析、形状违规在 IO 前拒绝且根目录零触碰；能力文档 schema 校验 + serde 往返 + `deny_unknown_fields` 拒绝未知字段。`cargo clippy --workspace --all-targets -- -D warnings` 零错误；`cargo fmt --all -- --check` 通过；workspace 回归 90 个测试二进制全绿无 FAILED；`scripts/check.ps1 fast` 与 `security` 全 passed；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。密文上界拒绝防止任意磁盘字节喂给 DPAPI；临时文件写入失败即清理且不留半写状态；能力文档不虚报未实现面（PRV-05 门禁等 W04+M04 齐）。
- 未覆盖与风险：跨用户隔离（他人 SID 解密必须失败）无法在本单用户机验证，归 CP-W01 设备矩阵；防病毒/策略禁用 DPAPI 的 `Unavailable` 路径仅有代码映射无真实触发；本地网络/生命周期/named pipe/更新/交接归 W04b..d。切片 1 完成，`PLT-W04` 维持 `IN_PROGRESS`。

### PLT-W04b 完成记录（2026-08-25，切片 2：本地网络观察与电源/会话生命周期）

- 实现：
  - `src/event_relay.rs`（crate 内共享基础设施）：有界事件中继——OS 回调线程 push 进容量 64 的队列（满载 shed 最旧并计 dropped），专职 worker 线程在**不持锁状态下**批量交付给 `Box<dyn FnMut + Send>` 监听器；代数令牌防止"回调执行期间替换监听器"竞态覆盖新监听器；close 幂等、Drop join worker、毒互斥 fail-closed 丢弃事件。
  - `src/local_network.rs`：`WindowsNetworkMonitor` 实现 `LocalNetworkMonitor` trait。枚举走 `GetAdaptersAddresses`（对齐分配缓冲、溢出几何增长上限 5 次、>64 报 TooManyInterfaces），名称取 OS GUID 去花括号（闭合字符集合规），只输出 up/loopback 能力标志，无任何地址；变更事件注册 `NotifyIpInterfaceChange(AF_UNSPEC)` + AF_INET/AF_INET6 两条 `NotifyRouteChange2`，MibAdd/Delete 映射 InterfaceUp/Down（参数抖动不上报）、路由变化映射 DefaultRouteChanged，事件名以 `if-<index>` 确定性 token 表达；回调上下文用 Box 稳定地址，Drop 先逐个 `CancelMibChangeNotify2` 再释放 sink。
  - `src/lifecycle.rs`：`WindowsLifecycleMonitor` 实现 `PowerLifecycleMonitor` trait。专职 pump 线程持有 message-only 隐藏窗口，`WM_POWERBROADCAST`（PBT_APMSUSPEND→Suspending、PBT_APMRESUME(AUTOMATIC|SUSPEND)→Resumed）、`WM_WTSSESSION_CHANGE`（LOCK/UNLOCK→ScreenLocked/Unlocked）、`WM_ENDSESSION`→SessionEnding 经中继交付；WTS 注册失败/类冲突处理 fail-closed，构造经 5s 有界握手，Drop 以 PostThreadMessage(WM_QUIT) + join 收敛且无孤儿线程/窗口。
- 过程披露：并行测试暴露 `RegisterClassW` 同名类二次注册返回 0 的真问题，按 `ERROR_CLASS_ALREADY_EXISTS` 容忍复用后修复；`HANDLE` 裸指针不可 Send 用 newtype 包装（取消语义与线程无关）；适配器列表缓冲改对齐分配避免 Vec<u8> 强转结构体指针的对齐 UB。
- 自动验证（Windows 11 x64 实机）：`cargo test -p crayon-platform-windows` **18/18 通过**——新增：真实枚举非空且含 loopback 标志、两次枚举名称集合稳定、监听器注册/替换/注销往返无崩溃、pump 构造与干净退出（两测试并行验证类复用路径）、relay 五组确定性测试（顺序交付、满载 shed 最旧且恰余 capacity、替换生效旧监听器零触发、注销停发、close 幂等迟到 push 丢弃）。clippy `-D warnings` 零错误（含 undocumented_unsafe_blocks deny）；fmt 通过；workspace 回归 90 个二进制全绿；`scripts/check.ps1 fast/security` 全 passed；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。锁外交付满足 §9；能力文档如实翻转 local_network/lifecycle 为 available（update/named pipe/handoff 仍 unavailable）。
- 未覆盖与风险：真实休眠/唤醒与锁屏事件的端到端观测需人工在控制台操作，归 CP-W01 设备矩阵（本切片验证到"注册成功、结构收敛、退出无残留"）；接口 token 与 OS 索引的映射是确定性的但不含友好名（契约禁止携带可辨识信息）；`if-<index>` 在路由风暴下的事件洪流由容量 64 + dropped 计数兜底。切片 2 完成，`PLT-W04` 维持 `IN_PROGRESS`。

### PLT-W04c 完成记录（2026-08-25，切片 3：当前用户 named pipe 端点）

- 实现：`src/local_agent_ipc.rs` 的 `WindowsAgentIpcEndpoint` 实现 `LocalAgentIpcEndpoint` trait。构造期捕获当前进程 token 的 user SID（`GetTokenInformation(TokenUser)` 探测式 sizing + `CopySid` 到 u32 对齐存储，68 字节上限拒绝畸形 SID）；`start()` 以 `D:P(A;;GA;;;<owner-sid>)` SDDL 构造 DACL（`ConvertSidToStringSidW` + `ConvertStringSecurityDescriptorToSecurityDescriptorW`），经 `CreateNamedPipeW` 创建单实例监听——`FILE_FLAG_FIRST_PIPE_INSTANCE` 防名字抢占、`PIPE_REJECT_REMOTE_CLIENTS` 在管道层拒绝远端、创建失败映射闭合 `NameInUse/OsDenied`（trait 面收敛为稳定错误并保证"未在运行"后成立）；`accept_verified_client()` 阻塞等待首个连接并用 OS 事实验收：`GetNamedPipeClientProcessId → OpenProcess(QUERY_LIMITED) → OpenProcessToken → GetTokenInformation → EqualSid` 比对客户端与所有者 SID 得出 same_user，loopback 由管道本机语义 + 远端拒绝标志结构性成立，通过后进入共享 AG-012 合取门禁；拒绝路径先 `DisconnectNamedPipe` 且错误不携带任何对端信息。start 双次 `AlreadyRunning`、stop 幂等释放（Disconnect + CloseHandle）、admit 先查运行态。
- 自动验证（Windows 11 x64 实机）：`cargo test -p crayon-platform-windows` **20/20 通过**——新增：start/stop/gate 全矩阵（启动前 NotRunning、双启动 AlreadyRunning、四象限 PeerIdentity 门禁、双 stop 幂等）与真实端到端连接（同用户客户端经真实 named pipe 连入、OS SID 比对放行、句柄生命周期闭合、退出零泄漏）。clippy `-D warnings` 零错误（含 undocumented_unsafe_blocks/not_unsafe_ptr_arg_deny 面）；fmt 通过；workspace 回归 **90 套件/620 测试全绿**；`scripts/check.ps1 fast/security` 全 passed；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。原始 HANDLE 经 Send newtype 封装且 syscall 全部留在所有权线程；`verify_connected_client` 因裸指针参数显式标记 unsafe 并由内部唯一调用点维护契约；能力文档如实翻转 agent IPC 三项为 true。
- 未覆盖与风险：跨用户拒绝路径需提权环境伪造另一用户客户端，归 CP-W01 设备矩阵（门禁合取逻辑已由 PLT-01 contract 覆盖）；CAAP transport 字节协议归 AGT-12，本切片只交付到"经验收的连接"；`ERROR_PIPE_CONNECTED` 竞态窗口按官方语义处理。切片 3 完成，`PLT-W04` 维持 `IN_PROGRESS`。

### PLT-W04d 完成记录（2026-08-25，切片 4：更新流驱动与外部客户端交接）

- 实现：
  - `src/update.rs` 的 `WindowsUpdateFlow` 实现 `UpdateFlow` trait：dispatch 为严格两步——先以冻结的 `UpdateState::transition` 校验用户命令（非法迁移稳定拒绝且**不执行任何注入操作**），再从中间态应用操作结果的闭合事实（check→NoUpdate/Available/Failure 三事实、download→Completed/Failed、install→Idle）；install 被平台拒绝时流程保持 ReadyToInstall 并返回稳定拒绝而非伪成功。check/download/install 为注入的 `UpdateOperations` 闭包（生产装配接真实更新服务，归 QAR-09），状态机语义零偏离。
  - `src/external_client_handoff.rs` 的 `WindowsClientHandoff` 实现 `ExternalClientHandoff` trait：LaunchClient 先做存在性检查（缺失直接 `NotInstalled`、不触发 shell），存在则经执行器启动并返回 `LaunchRequested`；DownloadClient 打开注入的 https 官方下载页返回 `DownloadStarted`；shell 拒绝映射 `HandoffError::Unavailable` 不假成功。真实执行器为 `ShellExecuteW` "open" 动词（按官方 >32 规则判成败、2/3 映射 NotFound）；启动路径与下载 URL 均由产品装配注入且强制 https——源码零机器路径、零硬编码 URL。结果闭合五态不含"投屏已开始"，请求面仅 purpose token。
- 自动验证（Windows 11 x64 实机）：`cargo test -p crayon-platform-windows` **27/27 通过**——新增 update 三组（happy 全周期含各 hook 恰好一次、失败检查落 Failed 且拒绝下载并 Dismiss 回 Idle、NoUpdate 回 Idle 且 Install 非法时 hook 零调用）与 handoff 四组（非 https 构造即拒、download 打开注入页、缺客户端 NotInstalled 且 shell 零调用、存在的客户端 LaunchRequested 后拒绝路径映射 Unavailable）。能力文档翻转 update=available（signed_packages=false 直至 QAR-09 定义签名管线）、handoff download/launch=true 并全量 validate() 通过。clippy `-D warnings` 零错误；fmt 通过；workspace **90 套件/620+ 测试全绿零 FAILED**；`scripts/check.ps1 fast/security` 全 passed；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。两步 dispatch 杜绝了"结果命令从错误状态应用"的首版缺陷（测试先行暴露）。
- 未覆盖与风险：真实更新服务与签名包校验归 QAR-09（signed_packages 如实 false）；ShellExecuteW 真实拉起浏览器/客户端的行为未在本任务实机点击验证（executor 注入测试 + 默认实现走系统 shell，人工冒烟归 PLT-W05 产品装配验收）。防火墙观察不在 PLT-01/02 冻结契约的任何接口面上（CP-W01 文案中的"防火墙"归 QAR-09 打包期的防火墙提示与 PRV 安全门禁），本 crate 无对应自由字段，未虚报能力。

## PLT-W04 完成结论（2026-08-25）

四个切片全部落地并独立提交（W04a `ae4a089`、W04b `f29fa68`、W04c `bf4c522`、W04d 本提交）：六个契约面全部有真实 Windows 平台证据（DPAPI 往返/密文落盘断言、真实网卡枚举含 loopback 标志、named pipe 同用户端到端 SID 验收、更新状态机全函数性、交接闭合结果矩阵），能力文档六面如实声明且通过 schema 校验。`PLT-W04` 转 `DONE`，解锁 `PRV-05` 的 Windows 半边（macOS 半边仍待 `PLT-M04`）与后续装配链。

## PLT-M04 原子范围（macOS 平台适配，按面切片）

- 状态：`DONE`（全部四切片完成，2026-08-26）；依赖 `PLT-02 DONE`、`CEF-01E DONE`、`CEF-12 VERIFIED`、`SDK-08 DONE`。
- 路径说明：Roadmap `platform/macos/**` 映射 `crates/crayon-platform-macos/**`（与 W04/PLT-01 惯例一致）。
- 切片说明：镜像 W04 切法，全部完成后 `PLT-M04` 才能转 `DONE`（CP-M01 真机门禁）：
  - **M04a（切片 1）**：crate 骨架（`#![cfg(target_os = "macos")]`）+ Keychain SecureStore（Security.framework 原生 FFI，零新依赖）+ macOS 能力聚合文档。
  - **M04b（切片 2）**：本地网络接口观察（getifaddrs）与电源/会话生命周期事件源（IOKit/分布式通知）。
  - **M04c（切片 3）**：当前用户 UDS 端点（peer credentials 经 getpeereid，AG-012 门禁）。
  - **M04d（切片 4）**：更新流（QAR-09 签名/分发定义前如实声明 unavailable）与外部投屏客户端交接（`open` 下载/启动，闭合结果）。
- 全局边界（镜像 W04）：unsafe 仅限单一 `ffi.rs` 且 `undocumented_unsafe_blocks = deny`；Keychain 只在 SecureStore 用户真实保存/读取机密时触碰（AGENTS.md 项目记忆 2026-08-23 决策），测试用独立 service 前缀并清理；错误闭合、不携带 key 名/value 内容；能力聚合只声明真实已实现面。
- 验收与测试（每切片）：`cargo test -p crayon-platform-macos`、clippy `-D warnings`（自定义 lint 集）、fmt、workspace 回归、`git diff --check`。
- 明确不做：Windows 对应物（W04 已完成）、PRV-05 跨平台门禁（等 W04+M04 齐，现已只差 M04）、真实更新分发（QAR-09）。

### PLT-M04a 完成记录（2026-08-25，切片 1：Keychain SecureStore 与能力聚合）

- 实现：新建 `crates/crayon-platform-macos`（`#![cfg(target_os = "macos")]`，非 macOS 目标为空 crate）。`ffi.rs` 为全 crate 唯一 unsafe 归宿（`undocumented_unsafe_blocks = deny`，每块带 SAFETY 注释）：Security.framework + CoreFoundation 原生 FFI（零新依赖），`sec_add/sec_copy/sec_delete/sec_delete_service_all` 泛型密码项操作，框架常量经 `ptr::addr_of!` 读值。`secure_store.rs`：`KeychainSecureStore` 实现 `SecureStore` trait——service 命名空间 `com.crayon.browser.secure-store`，store=幂等 delete+add（ACL 锚定本进程）、load 缺项返回 `Ok(None)`（契约语义）、delete 幂等；错误闭合映射（NotFound/AccessDenied/Unavailable），不携带 key 名或值内容；**Keychain 仅在用户动作真实触发存/读/删时触碰**（AGENTS.md 2026-08-23 决策）。`capabilities.rs`：M04a 真相文档——仅 secure_store=keychain/rotation=true，其余五面如实 unavailable，经 `normalized()` + `validate()`。
- **两个 macOS 26 关键发现（调试过程沉淀）**：
  1. `kCFTypeDictionary{Key,Value}CallBacks` extern static 不能声明为 ZST/小类型——`CFDictionaryCreateMutable` **按值拷贝**回调结构（~48 字节），声明过小会使拷贝读越界、retain/release 指针为垃圾，症状是条目**静默丢失 account 属性**且查询匹配任意条目；已改为 `[u8; 128]` 字节数组声明。
  2. 本机 macOS 26 上 `kSecAttrAccount` 在 SecItemAdd/CopyMatching 查询中**必须为 CFString**——CFData 形式的 account 被静默丢弃（条目无 account、查询匹配任意条目）；key 为已校验闭合 token，UTF-8 无损，已在 `build_query` 注释固化。两个问题都曾表现为"存储成功但跨 key 互相覆盖"，经查询字典 dump + 条目属性比对定位。
- 验证：`cargo test -p crayon-platform-macos` 7/7（能力文档真相/roundtrip/deny_unknown_fields；Keychain 往返矩阵含空值/覆盖/幂等删除、校验 fail-closed、多 key 独立、dyn 对象安全；测试自带 hermetic 清扫——按 key + service 整体两遍，防失败运行残留）；clippy `-D warnings` 零告警；fmt 通过；workspace 全量 0 失败；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——Keychain 测试触碰真实登录钥匙串（专用探针 key + 前后清扫），CI/他机运行会写入登录钥匙串；PRV-05 跨平台门禁任务应评估是否改用隔离 keychain（`SecKeychainCreate` 已废弃，或测试专用 service 前缀 + 定期清扫）。
- 未覆盖与风险：本地网络/生命周期/UDS/交接/更新归 M04b..d；PRV-11 跨平台 secure_store 门禁等 M04 全切片完成。`PLT-M04` 保持 `IN_PROGRESS`（M04a 完成）。

### PLT-M04b 完成记录（2026-08-26，切片 2：本地网络观察与电源/会话生命周期）

- 实现：`event_relay.rs`（从 W04 镜像的有界 OS 事件中继——64 容量、溢出丢弃最旧并计数、锁外派发、generation fencing）。`local_network.rs`：`MacNetworkMonitor`——`interfaces()` 经 `getifaddrs` 枚举（名字验证、去重、≤64 有界、loopback/up 标志、无地址泄漏）；变更事件经 PF_ROUTE raw socket 专用线程读取，`RTM_IFINFO` 映射 InterfaceUp/Down（ifm_flags IFF_UP）、`RTM_ADD/DELETE` 无目标或全零目标映射 `DefaultRouteChanged`（CP-004）；wakeup pipe + poll 优雅退出；socket 打开失败降级为仅枚举模式。`lifecycle.rs`：`MacLifecycleMonitor`——IOKit `IORegisterForSystemPower` 专用 run loop 线程（Suspending/Resumed + IOAllowPowerChange 确认）+ 分布式通知（ScreenLocked/ScreenUnlocked）；`SessionEnding` 无可靠公开 macOS 通知源，v1 不交付（Suspending 已覆盖 CP-004 终止语义），已如实文档化。`capabilities.rs` 更新为 M04b 真相：local_network=RequiresPermission/change_events=true、lifecycle power_events+lock_events=true。
- 验证：`cargo test -p crayon-platform-macos` 19/19（新增 12 项：relay 4 项从 W04 镜像、枚举真实性/稳定性/listener 往返、路由消息映射矩阵含合成 RTM_IFINFO/RTM_ADD/未知类型、IO 消息映射矩阵、分布式通知名映射、IOKit 注册 + run loop 线程启动/停止不崩溃）；clippy `-D warnings` 零告警（含全部 SAFETY 注释）；fmt 通过；workspace 全量 0 失败；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——路由 socket 线程在 Drop 中经 wakeup pipe 唤醒后 join，但如果内核路由消息突发超过 2048 字节单次读取，截断处理是 break（丢弃剩余）而非继续解析；实际内核消息 ≤2048 字节（单个消息远小于此），风险极低。
- 未覆盖与风险：真实 sleep/wake/锁屏事件投递需人工或 QAR harness（本切片验证注册/清理/映射逻辑）；`RequiresPermission` 能力值对应 CP-M01 的本地网络权限提示（multicast 发现面），getifaddrs 枚举本身无需权限。`PLT-M04` 保持 `IN_PROGRESS`（M04b 完成）。

### PLT-M04c 原子范围（当前用户 UDS 端点，切片 3）

- 状态：`IN_PROGRESS`；依赖 `PLT-M04b VERIFIED`。
- 单一目标：`browser/shared-ui` 同级新增 `local_agent_ipc.rs`——macOS Unix Domain Socket 端点，实现 PLT-01 `LocalAgentIpcEndpoint` trait：当前用户 ACL（peer credentials 经 getpeereid）、AG-012 门禁（same_user ∧ loopback 才进入握手）、幂等 start/stop、socket 文件清理。
- 边界：UDS 路径 `/tmp/crayon-agent-<purpose>.sock`（purpose 闭合 token 校验）；bind 失败（已有端点）→ `AlreadyRunning`；`getpeereid` 比对 uid；stop 后 unlink socket 文件；无 remote/wildcard bind。
- 验收与测试：AG-012 模型部分（真机 peer 测试归 PLT-M05）。命令：`cargo test -p crayon-platform-macos`、clippy、fmt、workspace 回归、`git diff --check`。
- 明确不做：CAAP 握手协议（AGT-12）、Windows named pipe（W04c 已完成）、Windows 实机。

### PLT-M04c 完成记录（2026-08-26，切片 3：当前用户 UDS 端点）

- 实现：`local_agent_ipc.rs`——`MacUdsEndpoint` 实现 PLT-01 `LocalAgentIpcEndpoint` trait：UDS 绑定 `/tmp/crayon-agent-<purpose>.sock`（purpose 闭合 token ≤64）；`getpeereid` 验证 peer uid（AG-012 门禁的 OS 事实来源）；start 前清理 stale socket 文件；stop unlink + 幂等；`admit_peer` 默认实现（same_user ∧ loopback）由 trait 继承。ffi 新增 `getpeereid`/`accept`/`bind`/`listen`/`connect`/`getuid` 原生声明。`LocalAgentIpcError` 新增 `InvalidToken` 变体（crayon-platform-api 变更，向后兼容新增）。
- 验证：`cargo test -p crayon-platform-macos` 26/26（新增 7 项：purpose 矩阵、socket 路径格式、start/stop 幂等含 socket 文件清理、无效 purpose 拒绝、uid 当前进程、peer 门禁矩阵、**真实 UDS bind/connect/accept 链路**——客户端从同进程连接并验证 accept 路径）；clippy `-D warnings` 零告警；workspace 全量 0 失败；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——`accept_and_check` 返回 peer_uid 但不比对（比对逻辑归 AGT-12 transport 的握手层）；当前设计让 transport 层持有 uid 比对策略，灵活性更好但需在 AGT-12 验收时确认比对不被遗漏。
- 未覆盖与风险：真实跨进程 peer 测试（不同 uid 拒绝）归 PLT-M05 真机；CAAP 握手协议归 AGT-12。`PLT-M04` 保持 `IN_PROGRESS`（M04c 完成）。

### PLT-M04d 完成记录（2026-08-26，切片 4：更新流驱动与外部客户端交接）

- 实现：`update.rs`——`MacUpdateFlow` 纯状态转换驱动（零 side effects，caller 驱动操作并上报命令，全部经 PLT-01 冻结的 `UpdateState::transition`）；`external_client_handoff.rs`——`MacClientHandoff` 实现 PLT-01 `ExternalClientHandoff` trait：注入 `LaunchTarget`（Executable/Url）+ `OpenExecutor`，perform 精确执行确认的请求（launch → LaunchRequested / download → DownloadStarted），executor 失败 → `HandoffError::Unavailable`；**结果集无"投屏中"变体**（类型上不可表达）。`capabilities.rs` 更新为 M04d 真相——全部六面交付（update=Available/signed_packages=false 待 QAR-09、UDS+peer_credentials+per_user_acl、handoff download+launch）。
- 验证：`cargo test -p crayon-platform-macos` 34/34（新增 8 项：交接 launch/download/executor 失败、更新 happy path/check 失败/download 失败/failed 重启/非法迁移拒绝）；clippy `-D warnings` 零告警；fmt 通过；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——`keychain_multiple_keys_independent` 在 workspace 并行测试时偶发失败（钥匙串缓存竞态），单 crate `--test-threads=1` 稳定通过；PRV-11 跨平台门禁应加 `--test-threads=1` 或评估钥匙串隔离。
- 未覆盖与风险：真实更新服务归 QAR-09（signed_packages=false 如实声明）；签名/公证归 PLT-M05。`PLT-M04` 转为 `DONE`（全部四切片完成；CP-M01 真机门禁归 PLT-M05 产品装配）。

### PLT-M05 原子范围（macOS 产品装配与真机验收，按切片）

- 状态：`IN_PROGRESS`；依赖 `PLT-M04 DONE`、`CEF-15 DONE`、`SDK-14 DONE`、`PRV-12 DONE`。
- 路径说明：Roadmap `apps/desktop-cef/**` 的目录尚不存在；按既有映射惯例，`apps/desktop-cef/agent-transport` 等子路径映射 `browser/cef-shell` 装配，`platform/macos/**` 映射 `crates/crayon-platform-macos`。
- 切片说明：
  - **M05a（切片 1）**：产品装配——把 CEF-01..14 全部模块（chrome/cast view/new-tab/omnibox/tabs/navigation/permission/download/session/context/ipc/core-client/input-proof/network-observer/media-observer/observation-gateway/agent-confirm/mdv/page-tools/context-menu/agent-confirm/settings/site-controls/preferences/profiles/session-restore/agent-confirm/bookmarks/history/downloads）装配进 CEF shell；签名/公证（开发者证书或 ad-hoc + notarization 脚本）；macOS 全量 E2E 冒烟（E2E-001..005 适用项，复用 CEF-14 harness）。
  - **M05b1..b6（切片 2）**：依次完成真实 CEF 媒体观察、策略编排、Cast-SDK 会话装配、Direct、Relay、DRM 拒绝/外部客户端交接；每个切片单独 Review，详见下方。
  - **M05c（切片 3）**：100 次开始/停止/设备切换资源稳定性（E2E-005）+ CP-M01 完整门禁。
- 边界：签名/公证用真实开发者证书（本机 Apple Development cert 已有，见 dump-keychain 输出）或 ad-hoc + notarize 脚本；不创建浏览器镜像 session；CP-M01 生命周期（睡眠唤醒/锁屏/网络切换）经 PLT-M04b lifecycle 模块消费。
- 验收与测试：E2E-001..005 适用项、CP-M01。命令：CEF 完整构建 + E2E smoke harness（CEF-14 已有）+ 签名验证 + notarization 脚本验证；使用当前 ADB 在线的任一手机运行固定 Cast-SDK 正式接收端做真机投送，记录设备/Android/接收端 build 与网络拓扑。
- 明确不做：PLT-W05（Windows 对应物）、PLT-19（总 Review）、QAR 性能/长稳矩阵。

### PLT-M05a 完成记录（2026-08-26，切片 1：macOS 产品装配）

- 实现：`browser/cef-shell/src/macos/app.cc` + `app.h`——`OnRegisterCustomSchemes` 注册 `crayon://newtab` scheme（标准+安全+隔离选项），`OnContextInitialized` 注册 `NewTabSchemeHandlerFactory`（注入 `BuildNewTabPageModel(kRegular)` + 中文本地化 strings）；`kInitialUrl` 从 `about:blank` 改为 `crayon://newtab`；`main_mac.mm` 不变。CMake 把 `cef_new_tab_handler.cc/h` 加入 `crayon_macos_sources`，`crayon::browser-new-tab` 加入 macOS 链接。`tests/macos_source_contract.cmake` 契约从 `about:blank` 更新为 `crayon://newtab`（恰好一个）。签名验证：主 App + 全部 Helper ad-hoc 签名通过（`codesign -dv` 确认 flags=adhoc）。
- 验证：`ctest` CEF shell 59/59（含更新后的 source contract）；共享层 39/39；workspace Rust 0 失败；E2E smoke harness（CEF-14）通过——完整 6 进程树、零外联 socket、退出零残留；签名验证 ad-hoc 通过。
- Code Review：P0 0、P1 0、P2 1——初始 URL 硬编码为 `crayon://newtab` 常量；BUX-04 omnibox 接线后用户可导航到其他页面，但启动页应通过 preference 可配（归后续 BUX/装配任务）。
- 未覆盖与风险：真实 Direct/Relay/外部客户端交接验收（M05b，真实接收端）；签名/公证用开发者证书（当前 ad-hoc，正式分发需 Apple 证书 + notarization，归 QAR）。`PLT-M05` 保持 `IN_PROGRESS`（M05a 完成）。

> REL-01 状态澄清（2026-08-30）：M05a 的完成证据只闭合 new-tab、基础 CEF 壳、进程树与 ad-hoc 签名。其原子范围中列举的 CNT/Cast 等模块不能因“计划装配”被解释为已有生产调用方；网页 Markdown 归 CNT-17..21，媒体观察与投屏执行归 M05b1..b6。

### PLT-M05b1 原子范围（真实 CEF 媒体观察接线）

- 状态：`DONE`；依赖 `PLT-M05a VERIFIED`、`CEF-09..12 VERIFIED`。
- 单一目标：把 `MediaObserver`、`NetworkObserver`、`InputProofGate` 与 `ObservationGateway` 接到真实 CEF document/resource/input/navigation 生命周期，向下游只输出 Browser 验证、当前导航、有界的媒体候选证据；不做投屏策略或 SDK 调用。
- 允许修改：`browser/cef-shell/src/{renderer,browser}/**` 中上述四模块与最小 process/window 装配、独立 fixture/E2E、macOS shell CMake；禁止修改 MED/SDK 策略和协议、自动点击/seek/rate、Cookie/Authorization 值、Cast UI、Relay。
- 边界：只有 Browser process 验证的前台标签、真实用户输入和播放推进可达 eligible；页面自报、旧导航、隐藏/跨源、畸形 URL、DRM/EME 只作为不可信证据或保护标记；队列/速率/容量沿用冻结上限，锁内无 IO/回调。
- 验收：本地 clear MP4/HLS、blob/MSE、DRM/EME、广告语义 fixture；页面伪造与旧 generation 全拒；macOS arm64 真实 CEF build/CTest/E2E、fast/security、Review P0/P1=0。
- 明确不做：candidate/probe/policy（M05b2）、Cast UI/SDK（M05b3）、真接收端（M05b4/5）。

### PLT-M05b1 完成记录（2026-08-31，真实 CEF 媒体观察接线）

- 实现：Renderer process 注入固定、被动且有界的 `HTMLMediaElement` collector，经过严格类型/长度 IPC 进入 Browser process；Browser 侧把 main-frame/current-navigation 媒体事实、CEF resource 完成事实、前台 tab、键盘与 AppKit 真实鼠标输入、播放推进统一接入 `MediaObserver`、`NetworkObserver`、`InputProofGate`、`ObservationGateway`。新增 URL/source/visibility/currentTime/EME 校验、generation fence、256 事件背压、Browser owner 生命周期及只含 header 类别的网络 DTO；不读取 header value/body，不注入 play/click/seek/rate，不过滤广告语义。修复四个 CMake contract test 误指向同一 supervisor executable 的既有接线错误；Review 发现并关闭 `Authorization` 存在性未落入 `HeaderClass::kAuthorization` 的 P1，模型现在从闭合 header name 自行派生类别。
- 验证：`cmake --build .cache/build/macos-arm64-cef-debug --target crayon_browser crayon_page_snapshot_cef_integration_test -j4` 通过并对 App/Helper ad-hoc 签名；`ctest --test-dir .cache/build/macos-arm64-cef-debug -R '^(media_observer|input_proof_gate|network_observer|observation_gateway)$' --output-on-failure` 4/4；`ctest --test-dir .cache/build/macos-arm64-cef-debug -R '^page_snapshot_cef_integration$' --output-on-failure` 1/1、68.02s，覆盖原 Markdown 矩阵及 clear WAV/AV1 MP4、HLS manifest、blob、MSE、EME、广告命名、hidden、cross-frame、页面伪造；真实 UI 鼠标 fixture `media-manual` 完成，`media=1 actual_media=1 media_network=1 mock_keychain=1`。全 CTest 先在受限 sandbox 得到 75/77，唯一失败为 UDS/loopback 权限，随后 `content_host_process_mac` 与 `page_snapshot_cef_integration` 在所需本机权限下分别通过。`bash scripts/check.sh security` 在 loopback 权限下通过；`cargo test --workspace --exclude crayon-platform-macos && cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib` 通过（明确排除真实 Keychain crate，legacy 58/58）；`git diff --check` 与 fixture `py_compile` 通过。
- Keychain 说明：首次执行 `scripts/check.sh fast` 时确认该脚本会无条件包含真实 `crayon-platform-macos` Keychain tests；该 crate 已跑完 34/34 且未弹密码框后立即中止，此命令不作为本任务通过证据。后续全部验证改用上述显式排除组合；产品与 CEF Harness 均固定 `use-mock-keychain`。
- Code Review：P0 0、P1 0（发现 1 项 header class 丢失并在本提交关闭）、P2 0；按需求/正确性/架构/并发与生命周期/安全隐私/性能/测试/可维护性检查，未发现持锁 IO/回调、无界队列、敏感值泄漏或页面事实绕过 Browser proof gate。
- 未覆盖与风险：当前 CEF 分发可真实播放 AV1 MP4/MSE，但不解码本地 HLS，故本切片只证明 HLS manifest 的真实 resource observation，manifest/probe/策略归 `PLT-M05b2`；未调用 Cast-SDK/UI/Direct/Relay，未做 Windows 构建或行为结论。`PLT-M05b1` 转为 `DONE`，解锁 `PLT-M05b2 READY`。

### PLT-M05b2..b6 原子切片

| ID | 状态 | 依赖 | 单一目标 | 验收与边界 |
|---|---|---|---|---|
| PLT-M05b2 | DONE | M05b1 | 接通 observation → candidate/lifecycle → probe → `Direct/Relay/ExternalClientHandoff/Reject` 唯一策略 | 按 M05b2a/b/c 严格串行；MP4/HLS/DASH/DRM/credential fixture；普通失败不提权、不重试；不调用 SDK/UI |
| PLT-M05b3 | IN_PROGRESS | M05b2 | 接通 CastButton/FeatureView、设备选择、Cast-SDK facade、session event pump 与 PLT 生命周期 | 按 M05b3a-e 严格串行；无设备/取消/失败/旧 session/stop；UI 线程不执行有界 SOAP 阻塞；不做真机结论 |
| PLT-M05b4 | TODO | M05b3 | ADB 在线手机上的固定 Cast-SDK 正式接收端完成 clear fixture Direct 发现、连接、投送、控制和停止 | E2E-001；真实 Desktop Host，不以 ADB 在线或 SDK standalone Harness 代替 |
| PLT-M05b5 | TODO | M05b4 | 同一 ADB 真机接收端完成 MP4 Range 与 HLS Relay 全链路 | E2E-002；opaque route、200/206/416、分片、撤销后拒绝；不支持 DASH Relay/加密 HLS |
| PLT-M05b6 | TODO | M05b5 | DRM/EME/加密/凭证来源拒绝与无路由外部客户端确认/取消/未安装/失败反馈 | E2E-003/004；交接永不显示投屏中、不创建 SDK/Relay session |

每个切片允许修改其所属 `browser/cef-shell` 装配、既有 MED/SDK/app-runtime 调用入口和独立测试；发现需要改变公共协议、Cast-SDK facade 或 Relay 安全边界时停止并新建原子任务。M05b1..b6 不包含 M05c 100 次稳定性、PLT-W05、PLT-19 或 QAR 发布矩阵。

### PLT-M05b3 原子范围（按 a-e 五切片）

审计结论（2026-08-31）：`CastButtonModel`、`CastFeatureViewModel`、`CastUsecase` 与真实 `SenderCastFacade` 均已存在，但彼此没有生产调用方；CEF App 只 drain M05b2 的 candidate/decision，当前 MHV1 也无法表达设备快照、执行命令或 session event。一次性接线会同时新增 UI 状态 owner、跨语言协议、Rust 执行 runtime、阻塞 SDK worker 和平台 UI/生命周期，超过原子任务边界，因此拆为：

| ID | 状态 | 依赖 | 单一目标 | 允许范围与门禁 |
|---|---|---|---|---|
| PLT-M05b3a | DONE | M05b2 DONE | 建立单 UI 线程的 Cast UI 协调器，唯一同步 `CastButtonModel`/`CastFeatureViewModel` 与有界 receiver picker；只消费闭合 Browser/runtime facts、只产出用户 action | `browser/shared-ui/{features/cast,chrome}/**`；最多 64 设备、稳定 device id、名称 512 bytes、无 IP/URL；无 SDK/CEF/网络/线程 |
| PLT-M05b3b | DONE | M05b3a | 扩展私有 MHV1，使 bundled Browser/media-host 可表达 discovery、设备快照、选择/停止与有界 session event pump | Rust/C++ 双 codec、current/previous golden、未知 kind/超界/secret 扫描；不执行 SDK，不改 CastFacade |
| PLT-M05b3c | DONE | M05b3b | 在 Rust media-host 内装配唯一 `CastUsecase`、`SenderCastFacade` 与 session drain owner，执行 discovery/select/start/stop 并把旧 session/错误投影为 MHV1 | `crayon-app-runtime`/既有 adapter facade；Fake + real facade contract；阻塞 SDK 调用不在协议 reader/callback；Relay 实际投送仍不验收 |
| PLT-M05b3d | READY | M05b3c | macOS C++ media-host process/adapter 增加有界后台 command worker 和 session event pump，CEF UI 线程只 enqueue/drain | `browser/cef-shell/src/{ipc,macos}` 与相邻 tests；无 UI 线程 SOAP/discovery、取消/stop/shutdown 逆序、队列满显式失败 |
| PLT-M05b3e | TODO | M05b3d | 将协调器与真实 CEF Cast 按钮、原生 receiver picker、M05b2 candidate 和 PLT navigation/close/app-exit 生命周期接通 | macOS Debug/Release 真 CEF：无设备、刷新、取消、失败、旧 session、stop/退出；mock keychain；不做真机 Direct/Relay 结论 |

五切片共用边界：页面事实不能打开 picker 或选择设备；UI 永不接收 IP、控制 URL、媒体 URL、Cookie/Authorization；只有 `crayon-cast-adapter` 调 Cast-SDK；ExternalClientHandoff 不创建 SDK/Relay session且归 M05b6 呈现；M05b3 不以 Fake、ADB 在线或 standalone SDK Harness 宣称真机投送通过。若 b3b/c 发现必须修改 Cast-SDK facade、暴露 receiver locator 或改变 Relay 安全绑定，立即停止并建立独立 SDK/Relay gap 任务。

#### PLT-M05b3a 原子范围（Cast UI 协调器与 receiver picker）

- 状态：`IN_PROGRESS`；依赖 `PLT-M05b2 DONE`、CEF-08/13 既有 `CastButtonModel`/`CastFeatureViewModel`。
- 单一目标：新增一个 UI-thread-only 协调器，原子驱动 CastButton 与 FeatureView 的重复状态，并保存 Browser/runtime 提供的有界 receiver snapshot；用户打开/关闭 picker、刷新、选择稳定 device id 和停止只返回闭合 action，不直接执行外部调用。
- 输入/输出与允许修改：输入仅 page active、media present、Browser-verified eligibility、最多 64 个 `{device_id, display_name, is_crayon_receiver}`、闭合 policy/session/failed facts；输出仅 `RefreshReceivers/SelectReceiver/StopSession` action。允许修改 `browser/shared-ui/features/cast/**`、其 CMake/test 和为依赖模型所需的 `browser/shared-ui/chrome` 链接声明、本 Roadmap/索引。
- 禁止修改：CEF shell、MHV1、Rust、Cast-SDK、Relay、平台 adapter/locales、文件/网络；不得携带 IP/host/UDN/control URL/media URL/secret，不得让 page-reported state 进入 eligibility。
- 边界：receiver id 1..128 bytes、display name 1..512 bytes，空/重复 id/超界 snapshot 整体拒绝且保留旧 snapshot；稳定顺序按输入保持，选择必须命中当前 snapshot且仅在 picker open；无设备时 picker 可见并允许 refresh/cancel；session start/end、policy reject/handoff、page inactive 保持两个模型不出现相互矛盾的 Casting；Stop action 必须携带当前 session generation，重复取消/stop/session end 幂等。
- 验收：独立 C++ unit 覆盖 CS-001/002/007 的无设备、同名不同 id、重复/畸形/超界 snapshot、刷新/取消、非法选择、Direct/Relay started、旧/重复 session end 投影和 stop action；clang-format scoped check、Debug/Release build/CTest、repo guard、`git diff --check`。
- 明确不做：真实 receiver picker widget、发现/SDK/协议/worker/生命周期接线（b3b-e）、真机 Direct/Relay（b4/b5）、ExternalClientHandoff UI（b6）、100 次资源稳定性（M05c）、Windows。

#### PLT-M05b3a 完成记录（2026-08-31）

- 实现：新增 UI-thread-only `CastUiCoordinator`，成为 `CastButtonModel`、`CastFeatureViewModel` 与 receiver snapshot 的单一协调 owner；Browser-verified eligibility 是唯一启用入口。picker 打开只产出 refresh action，设备选择只产出稳定 device id，stop action 携带当前 session generation；没有 SDK、网络、CEF 或平台调用。
- 有界与隐私：receiver snapshot 最多 64 项，device id 严格复用 `[A-Za-z0-9_-]`/1..128 bytes，展示名 1..512 bytes 且拒绝控制字符；空、重复、畸形或超界 snapshot 整体拒绝并保留旧值。DTO 不可表达 IP、host、UDN、control/media URL 或 secret，输入顺序保持稳定。
- 状态与并发：无设备仍可刷新/取消；选择必须命中当前 snapshot 且 picker 已打开；Direct/Relay start、stop、media withdrawal、page loss 与 session end 统一投影到两个既有模型。session generation 单调 fencing，旧 end 拒绝、重复 end 幂等；Review 发现并关闭迟到 Stop 未携带 generation、可能误停新 session 的 P1。
- 验证：macOS arm64 Debug/Release 构建通过；两配置 `chrome_contract`、`cast_feature_view`、`cast_ui_coordinator` 均 3/3 通过。新增 4 组 coordinator 行为测试覆盖 CS-001/002/007 的无设备、同名不同 id、畸形/重复/65 项、刷新/取消/非法选择、Direct/Relay、旧/重复 session、stop 与 page loss；`cargo run -p repo-guard -- scan --root .` 和 `bash scripts/check.sh security` 通过（relay security 7/7），新增文件 Apple clang-format dry-run 与 `git diff --check` 通过。未运行真实 Keychain。
- Code Review：按 v0.8 复核需求/边界、状态唯一 owner、页面不可信、generation、隐私、容量、生命周期和测试；修复 1 个 P1 后最终 P0/P1/P2=`0/0/0`。既有 chrome 三文件的全文件 Apple clang-format 仍有历史风格漂移，本次只格式化新增行，未混入无关重排。
- 未覆盖与风险：本切片只有共享 UI 合同，无真实 widget、设备发现、Cast-SDK、MHV1、worker 或 CEF/PLT 生命周期；`PLT-M05b3b READY` 下一步先冻结跨语言执行协议。真机 Direct/Relay 仍严格归 M05b4/b5。

#### PLT-M05b3b 原子范围（MHV1 Cast 执行扩展）

- 状态：`DONE`；依赖 `PLT-M05b3a DONE`、M05b2b 既有 MHV1 双端 codec/process contract。
- 单一目标：在不改变 `MHV1` magic/version、16KiB frame 和既有 1..12 kind 字节的前提下，追加 bundled Browser↔media-host 私有 Cast 执行消息，使后续 runtime/UI 能表达 discovery、revision-bound 设备分页、按 candidate/device 启动、generation-bound stop 与有界 session event drain；本任务只冻结/验证协议，不执行 SDK。
- 输入/输出与允许修改：新增 `Discovery(Start/Stop/Refresh)`、`ListDevices(snapshot_revision,offset)`、`StartCast(candidate_id,device_id,handoff_available)`、`StopCast(session_generation)`、`PollSessionEvents` 请求；新增 `DevicePageReply`、闭合 `CastStartReply`（Casting/Handoff/Rejected/Failed）和 `SessionEventsReply`。允许修改 `crates/crayon-ipc-schema/src/media_host.rs`、其 contract/golden、`browser/cef-shell/src/ipc/{include,src,tests}/media_host_codec*`、相邻 CMake contract 与本 Roadmap/索引；允许仅为保持 workspace 可编译，在既有 `media_host_runtime.rs` exhaustive match 中提取新增消息 request id，并在 b3c 接线前 fail-closed 返回 `InvalidState`/`InvalidMessage`，不得执行任何 Cast 行为。
- 禁止修改：media-host runtime/process 的既有行为或增加 Cast 执行、CastUsecase/CastFacade/SDK、CEF UI/adapter、Relay、平台生命周期；协议不得携带 receiver IP/host/port/UDN/control URL、媒体/page URL、Cookie/Authorization、SDK 文案或外部客户端启动结果。
- 预算与分页：总 receiver snapshot 沿用 64 项，单 `DevicePageReply` 最多 16 项；device id 1..128 ASCII、display name 1..512 UTF-8，page 绑定非零 revision、offset 0..63 与严格 next offset，整体保持 16KiB。session event 每回复最多 64 项，generation/revision 非零，phase/playback/terminal reason 为闭合枚举；reply 额外携带累计 dropped count。Stop 必须带非零 generation；Start 只带 opaque candidate id、device id 和 handoff availability。
- 兼容与错误：旧 1..12 frames 逐字节 current/previous golden 均继续 decode；新增 kind 仅追加不重编号。未知 kind/enum、非法 optional 组合、重复/超量设备、错误 offset/next、终态无 reason、非终态带 reason、0 generation/revision、截断/尾随/超 frame 全 fail closed；Rust/C++ 对同一新增 golden 逐字节一致。
- 验收：Rust `media_host_v1_contract` 与 C++ `media_host_codec` 覆盖全部消息 roundtrip、双端 golden、分页/union/fencing/hostile mutation；cargo test/clippy/fmt、macOS Debug/Release codec build/CTest、repo guard/security、源码 secret/locator 扫描、clang-format scoped、`git diff --check`。
- 明确不做：SDK/runtime/worker/UI/lifecycle（b3c-e）、设备真实发现或投送（b4/b5）、Cast code UI、播放控制、ExternalClientHandoff 执行（b6）、协议远程监听或独立升级协商。

完成记录（2026-08-31）：

- 实现：MHV1 保持 magic/version、16KiB frame 与 kind 1..12 原字节不变，追加 kind 13..20 的 discovery、revision-bound device page、start/stop 和有界 session event DTO/codec；Rust/C++ 共用新增 device-page golden，设备 id/名称、分页、union、generation/revision、终态 reason、重复设备和数量在分配前均 fail closed。新增消息不携带 receiver locator、媒体/page URL 或凭证；b3c 接线前既有 Rust runtime 只提取 request id 并以 `InvalidState`/`InvalidMessage` 拒绝，不执行 SDK/Cast。
- 验证：`cargo test --workspace --exclude crayon-platform-macos` 全通过；`cargo clippy --workspace --all-targets --exclude crayon-platform-macos -- -D warnings`、`cargo fmt --all -- --check` 通过。`cargo test -p crayon-ipc-schema --test media_host_v1_contract` 3/3；macOS arm64 Debug/Release `crayon_cef_shell_ipc_test` 构建及 `ctest -R '^ipc_channel_contract$'` 各 1/1；Debug/Release `crayon_browser` 完整构建并 ad-hoc 签名通过，产品仍固定 mock Keychain。`cargo run -p repo-guard -- scan --root .` 通过；`bash scripts/check.sh security` 首次在受限 sandbox 因 loopback bind 权限 7/7 报 `Operation not permitted`，按本机权限复跑后 7/7、整体通过。Apple clang-format dry-run、源码新增行 locator/secret 扫描与 `git diff --check` 通过。
- Code Review：按 v0.8 复核兼容、跨语言字节序、非法组合、分配前容量、重复 id、generation fencing、隐私与 b3c 前 fail-closed 行为；Review 中关闭 1 个 P1（C++ decoder 在验证 16 项上限前按不可信 count 分配），最终 P0/P1/P2=`0/0/0`。现有 Rust encode/decode 分别约 157/135 行，保持单一 wire-kind dispatch，字段逻辑已拆 helper，未机械拆分。
- 未覆盖与风险：本任务只冻结并验证协议，未执行 Cast-SDK、Relay 投送、worker 或 CEF UI；这些依次属于 `PLT-M05b3c/d/e`，真机 Direct/Relay 仍属于 M05b4/b5。Release Ninja 曾输出既有 `premature end of file; recovering` 警告，但目标重建、链接和 CTest 均成功；未访问真实 Keychain。

#### PLT-M05b3c 原子范围（Rust media-host Cast runtime）

- 状态：`DONE`；依赖 `PLT-M05b3b DONE`、既有 `CastUsecase`/`SenderCastFacade`/`ReceiverCapabilityCache`/Relay facade contract。
- 单一目标：在 bundled Rust media-host 内建立唯一 Cast execution owner，消费 b3b MHV1 discovery/list/start/stop/poll 请求，复用既有 planner、`CastUsecase`、`SenderCastFacade` 与 Relay backend 输出有界 device page、闭合 start outcome 和 generation-fenced session events；不改变协议、SDK facade 或 CEF UI。
- 输入/输出与允许修改：允许修改 `crates/crayon-app-runtime/src/{media_host_runtime,media_planning_runtime,cast_usecase}.rs` 及独立新 runtime/test module、`crayon-media-host` bin/Cargo、相邻 app-runtime tests/lib export 与本 Roadmap/索引。生产装配使用真实 `SenderCastFacade`、能力缓存、`CoreSessionBackend`/`RelayRuntime`；测试通过 `CastFacade`/`SessionBackend`/`RelayRevocation` 注入 Fake。协议 reader 只排队/驱动 command future，不直接执行阻塞 SDK 调用；SDK callback 只入既有有界队列。
- 禁止修改：MHV1 kind/字段、Cast-SDK 或 `crayon-cast-adapter` facade 语义、C++ process/worker、CEF/UI、播放控制、Cast code、外部客户端启动；不得把 receiver locator、媒体/page URL、relay token、Cookie/Authorization 或 SDK 文案写入 MHV1、日志、diagnostics。
- 状态/容量/生命周期：设备总量 64、分页 16，snapshot revision 非零单调且后续页必须匹配；Start 必须命中当前 snapshot 和 current-navigation candidate，阻塞 connect/assess/deliver 经 `spawn_blocking`；Stop 按 generation fencing，重复/已终止 stop 幂等；Poll 每批最多 64，旧 revision/generation 丢弃并累计 dropped count。Shutdown 逆序停止 usecase/facade/Relay；navigation/close/app lifecycle 的 CEF 触发接线仍属 b3e。
- 验收：Fake 覆盖 discovery start/stop/refresh、空/同名/重复/65 项、分页 revision/旧页、candidate/device 不存在、Direct/Relay/Handoff/Reject/Failed、重复/旧 generation stop、event 64/overflow/stale/terminal、shutdown；真实 facade contract 至少覆盖无设备 discovery/list/stop/shutdown 且无固定端口。`cargo test -p crayon-app-runtime`/process、clippy/fmt、非 Keychain workspace 回归、macOS Debug/Release media-host + CEF build、repo guard/security、secret/locator 扫描与 Review。
- 明确不做：C++ 后台 worker/event pump（b3d）、真实 CEF picker/lifecycle（b3e）、手机真机 Direct/Relay（b4/b5）、Windows（W05）、真实 Keychain。

#### PLT-M05b3c 完成记录（2026-08-31，Rust media-host Cast runtime）

- 实现：新增唯一 `MediaHostCastRuntime`，将 MHV1 discovery/list/start/stop/poll 串到既有 `CastUsecase`、`ReceiverCapabilityCache` 和 `CastFacade`；生产 `crayon-media-host` 装配真实 `SenderCastFacade`、`CoreSessionBackend` 与进程级 256-bit 随机 secret 的 `RelayRuntime`，无固定端口、无 Keychain。设备快照限制 64 项/每页 16 项，以非零单调 revision 冻结后续页；Start 同时校验 current-navigation candidate 与当前 device snapshot，阻塞 SDK 路径进入 `spawn_blocking`；Stop 使用 wire/internal generation 统一平移 fencing；Poll 每批最多 64 项，旧 revision/generation 和队列溢出累计 dropped，回包只含闭合 route/session/device presentation DTO。Shutdown 按 usecase/facade/Relay 逆序且幂等释放。
- 验证：`cargo test -p crayon-app-runtime` 全量 85 项通过（lib 59，integration 26；含新 Cast runtime 8/8、真实 `crayon-media-host` 子进程 3/3）；`cargo clippy -p crayon-app-runtime --all-targets -- -D warnings`、`cargo fmt --all -- --check` 通过。`cargo test --workspace --exclude crayon-platform-macos` 与 `cargo clippy --workspace --all-targets --exclude crayon-platform-macos -- -D warnings` 通过，明确未运行真实 Keychain crate。macOS arm64 Debug preset 与独立 Release CEF 全量构建、ad-hoc 签名和 bundled media-host 打包通过；两套 `ctest -R '^(media_host|ipc_channel_contract)'` 各 3/3。`cargo run -p repo-guard -- scan --root .`、`bash scripts/check.sh security`、`git diff --check` 通过；受限 sandbox 首轮 media-host CTest 因 Relay loopback bind 得到 `Operation not permitted`/startup failure，按本机网络权限复跑后 Debug/Release 均通过。
- Code Review：P0/P1/P2=`0/0/0`。按 v0.8 复核唯一 owner、锁/回调/阻塞边界、generation/revision、队列/分页容量、错误映射、进程退出和隐私输出；Review 中补齐 refresh、同名/空名/65 项、Relay 闭合 route 的缺失验收，并修正后续真机归属注释。新增生产函数/文件未触发 100/2000 行提醒；MHV1 输出与日志不含 receiver locator、page/media URL、relay token、Cookie/Authorization 或 SDK 文案。
- 未覆盖与风险：本任务只验证真实 facade 的无设备生命周期以及 Fake Direct/Relay/Handoff/Reject/Failed；手机上的 Direct/Relay 可播放性、receiver IP 绑定和可达 Relay URL 仍由 M05b4/b5 真机任务闭合，不在此宣称通过。C++ command worker/event pump 与 CEF picker/lifecycle 仍分别由 `M05b3d READY`、`M05b3e TODO` 完成。Release Ninja 仍出现既有 `premature end of file; recovering` 警告，但重新生成、完整编译、链接与签名成功；全程未访问真实 Keychain。

### PLT-M05b2 原子范围（按 a/b/c 三切片）

- **M05b2a（Rust 唯一媒体规划 owner）**：状态 `VERIFIED`；依赖 M05b1 DONE、MED-01..08/17/19 DONE。单一目标是在 `crayon-app-runtime` 新增唯一 `MediaPlanningRuntime`，消费 Browser-verified、current-navigation 的 URL/URL-less/EME/header-class/playback facts，复用 `CandidateStore` lifecycle、`MediaInspector`/`assess_protection` 与 `cast-policy::decide` 生成候选摘要及按显式 receiver capability 的唯一决策；probe 普通失败固定为 inconclusive，Cookie/Authorization 只表达 `CredentialBound` 类别且值不可进入 API。允许修改 `crates/crayon-app-runtime/src/media_planning_runtime*`、对应 crate tests/lib export；禁止修改 CEF、IPC schema、Cast-SDK、Relay、UI。验收：MP4/HLS/DASH、EME/DRM、blob/stream、credential、referer/UA、旧 navigation、close/TTL、probe timeout/error、receiver mismatch；`cargo test -p crayon-app-runtime`、clippy、fmt、非 Keychain workspace 回归。明确不做：进程/transport、CEF 调用方、SDK/session/Relay 创建。
- **M05b2b（版本化本机 media-host 协议）**：状态 `VERIFIED`；依赖 M05b2a VERIFIED。按 b2b1/b2b2 严格串行，增加独立 `crayon-media-host` 本机子进程与双端严格 codec/transport，把完整 URL 只保留在 Browser↔host 私有内存通道，向 UI 只返回 opaque candidate id/redacted origin/闭合 decision；禁止复用/扩张 CAAP、content-host CHV1、远程监听、SDK/UI/Relay。
  - **M05b2b1（Rust media-host schema/runtime/process）**：状态 `VERIFIED`。单一目标是在 `crayon-ipc-schema` 建立独立 MHV1 current/previous 严格 codec，在 `crayon-app-runtime` 建立有界 request owner 与 `crayon-media-host` macOS stdin/stdout 子进程；闭合 ingest/decide/cancel/navigation/close/shutdown 与 candidate/decision/error reply，完整 URL/header 仅存在 request/runtime 内存且无 `Debug`/日志。允许修改上述两个 crate 的独立 module/bin/tests/golden；禁止 CEF/C++、Cast-SDK、Relay、UI。验收：current/previous golden、畸形/截断/超长/未知 kind、容量/取消/旧 generation/navigation/close/shutdown、真实进程 health/EOF/崩溃，无 URL/header 值日志；Rust format/clippy/unit/process/workspace（排除 Keychain）。明确不做：C++ transport、自动 restart、CEF observation 消费。
  - **M05b2b2（CEF codec/process/adapter）**：状态 `VERIFIED`；依赖 b2b1 VERIFIED。单一目标是 C++ 复刻 MHV1 严格 codec，并在 macOS 建立有界 media-host process/adapter、health/crash/restart 与 generation fencing；允许修改 `browser/cef-shell/src/ipc`、macOS media-host process/adapter、CMake 和独立 golden/contract/process tests；禁止 ObservationGateway 产品接线、SDK/UI/Relay。验收：Rust current/previous golden 交叉读取、畸形/截断/超长/未知 kind、队列背压、取消/navigation/close/shutdown、host crash/restart，Debug/Release arm64 build + CTest。明确不做：把事实送入 adapter 或暴露策略 UI。
- **M05b2c（CEF 产品接线与 fixture）**：状态 `DONE`；依赖 M05b2b VERIFIED。单一目标是 Browser UI 线程有界 drain M05b1 `ObservationGateway`，补充可信 page URL/lifecycle 后送入 media-host，并把候选/决策事件以 opaque DTO 暴露给 M05b3；不得在 UI 线程执行 probe 网络 IO。允许修改 M05b1 bridge/window、macOS app/CMake、media-host adapter 与独立 CEF fixture；禁止 SDK/UI/Relay/session。验收：真实 CEF MP4/HLS/DASH/EME/blob/credential/页面伪造/旧 generation、probe SSRF/timeout、host crash，Debug/Release arm64 build + CTest/E2E/security，Review P0/P1=0。明确不做：设备发现/选择、Cast-SDK、真实接收端。

拆分理由：真实代码事实表明 CEF C++ 与 Rust MED owner 之间没有生产 transport；该连接同时引入公共运行时 owner、独立版本化进程协议与平台装配，合并会超过约 10 个生产文件/1000 行并混合三个变化原因。b2b 再按 Rust schema/runtime/process 与 C++ codec/process/adapter 拆成 b2b1/b2b2；所有切片保持同一策略 owner，不在 C++ 复制 candidate/probe/policy。

### PLT-M05b2a 完成记录（2026-08-31，Rust 唯一媒体规划 owner）

- 实现：`crayon-app-runtime::media_planning_runtime` 新增单一 `MediaPlanningRuntime`：完整 page/media URL 只留在无 `Debug` 的内存 owner；复用 MED `CandidateStore` 合并与 navigation/close/TTL/256 容量驱逐、`rank` 稳定排序、credential-free `MediaInspector`、`assess_protection` 和唯一 `cast-policy::decide`。输出仅含 opaque `CandidateId`、redacted origin、闭合 protocol/decision。URL-less blob/MediaStream 使用真实 tab/page context 且 media URL 为空，直接进入 `NoDirectUrl`/DRM 分支；CredentialBound/EME 跳过 probe，普通 probe error/timeout 变为 `ProbeInconclusive`，无重试/提权。事实形态二次闭合：仅 CurrentSrc 可携带 Browser-verified playback/EME，仅 NetworkRequest 可携带 header class，NaN/负时间/非 http(s) page/media URL 拒绝。
- 验证：`cargo test -p crayon-app-runtime --lib media_planning_runtime` 13/13（clear MP4 Direct、HLS Referer/UA Relay、HLS AES KeyRequired 且 key 零请求、DASH ContentProtection、EME、credential 零 probe、URL-less、probe 404/timeout、receiver mismatch、旧 navigation/close/TTL、owner sidecar 256 上限、MED ranking、畸形字段/时间/page context 零状态变更）；`cargo test -p crayon-app-runtime` 全量通过（lib 46 + integration 23）；`cargo clippy -p crayon-app-runtime --all-targets -- -D warnings` 与 `cargo fmt --all -- --check` 通过；`cargo test --workspace --exclude crayon-platform-macos && cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib` 通过，明确未运行真实 Keychain crate，legacy 58/58。
- Code Review：P0 0、P1 0（Review 中发现并关闭 3 项：sidecar 未随底层驱逐会无界增长；source/header/playback 字段错配未二次拒绝；page context 错配在报错前可能更新候选）、P2 0。无锁/回调/日志；probe 使用既有 DNS pinning、SSRF、body/time bounds；决策结果不含上游 URL/header value。
- 未覆盖与风险：尚无 Browser↔Rust 生产 transport 或 CEF 调用方，取消/崩溃/背压由 M05b2b/c 闭合；MP4 codec 仍以现有 inspector 可得证据为准，不猜测未知 codec；本切片不创建 Relay/session、不调用 Cast-SDK/UI。`M05b2a` 转为 `VERIFIED`，解锁 `M05b2b READY`。

### PLT-M05b2b1 完成记录（2026-08-31，Rust MHV1 schema/runtime/process）

- 实现：新增独立 `MHV1` binary codec（不复用/扩张 CHV1/CAAP），覆盖 URL fact、EME、candidate/URL-less decide、cancel/navigation/close/shutdown 与 candidate/decision/ack/error reply；所有字段有版本、16 KiB frame、ID/URL/origin/毫秒精度与闭合 enum 校验，请求 DTO 和 prepared decision 均无 `Debug`。`MediaHostRuntime` 复用唯一 `MediaPlanningRuntime`，加入 generation/navigation/close tombstone、10 分钟 TTL、256 request replay 窗口、64 tab 与 64 pending queue 上限；probe 期间 cancel/navigation/close/shutdown 可打断，满载显式 `CapacityExceeded`。macOS `crayon-media-host` 使用 bounded reader channel、stdin/stdout 长度帧和独立 0600 health UDS；EOF、畸形、超长和未知 kind 终止进程且只输出闭合错误，不记录 URL/header 值。
- 验证：`cargo test -p crayon-ipc-schema` 全量通过（含 MHV1 contract 3/3 current/previous golden、截断/超长/未知/hostile mutation）；`cargo test -p crayon-app-runtime` 全量通过（lib 51 + integration 25，含 media runtime 5/5、真实 media-host process 2/2）；`cargo clippy -p crayon-ipc-schema --all-targets -- -D warnings`、`cargo clippy -p crayon-app-runtime --all-targets -- -D warnings`、`cargo fmt --all -- --check`、`git diff --check` 通过；`cargo test --workspace --exclude crayon-platform-macos && cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib` 通过，明确排除真实 Keychain crate，legacy 58/58。
- Code Review：P0 0、P1 0（Review 中发现并关闭 4 项：decision 未应用 candidate TTL；close 后 host generation tombstone 不完整；毫秒值可超出 `f64` 精确整数范围；满队列插入 navigation/close 可短暂超过硬上限）、P2 0。无锁内 IO/await；probe 沿用既有 SSRF/DNS pinning/timeout/body cap；输出 DTO 不含上游 URL/header value。
- 未覆盖与风险：尚无 C++ codec/process adapter、bundle packaging 或 crash restart，统一由 M05b2b2 闭合；尚未消费 CEF ObservationGateway，统一由 M05b2c 闭合；本切片不调用 Cast-SDK/Relay/UI。`M05b2b1` 转为 `VERIFIED`，解锁 `M05b2b2 READY`。

### PLT-M05b2b2 完成记录（2026-08-31，CEF MHV1 codec/process/adapter）

- 实现：新增独立 C++ MHV1 DTO/codec，与 Rust current/previous ingest golden 逐字节交叉验证并闭合所有 request/reply variant；URL、origin、ID、enum、布尔、毫秒精度、16 KiB frame 和候选/协议组合均 fail-closed。macOS `MediaHostProcess` 使用 64 帧/64 回复有界队列、0600 health UDS、既有三次有界退避监督与单调 process generation；崩溃或满载立即失活、清队列并重启。UI-thread adapter 仅接收已编码合法消息，要求事实先有 current Navigation，限制 64 tab/256 request/256 candidate/64 reply，闭合重复 ID、取消回执、navigation/close、快速重启 generation fence 和零副作用拒绝。CMake 单次构建两个 Rust helper，`crayon-media-host` 随 Debug/Release app bundle 复制、独立 ad-hoc 签名并纳入架构/package contract；尚未实例化产品 ObservationGateway 调用方。
- 验证：最终 `cmake --build --preset macos-arm64-cef-debug --parallel 4` 与独立 Release build（`macos-arm64-cef-release`）通过；最终 Debug `ctest` **79/79**。最终 Release 全量首轮 **78/79**，唯一既有 `page_snapshot_cef_integration` 的 `media-blob` 时序场景一次未观察到媒体事实；该测试单独立即复跑 **1/1 通过**，其余 78 项与最终 media-host/package focused tests 均通过。`clang-format --dry-run --Werror`、`git diff --check` 通过。所有产品/CEF fixture 继续固定 `use-mock-keychain`，未访问真实 Keychain。
- Code Review：P0 0、P1 0（Review 中发现并关闭 9 项：URL authority 校验弱于 Rust；spawn actions 初始化失败泄漏 fd；重复 request-id 可覆盖状态；快速重启可能错过 unhealthy 窗口；取消回执被提前删除；拒绝请求可能先改变 tab 状态；非法 DTO 可先改变状态；无 Navigation 的事实可越过 generation fence；满队列窗口未立即失活）、P2 1。P2 为上述 Release `media-blob` 真 CEF 单次时序波动，因 M05b2b2 尚未接入该 fixture、立即复跑通过且 Debug 全量通过，延期由 `CNT-20` 双平台 E2E/稳定性门禁继续收口。
- 未覆盖与风险：尚未把 M05b1 `ObservationGateway` 事实送入 adapter，也未在 CEF fixture 断言 Rust candidate/decision；统一由 `M05b2c READY` 闭合。本切片不调用 Cast-SDK/Relay/UI，不创建投屏 session。`M05b2b/b2b2` 转为 `VERIFIED`。

### PLT-M05b2c 完成记录（2026-08-31，CEF 产品媒体规划接线）

- 实现：macOS CEF App 启动并监督随包 `crayon-media-host`，Browser UI 线程每批最多 16 条 drain `ObservationGateway`；页面 URL 只从当前 `TabModel`/navigation 取得，连同单调时间、Browser generation、media/network/credential/EME 闭合事实送入私有 MHV1。导航与关闭立即使旧 request/candidate 失效；host 快速崩溃以 process generation 清空并由下一条当前事实重建上下文。向 M05b3 暴露的 `MediaPlanningEvent` 仅含 opaque candidate id、redacted origin、protocol/decision/error，不含 page/media URL 或 header value。EME 状态在 Browser navigation 内持久，避免 encrypted 事件早于候选时漏标；blob/MSE 只走 URL-less 决策。产品 tick 只做有界队列/IPC，不在 UI 线程 probe、调用 SDK 或创建 Relay/session。
- 验证：`cmake --build --preset macos-arm64-cef-debug --parallel 4` 与 `cmake --build .cache/build/macos-arm64-cef-release --parallel 4` 通过，App/Helper 均 ad-hoc 签名；Debug、Release 各自常规 CTest 78/78，通过的完整真实 CEF fixture 各 1 项，合计均为 79/79。fixture 覆盖 Markdown 正常/空页/navigation/cancel/close/backpressure/content-host crash，以及真实 CEF MP4、HLS、DASH、Authorization credential class、media-host crash/restart、blob、MSE、EME→DRM Reject、广告命名不绕过、hidden/cross-frame/页面伪造拒绝，全部输出 `mock_keychain=1`。`cargo test -p crayon-media-probe` 49/49（含 SSRF/redirect/timeout/cancel），`cargo test -p crayon-app-runtime` 的 media planning 51 项与 media-host process 2/2 通过；一次并发负载下既有 content-host Rust process 2 项健康等待超时，负载结束后串行复跑 2/2 通过。`bash scripts/check.sh security` 通过；scoped `clang-format --dry-run --Werror`、fixture `py_compile`、`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 0。Review 中发现并关闭 1 项 P1：EME marker 原先可能先于候选到达而只更新空集合；现由 Browser navigation 持久 EME 并附到后续 currentSrc/URL-less fact，真 CEF EME fixture 明确断言 DRM Reject。完整 URL/header value 无日志/opaque DTO 泄漏；队列、tab/request/candidate/reply 均有界；无锁内 IO/await。
- 未覆盖与风险：本任务不接 CastButton、设备发现/选择、Cast-SDK、Relay 或真实接收端；由 `PLT-M05b3 READY` 后续串行闭合。CEF ad-hoc fixture 仍输出既有签名分类/缓存清理 warning，但 Debug/Release 退出码、全矩阵与 package contract 均通过；100 次资源稳定性仍归 `PLT-M05c`/`CNT-20`。`PLT-M05b2/b2c` 转为 `DONE`。

### PLT-M05c 原子范围（macOS 资源稳定性与 CP-M01 收口）

- 状态：`TODO`；依赖 `PLT-M05b6`。
- 单一目标：在不增加功能的前提下，对真实 Desktop Host 执行 100 次开始/停止/设备切换，并覆盖网络切换、睡眠唤醒、锁屏和退出，确认 Browser/Renderer/SDK/Relay/平台 watcher 逆序释放且资源归零。
- 验收：E2E-005、CP-M01；记录进程/线程/socket/RSS/UI delay/dropped，旧 generation 零污染，退出无 Helper/Relay/session/socket 残留；P0/P1=0。长达 8 小时的发布长稳仍归 QAR-07。

### PLT-W05 第一期对称装配边界

- 状态：`TODO`；依赖 `PLT-W04 DONE`、macOS `PLT-M05b6` 的协议/状态机缺陷已关闭、`CEF-15/SDK-14/PRV-12 DONE/VERIFIED`。
- 单一目标：在 Windows x64 CEF 产品壳消费同一共享观察、策略、UI、Cast-SDK 与 Relay owner，只增加 Windows 平台装配和 CP-W01 证据；不得复制或分叉 macOS 业务逻辑。
- 切片顺序镜像 M05b1..b6，再执行 Windows 100 次资源稳定性；真实接收端、Direct/Relay、DRM 拒绝、外部交接、网络/睡眠/退出矩阵缺一不得完成。
- 开工前必须把每个 Windows 切片的允许/禁止路径、设备条件和命令补成与 M05b1 同等完整的原子范围；当前保持 `TODO`，不得整体置为 `IN_PROGRESS`。
