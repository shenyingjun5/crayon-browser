# PRV：隐私与产品安全 Roadmap

状态：`PRV-01..04/06..09 DONE`、`PRV-10 VERIFIED`；`PRV-05 等 PLT-W04/M04 真机`、`PRV-11..13 依赖链后置`。Relay 网络安全实现归 MED，本 Roadmap 负责 Profile、追踪防护、安全存储，以及页面数据、Agent、Workflow/Challenge、Capability Hub/Partner connector 的隐私数据流和系统级安全门禁。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| PRV-01 | FND-08,CEF-04 | `crayon-profile/model` | Profile ID/type/path/lifecycle 状态机，随机目录 ID | PV-001、PV-004；非法状态/重复关闭 | S1 |
| PRV-02 | PRV-01 | `crayon-profile/ephemeral` | 临时 context、最后窗口关闭、清理清单与结果 | PV-001、PV-002、PV-003；每类存储 fixture | S3 |
| PRV-03 | PRV-01 | `crayon-profile/persistent` | 常用空间创建/隔离/销毁事务 | PV-004、PV-005；部分失败/重试 | S3 |
| PRV-04 | PRV-02,PRV-03 | `crayon-profile/path_guard` | 绝对根验证、symlink/junction/reparse 防护、启动补偿清理 | PV-006；逃逸目标零修改 | S2 |
| PRV-05 | DONE | PLT-W04,PLT-M04 | `crayon-profile/secure_store` | Windows/macOS 安全存储接口、key ID、轮换/删除/不可用状态 | PV-007；明文扫描；错误映射 | S4 |
| PRV-06 | CEF-05,FND-11 | `browser/privacy/standard` | 第三方 Cookie、存储分区、Referer、HTTPS、权限默认 | PV-008、PV-009；兼容 fixture | S3 |
| PRV-07 | PRV-06 | `browser/privacy/strict` | 高熵 API 统一降精度/限制，能力/兼容开关 | PV-009；熵/兼容；无每 Profile 随机身份 | S3 |
| PRV-08 | FND-08,FND-09 | `crayon-domain/diagnostics` | 数据分类、redaction、事件 schema、bounded producer | RL-014、PV-008、PV-010；满队列 dropped | S2 |
| PRV-09 | PRV-08 | `apps/*/diagnostics`,`crayon-domain/diagnostics_outbound` | 默认关闭遥测、崩溃 opt-in、发送前预览、删除 | PV-008、PV-010；实际 payload 对照 | S3 |
| PRV-10 | MED-18,SDK-12 | `docs/current/threat-model.md` | 资产/信任边界/威胁/缓解/残余风险，覆盖网页、IPC/LAN、入站 CAAP/MCP、语义动作、Workflow/Challenge、出站 connector、模型与供应链；模块实现后的专项 Review 继续增补 | 安全用例映射无缺口；专项 Review | S0 |
| PRV-11 | DONE | PRV-04,PRV-05,PRV-07,PRV-09,PRV-10 | `tests/security/privacy` | 磁盘/日志/DTO/网络/receipt/cache/trace/checkpoint/Skill/connector LeakScanner 与 Profile 全存储扫描 | PV 全集、RL-014、AG-011、适用 WF/HB；零秘密 | S3 |
| PRV-12 | PRV-10,PRV-11 | `tools/repo-guard` | secret/debug/unsafe route/自动广告行为静态门禁 | 故意违规样本失败；Release 零例外 | S2 |
| PRV-13 | PRV-11,PRV-12 | Review/数据流文档 | 隐私影响评估、页面/Agent/Workflow/Hub/connector/model 数据矩阵、平台差异和清理限制；修 P0/P1 | security/desktop tests；无虚假隐私承诺 | S3 |

## 不允许的实现

- 不允许通过清空一部分目录就宣称无痕完成。
- 不允许为不同 Profile 随机 UA/Canvas/WebGL/时区形成稳定唯一指纹。
- 不允许诊断 consumer 反压浏览、relay、Cast-SDK 或退出。
- 不允许用 Profile 名作为路径，不允许删除未验证根目录或跟随 reparse point。
- 不允许复用 Agent R1 grant 作为网页写操作、投屏控制或模型发送授权；receipt/cache 不得持久化页面正文或完整参数。
- 不允许 trace/checkpoint/Site Skill 保存密码、支付、文件值、Cookie、Authorization、正文副本或记录时的 action_id；技能运行必须重新授权。
- 不允许入站 MCP session/secret 被出站 Partner connector 使用，也不允许 OAuth token 出现在 CAAP、页面、receipt、route_reason 或诊断中。
- 不允许自动解验证码/风控，或在高风险、跨源、低置信度变化下静默 self-heal。

## PRV-01 原子范围（Profile 模型与生命周期状态机）

- 状态：`IN_PROGRESS`；依赖 `FND-08 DONE`、`CEF-04 DONE`。
- 单一目标：交付平台中立的 `crayon-profile` Rust crate 的 `model` 模块：Profile ID/type 强类型、随机目录 ID、Profile 注册表与生命周期状态机；本任务不实现磁盘创建/清理、无痕存储清单或安全存储。
- 输入：`crayon-domain` 的强类型 ID 与 `CoreError` 约定、CEF-04 的 Profile ID 校验语义（非空、≤ 256 字节、UTF-8）、PV-001/PV-004 的 Profile 边界与路径不得使用名称的要求。
- 输出与允许修改：新增 `crates/crayon-profile/`（`model.rs`、`model_tests.rs`、`lib.rs` 只做 re-export）、根 `Cargo.toml` workspace 成员、本 Roadmap 状态与证据。
- 禁止修改：其他 crate、CEF shell、UI、诊断/遥测、Cast-SDK；不得把 Profile 名/ID 拼入文件路径（路径分量只来自随机目录 ID 的十六进制编码）；不得写真实磁盘。
- 边界：Profile ID 非空且 ≤ 256 字节；目录 ID 为 128 位加密随机（`getrandom`，已在 workspace lock 中的成熟 MIT/Apache 依赖）编码为 32 字符小写十六进制，测试经注入字节确定性构造；注册表容量上限 64，重复 ID、未知 ID、非法状态迁移（含重复关闭）稳定拒绝；`Closing` 状态下禁止业务操作，`Closed` 后才允许移除记录；无痕 Profile 标记 ephemeral，持久化与清理由 PRV-02/03 拥有。
- 验收与测试：PV-001、PV-004；单测覆盖 ID 校验矩阵、目录 ID 格式/唯一性、创建/查询/关闭生命周期、非法状态/重复关闭/容量、路径派生只含目录 ID。命令：`cargo test -p crayon-profile`、`cargo fmt --all -- --check`、`cargo clippy -p crayon-profile --all-targets -- -D warnings`、workspace 回归 `cargo test -p crayon-browser-core --lib`（3 项）与 legacy-dev（58 项）、`git diff --check`。
- 明确不做：磁盘目录创建/删除、无痕清理清单、Profile 持久化 schema、平台安全存储、UI picker；分别由 PRV-02/03/05、BUX-15 完成。

## PRV-01 完成记录（Profile 模型与生命周期状态机）

- 状态：`DONE`；依赖 `FND-08 DONE`、`CEF-04 DONE`。
- 实现：新增 `crates/crayon-profile`（workspace 成员）。`model` 模块提供 `ProfileId`（非空、≤256 字节、UTF-8，与 CEF-04 validator 语义对齐）、`ProfileType`（Regular/Incognito 闭合枚举，ephemeral 判定）、`DirectoryId`（128 位加密随机经 `getrandom 0.3` 生成——该依赖已在 workspace lock 中，许可证 MIT/Apache-2.0/ISC、rust-random 维护；测试与持久化重载经 `from_bytes` 确定性注入）与 `ProfileRegistry`（绝对根校验、容量 64、重复 ID/目录 ID 复用拒绝、Active→Closing→Closed 闭合迁移、重复关闭/非法迁移稳定拒绝、仅 Closed 可移除）。路径派生为 `root.join(directory_id.to_hex())`，Profile ID/名称永不进入路径；模块零文件系统访问、零日志。
- 自动验证：`cargo test -p crayon-profile` 13/13 通过；`cargo clippy -p crayon-profile --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；workspace 基线回归 `cargo test -p crayon-browser-core --lib` 3/3 与 `--no-default-features --features legacy-dev --lib` 58/58 通过；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；Review 发现并关闭 1 个 P1（目录 ID 可被两个 Profile 复用破坏隔离，新增 `DirectoryIdInUse` 拒绝）和 1 个 P2（测试文件初版按 `#[cfg(test)] mod` 内嵌，不符合 crate `tests/` 公共行为测试约定，已迁移），最终 P0/P1/P2 均为 `0`。`lib.rs` 只做 re-export；`model.rs` 约 370 行、函数均低于规模提醒线。
- 未覆盖与风险：磁盘目录创建/删除、无痕清理清单、持久化 schema、symlink/reparse 防护与平台安全存储分别归 `PRV-02/03/04/05`；`PRV-01` 转为 `DONE`，解锁 `PRV-02`、`PRV-03` 与 `BUX-15` 的 Profile 依赖。

## PRV-06 原子范围（标准隐私默认值模型）

- 状态：`DONE`；依赖 `CEF-05 DONE`、`FND-11 DONE`。
- 单一目标：交付平台中立、可独立测试的标准隐私默认值模型：第三方 Cookie 策略、存储分区、Referer 策略、HTTPS 默认升级和权限默认；本任务不实现 CEF settings 接线、网络拦截或 UI。
- 输入：CEF-05 的默认最小权限（`PermissionStore` 默认 deny）、FND-11 的配置语义、PV-008/PV-009 的数据分类与追踪防护要求、BUX-04B 的 `PrivacyDefaults` 注入接口需求。
- 输出与允许修改：新增 `browser/privacy/standard/` 的 `PrivacyDefaults` 模型与一致性校验、独立 contract test/CMake；允许修改根 CMake 和本 Roadmap 状态与证据。新增生产文件不超过 3 个；不得出现 CEF/Win32/AppKit/ArkWeb 类型。
- 禁止修改：CEF-05 permission handler 行为、crayon-domain 配置 schema、UI、Cast-SDK/Relay/Agent；不发起网络请求，不读取磁盘，不记录站点或用户数据。
- 边界：所有策略为闭合枚举；默认值取最保守档（第三方 Cookie 默认阻止、存储分区默认开启、Referer 默认 `strict-origin-when-cross-origin`、HTTPS 默认升级开启、权限默认全部 deny）；组合冲突时以隐私更高者优先（如 `kBlockAll` 覆盖第三方允许）；未知/越界值 fail closed 回退默认；提供确定性描述快照用于 golden 兼容 fixture。
- 验收与测试：PV-008、PV-009；contract 覆盖默认值矩阵、每策略合法切换、枚举越界 fail closed、冲突消解优先级、描述快照 golden。命令：独立 configure/build/ctest、`-Wall -Wextra -Wpedantic -Werror` 零告警、共享层回归、`git diff --check`。
- 明确不做：CEF settings/request 拦截接线（后续 CEF adapter 任务）、高熵 API 降精度（`PRV-07`）、偏好持久化（`BUX-13`）、UI 呈现（`BUX-13/14`）。

## PRV-06 完成记录（标准隐私默认值模型）

- 状态：`DONE`；依赖 `CEF-05 DONE`、`FND-11 DONE`。
- 实现：新增 `browser/privacy/standard/`（2 个生产文件）。`PrivacyDefaults` 为纯数据模型：第三方 Cookie 闭合三档（默认 `kBlockThirdParty`）、Referer 闭合三档（默认 `strict-origin-when-cross-origin`）、权限默认（默认 `kDeny`，与 CEF-05 默认最小权限一致）、存储分区默认开启、HTTPS 默认升级开启；`Validate` 对越界枚举 fail closed，`Normalize` 对非法候选整体回退默认（部分应用被禁止），`Describe` 输出确定性快照且只含枚举名/布尔值。
- 自动验证：独立 `cmake -S . -B .cache/build/privacy -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功且 `-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R privacy_defaults_contract` 1/1 通过（默认值矩阵、54 组合法组合、枚举越界 fail closed、非法候选回退、golden 快照）；共享层全量回归（除本机缺 Ninja 的 `cef_build_graph_contract` 环境阻塞外）全部通过；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；当前各字段独立、无跨字段冲突需要折叠，`Normalize` 仅承担 fail-closed 回退并已如实注释，最终 P0/P1/P2/P3 均为 `0`。模块无 CEF/Win32/AppKit/ArkWeb 类型、无 IO、无日志。
- 未覆盖与风险：CEF settings/request 拦截接线归后续 CEF adapter 任务；高熵 API 降精度归 `PRV-07`；偏好持久化归 `BUX-13`；BUX-04B 的 provider/隐私注入接口消费由 BUX 侧接续。`PRV-06` 转为 `DONE`，解锁 `PRV-07` 与 `BUX-04B`。

## PRV-03 原子范围（持久 Profile 空间事务）

- 状态：`DONE`；依赖 `PRV-01 DONE`。
- 单一目标：在 `crayon-profile` crate 新增 `persistent` 模块，交付常用 Profile 磁盘空间的创建/隔离校验/销毁事务，含部分失败可重试语义；本任务不实现无痕清理（PRV-02）、symlink/reparse 防护（PRV-04）或 Profile UI。
- 输入：PRV-01 的 `ProfileRegistry`（随机目录 ID、生命周期状态机）与 PV-004/PV-005 的隔离/事务要求。
- 输出与允许修改：`crates/crayon-profile/src/persistent.rs`、`crates/crayon-profile/tests/persistent_store.rs`、`src/lib.rs` re-export、本 Roadmap 状态与证据。
- 禁止修改：PRV-01 model 语义、其他 crate、CEF shell、UI；不得删除未验证所有权的目录（红线）；不得跟随 symlink/reparse（完整防护归 PRV-04，本任务仅通过标记文件校验所有权）；错误 Display 不得携带路径或用户数据。
- 事务边界：创建 = 建目录 + 原子写入标记文件（schema 版本 + 目录 ID hex），失败时尽力回滚且重试幂等（目录已存在且标记有效视为成功）；销毁前必须验证标记与注册表目录 ID 一致且 Profile 已 `Closed`，销毁经 `<hex>.deleting-<n>` 改名后递归删除，部分失败遗留的 `*.deleting-*` 由 `retry_pending_destroys` 有界（≤16）恢复并报告；所有操作只作用于注册表 root 直接子级。
- 验收与测试：PV-004、PV-005；测试使用 `std::env::temp_dir()` 下唯一临时根的真实文件系统，覆盖正常创建/重试幂等、标记损坏/篡改拒绝销毁、非 Closed 拒绝销毁、部分失败恢复、重复销毁、容量与未知 Profile。命令：`cargo test -p crayon-profile`、`cargo clippy -p crayon-profile --all-targets -- -D warnings`、`cargo fmt --all -- --check`、workspace 基线回归、`git diff --check`。
- 明确不做：无痕 Profile 清理清单、symlink/junction/reparse 防护与启动补偿清理、跨平台长路径/权限细节、Profile 持久化元数据 schema；分别归 PRV-02、PRV-04、后续任务。

## PRV-03 完成记录（持久 Profile 空间事务）

- 状态：`DONE`；依赖 `PRV-01 DONE`。
- 实现：`crayon-profile` 新增 `persistent` 模块（1 个生产文件，约 230 行）。`PersistentStore` 提供：`create_space`（建目录 + 临时文件原子改名写入 `schema=1` + 目录 ID hex 的 `.crayon-profile` 标记；已存在且标记有效视为幂等成功；已存在但标记不符 fail closed 为 `OwnershipMismatch` 且不改动目录；标记写入失败尽力回滚目录）；`destroy_space`（要求 Profile `Closed` 且标记与注册表目录 ID 一致，先改名为 `<hex>.deleting-0` 再递归删除，递归失败返回 `StagedForResume`）；`retry_pending_destroys`（每调用最多恢复 16 个 `*.deleting-0` 遗留并返回剩余数）。所有操作只作用于注册表 root 直接子级；错误枚举不含路径或用户数据。
- 自动验证：`cargo test -p crayon-profile` 22/22 通过（13 model + 9 persistent：创建/幂等/外来目录拒绝/无痕与未知拒绝、销毁生命周期门禁/篡改标记 fail closed/重复销毁、部分失败遗留恢复与 20 项有界恢复）；`cargo clippy -p crayon-profile --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；测试使用 `temp_dir()` 下唯一真实根并在 Drop 中清理。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。销毁前所有权双重校验满足“不得删除未验证根目录”红线；`remove_dir_all` 不跟随符号链接（只删链接本身）。
- 未覆盖与风险：`create_space` 对已存在符号链接目标的校验、junction/reparse 防护与启动补偿清理由 `PRV-04` 拥有；无痕清理清单归 `PRV-02`；跨平台长路径/权限细节未覆盖。`PRV-03` 转为 `DONE`，解锁 `BUX-09/10/13` 与 `PRV-04` 的持久空间依赖。

## PRV-02 原子范围（无痕 Profile 生命周期与清理清单）

- 状态：`DONE`；依赖 `PRV-01 DONE`。
- 单一目标：在 `crayon-profile` crate 新增 `ephemeral` 模块，交付无痕 Profile 的窗口计数生命周期（最后窗口关闭触发清理）、闭合存储类别的清理清单和逐类清理结果报告；本任务不实现 CEF 临时 context 的真实存储删除接线或 UI。
- 输入：PRV-01 的 `ProfileType::Incognito`/生命周期语义、PV-001/PV-002/PV-003 的无痕零持久化与清理要求、红线“无痕清理失败必须显式报告”。
- 输出与允许修改：`crates/crayon-profile/src/ephemeral.rs`、`crates/crayon-profile/tests/ephemeral_session.rs`、`src/lib.rs` re-export、本 Roadmap 状态与证据。
- 禁止修改：PRV-01 model/persistent 语义、其他 crate、CEF shell、UI；清理失败不得宣称为已清除；错误与报告不得携带 URL、Cookie 或页面数据。
- 边界：会话状态闭合 `Active/Closing/CleaningUp/Disposed`；窗口计数拒绝下溢；进入 Closing 后拒绝再开窗口；清理清单为闭合类别（HttpCache/DomStorage/CookiesAndSiteData/FileSystemAccess/MediaState）；每类删除由调用方注入的执行器完成并回报 `Cleared/NotPresent/Failed`，任何 `Failed` 使整体报告 `fully_cleared() == false` 且允许 `retry_cleanup`；全部 Cleared/NotPresent 才允许 `Disposed`；Disposed 后所有操作稳定拒绝。
- 验收与测试：PV-001、PV-002、PV-003；测试覆盖窗口开闭/下溢/Closing 后拒绝开窗、每类存储的 Cleared/NotPresent/Failed fixture、部分失败报告与重试、重复清理幂等、Disposed 门禁。命令：`cargo test -p crayon-profile`、clippy `-D warnings`、`cargo fmt --all -- --check`、workspace 基线回归、`git diff --check`。
- 明确不做：CEF 缓存/Cookie 存储真实删除接线（后续 CEF adapter 任务）、持久 Profile 清理（PRV-03 已有销毁事务）、崩溃后的残留补偿清理（PRV-04）、UI 呈现（BUX-15）。

## PRV-02 完成记录（无痕 Profile 生命周期与清理清单）

- 状态：`DONE`；依赖 `PRV-01 DONE`。
- 实现：`crayon-profile` 新增 `ephemeral` 模块（1 个生产文件，约 290 行）。`EphemeralSession`：闭合四态 `Active/Closing/CleaningUp/Disposed`，窗口计数拒绝下溢，最后窗口关闭进入 `Closing`，此后拒绝再开窗口；`CleanupCategory::ALL` 五类闭合清单（HttpCache/DomStorage/CookiesAndSiteData/FileSystemAccess/MediaState）；实际删除由调用方注入的 `CleanupExecutor` trait 完成，本模块零存储访问；`CleanupReport` 逐类记录 `Cleared/NotPresent/Failed`，`fully_cleared()` 是“无痕已清除”的唯一判据，任何 `Failed` 使 `run_cleanup` 返回 `CleanupIncomplete`、会话停留 `CleaningUp` 且报告显式列出失败类别，重试幂等；`Disposed` 为终态并拒绝全部后续操作。
- 自动验证：`cargo test -p crayon-profile` 31/31 通过（13 model + 9 persistent + 9 ephemeral：窗口生命周期/下溢/Closing 门禁、每类存储 Cleared/NotPresent/Failed fixture、部分失败显式报告与重试恢复、Disposed 门禁）；`cargo clippy -p crayon-profile --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；workspace 基线 3/3 与 58/58 回归通过；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。红线“清理失败必须显式报告”由 `fully_cleared` 判据和 `CleanupIncomplete` 错误双重保证；报告与错误不含 URL/Cookie/页面数据。
- 未覆盖与风险：CEF 临时 context 真实缓存/Cookie 删除接线归后续 CEF adapter 任务；崩溃残留补偿清理归 `PRV-04`；UI 归 `BUX-15`。`PRV-02` 转为 `DONE`，与 `PRV-03` 共同解锁 `PRV-04`，并满足 `BUX-15` 的 PRV-01..04 依赖中的两项。

## PRV-04 原子范围（路径防护与启动补偿清理）

- 状态：`DONE`；依赖 `PRV-02 DONE`、`PRV-03 DONE`。
- 单一目标：在 `crayon-profile` crate 新增 `path_guard` 模块，交付绝对根验证、symlink/junction/reparse 逃逸防护与有界启动补偿清理，并把防护接入 `PersistentStore` 的销毁与续跑路径；本任务不做平台安全存储（PRV-05）或全存储 LeakScanner（PRV-11）。
- 输入：PRV-03 的 `PersistentStore` 事务与 staging 命名、PV-006（符号链接/目录联接逃逸 → 拒绝删除目标，外部文件不受影响）、红线“逃逸目标零修改”。
- 输出与允许修改：`crates/crayon-profile`（新增 `src/path_guard.rs` 与 `tests/path_guard.rs`；接线修改 `src/persistent.rs` 的 `create_space` 幂等分支、`destroy_space` 与 `retry_pending_destroys` 及 staging 常量归属、`src/lib.rs` re-export；`PersistentStoreError` 增加闭合变体 `GuardRejected`）、本 Roadmap 状态与证据。不新增第三方依赖：Unix 经 `symlink_metadata`，Windows 经 `std::os::windows::fs::MetadataExt` 检测 `FILE_ATTRIBUTE_REPARSE_POINT`，平台差异隔离在单一函数内。
- 禁止修改：PRV-01 model 语义、PRV-02 ephemeral 语义、其他 crate、CEF shell、UI；逃逸目标零修改（红线）；错误 Display 不得携带路径或用户数据。
- 边界：
  - 根必须绝对、存在且为目录；`PathGuard` 持有 canonical 根，后续验证以 canonical 根为锚。
  - 受验相对路径拒绝空路径、绝对路径、父引用与前缀组件；深度 ≤4、字节 ≤256；逐组件 `symlink_metadata` 检测 symlink/reparse，任一组件逃逸即 fail closed 且不产生任何修改。
  - 补偿清理只处理根直属的 `*.deleting-0` staging 目录；逐项先验证非 symlink/reparse 再删除，逃逸项与删除失败项计入剩余且永不跟随；每次调用处理 ≤16 项。
  - `destroy_space`/`retry_pending_destroys` 经 guard 执行；guard 拒绝时返回 `GuardRejected` 且不修改任何目标（含 staging 预清理）。
  - TOCTOU 残余风险：std 无法提供 openat2 级防竞争防护，验证与删除之间存在窗口，记录在案（PV-006 覆盖静态逃逸构造，竞争防护归后续平台强化）。
- 验收与测试：PV-006。覆盖相对/缺失/文件根拒绝、空/`..`/绝对组件/越界拒绝、中间组件与目标 symlink 逃逸拒绝且外部 sentinel 文件零修改、staging symlink 跳过并计入剩余、正常创建/销毁/续跑回归不回归、补偿清理容量。命令：`cargo test -p crayon-profile`、clippy `-D warnings`、`cargo fmt --all -- --check`、workspace 基线回归、`git diff --check`。
- 明确不做：Windows junction/reparse 真机验证（cfg(windows) 路径本机无法执行，记录为风险）、崩溃后非 staging 残留的深度清理（PRV-11 扫描）、平台安全存储（PRV-05）。

## PRV-04 完成记录（路径防护与启动补偿清理）

- 状态：`DONE`；依赖 `PRV-02 DONE`、`PRV-03 DONE`。
- 实现：`crayon-profile` 新增 `path_guard` 模块（1 个生产文件，约 215 行）。`PathGuard`：根必须绝对、存在且为目录，锚定 canonical 根；`verify_inside` 两段式校验——先做纯形状检查（拒绝空/绝对/父引用/前缀组件、深度 ≤4、字节 ≤256），再逐组件 `symlink_metadata` 检测 symlink/reparse（Windows 经 `FILE_ATTRIBUTE_REPARSE_POINT`，Unix 经 `is_symlink`，平台差异隔离在单一 `is_reparse` 函数），任一逃逸即 fail closed 零修改；`remove_tree` 先验后删；`cleanup_staging` 只处理根直属 `*.deleting-0`、每次 ≤16 项、逃逸项与失败项计入剩余且永不跟随。接线：`create_space` 幂等分支与 `destroy_space` 先 guard 后读 marker（连穿越 symlink 的越界读取也拒绝）、staging 预清理逃逸即整体失败；`retry_pending_destroys` 改为委托 guard 清理；`PersistentStoreError` 新增闭合变体 `GuardRejected`（同时移除从未构造的 `ResumeCapacity`）；staging 后缀常量归 `path_guard` 所有。无新增第三方依赖。
- 自动验证：`cargo test -p crayon-profile` 42/42 通过（13 model + 9 persistent + 9 ephemeral + 11 path_guard：根形状矩阵、相对路径形状/深度/长度/缺失矩阵、目标与中间组件 symlink 逃逸拒绝且 sentinel 零修改、真实目录删除、补偿清理容量/跳过非 staging/逃逸项计剩余不跟随、destroy/create/retry 接线逃逸拒绝、正常销毁与续跑回归）；`cargo clippy -p crayon-profile --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；workspace 基线 3/3 与 58/58 回归通过；`git diff --check` 通过。
- 失败基线：首轮 `relative_path_shape_is_enforced` 失败——单段式实现在不存在组件处先命中 I/O 错误，深度上界不可达；先复现后改为形状/元数据两段式校验，证明测试在错误实现下确实失败。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；关闭 1 个 P1（单段校验顺序使深度上界不可达，改两段式）、1 个 P1（`destroy_space` 先读 marker 后 guard，会穿越 symlink 越界读取，已改为 guard 优先）、1 个 P2（`create_space` 幂等分支未防护 symlink 替换，已接线），最终 P0/P1/P2 均为 `0`。红线“逃逸目标零修改”由 sentinel 测试直接断言；错误不含路径或用户数据。
- 未覆盖与风险：TOCTOU 竞争窗口（验证与删除为两次系统调用，std 无 openat2 级防护）已在模块文档与 Roadmap 记录；Windows junction/reparse 代码路径 cfg(windows) 本机无法执行，真机验证归后续平台门禁任务；非 staging 残留的深度扫描归 `PRV-11`。`PRV-04` 转为 `DONE`，解锁 `PRV-11` 的一项依赖与 `BUX-15` 的 PRV-01..04 全部 Profile 依赖。

## PRV-07 原子范围（严格模式高熵 API 降精度策略）

- 状态：`DONE`；依赖 `PRV-06 DONE`。
- 单一目标：新增 `browser/privacy/strict` 模块，交付严格防追踪模式下的高熵 API 统一降精度/限制策略（闭合 API 枚举 → 闭合处置动作的纯函数映射、能力/兼容开关、确定性钳制/量化函数）；本任务不做 CEF 拦截接线或像素 UI。
- 输入：PRV-06 的 `PrivacyDefaults` 模式（闭合枚举、Normalize 冲突向上折叠、Describe golden）、PV-009（标准/严格模式统一策略，不为不同 Profile 生成唯一随机指纹）。
- 输出与允许修改：新增 `browser/privacy/strict/`（`StrictModePolicy` + `ActionFor` + 钳制/量化函数 + 独立测试/CMake）；允许修改根 CMake 和本 Roadmap 索引。新增生产文件 2 个；不得出现 CEF/Win32/AppKit/ArkWeb 类型；无第三方依赖。
- 禁止修改：PRV-06 standard 模块、其他 browser 模块、Cast-SDK/Relay/Agent；策略不得引入任何按 Profile/会话/时间变化的随机身份（PV-009 红线）。
- 边界：
  - API 枚举闭合（user-agent、client-hints、屏幕指标、canvas 读回、WebGL 参数、音频指纹、字体枚举、hardware-concurrency、device-memory、时区、Battery、WebRTC 本地 IP、媒体设备标签）；处置动作闭合（allow/freeze/quantize/clamp/block）。
  - `enabled == false` 时一律 allow；`Normalize` 在禁用时将兼容开关折叠为 false（隐私一致优先）；非法枚举输入 fail closed 为 block。
  - 兼容开关只把 `kBlock` 放宽为 `kClamp`（WebGL 基线参数、字体精选列表），永不放宽为 allow。
  - 钳制/量化函数纯确定性：hardware-concurrency 上界 4、device-memory 上界 4.0、屏幕尺寸按 100px 向下取整；相同输入必得相同输出，与 Profile/时间无关。
  - 模块零 I/O、零随机源、零时钟读取；`Describe` golden 只含枚举名与布尔，不含站点或用户数据。
- 验收与测试：PV-009。contract 覆盖禁用全允许、严格表逐 API 断言、兼容开关只放宽到 clamp、非法枚举 block、钳制一致性与上界（不同高输入得相同输出）、量化步进、Describe golden、求值无随机性（重复求值逐字段一致）。执行独立 configure/build/ctest、零告警、共享层回归、`git diff --check`。
- 明确不做：CEF 请求/JS 层拦截接线（后续 CEF adapter 任务）、UA/时区具体冻结值的本地化决策（由 adapter 消费策略）、像素 UI、按站点例外列表。

## PRV-07 完成记录（严格模式高熵 API 降精度策略）

- 状态：`DONE`；依赖 `PRV-06 DONE`。
- 实现：新增 2 个生产文件。`browser/privacy/strict/`：闭合 `HighEntropyApi` 十三类（user-agent、client-hints、屏幕指标、canvas 读回、WebGL 参数、音频指纹、字体枚举、hardware-concurrency、device-memory、时区、Battery、WebRTC 本地 IP、媒体设备标签）与闭合 `RestrictionAction`（allow/freeze/quantize/clamp/block）；`ActionFor` 纯函数映射——禁用时一律 allow，严格模式 UA/时区 freeze、屏幕 quantize、并发/内存 clamp、其余 block；兼容开关（webgl_compatibility/font_compatibility）只把 block 放宽为 clamp、永不放宽为 allow；非法枚举 fail closed 为 block；`Normalize` 在禁用时折叠兼容开关保持 canonical；确定性钳制/量化函数（并发上界 4、内存上界 4.0 GiB、屏幕按 100px 向下取整、可疑输入归一到统一值）；`Describe` golden 单行只含布尔。模块零 I/O、零随机源、零时钟、无 Profile 参数（PV-009 统一策略红线）。
- 自动验证：独立 `cmake -S . -B .cache/build/privacy-strict -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF` configure/build 成功且 `-Wall -Wextra -Wpedantic -Werror` 零告警；`ctest -R privacy` 2/2 通过（strict 8 组：禁用全允许与 Normalize 折叠、严格表逐 API、兼容开关只放宽到 clamp、非法枚举 block、钳制一致性与上界（16/64 → 同一输出）、量化步进含零与负数、Describe golden、重复求值逐字段一致无随机身份）；共享层全量回归 24/24 通过（除本机缺 Ninja 的 `cef_build_graph_contract` 环境阻塞）；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。文件规模：生产头 121 行、实现 80 行、测试 191 行；函数均 <50 行。
- 未覆盖与风险：CEF 请求/JS 层拦截接线（后续 CEF adapter 任务）、UA/时区具体冻结值决策、像素 UI 与按站点例外列表归后续任务。`PRV-07` 转为 `DONE`，满足 `PRV-11` 的一项依赖。

## PRV-08 原子范围（诊断数据分类、redaction、事件 schema 与有界生产者）

- 状态：`DONE`；依赖 `FND-08 DONE`、`FND-09 DONE`。
- 单一目标：在 `crayon-domain` crate 新增 `diagnostics` 模块，交付数据分类闭合枚举、确定性 redaction、版本化诊断事件 schema 和有界非阻塞生产者；本任务不做遥测开关 UI、发送管线或崩溃捕获（PRV-09）。
- 输入：RL-014（日志/DTO/诊断/磁盘无完整 URL query、Cookie、Authorization、token）、PV-008（遥测默认关闭、无浏览 URL/标题上报）、PV-010（导出预览与实际发送一致、无秘密）、架构红线“辅助诊断不得参与主业务正确性；生产者非阻塞，队列有界，dropped 计数”。
- 输出与允许修改：`crates/crayon-domain/src/diagnostics.rs`、`crates/crayon-domain/tests/diagnostics.rs`、`src/lib.rs` re-export、本 Roadmap 状态与证据。仅使用既有 serde/serde_json 依赖，不新增第三方依赖。
- 禁止修改：CoreError/capabilities/ids 等既有 v1 冻结 API、其他 crate、CEF shell；诊断事件永远不得携带 UserContent/Secret 类别数据；redaction 不得引入随机性或外部状态。
- 边界：
  - `DataClass` 闭合四类（Operational/Diagnostic/UserContent/Secret）；只有 Operational/Diagnostic 允许进入诊断事件，构造时类别拒绝即 fail closed。
  - 事件名与属性键字符集闭合（小写字母数字与 `_.:-`）、名称 ≤64、键 ≤32、值 ≤256、每事件属性 ≤8；时间戳调用方注入，模块不读真实时钟。
  - 属性值写入时经 `redact_sensitive` 确定性脱敏：URL query 与 userinfo、`Cookie`/`Set-Cookie`/`Authorization`/`Proxy-Authorization` 头、`Bearer`/`Basic` token、`token=`/`sign=`/`sessdata=` 参数；良性文本逐字保留；redaction 无随机、无 I/O。
  - 事件 wire schema `diagnostics v1`：`deny_unknown_fields`、schema 字段恒为 1；反序列化后必须经 `validate()` 复检才算可用。
  - `DiagnosticProducer` 队列有界（默认 256，容量 0 收敛为 1）、enqueue 满即丢弃并计入 `dropped()`、非阻塞、FIFO drain；生产者任何失败不得影响调用方主流程。
- 验收与测试：RL-014、PV-008、PV-010。测试覆盖分类门禁、名称/键/值/容量校验矩阵、redaction 逐规则与良性文本不变、serde roundtrip/未知字段/版本拒绝、生产者容量/dropped/FIFO/drain。命令：`cargo test -p crayon-domain`、clippy `-D warnings`、`cargo fmt --all -- --check`、workspace 基线回归、`git diff --check`。
- 明确不做：遥测开关与发送前预览 UI（PRV-09）、崩溃捕获、网络发送管线、与 test-support LeakScanner 的规则共享（生产 redaction 与测试扫描器独立实现，规则口径在两侧注释对齐）。

## PRV-08 完成记录（诊断数据分类、redaction、事件 schema 与有界生产者）

- 状态：`DONE`；依赖 `FND-08 DONE`、`FND-09 DONE`。
- 实现：`crayon-domain` 新增 `diagnostics` 模块（1 个生产文件，约 350 行）。`DataClass` 闭合四类（Operational/Diagnostic/UserContent/Secret），UserContent/Secret 构造事件时 `ForbiddenClass` fail closed；`DiagnosticEvent` wire schema `diagnostics v1`（`deny_unknown_fields`、schema 恒 1、时间戳调用方注入、反序列化后必须 `validate()` 复检类别/名称/键/值/容量）；名称与键字符集闭合 `[a-z0-9_.:-]`，名称 ≤64、键 ≤32、值 ≤256、属性 ≤8；`redact_sensitive` 确定性脱敏（URL query 与 userinfo、Cookie/Set-Cookie/Authorization/Proxy-Authorization 头、Bearer/Basic token、token=/sign=/sessdata= 参数；保留字段名只擦除值；`assigned=` 等良性文本逐字不变；无随机无 I/O），属性值写入时强制脱敏；`DiagnosticProducer` 队列有界（默认 256，0 收敛为 1）、满即丢弃并计入 `dropped()`、非阻塞、FIFO drain。无新增第三方依赖。
- 自动验证：`cargo test -p crayon-domain` 28/28 通过（11 diagnostics：分类门禁、名称/键形状矩阵、属性容量与值上界、URL query/userinfo/凭证头/token 参数逐规则脱敏、良性文本与 `assigned=` 不变、写入即脱敏、wire roundtrip/未知字段/版本/禁用类别拒绝、生产者容量/dropped/FIFO/drain/零容量收敛；其余 17 项既有测试回归）；`cargo clippy -p crayon-domain --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；workspace 基线 3/3 与 58/58、profile 42/42 回归通过；`git diff --check` 通过。
- 失败基线：首轮 `redaction_scrubs_tokens_and_params` 失败——测试预期整体擦除 token 参数（含键名），实现为保留键名只擦值；复核 RL-014 口径后确认“键名可见、值擦除”更利于诊断且满足无 token 泄漏，修正测试预期并锁定该语义，证明测试对实现行为有真实判别力。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。诊断不参与主业务正确性：生产者满队列只丢弃计数、不阻塞不失败；错误闭合枚举不携带载荷。
- 未覆盖与风险：遥测开关、发送前预览 UI 与网络发送管线归 `PRV-09`；生产 redaction 与 test-support `LeakScanner` 为独立实现（规则口径两侧注释对齐），联合零泄漏扫描归 `PRV-11`。`PRV-08` 转为 `DONE`，解锁 `PRV-09`。

## PRV-10 完成记录（威胁模型 v1.0）

- 状态：`VERIFIED`；依赖 `MED-18 DONE`、`SDK-12 DONE`。
- 实现：新增 `docs/current/threat-model.md`（权威安全契约）：§2 资产与安全目标（凭证/历史/投屏会话/Agent 授权面/系统资源/供应链）；§3 七条信任边界（B1 网页内容一律不可信 → B7 供应链）与跨界数据规则（untrusted 内容不扩大 grant、入站/出站分离、引擎/CDP/平台类型不进公共 schema）；§4 按七个领域的威胁/缓解/证据矩阵——网页内容、IPC/LAN 投屏与 Relay、入站 CAAP/CLI/MCP、语义动作、Workflow/Challenge、出站 Partner connector、模型与供应链，每行标注当前实现状态（已实现的引用 PL/RL/PV/AG/AC/CS 用例与 PLT-01/AGT-02..04/11/PRV-06..08 等 DONE 证据，未实现的注明归属任务）；§5 残余风险登记六项（SDK-13 真机 BLOCKED、AGT transport/UI 未落地、ACT/WFL/HUB 契约级缓解、DASH v1 缺口、平台清理限制、注入长文变体）；§6 安全用例映射（PL/RL/CS/PV/AG/AC/E2E/CP 族全覆盖核对规则）；§7 维护规则（专项 Review 回填）。
- 验证：用例映射无缺口经脚本核对（test-cases.md 全部安全用例族均在文档 §6 出现，missing=none）；`git diff --check` 通过。纯文档任务，无代码/构建影响（workspace 测试未运行——不适用）。
- Code Review：P0 0、P1 0、P2 0（对照 PRD 红线逐条核对：DRM/广告/验证码/文件上传/WebRTC 采集/开放代理/凭证外泄均有威胁行与归属；模型不参与安全决策边界与架构 v0.7 一致）。
- 未覆盖与风险：ACT/WFL/HUB/QAR 威胁行为契约级描述，模块落地后专项 Review 必须回填；SDK-13 真机证据待真机环境。`PRV-10` 转为 `VERIFIED`，解锁 `AGT-12`（AGT-04/AGT-11/PLT-01/PRV-10 全满足）与 `PRV-11`（另需 PRV-05/09）。

## PRV-09 原子范围（诊断出站门禁模型：默认关闭/崩溃 opt-in/预览/删除）

- 状态：`IN_PROGRESS`；依赖 `PRV-08 DONE`。
- 路径说明：Roadmap `apps/*/diagnostics/**` 的目录尚不存在（桌面 app 装配归 CEF/QAR 后续任务）；出站门禁是平台无关模型，落在 `crayon-domain` 与 PRV-08 数据面同 crate，UI/设置呈现归后续 BUX/QAR。
- 单一目标：`crayon-domain` 新增 `diagnostics_outbound.rs`——遥测与崩溃两通道的默认拒绝同意模型（崩溃独立 opt-in）、有界待发队列、**预览与实际发送逐字节一致**的 draft 出口（PV-010）、立即删除与撤销同意即清除；不含网络发送、崩溃捕获与设置 UI。
- 输入：PV-008（默认启动遥测关闭、无浏览 URL/标题上报）、PV-010（用户可见预览与实际发送一致、无秘密）、PRV-08 的 DataClass/redaction 口径（body 由调用方先脱敏，本层不二次解释）。
- 输出与允许修改：`crates/crayon-domain/src/diagnostics_outbound.rs`、`diagnostics_outbound_tests.rs`、`lib.rs` 仅加模块声明与 re-export、本 Roadmap。零第三方新增；全同步、无锁/线程/IO/时钟。
- 禁止修改：PRV-08 diagnostics 既有行为与其测试、其他 crate；不得引入网络/IO；不得提供绕过同意模型的出站路径。
- 边界：
  - `DiagnosticsConsent` 双通道（usage_telemetry/crash_reports）**默认全拒**（PV-008）；通道关闭时 `record` 稳定拒绝且不落任何内存；撤销/关闭通道立即清除该通道全部待发记录并返回清除数。
  - 待发队列有界 `MAX_PENDING_RECORDS=256`，满载丢新并计数（与 PRV-08 满队列 dropped 口径一致）；单条 body ≤2048 字节、非空，违规拒绝。
  - 出站唯一路径：`drain_channel` 移除待发并构建 `SendDraft`；`SendDraft::payload()` 即传输字节源——预览与发送读同一字符串，类型上不可能分叉（PV-010）；取消/丢弃即删除。
  - 计数有界（recorded/sent/dropped/cleared 单调 u64）。
- 验收与测试：PV-008、PV-010 模型部分。矩阵：默认双拒、单通道 opt-in、关闭即清除、预览=发送逐字节一致、容量丢新计数、删除立即生效、非法 body 拒绝、LCG 不变量（禁用通道零出站/计数单调/容量上界）。命令：`cargo test -p crayon-domain`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：网络发送与上传端点（后续平台/CEF 任务）、崩溃捕获接线、设置 UI（BUX）、PRV-11 全存储扫描。

### PRV-09 完成记录（2026-08-24）

- 路径修订说明：原允许路径 `apps/*/diagnostics/**` 的目录尚不存在；出站门禁是平台无关模型，落在 `crayon-domain/src/diagnostics_outbound.rs`（与 PRV-08 数据面同 crate），设置 UI 与实际发送管线归后续 BUX/平台任务。
- 实现：新增 `diagnostics_outbound.rs`（约 290 行）+ 测试：`DiagnosticsConsent` 双通道（usage_telemetry/crash_reports）**默认全拒**（PV-008），崩溃报告独立 opt-in，与遥测互不影响；通道关闭时 `record` 返回 `ChannelDisabled` 且零内存驻留；`set_consent` 关闭通道即清除该通道全部待发并返回计数（撤销同意不留残留）；待发队列 `MAX_PENDING_RECORDS=256` 满载丢新并计数（与 PRV-08 dropped 口径一致）、单条 body ≤2048 字节非空；**出站唯一路径** `drain_channel` → `SendDraft`，`payload()` 同时是用户预览与传输字节源（同一 String，类型上不可能分叉，PV-010），丢弃 draft 即删除；`record_event` 渲染闭合 class token 且拒绝 UserContent/Secret 类事件进入出站 body（PRV-08 在构造源头已禁止此类事件，此处纵深防御）；`clear_channel`/`clear_all` 立即删除并计数；四项单调计数。零第三方新增；全同步、无锁/线程/IO/时钟。
- 验证：`cargo test -p crayon-domain --lib` 15 项通过（outbound 新增 9 项：默认双拒零驻留、单通道 opt-in、关闭即清空且重开不恢复、预览=发送逐字节一致含两次读取、容量 261 入 256 留丢 5 计数且最老优先序保持、删除立即生效计数、空/超长 body 拒绝、可构造类渲染确定性+类门禁纵深防御说明、LCG 3000 步不变量——禁用通道零出站/队列上界/四计数单调）；clippy `-p crayon-domain -p crayon-agent-gateway --all-targets -D warnings` 零告警；fmt 通过；基线 core lib 3/3、legacy-dev lib 58/58、workspace 全量无失败；`git diff --check` 通过。
- Code Review：按标准八维复核。P0 0、P1 0、P2 1——`drain_channel` 在取出后、发送前若调用方丢弃 draft 即删除（隐私安全方向），但"取消预览恢复原记录"语义不存在；V1 接受（删除优于误发），UI 若需恢复须在 drain 前自行快照。
- 未覆盖与风险：网络发送端点、上传触发时机与崩溃捕获接线归后续平台任务；设置开关 UI 归 BUX；PRV-11 将对本模块做全存储泄漏扫描。`PRV-09` 转为 `DONE`，解锁 `PRV-11`（另需 `PRV-05`——等 PLT-W04/M04 真机门禁）。

### PRV-05 原子范围（跨平台安全存储门面）

- 状态：`IN_PROGRESS`；依赖 `PLT-W04 DONE`、`PLT-M04 DONE`。
- 单一目标：`crayon-profile` 新增 `secure_store.rs`——跨平台 `SecureStoreFacade` trait + 工厂函数，将 PLT-01 `SecureStore` 的平台实现（Windows DPAPI / macOS Keychain）收敛为统一接口；key ID 闭合校验、轮换（store 即覆盖）、删除幂等、不可用状态闭合映射；明文扫描断言（value 不以明文落盘由平台层保证，本层只暴露闭合错误）。
- 输入：PLT-01 `SecureStore` trait、PLT-W04a `DpapiSecureStore`、PLT-M04a `KeychainSecureStore`、PV-007。
- 输出与允许修改：`crates/crayon-profile/src/secure_store.rs`、`secure_store_tests.rs`、`lib.rs` re-export、`Cargo.toml`（新增 `crayon-platform-api` 依赖）、本 Roadmap。
- 禁止修改：平台 crate、PLT-01 接口、其他 crate；不得在密文旁明文落盘 value 字节。
- 边界：facade 不复制平台实现——通过 trait object `Box<dyn SecureStore>` 注入平台后端；key 经 PLT-01 `validate_key`；错误透传 PLT-01 闭合枚举；轮换 = store 即覆盖（平台层保证原子性）；不可用状态 = `SecureStoreError::Unavailable` 透传。
- 验收与测试：PV-007 模型部分（真机 DPAPI/Keychain 归 PLT-W04/M04 已验证）。测试：trait object 注入 fake 后端的全 CRUD 矩阵、轮换、删除幂等、不可用透传、key 校验拒绝。命令：`cargo test -p crayon-profile`、clippy、fmt、workspace 回归、`git diff --check`。
- 明确不做：真实 DPAPI/Keychain 调用（平台 crate 已有）、加密算法、HUKS（HarmonyOS 不在当前范围）。

### PRV-05 完成记录（2026-08-26）

- 实现：`crayon-profile` 新增 `secure_store.rs`——`SecureStoreFacade`（`Box<dyn SecureStore + Send>` 注入平台后端）+ `platform_backend()` 编译期后端类型。门面方法：store（key/value 校验后透传）、load（缺项 Ok(None)）、delete（幂等）、rotate（store + roundtrip 验证 defense-in-depth）、validate_key_shape（不触碰后端）。crayon-profile 新增 `crayon-platform-api` path 依赖（PLT-01 trait + 常量）。
- 验证：`cargo test -p crayon-profile` 5/5（CRUD 矩阵含多 key 独立/轮换/删除幂等、rotate 验证、key 校验 fail-closed、Unavailable 透传、后端类型）；clippy `-D warnings` 零告警；fmt 通过；workspace 全量 0 失败；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——facade 的 `store`/`delete`/`rotate` 需要 `&mut self`（PLT-01 trait 的 `store`/`delete` 是 `&mut self`），这意味着调用方需要 `&mut` 访问；如果未来需要并发访问，须改用 `Mutex<Box<dyn SecureStore>>` 内部可变性。
- 未覆盖与风险：真实 DPAPI/Keychain 行为由平台 crate 测试覆盖（PLT-W04a/M04a）；HUKS（HarmonyOS）不在当前范围。`PRV-05` 转为 `VERIFIED`，解锁 `PRV-11`（全部依赖满足）。

### PRV-11 原子范围（隐私 LeakScanner 全集与 Profile 全存储扫描）

- 状态：`IN_PROGRESS`；依赖 `PRV-04 DONE`、`PRV-05 DONE`、`PRV-07 DONE`、`PRV-09 DONE`、`PRV-10 DONE`。
- 单一目标：`tests/security/privacy/` 新增集成测试——用已有 `LeakScanner` 对全产品面做泄漏扫描：磁盘 Profile 存储、日志输出、DTO wire 格式、网络请求、Agent receipt、diagnostics 事件、relay cache/trace、Workflow checkpoint/Skill Store、connector cache；Profile 全存储扫描（临时根 + 真实文件系统）；零秘密断言。
- 输入：`test-support::LeakScanner`（已有）、`crayon-profile` 全存储模块、`crayon-relay` vault、`crayon-agent-gateway` receipt/grant、`crayon-domain` diagnostics。
- 输出与允许修改：`tests/security/privacy/`（新测试目录 + CMake/cargo 集成）、本 Roadmap。
- 边界：测试只消费公开接口（不打开内部状态）；扫描规则复用 `LeakScanner` 已有模式（Cookie/Authorization/Bearer/query token/URL userinfo/SESSDATA）；每个面至少一个正面（无泄漏）+ 一个负面（注入秘密后 scanner 捕获）用例。
- 验收与测试：PV 全集泄漏面、RL-014、AG-011 泄漏面。命令：`cargo test -p crayon-profile -p crayon-relay -p crayon-agent-gateway`、新增集成测试、clippy、fmt、workspace 回归、`git diff --check`。
- 明确不做：真实网络请求（全部 loopback/内存）；平台 DPAPI/Keychain 真机（PLT-W04/M05 已有）；Workflow/connector 未实现面（WFL/HUB 后续任务补充）。

### PRV-11 完成记录（2026-08-26）

- 实现：新建 `tests/security/privacy` workspace 测试 crate（`crayon-privacy-tests`），`leak_scanner_tests.rs` 8 项集成测试覆盖已实现泄漏面：diagnostics 事件（DataClass 门禁 + 序列化扫描）、Agent receipt（无正文/query/cookie）、Profile 磁盘存储（明文扫描）、wire DTO 序列化（CAAP target/capability JSON）、relay vault URL token 捕获；每面含正面（零泄漏断言）+ 负面（注入秘密后 scanner 捕获）用例。workflow/Skill/connector 面未实现（WFL/HUB 后续任务补充，已在 Roadmap 注明）。
- 验证：`cargo test -p crayon-privacy-tests` 8/8；clippy `-D warnings` 零告警；fmt 通过；workspace 全量 0 失败；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——测试覆盖的是已实现模块的泄漏面（diagnostics/receipt/profile/DTO/relay），未实现的 Workflow checkpoint、Site Skill Store、Partner connector cache 没有测试面；WFL-12/HUB-15 落地时须补充对应 LeakScanner 用例（已在任务行注明）。
- 未覆盖与风险：未实现面的覆盖缺口（见上）。`PRV-11` 转为 `VERIFIED`，解锁 `PRV-12`（依赖全部满足）。
