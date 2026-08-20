# PRV：隐私与产品安全 Roadmap

状态：`FND-08 DONE`，`CEF-04/CEF-05 DONE`，`PRV-01 DONE`。Relay 网络安全实现归 MED，本 Roadmap 负责 Profile、追踪防护、安全存储，以及页面数据、Agent、Workflow/Challenge、Capability Hub/Partner connector 的隐私数据流和系统级安全门禁。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| PRV-01 | FND-08,CEF-04 | `crayon-profile/model` | Profile ID/type/path/lifecycle 状态机，随机目录 ID | PV-001、PV-004；非法状态/重复关闭 | S1 |
| PRV-02 | PRV-01 | `crayon-profile/ephemeral` | 临时 context、最后窗口关闭、清理清单与结果 | PV-001、PV-002、PV-003；每类存储 fixture | S3 |
| PRV-03 | PRV-01 | `crayon-profile/persistent` | 常用空间创建/隔离/销毁事务 | PV-004、PV-005；部分失败/重试 | S3 |
| PRV-04 | PRV-02,PRV-03 | `crayon-profile/path_guard` | 绝对根验证、symlink/junction/reparse 防护、启动补偿清理 | PV-006；逃逸目标零修改 | S2 |
| PRV-05 | PLT-W04,PLT-M04 | `crayon-profile/secure_store` | Windows/macOS 安全存储接口、key ID、轮换/删除/不可用状态 | PV-007；明文扫描；错误映射 | S4 |
| PRV-06 | CEF-05,FND-11 | `browser/privacy/standard` | 第三方 Cookie、存储分区、Referer、HTTPS、权限默认 | PV-008、PV-009；兼容 fixture | S3 |
| PRV-07 | PRV-06 | `browser/privacy/strict` | 高熵 API 统一降精度/限制，能力/兼容开关 | PV-009；熵/兼容；无每 Profile 随机身份 | S3 |
| PRV-08 | FND-08,FND-09 | `crayon-domain/diagnostics` | 数据分类、redaction、事件 schema、bounded producer | RL-014、PV-008、PV-010；满队列 dropped | S2 |
| PRV-09 | PRV-08 | `apps/*/diagnostics` | 默认关闭遥测、崩溃 opt-in、发送前预览、删除 | PV-008、PV-010；实际 payload 对照 | S3 |
| PRV-10 | MED-18,SDK-12 | `docs/current/threat-model.md` | 资产/信任边界/威胁/缓解/残余风险，覆盖网页、IPC/LAN、入站 CAAP/MCP、语义动作、Workflow/Challenge、出站 connector、模型与供应链；模块实现后的专项 Review 继续增补 | 安全用例映射无缺口；专项 Review | S0 |
| PRV-11 | PRV-04,PRV-05,PRV-07,PRV-09,PRV-10 | `tests/security/privacy` | 磁盘/日志/DTO/网络/receipt/cache/trace/checkpoint/Skill/connector LeakScanner 与 Profile 全存储扫描 | PV 全集、RL-014、AG-011、适用 WF/HB；零秘密 | S3 |
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
