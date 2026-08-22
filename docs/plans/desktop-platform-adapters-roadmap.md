# PLT Windows/macOS 平台适配 Roadmap

- 状态：规划中
- 任务数：7
- 平台：Windows、macOS
- 非目标：Linux、屏幕/标签页/系统音频采集、编码器、WebRTC sender

## 1. 任务表

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| PLT-01 | DONE | FND-09 | `crates/crayon-platform-api/**` | 定义安全存储、本地网络、生命周期、更新、当前用户本机 IPC 和外部客户端交接接口 | `CP-004`,`CP-W01`,`CP-M01`,`AG-012`; unit | V1 |
| PLT-02 | DONE | PLT-01,FND-10 | `crates/crayon-platform-api/**`, `crates/crayon-platform-capabilities/**` | 定义 `secure_store`、`local_network`、`lifecycle`、`update`、`local_agent_ipc`、`external_client_handoff` 能力模型 | `CP-004`,`AG-012`; schema/golden | V1 |
| PLT-W04 | TODO | PLT-02,CEF-12,SDK-08 | `platform/windows/**` | 实现 DPAPI、本地网络/防火墙、多网卡、睡眠唤醒、更新、当前用户 named pipe 与投屏客户端交接 | `CP-W01`,`AG-012`; Windows integration | V4W |
| PLT-W05 | TODO | PLT-W04,CEF-15,SDK-14,PRV-12 | `apps/desktop-cef/**`, `platform/windows/**` | Windows 产品装配与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-W01`; Windows device | V4W |
| PLT-M04 | TODO | PLT-02,CEF-01E,CEF-12,SDK-08 | `platform/macos/**` | 实现 Keychain、本地网络权限、生命周期、更新、当前用户 UDS 与投屏客户端交接 | `CP-M01`,`AG-012`; macOS integration | V4M |
| PLT-M05 | TODO | PLT-M04,CEF-15,SDK-14,PRV-12 | `apps/desktop-cef/**`, `platform/macos/**` | macOS 产品装配、签名/公证与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-M01`; macOS device | V4M |
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
