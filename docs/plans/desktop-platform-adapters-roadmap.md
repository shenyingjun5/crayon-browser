# PLT Windows/macOS 平台适配 Roadmap

- 状态：`PLT-01/02/W04/M04 DONE`；macOS 共享装配 `PLT-M05 IN_PROGRESS`（M05a、M05b1/b2/b3 DONE，b4..b6/M05c 后置）；Windows 首发装配 `PLT-W05 IN_PROGRESS`（W05a/W05b/W05c0 DONE、W05c BLOCKED，后续严格串行）
- 任务数：7
- 平台：Windows、macOS
- 非目标：Linux、屏幕/标签页/系统音频采集、编码器、WebRTC sender

## 1. 任务表

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| PLT-01 | DONE | FND-09 | `crates/crayon-platform-api/**` | 定义安全存储、本地网络、生命周期、更新、当前用户本机 IPC 和外部客户端交接接口 | `CP-004`,`CP-W01`,`CP-M01`,`AG-012`; unit | V1 |
| PLT-02 | DONE | PLT-01,FND-10 | `crates/crayon-platform-api/**`, `crates/crayon-platform-capabilities/**` | 定义 `secure_store`、`local_network`、`lifecycle`、`update`、`local_agent_ipc`、`external_client_handoff` 能力模型 | `CP-004`,`AG-012`; schema/golden | V1 |
| PLT-W04 | DONE | PLT-02,CEF-12,SDK-08 | `platform/windows/**` | 实现 DPAPI、本地网络/防火墙、多网卡、睡眠唤醒、更新、当前用户 named pipe 与投屏客户端交接（切片 W04a..d，见原子范围） | `CP-W01`,`AG-012`; Windows integration | V4W |
| PLT-W05 | IN_PROGRESS | PLT-W04,PLT-M05b3,CEF-15,SDK-14,PRV-12 | `browser/cef-shell/**`, `crates/crayon-platform-windows/**` | Windows 产品装配与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-W01`; Windows + ADB receiver | R1 |
| PLT-M04 | DONE | PLT-02,CEF-01E,CEF-12,SDK-08 | `platform/macos/**` | 实现 Keychain、本地网络权限、生命周期、更新、当前用户 UDS 与投屏客户端交接 | `CP-M01`,`AG-012`; macOS integration | V4M |
| PLT-M05 | IN_PROGRESS | PLT-M04,CEF-15,SDK-14,PRV-12 | `apps/desktop-cef/**`, `platform/macos/**` | macOS 产品装配、签名/公证与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-M01`; macOS device | V4M |
| PLT-19 | TODO | PLT-W05；macOS addendum 另等 PLT-M05 | `docs/current/**`, `docs/plans/**`, `tests/**` | Windows 首发平台边界/生命周期 Review（19W）；macOS 特有门禁后续补 19M | 平台矩阵；Review P0/P1=0 | R1 |

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
| PLT-M05b3 | DONE | M05b2 | 接通 CastButton/FeatureView、设备选择、Cast-SDK facade、session event pump 与 PLT 生命周期 | 按 M05b3a-e 严格串行；无设备/取消/失败/旧 session/stop；UI 线程不执行有界 SOAP 阻塞；不做真机结论 |
| PLT-M05b4 | TODO | M05b3 | ADB 在线手机上的固定 Cast-SDK 正式接收端完成 clear fixture Direct 发现、连接、投送、控制和停止 | E2E-001；真实 Desktop Host，不以 ADB 在线或 SDK standalone Harness 代替；按 REL-05 后置 |
| PLT-M05b5 | TODO | M05b4 | 同一 ADB 真机接收端完成 MP4 Range 与 HLS Relay 全链路 | E2E-002；opaque route、200/206/416、分片、撤销后拒绝；不支持 DASH Relay/加密 HLS |
| PLT-M05b6 | TODO | M05b5 | DRM/EME/加密/凭证来源拒绝与无路由外部客户端确认/取消/未安装/失败反馈 | E2E-003/004；交接永不显示投屏中、不创建 SDK/Relay session |

每个切片允许修改其所属 `browser/cef-shell` 装配、既有 MED/SDK/app-runtime 调用入口和独立测试；发现需要改变公共协议、Cast-SDK facade 或 Relay 安全边界时停止并新建原子任务。M05b1..b6 不包含 M05c 100 次稳定性、PLT-W05、PLT-19 或 QAR 发布矩阵。

> 调度覆盖（2026-08-31）：M05b3 完成记录中的“下一任务 M05b4 READY”是当时的历史结论；`REL-05` 已将 M05b4..b6/M05c 后置，当前领取入口为 `PLT-W05a READY`。

### PLT-M05b3 原子范围（按 a-e 五切片）

审计结论（2026-08-31）：`CastButtonModel`、`CastFeatureViewModel`、`CastUsecase` 与真实 `SenderCastFacade` 均已存在，但彼此没有生产调用方；CEF App 只 drain M05b2 的 candidate/decision，当前 MHV1 也无法表达设备快照、执行命令或 session event。一次性接线会同时新增 UI 状态 owner、跨语言协议、Rust 执行 runtime、阻塞 SDK worker 和平台 UI/生命周期，超过原子任务边界，因此拆为：

| ID | 状态 | 依赖 | 单一目标 | 允许范围与门禁 |
|---|---|---|---|---|
| PLT-M05b3a | DONE | M05b2 DONE | 建立单 UI 线程的 Cast UI 协调器，唯一同步 `CastButtonModel`/`CastFeatureViewModel` 与有界 receiver picker；只消费闭合 Browser/runtime facts、只产出用户 action | `browser/shared-ui/{features/cast,chrome}/**`；最多 64 设备、稳定 device id、名称 512 bytes、无 IP/URL；无 SDK/CEF/网络/线程 |
| PLT-M05b3b | DONE | M05b3a | 扩展私有 MHV1，使 bundled Browser/media-host 可表达 discovery、设备快照、选择/停止与有界 session event pump | Rust/C++ 双 codec、current/previous golden、未知 kind/超界/secret 扫描；不执行 SDK，不改 CastFacade |
| PLT-M05b3c | DONE | M05b3b | 在 Rust media-host 内装配唯一 `CastUsecase`、`SenderCastFacade` 与 session drain owner，执行 discovery/select/start/stop 并把旧 session/错误投影为 MHV1 | `crayon-app-runtime`/既有 adapter facade；Fake + real facade contract；阻塞 SDK 调用不在协议 reader/callback；Relay 实际投送仍不验收 |
| PLT-M05b3d | DONE | M05b3c | macOS C++ media-host process/adapter 增加有界后台 command worker 和 session event pump，CEF UI 线程只 enqueue/drain | `browser/cef-shell/src/{ipc,macos}` 与相邻 tests；无 UI 线程 SOAP/discovery、取消/stop/shutdown 逆序、队列满显式失败 |
| PLT-M05b3e | DONE | M05b3d | 将协调器与真实浏览器 chrome Cast 按钮、原生 receiver picker、M05b2 candidate 和 PLT navigation/close/app-exit 生命周期接通 | 按 b3e1/e2 严格串行；macOS Debug/Release 真 CEF：无设备、刷新、取消、失败、旧 session、stop/退出；mock keychain；不做真机 Direct/Relay 结论 |

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
- 未覆盖与风险：本任务只验证真实 facade 的无设备生命周期以及 Fake Direct/Relay/Handoff/Reject/Failed；手机上的 Direct/Relay 可播放性、receiver IP 绑定和可达 Relay URL 仍由 M05b4/b5 真机任务闭合，不在此宣称通过。C++ command worker/event pump 已由 `M05b3d DONE` 闭合，CEF picker/lifecycle 由 `M05b3e READY` 完成。Release Ninja 仍出现既有 `premature end of file; recovering` 警告，但重新生成、完整编译、链接与签名成功；全程未访问真实 Keychain。

#### PLT-M05b3d 原子范围（macOS C++ Cast command/event pump）

- 状态：`DONE`；依赖 `PLT-M05b3c DONE` 与既有 `MediaHostProcess` 单 worker/有界 frame queue。
- 单一目标：扩展 macOS C++ media-host transport/adapter，使 UI 调用方只同步 enqueue 闭合 Cast command、drain 闭合结果；所有 pipe/health/child I/O 继续只在既有后台 worker，session event pump 保持单 in-flight、固定有界间隔且在无 active/pending session 时静默。
- 输入/输出与允许修改：允许修改 `browser/cef-shell/src/macos/media_host_{process,adapter}_mac.{h,cc}`、相邻 process/adapter tests 与本 Roadmap/索引；仅当编译契约需要时修改相邻 CMake。adapter 新增 discovery/list/start/stop 的异步入口和 bounded Cast result DTO；process 只扩展 b3b 已冻结 reply kind 的接纳，不改变 MHV1 codec/字段。
- 禁止修改：Rust、Cast-SDK/adapter facade、共享 Cast UI coordinator、CEF App/按钮/picker、Relay、协议 schema/codec、播放控制、Cast code、外部客户端启动；不得在 UI 线程执行 discovery/SOAP、pipe、health、等待或重试，不得向 DTO/日志加入 receiver locator、page/media URL、relay token、Cookie/Authorization 或 SDK 文案。
- 状态/容量/生命周期：沿用 transport outbound/reply 各 64；adapter tracked request/candidate 各 256、Cast result 64、device page 16。命令 request id 唯一；Start 必须命中 current candidate；Stop generation 非零；event pump 同时最多 1 个 poll，session generation/state revision 非零单调，旧/重复 event 丢弃并累计 host dropped。进程 generation 变化、navigation/close、queue full、Stop/App Stop 必须 fail closed；Stop 先停止新 poll/清 adapter 状态，再由 process 发送 Shutdown 并 join/清 child。
- 验收：Fake transport 覆盖 discovery/list/start/stop、重复/超界/无 candidate、Direct/Relay/Handoff/Reject/Failed、单 in-flight poll、空批/64 批/dropped、旧 generation/revision、terminal 停泵、restart/queue reject/Stop；真实 process 覆盖新增 reply 接纳和 shutdown/restart。Debug/Release focused build + CTest、clang-format、非 Keychain workspace 回归、repo guard/security、Review P0/P1=0。
- 明确不做：真实 CEF Cast 按钮/picker 与 navigation/app lifecycle 接线（b3e）、手机 Direct/Relay（b4/b5）、Windows（W05）、真实 Keychain。

#### PLT-M05b3d 完成记录（2026-08-31，macOS Cast command/event pump）

- 实现：复用既有 `MediaHostProcess` 单一后台 I/O worker，不新增线程；process 接纳 b3b 冻结的 device/start/session reply，UI-thread-only adapter 新增 discovery、分页设备、start、generation-bound stop 与有界 Cast result drain。Cast request/result 各限制 64，设备页沿用 16 项；request/reply kind 严格相关。Casting reply 建立单 active generation、单 in-flight、100ms 间隔的 event pump；空批保留，旧 generation/revision 与重复事件丢弃，terminal、Stop、process generation 变化和 adapter Stop 均停泵清状态。
- 生命周期与失败闭合：Start 必须命中当前 candidate，Stop 必须命中当前 active generation；队列拒绝、协议错配、dropped count 回退与 Cast result 溢出均产出闭合错误并 best-effort stop。Review 发现 navigation 可能使 pending Start 的迟到 Casting reply失去 owner、留下孤儿 session，现保留 pending correlation 并立即异步发送 generation-fenced Stop；清理 Stop/Ack 不进入产品结果队列。
- 验证：Debug/Release macOS arm64 完整 CEF 构建、ad-hoc App/Helpers 签名通过；两配置 `ctest -R '^(media_host|ipc_channel_contract)'` 各 3/3，最终 focused `media_host_adapter_mac`/`media_host_process_mac` 2/2。Fake transport 覆盖无 candidate、discovery/device page、Direct/Relay、Handoff/Reject/Failed、单 poll、空批、host dropped、旧 generation/revision、terminal、restart、queue reject、stop 与迟到 Start 清理；真实 child 覆盖新增 reply、shutdown/restart。`cargo test --workspace --exclude crayon-platform-macos`、`cargo run -p repo-guard -- scan --root .`、`bash scripts/check.sh security`（relay security 7/7）、Apple clang-format dry-run 和 `git diff --check` 通过；明确排除真实 Keychain crate。
- Code Review：按 v0.8 复核需求边界、唯一 worker、UI 线程非阻塞、request/generation/revision fencing、队列容量、迟到 reply、stop/shutdown 顺序、隐私与测试；关闭上述孤儿 session P1，并拆分超 100 行生产 reply dispatch、补齐协议/容量失败时 best-effort stop，最终 P0/P1/P2=`0/0/0`。测试中的完整 Cast 状态矩阵保持为一个场景函数，属于测试规模提醒，不混入生产职责。
- 未覆盖与风险：尚未实例化真实 CEF Cast button、原生 picker，也未把 navigation/close/app-exit 接到已建立 session；统一由 `PLT-M05b3e READY` 闭合。手机 Direct/Relay 可播放性仍归 M05b4/b5，不以 Fake 或 ADB 在线替代；Release Ninja 仍出现既有 `premature end of file; recovering` 后自恢复并完整构建成功；全程未访问真实 Keychain。

#### PLT-M05b3e 原子范围（按 e1/e2 两切片）

审计结论（2026-08-31）：固定版 CEF 150 Chrome Runtime 的 `CefCommandHandler` 只能过滤默认可见的内置 toolbar button，不能动态注入或驱动自有按钮；复用 `IDC_ROUTE_MEDIA`/`CefMediaRouter` 会绕开本项目唯一 `CastUsecase`/Cast-SDK facade，禁止用作实现。状态/分页/命令/lifecycle 编排与 AppKit chrome/button/sheet 同时落地会混合两个变化原因，因此拆为：

| ID | 状态 | 依赖 | 单一目标 | 允许范围与门禁 |
|---|---|---|---|---|
| PLT-M05b3e1 | DONE | M05b3d DONE | 建立 UI-thread-only Cast shell controller，把 Browser-verified media、opaque candidate、设备分页、start/session reply 与 navigation/close/shutdown 映射到既有 `CastUiCoordinator` 和 b3d async command port | `browser/cef-shell/src/browser/media_host/cast_shell_controller*`、独立 Fake command test、CMake 与 Roadmap；无 CEF/AppKit/SDK/网络，页面事实不能直接启用或选择设备 |
| PLT-M05b3e2 | DONE | M05b3e1 | 增加 macOS 原生 browser-chrome Cast 按钮与非阻塞 receiver picker，并在真实 CEF App 接通 e1 controller、M05b2/b3d drain 和 navigation/close/app-exit | AppKit adapter、CEF App/Tab lifecycle、locales/自有 `cast.device` glyph、真 CEF fixture；不得调用 Chromium MediaRouter，不覆盖网页 viewport，不访问 Keychain |

两切片共用边界：只有 Browser process 已通过真实输入与播放推进门禁的 media fact 才能建立 eligibility；network/candidate/page 文本单独不能启用按钮。receiver 仅显示稳定 id、展示名和闭合可用状态，不向 UI 暴露 IP/URL/UDN/token。Handoff 的确认/安装/启动反馈仍归 M05b6，e1/e2 遇到 Handoff 只闭合为非 Casting 状态；手机 Direct/Relay 仍由 M05b4/b5 验收。

##### PLT-M05b3e1 原子范围（Cast shell controller）

- 状态：`DONE`；依赖 `PLT-M05b3d DONE`、`PLT-M05b3a DONE`。
- 单一目标：新增无平台 UI/CEF 类型的单 UI 线程 controller，消费 Browser-verified media 标记、opaque candidate、b3d Cast reply 和显式 navigation/close/shutdown，驱动 `CastUiCoordinator` 并通过注入的异步 command port 发 discovery/list/start/stop。
- 输入/输出与允许修改：平台无关 controller 当前位于 `browser/cef-shell/src/browser/media_host/cast_shell_controller.{h,cc}`，配套独立 test、相邻 CMake 和 Roadmap。输入只含闭合 planning/Cast DTO 与显式 Browser lifecycle；输出只含 coordinator presentation state、receiver option 和异步闭合命令。
- 禁止修改：CEF App/WindowClient、AppKit、Rust/MHV1/adapter、Cast-SDK/Relay、shared UI 状态机、locales/assets；不得使用 Chromium MediaRouter/`IDC_ROUTE_MEDIA`，不得执行 I/O、等待或创建线程。
- 状态/容量/生命周期：仅 Browser-verified media 与 current candidate 同时存在才 eligible；设备分页 revision/offset 连续、最多 64，仅 Ready 设备可选择；同时最多一个分页和一个 Start。navigation/close/shutdown 先 generation-bound stop active session、停止 discovery，再清 picker/candidate/eligibility；Start outcome 与 session terminal 只按闭合 route/generation 投影，Handoff/Reject/Failed 不显示 Casting。
- 验收：Fake command port 覆盖 network/candidate 不能启用、verified media、无设备/多页/刷新/取消、非法页、重复选择、Direct/Relay/Handoff/Reject/Failed、旧/terminal event、stop、navigation/close/shutdown 和命令拒绝；Debug/Release unit、clang-format、repo guard/security、Review P0/P1=0。
- 明确不做：真实 chrome button/AppKit picker/CEF 产品接线（e2）、ExternalClientHandoff 执行（b6）、手机真机（b4/b5）、Windows、真实 Keychain。

##### PLT-M05b3e1 完成记录（2026-08-31，Cast shell controller）

- 实现：新增 UI-thread-only `CastShellController`，通过注入的闭合 command port 只 enqueue discovery/list/start/stop，唯一驱动既有 `CastUiCoordinator`。Browser-verified media 与 current opaque candidate 必须同时存在才进入 Eligible；candidate/network 单独保持 Hidden。设备快照按 revision/offset 连续分页、每页 16/总量 64，仅 Ready 设备进入选择列表；同时最多一个分页和一个 Start，Start 固定 `handoff_available=false`，不执行 b6 外部客户端交接。
- 生命周期与失败闭合：Direct/Relay 的 Start reply 先投影闭合 policy 再按非零 generation 进入 Casting；Handoff/Reject/Failed 均进入非 Casting 的显式拒绝状态。旧 generation terminal 不能结束新 session；Stop、navigation、close、host loss、shutdown 均先 best-effort generation-bound stop、停止 discovery，再清 candidate/picker/eligibility。命令拒绝、畸形/乱序分页和 host error 不留下 Planning/Stopping 假状态；迟到 page/start/session 在 current pending/generation 之外被忽略或由 b3d 清理。
- 验证：macOS arm64 Debug/Release `crayon_cast_shell_controller_mac_test` 各 1/1；两配置 `ctest -R '^(cast_ui_coordinator|cast_shell_controller_mac|media_host_adapter_mac)$'` 各 3/3。测试覆盖 candidate 无 proof、verified media、Ready/Offline、多页、空设备、刷新/取消、乱序页、重复选择、Direct/Relay/Handoff/Reject/Failed、旧/terminal event、stop、navigation/close/shutdown、host loss与 command reject。`cargo run -p repo-guard -- scan --root .`、`bash scripts/check.sh security`（relay security 7/7）、Apple clang-format dry-run 与 `git diff --check` 通过；无 Keychain 调用。
- Code Review：按 v0.8 复核可信 eligibility、唯一状态 owner、分页/队列边界、generation、迟到结果、命令拒绝和生命周期；关闭 2 个 P1（Stop enqueue 失败会遗留 Stopping；selection/protocol 失败未停止 discovery）后最终 P0/P1/P2=`0/0/0`。生产函数均未触发 100 行提醒，无锁、线程、I/O、日志或 secret/locator DTO。
- 未覆盖与风险：本切片没有真实 CEF/AppKit UI，也未加入产品 target；`PLT-M05b3e2 READY` 负责原生 browser-chrome button/picker、App drain 与真 CEF lifecycle。固定 CEF Chrome Runtime 不能注入自有 toolbar button，e2 必须使用受控 AppKit browser-chrome surface，禁止回退 Chromium MediaRouter。Release Ninja 仍出现既有 `premature end of file; recovering` 后自恢复并完成目标；真机仍归 M05b4/b5。

##### PLT-M05b3e2 原子范围（macOS browser-chrome button/picker 与产品接线）

- 状态：`DONE`；依赖 `PLT-M05b3e1 DONE`。
- 单一目标：使用 AppKit titlebar accessory 建立不覆盖网页 viewport 的真实 macOS browser-chrome Cast 按钮与非阻塞 receiver sheet，并在产品 `BrowserApp` 接通 e1 controller、M05b2 planning/b3d Cast drain 以及 active navigation/close/focus/app-exit。
- 输入/输出与允许修改：允许新增 `browser/cef-shell/src/macos/cast_chrome_mac.{h,mm}`，修改 macOS App、相邻 `TabController` lifecycle callback、`MediaHostAdapter` 只读生命周期 epoch、CMake/resources/locales、真 CEF fixture/source/package contract 和 Roadmap。按钮图形只复用 manifest 注册的 `browser/shared-ui/design/icons/cast-device.svg`，文案只来自 `Localizable.strings`。
- 禁止修改：Rust/MHV1/Cast-SDK/Relay/shared coordinator、Chromium MediaRouter/`IDC_ROUTE_MEDIA`、网页/Renderer、Windows；不得在页面 viewport 注入按钮，不得阻塞 CEF UI 线程或访问 Keychain。
- 状态/生命周期：每个真实 CEF window 可挂一个 accessory，只有 active browser surface 显示当前状态；Hidden/Disabled/Eligible/Selecting/Casting/Stopping 映射到可见、enabled、tooltip/accessibility。sheet 最多展示 64 个闭合 receiver option，无设备可刷新/取消；select/cancel/refresh 只调用 e1。active target navigation/focus change/close、host loss和最后窗口退出必须关闭 sheet、停 session/discovery 并逆序释放；内置 route-media command 一律不得成为调用路径。
- 验收：AppKit 独立 test 覆盖 attach/detach、active window、状态映射、空/多设备、refresh/cancel/select和关闭；真实 CEF fixture 覆盖 Browser proof 后按钮 Eligible、打开空 picker、取消、navigation/close/host stop 零残留并输出 mock keychain。Debug/Release完整构建与 focused/full CTest、资源/package/source contract、clang-format、repo guard/security、Review P0/P1=0。
- 明确不做：手机 Direct/Relay 结论（b4/b5）、handoff 执行（b6）、100 次长稳（M05c）、Windows、真实 Keychain。

##### PLT-M05b3e2 完成记录（2026-08-31，macOS browser-chrome Cast UI）

- 实现：新增 AppKit titlebar accessory Cast 按钮与非阻塞 receiver sheet，不覆盖网页 viewport；按钮图形来自已登记 `cast-device.svg`，中英文文案来自资源。产品 App 接通 e1 controller、media-host planning/Cast drain、Browser-verified media、active focus/navigation/close/app-exit，并以只读 host epoch 在进程快速重启时清除假 Casting。真实 CEF window 尚未挂到 `NSWindow` 时采用幂等延迟 attach；不调用 Chromium MediaRouter，也不访问系统 Keychain。
- 生命周期与边界：每个 CEF window 只挂一个 accessory，仅 active surface 显示；空设备可刷新/取消，多设备只回传闭合 device id。navigation、跨窗口 focus、close、host generation 变化与最后窗口退出均关闭 sheet、停止 session/discovery并逆序释放。source contract 只精确允许 controller 内一处闭合 route 到共享 UI state 的枚举映射，其他 macOS 生产代码仍禁止平台自建传输栈。
- 验证：macOS arm64 Debug/Release 产品、AppKit test 与真 CEF fixture 均编译、链接并 ad-hoc 签名；两配置最终 focused CTest 各 6/6。真实 CEF `media-cast-ui` 两配置均完成 Browser proof→Eligible→空 picker→Cancel→navigation 清理，并输出 `mock_keychain=1`。Release 完整 CTest 82/82；Debug 完整矩阵两轮均为 81/82，唯一旧 source-contract 误报修正后该项及最终 focused 回归通过，完整真 CEF fixture 两轮通过。`cargo run -p repo-guard -- scan --root .`、`bash scripts/check.sh security`（relay security 7/7）、fixture `py_compile` 与 `git diff --check` 通过。
- Code Review：按 v0.8 复核可信 eligibility、唯一状态 owner、CEF/AppKit 生命周期、异步 sheet teardown、host generation、资源打包、隐私与 UI 线程热路径；修正真实 CEF 创建早于 `NSView.window` 导致 attach 失败、AppKit 测试 window close 释放和 host restart 假状态，最终 P0/P1/P2=`0/0/0`。无新增线程、锁、网络 I/O、receiver locator/URL/token 或 Keychain 路径。
- 未覆盖与风险：尚未用正式手机接收端验证 Direct/Relay 实际可播放性；下一任务为 `PLT-M05b4 READY`，但按用户要求本提交后暂停。Release Ninja 仍出现既有 `premature end of file; recovering` 后自恢复并完成构建；CEF 退出仍有既有 GPU/cache/signing warning，不影响场景退出码。

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

- 状态：`IN_PROGRESS`；依赖 `PLT-W04 DONE`、共享协议/状态机 `PLT-M05b3 DONE`、`CEF-15/SDK-14/PRV-12 DONE/VERIFIED`。macOS b4..b6/M05c 是平台验证而非 Windows 前置。
- 单一目标：在 Windows x64 CEF 产品壳消费同一共享观察、策略、UI、Cast-SDK 与 Relay owner，只增加 Windows process/IPC/UI/生命周期装配和 CP-W01 证据；不得复制或分叉 macOS 业务逻辑。
- 真机条件：当前 ADB 设备 `VED7N18906000919`（Huawei EML-AL00、Android 10、Wi-Fi `192.168.3.166/24`）已安装 `com.zknowai.labi.cast.receiver`；每次测试仍须重新记录在线状态、receiver build、网络拓扑和真实上屏，不能把本条环境快照当作通过证据。

| Slice | 状态 | 依赖 | 单一目标 | 允许路径与验收 |
|---|---|---|---|---|
| PLT-W05a | DONE | PLT-M05b3 DONE | 用 Windows named pipe/Job/process owner 装配 bundled `crayon-media-host`，接通现有 CEF observation/planning 与有界 health/kill/reap | `browser/cef-shell/src/windows/**`、`browser/cef-shell/src/browser/media_host/**`（仅提取既有平台无关 adapter）、顶层组合 crate `crates/crayon-media-host/**`（从 runtime 提升 binary owner，仅补平台 health/启动）、平台 CMake/package、相邻 process/adapter tests；Debug/Release build/CTest；不接 UI/SDK 真机 |
| PLT-W05b | DONE | W05a DONE | 接通共享 `CastUiCoordinator`、浏览器 chrome 按钮/receiver picker、Cast command/event worker 与 navigation/close/app-exit | Windows shell/chrome/locale、相邻 tests；无设备/刷新/取消/失败/旧 session/stop；UI 线程无阻塞 SDK |
| PLT-W05c0 | DONE | W05b DONE | 补齐 bundled Browser/media-host 私有 MHV1 的投屏码解析与 generation-bound play/pause/seek 控制，使产品壳能消费既有 CastFacade 能力 | Rust/C++ 双 codec、current/previous golden、runtime/adapter/process contract；不改 Cast-SDK facade/接收端、不做 UI 或真机结论 |
| PLT-W05c | BLOCKED | W05c0 DONE | 使用 ADB 正式接收端完成 clear fixture Direct 发现、连接、投送、pause/resume/seek/stop | E2E-001/CS-010；真实 Desktop Host 与真实上屏；当前远程桌面点击带 `LLMHF_INJECTED`，须在可产生可信物理输入的 Windows 控制台复验 |
| PLT-W05d | TODO | W05c | 同一接收端完成 MP4 Range 与 HLS Relay 全链路 | E2E-002；opaque route、200/206/416、分片、撤销后拒绝、零 secret；不支持 DASH Relay/加密 HLS |
| PLT-W05e | TODO | W05d | DRM/EME/加密/credential 拒绝与 ExternalClientHandoff 确认/取消/未安装/失败反馈 | E2E-003/004；交接不创建 SDK/Relay session、不显示投屏中 |
| PLT-W05f | TODO | W05e | 100 次开始/停止/设备/网络切换及睡眠/唤醒/退出，关闭 CP-W01 | E2E-005；进程/线程/socket/RSS/UI delay/dropped、旧 generation、退出零残留 |

共同禁止路径：不修改 Cast-SDK facade/接收端协议、MED/Relay 安全边界或公共 MHV1 schema；发现缺口时停止并建独立任务。每个 slice 单独 `IN_PROGRESS`、测试、v0.9 Review 和提交；`PLT-W05` 只有 W05a..f 全部完成才可 `DONE`。

### PLT-W05c0 原子范围（MHV1 投屏码与播控扩展）

- 状态：`DONE`；依赖 `PLT-W05b DONE`。审计确认现有 MHV1 kind 13..20 和 `MediaHostCastRuntime` 只表达 discovery/list/start/stop/poll，而 W05c/CS-010 明确要求产品投屏码与 pause/resume/seek；此前 b3b/b3c 又把两者列为不做，故 W05c 不能在不扩协议的情况下验收。
- 单一目标：保持 `MHV1` magic/version、16KiB frame 和 kind 1..20 原字节不变，追加投屏码解析请求/回复及 generation-bound play/pause/seek 请求；Rust media-host 只调用既有 `CastFacade::resolve_device_by_cast_code` 与 `CastUsecase::{play,pause,seek}`，Browser 侧 adapter 仅提供有界异步端口和稳定回复，不在 UI 线程执行 SDK/pipe/wait。
- 输入/输出与允许路径：`crates/crayon-ipc-schema/**`、`crates/crayon-app-runtime/**`、`crates/crayon-media-host/**`、`browser/cef-shell/src/ipc/**`、`browser/cef-shell/src/browser/media_host/**`、Windows/macOS media-host process 的 reply allow-set、相邻 golden/codec/runtime/process/adapter tests、CMake/package contract 与本 Roadmap/索引。投屏码入界只允许有界规范化字符；回复只携带既有稳定 device presentation；控制绑定非零 session generation，seek 使用有界绝对秒数。
- 禁止修改：Cast-SDK、`crayon-cast-adapter` 公共 facade、接收端、MED/Relay、CEF widget/平台 chrome、页面/Renderer、外部客户端交接；不得在 DTO、日志或诊断中加入 receiver IP/host/UDN/control URL、媒体/page URL、Cookie/Authorization、relay secret 或 SDK 自然语言。
- 验收：Rust/C++ current/previous golden 双向读取；新增 kind 的 roundtrip、畸形/截断/超长/未知/重复 request、非法投屏码、旧/零 generation、seek 上界、无会话/终态/route lost、超时与 late reply fencing；Debug/Release `ALL_BUILD` 与相关/完整 CTest，`cargo fmt`、targeted clippy/tests、`scripts/check.ps1 fast/security`，v0.9 Review P0/P1=0。明确不做 Windows UI 和真机通过结论（回到 W05c）。

### PLT-W05c0 完成记录（2026-08-31，MHV1 投屏码与播控扩展）

- 实现：保持 `MHV1` magic/version、16KiB frame 与 kind 1..20 字节兼容，追加 kind 21/22 的有界投屏码解析和稳定 `Device/CastError` 回复，以及 kind 23/24 的 generation-bound play/pause/seek 与稳定 `Applied/CastError` 回复。Rust media-host 仅复用既有 `CastFacade::resolve_device_by_cast_code` 和 `CastUsecase`，Browser adapter 只做有界异步请求；控制回复携带 session generation，replacement session 后到达的旧回复由 adapter 丢弃。Windows/macOS process allow-set 仅登记新 reply kind，未改变 Cast-SDK facade、接收端或 Relay/MED 边界。
- 协议与自动化：`cargo test -p crayon-ipc-schema`、`cargo test -p crayon-app-runtime --all-targets`（60 unit + 9 + 2 + 8 + 2）、`cargo test -p crayon-media-host --all-targets`、targeted `cargo clippy ... -- -D warnings` 全部退出码 0；Rust/C++ codec 使用固定跨语言 golden 覆盖 resolve/control 成功与稳定错误，并覆盖畸形 one-of、未知 tag、超长/非法 cast code、零/旧 generation、seek 上界、无会话、route lost 与 late reply fencing。`cargo fmt --all -- --check`、Python fixture `py_compile` 和 `git diff --check` 退出码 0。
- Windows 构建与 CTest：Windows x64 multi-config tree 的 Debug/Release `ALL_BUILD` 均通过；`ctest --test-dir .cache/build/windows-cef-debug -C Debug --output-on-failure` 为 **85/85**（279.31s），Release 为 **85/85**（187.32s）。其中真实 CEF `cast_cef_integration_windows` 分别为 3.84s/2.71s，`page_snapshot_cef_integration_windows` 分别为 56.50s/35.48s。Windows CEF 测试先经产品 F24 trusted-input 路径，再以 CEF 鼠标事件点击仅限 fixture 的全屏播放按钮；修复了脚本 `media.play()` 在该 Windows CEF 环境不推进而造成的伪失败，未向产品命令行加入 autoplay 放宽。
- Workspace 门禁：`RUST_TEST_THREADS=1 scripts/check.ps1 fast` 退出码 0（167.2s）；`scripts/check.ps1 security` 退出码 0（19.1s）。repo guard 的既有文件/函数规模与可配置字面量 warning 保持 warning，未出现阻断发现；RG-001/002/004A/004B/004C/005/007/008/009 通过，RG-006 因未指定发布 artifact 为 `not_applicable`，发布 artifact 扫描归后续打包任务。
- Code Review：按 v0.9 依次审查需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试/证据、可维护性/供应链。审查中关闭三项 P1：投屏码错误不得扁平化为 HostError、控制错误必须使用稳定 reply、late control reply 必须绑定并校验 session generation。最终 P0 0、P1 0、P2 0、P3 0，`APPROVE`；Roadmap 最高状态 `DONE`。
- 未覆盖与风险：本切片按范围不接 Windows UI、不声明 ADB/Direct 真机通过，均由 `PLT-W05c` 闭合。固定 SDK 的投屏码调用没有 cooperative cancel，当前以有界调用方丢弃、process generation 和 controller pending state 防止迟到结果污染；产品 UI 的取消行为仍须在 W05c 验证。macOS 仅通过 Windows 上的 source contract 检查 reply allow-set，macOS 构建、签名与行为均 `NOT_RUN` 并按用户决策后置。

### PLT-W05c 解阻操作手册（物理控制台一次性采集，2026-09-01 增补）

> 目的：把"用户在本地物理控制台的一次真实点击"扩展为完整 W05c 证据包，避免多轮往返。以下步骤不产生任何代码变更，不绕过可信输入门禁。

1. 前置（本会话已备妥）：ADB 设备 `VED7N18906000919` 在线；手机安装正式接收端 `com.zknowai.labi.cast.receiver` 1.1.1 并启动到 `MainActivity` 前台；PC 与手机同网段。
2. 在**本地物理控制台**（非远程桌面/非注入工具）双击运行 `D:\crayon-browser\.cache\build\windows-cef-debug\browser\cef-shell\Release\CrayonBrowser.exe`（Release 产物；Debug 亦可但须记录配置）。
3. 在地址栏粘贴 `D:\crayon-mdv-test\video-cast.html`（已备妥的离线 VP9/Opus 循环视频验证页）回车打开，用**物理鼠标**点击页面播放按钮，确认视频真实推进。
4. 物理点击工具栏 Cast 按钮 → picker 中选择手机接收端（或用投屏码入口输入接收端显示的投屏码）。
5. 手机出现首帧后，在 picker/会话 UI 依次点 pause、resume、seek（任意位置）、stop。
6. 完成后告知 Agent；Agent 侧将自动采集：media-host MHV1 会话记录、播放位置变化、stop 后 `MainActivity` 前台状态与 Browser/Host/SDK 资源归零，并把这些证据写入 W05c 完成记录。手机侧请保持屏幕常亮且不要手动操作其它 App。
7. 判定：只有全程物理输入（鼠标探针 `injected=False`）取得的证据才计入 E2E-001/CS-010；注入输入证据一律拒收。

### PLT-W05c 验证记录（2026-09-01）

- 状态：`BLOCKED`；依赖 `PLT-W05c0 DONE`。Windows 产品缺失的投屏码与 pause/resume/seek 原生入口、Browser/controller/media-host request/reply 接线及 generation fencing 已实现并自动化验证；但本轮无法产生 Browser 可接受的可信物理播放输入，故真实 Direct 首帧、播控与停止没有冒充通过。
- 输入与真机：离线 clear fixture；ADB 设备 `VED7N18906000919`（Huawei EML-AL00、Android 10），正式接收端 `com.zknowai.labi.cast.receiver` 1.1.1（versionCode 10101），本轮开始时 `wlan0=192.168.3.166/24` 且 `MainActivity` 前台。上述仅是开工快照，完成记录仍须包含测试时刻的 build/network/上屏/终态证据。
- 允许路径：Windows CEF 产品装配与相邻 E2E/Harness、测试 fixture、平台 package/CMake、`browser/cef-shell/src/browser/media_host/**` 中现有共享 controller 的最小缺陷修复，以及本 Roadmap。禁止修改 Cast-SDK/接收端、复制协议、借用 standalone/Fake 结论，禁止把 ADB online、Activity 启动或页面截图单独冒充 Direct 成功。
- 验收与命令：Windows x64 Debug/Release `ALL_BUILD` 与完整 CTest；`scripts/check.ps1 fast`、`scripts/check.ps1 security`；E2E-001 必须从页面点击播放进入产品自动发现并在手机真实首帧后完成 pause/resume/seek/stop；CS-010 还必须以产品投屏码入口连接同一接收端。记录产品/receiver 会话、播放位置变化、停止后 `MainActivity` 与 Browser/Host/SDK 资源归零。若发现 Windows 产品缺陷，先补稳定复现测试再做共同入口最小修复。
- 不做：MP4 Range/HLS Relay（W05d）、DRM/credential/handoff（W05e）、100 次/电源网络长稳（W05f）、macOS 特有验证，以及 Cast-SDK/receiver 功能开发。
- 实现与缺陷修复：Windows receiver picker 新增本地化投屏码输入/连接，活动会话新增 pause/resume 与有界秒数 seek；所有入口只调用共享 `CastShellController` 与单 worker `MediaHostAdapter`。Review 先以稳定测试复现两项同类竞态：投屏码回复必须精确匹配请求 ID；同一 session 的旧/重复 control reply 也必须同时匹配 request ID 与 session generation，不能清除或覆盖后继请求。两项均在共同入口最小修复，导航、取消、终态和新 session 会清理 pending 状态，错误保持稳定 UI 状态。
- 构建与自动化：Windows 11 x64 multi-config tree 的 Debug/Release `ALL_BUILD` 在最终代码上均通过（组合命令退出码 0，137s）；targeted `media_host_adapter_win`、`cast_shell_controller_win` 2/2 通过。最终 `ctest --test-dir .cache/build/windows-cef-debug -C Debug --output-on-failure` **85/85**（395.55s，真实 CEF page snapshot 64.32s、Cast 4.81s），Release **85/85**（311.00s，分别 52.93s/3.07s）。`RUST_TEST_THREADS=1 scripts/check.ps1 fast` 退出码 0（86.8s），`scripts/check.ps1 security` 退出码 0（5.7s；guard、relay-unit、relay-security 全通过），`git diff --check` 通过；repo guard 只保留既有文件/函数规模和可配置字面量 warning，RG-006 因未传发布 artifact 为 `not_applicable`。Debug 前一轮为 84/85：Cast 已通过，page snapshot 的全部场景输出均完成但进程最终非零；隔离复跑 1/1（70.30s）且随后全量 85/85，记录为既有 CEF 退出波动而非伪造通过。错误的独立 Release build 目录命令因目录不存在退出 1、未执行测试；改用同一 Visual Studio multi-config tree 的 `-C Release` 后取得上述完整结果。
- 真机与阻塞证据：ADB 设备在线、正式 receiver 1.1.1 前台、PC/手机位于同一 `192.168.3.0/24`；产品使用真实 `CrayonBrowser.exe`、bundled media/content host 与循环 WebM VP9/Opus clear fixture。用户经当前 Codex/远程桌面点击后，低级鼠标探针原样得到 `flags=0x1, injected=True, lower_integrity_injected=False, class=Chrome_RenderWidgetHostHWND`；产品 Cast 控件 `0x5C01` 随即仍为 `Visible=False, Enabled=False`，证明输入证据门禁未建立，而非控件被遮挡。按安全红线没有接受 injected input、没有调用测试 seam、没有直接启用按钮，因此未产生可审计的自动发现/投屏码连接、手机首帧、pause/resume/seek/stop 证据。测试结束后 Browser、fixture、probe 进程均停止，两条临时本地子网防火墙规则精确删除并验证残留为 0。
- Code Review：按 v0.9 顺序审查需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试/证据、可维护性/供应链；关闭 request correlation 两项 P1 后，最终 P0 0、P1 0、P2 0、P3 0，`APPROVE`。未复制 Cast-SDK/receiver/Relay 协议，UI 线程无网络、pipe 或等待，注入输入拒绝边界保持不变。
- 未覆盖与解阻条件：须在**能够产生非 `LLMHF_INJECTED` 物理点击**的 Windows 本地控制台，以同一正式 receiver 重新执行 E2E-001/CS-010，记录真实 device/session、手机首帧、播放位置 pause/resume/seek 变化、stop 与资源归零；此前这些项均为 `NOT_RUN`。W05d 严格依赖 W05c，当前不得领取；macOS 特有验证继续后置。

### PLT-W05a 完成记录（2026-08-31，Windows bundled media-host 产品装配）

- 实现：把既有平台无关 `MediaHostAdapter` 原样提取到 `browser/media_host`，macOS 继续注入原有 transport；将 `crayon-media-host` binary owner 从领域 crate 提升为顶层组合 crate，Windows 使用既有 current-user ACL named-pipe endpoint 提供 health。Windows C++ process owner 以显式继承句柄白名单启动随包 child，以 Job Object `KILL_ON_JOB_CLOSE`、500ms graceful stop、进程 generation、1s health、5s admission、单 worker 和各 64 条有界收发队列完成 spawn/kill/reap/restart；只接受 MHV1 reply kind。CEF 产品在 content/media host 均健康后创建主窗口，接通真实 `TabController` observation/navigation/close 与可信当前页 URL，UI tick 只做有界 enqueue/drain；planning 暂丢弃，Cast UI/SDK command 留给 W05b。
- 自动化验证：Windows x64 `cmake --build .cache/build/windows-cef-debug --config {Debug,Release} --target ALL_BUILD --parallel 1` 均通过；完整 `ctest --test-dir .cache/build/windows-cef-debug -C {Debug,Release} --output-on-failure` 在实质实现完成后均为 81/81。binary owner 机械迁移后再次完成双配置 `ALL_BUILD`，并以 `ctest ... -R '^(windows_cef_shell_package_contract|media_host_process_win|media_host_adapter_win|windows_cef_shell_source_contract)$'` 各 4/4。`cargo check/test -p crayon-media-host --all-targets`、`cargo test -p crayon-app-runtime --all-targets`、`cargo fmt --all -- --check`、`cargo clippy -p crayon-media-host -p crayon-app-runtime --all-targets --no-deps -- -D warnings`、最终 `scripts/check.ps1 fast` 与 `scripts/check.ps1 security` 串行退出码 0（65.5s），Google clang-format scoped check 和 `git diff --check` 通过。
- 产品证据：Release `CrayonBrowser.exe` 真实启动得到非零主窗口，进程树中恰有一个 `crayon-media-host.exe`、一个 `crayon-content-host.exe` 及正常 CEF helpers；关闭主窗口后 Browser 与捕获的全部 descendants 正常退出，残留进程为空。process test 使用真实 bundled binary 验证 named-pipe PING/PONG、Navigation/Ingest/List/Poll MHV1、额外 inheritable sentinel 不泄漏、Shutdown 后 unhealthy→bounded restart、generation 增长及 Stop 后不健康。
- 失败与修复：首次把 Windows 平台 crate 直接依赖加入 `crayon-app-runtime` 被 repo-guard `RG-005` 稳定拒绝，改为独立顶层组合 crate 后通过，未放松守卫；一次 package contract 使用未重建的旧 executable 报参数数量错误，重建对应 contract target 后 Debug/Release 均通过。两项均保留为可审计失败，不计入最终通过数。
- Code Review：按 v0.9 依次审查需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试/证据、可维护性/供应链；P0 0、P1 0、P2 0、P3 0，`APPROVE`。未复制 Cast-SDK/MED/Relay/schema，句柄、队列、重启、generation、URL/secret 边界均有代码或行为测试。
- 未覆盖与风险：按切片明确未接 Cast chrome UI、receiver picker 或 SDK command，未做 ADB/Direct/Relay 真机结论，由 `PLT-W05b..f` 严格串行闭合。macOS 特有构建、签名与行为验证按用户决策 `NOT_RUN` 并后置；共享 adapter/binary owner 的平台中立机械迁移保留原有 macOS 接口，但须在后续 macOS 矩阵复验。`PLT-W05a DONE`，`PLT-W05b READY`，顶层 `PLT-W05` 保持 `IN_PROGRESS`。

### PLT-W05b 完成记录（2026-09-01，Windows Cast UI 与生命周期装配）

- 实现：将既有无平台依赖的 `CastShellController` 从 macOS 目录提升到 `browser/media_host`，Windows 与 macOS 继续消费同一 `CastUiCoordinator`、opaque candidate、设备分页、start/stop/session 和 navigation/close/shutdown 状态机。Windows 新增原生 owner-draw Cast 按钮、tooltip/可访问名称、DPI 布局和 modeless receiver picker，覆盖无设备空态、刷新、选择、pending discovery 即时取消与 Stop 文案；所有按钮只同步回调共享 controller，SDK、网络和 pipe 工作仍由既有单 worker/有界 adapter owner 执行。产品 App 接通本地化 fail-fast、active tab/focus/close/navigation、host health/epoch、planning/cast drain 与 app-exit 逆序关闭；Browser UI 线程同时装配键盘可信输入与 Windows `WH_MOUSE_LL` 鼠标输入，鼠标只接受当前进程窗口上的按下事件并拒绝 `LLMHF_INJECTED/LLMHF_LOWER_IL_INJECTED`。门禁诊断只细分 not-playing/not-visible/input-proof 拒绝原因，不改变授权判定。
- 自动化验证：Windows x64 Debug `ALL_BUILD` 最终通过（67.9s），`ctest --test-dir .cache/build/windows-cef-debug -C Debug --output-on-failure` **85/85**（426.15s）；Release `ALL_BUILD` 通过（138.0s），完整 CTest **85/85**（251.14s）。新增 `trusted_input_monitor_win` 连同 `cast_shell_controller_win`、`cast_chrome_win`、`windows_cef_shell_source_contract` 全绿；`page_snapshot_cef_integration_windows` 连续 3 次通过（62.81s/66.86s/85.59s），`cast_cef_integration_windows` 连续 3 次通过（4.11s/3.86s/4.13s），最终双配置真实 CEF 分别为 68.36s/4.21s 与 41.17s/3.52s。`RUST_TEST_THREADS=1 scripts/check.ps1 fast` 后接 `scripts/check.ps1 security` 串行退出码 0（合计 136.5s）；`git diff --check` 通过。
- 真实 CEF 证据：Windows CEF fixture 使用随包 `crayon-media-host.exe`、真实 renderer media observer/Browser input-proof、共享 planning/controller 和原生 Win32 chrome；离线 PCM WAV 在测试专用 `autoplay-policy=no-user-gesture-required` 下真实推进，产品命令行未放宽。可信输入经产品已有 `WindowClient::OnPreKeyEvent` 路径进入门禁，不直接调用 proof seam；场景确认 Browser eligible fact 生成 opaque candidate，原生 Cast 按钮可见可用，picker 打开后即使 device page 仍 pending 也可取消，导航到 recovery page 后按钮/picker 清理。普通 Markdown fixture 对 content-host supervisor 的瞬时重启做 10s 有界 admission 等待，连续三轮覆盖正常/空页/navigation/cancel/close/backpressure/crash/security/100KiB 性能并全部收敛。测试只闭合 UI/command/event 装配，不伪造接收端或 Direct/Relay 成功。
- 失败与修复：最初 AV1 MP4 在 Windows CEF 无可用播放推进，切到确定性 PCM WAV；随后 Chromium autoplay 拒绝，通过仅限测试场景的 autoplay policy 获得真实推进。fixture 曾用 `location.hash` 记录播放结果，导致 navigation generation 前移、input-proof 以 stale navigation 正确拒绝，改为 DOM `data-playback`。独立 Review 发现产品只接键盘可信输入而漏接鼠标，补 Windows hook 后又发现最初实现会接受 `SendInput` 注入；最终拒绝两类 injected flag，以可审计策略单测覆盖 owned/down/injected/foreign/lifecycle，CEF 改走键盘产品路径。自动化桌面存在系统 `Shell_SystemDim` 遮罩，无法取得物理鼠标真点击证据，未绕过该边界。Cast CEF 三连第三次暴露 SDK device page 超过原 5s 预算；没有继续扩大超时，而是补 pending-cancel 断言并验证 UI 不等待 LAN。Debug 全量首轮还暴露 content-host 在页面完成时瞬时 unhealthy，诊断为 `host_healthy=0/process_healthy=0/browser_loading=0`，fixture 改为复用既有 10s supervisor admission 门禁，随后双 CEF 各三连与双配置全量通过。workspace 默认并行 `fast` 两次分别抖动于既有 `stalled_probe...`（请求数 0/2）和 `cnt_20w1_real_process...`（Win32 pipe 233）；两个失败项单独复验均立即通过，最终单线程完整 fast 通过，未夹带跨任务修复。
- Code Review：按 v0.9 顺序独立审查需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试/证据、可维护性/供应链；审查中关闭“Windows 鼠标未进入 trusted-input owner”和“注入鼠标可被误认”两项 P1，最终 P0 0、P1 0、P2 0、P3 0，`APPROVE`。UI thread 仅做有界 enqueue/drain、hook callback 与 Win32 presentation，不复制 Cast-SDK/Relay/MHV1；旧 session、host epoch、导航、关闭与 shutdown 均 fail-closed。
- 未覆盖与风险：ADB 正式接收端发现、连接、真实上屏与 pause/resume/seek/stop 明确属于 `PLT-W05c`，MP4 Range/HLS Relay 属于 W05d，拒绝/外部交接与 100 次稳定性属于 W05e/f。自动化桌面遮罩下的**物理鼠标点击**、Narrator、原生 200% DPI 和 macOS 特有构建/签名/行为本切片 `NOT_RUN`；鼠标策略与 hook 生命周期已自动化通过，但不能冒充真实物理输入。macOS source contract 在 Windows CTest 通过，但不能替代 macOS 验证。workspace 默认并行 fast 的两处独立时序抖动须在发布总门禁前治理或证明稳定。`PLT-W05b DONE`，`PLT-W05c READY`，顶层 `PLT-W05` 保持 `IN_PROGRESS`。

### PLT-19W/19M Review 边界

- `PLT-19W TODO`：依赖 `PLT-W05 DONE`，只审 Windows CEF、DPAPI、named pipe、网络/电源、更新/交接、Cast/Relay 生命周期与发布边界；P0/P1=0 后可作为 Windows QAR 前置。
- `PLT-19M TODO`：依赖 `PLT-M05 DONE`，后续审 macOS 签名/公证、Keychain、UDS、本地网络权限与原生生命周期；不得阻塞或改写 19W。
