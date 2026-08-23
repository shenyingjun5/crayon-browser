# AGT CAAP / CLI / MCP Agent 访问 Roadmap

- 状态：规划完成，尚未开工
- 任务数：16
- 目标：用自有 CAAP、入站 tool registry 和 capability guard，为 AI Agent 提供高性能读页，并把受控操作接到 `ACT` 的可验证语义动作运行时
- 非目标：远程控制、原始 CDP/WebDriver、任意 JavaScript、Cookie/凭证、密码/支付、文件上传、任意文件/网络工具

## 1. 架构边界

- `crayon-agent-gateway` 只编排协议、工具、授权、确认、任务代际和 receipt；实际行为调用 app-runtime 正常用例。
- CLI 使用当前用户可访问的本机 IPC；MCP 是 loopback CAAP adapter；二者共享 registry、错误、取消、幂等和安全门禁。
- 页面、模型、工具结果和 client 输入都不可信，不能生成/扩大 grant 或触发第二工具。
- R0/R1 先交付；R2/R3 后交付；R4 只有专项安全 Review GO 才进入 Preview。
- Agent 页面读取复用正式 page data/Markdown，不建立基于截图/OCR或外部自动化的第二数据面。
- 本 Roadmap 只负责 Agent 入站访问；Partner API/MCP 的出站连接由 `HUB` Roadmap 负责，两类 MCP 不共享 session、token、registry 或权限。
- Action/Form/Media/Risk Map、action_id、多信号 locator、前置条件和效果验证由 `ACT` Roadmap 拥有，AGT 只把它们以 CAAP 工具接入。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 单一交付 | 验收与测试 | 阶段 |
|---|---|---|---|---|---|---|
| AGT-01 | DONE | FND-08,PRV-08 | `crayon-domain/agent/**`,`crayon-ipc-schema/**`,`docs/current/**` | 冻结 CAAP v1 envelope、握手、版本/能力、target、stream、cancel、deadline、错误和 previous/current golden | `AG-001`; schema/compat/fuzz；无 OS/CEF/SDK 类型 | A0 |
| AGT-02 | DONE | AGT-01 | `crayon-domain/agent/**`,`crayon-agent-gateway/registry/**` | Tool/capability/risk R0～R4 registry 与永久禁止清单 | `AG-001`,`AG-015`; registry snapshot | A0 |
| AGT-03 | DONE | AGT-01,FND-09 | `crayon-agent-gateway/session/**` | client/task/session/target/generation、取消、超时、幂等和有界队列状态机 | `AG-002`; unit/property | A0 |
| AGT-04 | VERIFIED | AGT-02,AGT-03,PRV-08 | `crayon-agent-gateway/grant/**` | 单次/任务/App 会话 grant、Profile 隔离、撤销和目标变化失效 | `AG-003`,`AG-005`; default deny | A0 |
| AGT-05 | TODO | AGT-04,CEF-08 | `apps/desktop-cef/**/agent-confirm/**`,locales | 确认 UI：client、工具、route、目标、参数摘要、数据披露、到期和无障碍 | `AG-004`; UI integration | A0 |
| AGT-06 | TODO | CNT-03,AGT-03 | `crayon-page-data/**`,`crayon-agent-gateway/page_stream/**` | generation-scoped 快照缓存、分页/流式/增量、索引、背压和性能 instrumentation | `AG-006`,`AG-015`; benchmark/soak | A1 |
| AGT-07 | TODO | AGT-04,AGT-06,CNT-08 | `crayon-agent-gateway/tools/content/**`,`crayon-app-runtime/**` | R1 target/标题/选区/结构化页面/Markdown 读取工具 | `AG-006`; 跨 Profile/后台/过期/超量拒绝 | A1 |
| AGT-08 | TODO | AGT-04,SDK-08 | `crayon-agent-gateway/tools/cast_read/**` | R0/R1 接收端能力和投屏状态读取，不返回 IP/URL/token | `AG-007`; adapter tests | A1 |
| AGT-09 | TODO | AGT-05,CEF-07,ACT-07,ACT-11 | `crayon-agent-gateway/tools/navigation/**`,`crayon-app-runtime/**` | R2 打开/切换/关闭标签、导航、后退、刷新、滚动及人工接管结果 | `AG-008`; scheme/redirect/download/popup/cancel | A2 |
| AGT-10 | TODO | AGT-05,SDK-12,MED-19 | `crayon-agent-gateway/tools/cast_control/**` | R3 选择设备、开始/暂停/seek/停止；沿用正常投屏门禁 | `AG-009`; 目标变化重确认；不控制外部镜像客户端 | A2 |
| AGT-11 | VERIFIED | AGT-03,AGT-04 | `crayon-agent-gateway/receipt/**`,diagnostics | 有界脱敏 action receipt、TTL、用户预览/清除 | `AG-011`,`PV-010`; 无正文/query/secret | A0 |
| AGT-12 | TODO | AGT-04,AGT-11,PRV-10,PLT-01 | `apps/desktop-cef/agent-transport/**`,`crayon-platform-api/**` | Windows named pipe/macOS UDS CAAP transport；当前用户 ACL、限流、单客户端、stop | `AG-012`; 恶意本机 client/replay/oversize | A1 |
| AGT-13 | TODO | AGT-05,AGT-07,AGT-08,AGT-12 | `apps/agent-cli/**`,docs/tests | R0/R1 CLI Developer Preview；机器可读结果、版本、cancel | `AG-013`; 无交互不绕确认 | A1 |
| AGT-14 | TODO | AGT-05,AGT-07,AGT-08,AGT-12 | `apps/mcp/**`,MCP contracts | 只读 MCP Developer Preview，将 initialize/list/call/cancel 映射到 CAAP | `AG-014`; schema 同源、loopback only | A1 |
| AGT-15 | TODO | AGT-06,AGT-09,AGT-10,ACT-12 | `crayon-agent-gateway/tools/semantic/**`,`tests/security/agent/**`,`tests/perf/agent/**` | 把 R4 Action Map/action_id/effect 接入 CAAP，并完成提示注入/fuzz/恶意 client/性能专项 | `AG-005`,`AG-010`,`AG-015`; 不复制 locator/runtime；永久禁止 surface 零命中 | A2 |
| AGT-16 | TODO | AGT-09,AGT-10,AGT-13,AGT-14,AGT-15,ACT-12 | threat model,Review,`docs/current/**` | CAAP/CLI/入站 MCP 总 Review、数据流、benchmark、默认开关与 GO/NO-GO | 全 AG、适用 AC；P0/P1=0；独立发布决策 | A2 |

## 3. 垂直切片

1. `A0 权限内核`：`AGT-01..05,11`，不开 transport。
2. `A1 只读 Preview`：`AGT-06..08,12..14`，提供高性能 R0/R1。
3. `A2 受控操作 Preview`：`AGT-09,10,15,16`，R2～R4 与专项安全/性能门禁。

## 4. 性能门禁

- 已有缓存的标题/元数据 R1 本机 P95 目标 ≤50ms。
- 100KB 清洗内容的结构化快照/Markdown P95 目标 ≤500ms。
- 记录 first chunk、complete、CPU、RSS、序列化字节、UI event-loop delay 和增量复用率。
- benchmark 使用本地固定 fixture 和固定硬件记录；未完成对照实验前不得宣传“快于某浏览器”。
- 队列、snapshot cache、未确认 chunk、并发 task 和 receipt 全部有界；取消后资源可验证释放。

## 5. Review 专项

- MCP/CLI client 不能替代服务端确认；服务端始终 fail closed。
- 检查 confused deputy、跨 Profile、target 替换、TOCTOU、重放、间接提示注入和旧结果副作用。
- Release 扫描 remote bind、CDP/WebDriver、任意脚本、Cookie API、密码/支付、文件上传和通用文件/网络工具。
- Agent 功能可以单独 NO-GO，不阻塞浏览器/投屏核心；但不能以 debug 后门代替正式 Preview。

## AGT-01 原子范围（CAAP v1 envelope 与握手冻结）

- 状态：`DONE`；依赖 `FND-08 DONE`、`PRV-08 DONE`。
- 单一目标：冻结 CAAP v1 的 wire 契约——envelope（hello/welcome/request/chunk/cancel/error_reply 闭合六种）、握手与版本/能力协商、target、stream chunk、cancel、deadline、稳定错误码和 previous/current golden；本任务不开 transport、不做 registry/session 状态机（AGT-02/03）。
- 输入：架构 §CAAP 边界（CLI 本机 IPC、MCP loopback adapter、共享握手/工具/错误/取消/幂等/generation 语义）、AG-001（版本协商、R0～R4、错误、chunk/cancel/deadline 稳定；永久禁止能力不可表达）、FND-08 的 golden/previous 窗口机制与 PRV-08 的数据分类。
- 输出与允许修改：`crates/crayon-domain/src/agent.rs`（`AgentTarget`/`AgentCapability`/`RiskLevel`/`CaapError` + 校验）、`crates/crayon-ipc-schema/src/caap.rs`（六个 envelope 消息 + 边界）、`schemas/current/caap_*.json` 与 `schemas/previous/caap_*.json` golden、`crates/crayon-domain/tests/agent.rs`、`crates/crayon-ipc-schema/tests/caap_v1_contract.rs`、`docs/current/caap-v1.md` 与 README 索引行、本 Roadmap 状态。仅使用既有 serde/serde_json 依赖。
- 禁止修改：FND-08 已冻结的 v1 消息与 golden（只新增不改动）、CoreError、其他 crate、CEF shell；不得出现 OS/CEF/SDK 类型；不得引入远程监听或 transport 代码。
- 边界：
  - `AgentCapability` 闭合五类（page_read/navigation/cast_read/cast_control/semantic_action）；永久禁止能力（原始 CDP、任意 JavaScript、Cookie/凭证、密码/支付、文件上传、任意文件/网络）在类型上不可表达，golden 键集合契约锁定。
  - `RiskLevel` 闭合 R0～R4；`AgentTarget` 闭合（`tab(TabId)` / `active_tab`）；`CaapError` 闭合稳定码（版本不支持/能力拒绝/target 无效或过期/取消/deadline/队列满/未授权/消息非法），wire 为 snake_case 字符串。
  - 消息边界：client/tool 名为闭合字符集 token ≤64；request 参数 ≤16 项、键 ≤32、值 ≤1024；chunk `data` ≤4096 字节、`seq` 单调由 session 层（AGT-03）校验、schema 层只冻结字段；deadline 为调用方注入 epoch ms；`deny_unknown_fields` 全覆盖；schema 版本复用 FND-08 非零 `SchemaVersion`。
  - golden：current 与 previous 各 6 个向量逐字节一致（v1 为首个版本，previous 镜像 current 直到 v2）。
- 验收与测试：AG-001。golden roundtrip、previous 窗口兼容、未知字段/零版本/越界拒绝、能力闭合与永久禁止不可表达、错误码 golden 锁定、确定性伪 fuzz（golden 字节确定性变异/截断输入只返回错误不 panic）。命令：`cargo test -p crayon-domain -p crayon-ipc-schema`、clippy `-D warnings`、`cargo fmt --all -- --check`、workspace 基线回归、`git diff --check`。
- 明确不做：transport（AGT-12）、tool registry（AGT-02）、session/取消/幂等状态机（AGT-03）、grant（AGT-04）、确认 UI（AGT-05）。

## AGT-01 完成记录（CAAP v1 envelope 与握手冻结）

- 状态：`DONE`；依赖 `FND-08 DONE`、`PRV-08 DONE`。
- 实现：`crayon-domain` 新增 `agent` 模块（1 个生产文件，约 150 行）：`AgentCapability` 闭合五类（page_read/navigation/cast_read/cast_control/semantic_action）与 R0～R4 风险映射、`AgentTarget` 闭合（tab(TabId)/active_tab）、`CaapError` 闭合十码（wire snake_case，Display 不参与契约）。`crayon-ipc-schema` 新增 `caap` 模块（1 个生产文件，约 390 行）：闭合六种 envelope（`CaapHello`/`CaapWelcome`/`CaapRequest`/`CaapChunk`/`CaapCancel`/`CaapErrorReply`），全部 `deny_unknown_fields`、构造校验 + 解码后 `validate()` 复检；token 字符集 `[a-z0-9_.:-]` ≤64、参数 ≤16 项/键 ≤32/值 ≤1024、chunk ≤4096 字节、能力 ≤8；schema 版本复用 FND-08 非零 `SchemaVersion`。golden：`schemas/current` 与 `schemas/previous` 各新增 6 个向量（v1 首版逐字节镜像）。文档：新增 `docs/current/caap-v1.md` 并登记 README 索引。无新增依赖、无 OS/CEF/SDK 类型、无 transport 代码。
- 自动验证：`cargo test -p crayon-domain -p crayon-ipc-schema` 49/49 通过（agent 5 组：能力闭合与 wire 名锁定、13 个永久禁止能力名不可反序列化、风险映射闭合、target wire/非法拒绝、错误码集合锁定与 roundtrip；caap 5 组：current/previous golden roundtrip 与逐字节镜像、未知字段六消息全拒、零版本拒绝、边界矩阵、固定种子 LCG 变异/截断 1200 样本解码零 panic；其余既有测试回归）；`cargo clippy -p crayon-domain -p crayon-ipc-schema --all-targets -- -D warnings` 零告警；`cargo fmt --all -- --check` 通过；workspace 基线 3/3 与 58/58、profile 42/42 回归通过；`git diff --check` 通过。
- Code Review：按需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性复核；P0/P1/P2 均为 `0`。永久禁止能力在类型层不可表达由黄金测试锁定；`CaapWelcome` 不携带任何 session 材料（grant 归 AGT-03/04）；参数值凭证禁令写入模块文档与契约文档。
- 未覆盖与风险：transport（`AGT-12`）、tool registry（`AGT-02`）、session/取消/幂等状态机（`AGT-03`）、grant（`AGT-04`）、确认 UI（`AGT-05`）归后续任务；v2 起 previous 窗口演进规则已写入契约文档。`AGT-01` 转为 `DONE`，解锁 `AGT-02`、`AGT-03`。

## AGT-02 原子范围（Tool/capability/risk registry 与永久禁止清单）

- 状态：`DONE`；依赖 `AGT-01 DONE`。
- 单一目标：新建 `crates/crayon-agent-gateway`，交付 v1 工具 registry（闭合 ToolSpec：名称/所需能力/风险级/确认要求/幂等/流式/参数表）与永久禁止清单；registry 只做声明与查询，不做调度、grant 或 session（AGT-03/04）。
- 输入：AG-001（tool/risk schema 稳定）、AG-015（永久禁止 surface 零命中）、架构红线（无原始 CDP/任意 JS/Cookie/密码支付/文件上传/任意文件网络）、Roadmap 垂直切片（R0/R1 先交付，R4 仅专项 Review 后 Preview）。
- 输出与允许修改：`crates/crayon-agent-gateway/**`（registry 模块 + 契约测试 + snapshot golden）、`crates/crayon-domain/src/agent.rs`（仅追加 `AgentCapability`/`RiskLevel` 的 `wire_name()` 常量方法）、根 `Cargo.toml` members、本 Roadmap 状态。无新增第三方依赖。
- 禁止修改：AGT-01 已冻结的 CAAP wire 消息与 golden、其他 crate、CEF shell；registry 不得包含永久禁止工具或自由文本参数 schema。
- 边界：
  - v1 工具集冻结 20 个：page_read 5（page.list_targets/get_title/get_selection/snapshot/markdown）、cast_read 2（cast.list_receivers/get_state，均为 R0）、navigation 7（nav.open_tab/switch_tab/close_tab/navigate/go_back/reload/scroll）、cast_control 5（cast.select_receiver/start/pause/seek/stop）、semantic_action 1（act.invoke，PreviewGated）。
  - 每个工具的 risk 与其 capability 的 `risk_level()` 一致（`validate()` 强制）；确认要求由风险级派生：R0/R1 无需确认，R2/R3 需要确认，R4 PreviewGated（专项 Review 前不可启用）。
  - 注册拒绝：重复名称、非法 token、永久禁止清单命中（`cdp.*`/`webdriver`/`*.execute_js`/cookie/credential/password/payment/file/upload/network/proxy 等闭合名表）；查找未知工具返回 None。
  - 参数表为闭合 `ParamSpec { key, required }`，键为 token；registry 容量 ≤64。
  - snapshot golden 锁定全部工具的 (name|capability|risk|confirmation|availability|idempotent|streaming|params) 行序。
- 验收与测试：AG-001、AG-015。测试覆盖 snapshot golden 逐行一致、capability-risk 一致性全量自检、确认派生规则、注册拒绝矩阵、永久禁止清单命中与误放行（如 `page.read_cookies` 含 cookies 字样但属禁止表；`page.snapshot` 放行）、容量上界。命令：`cargo test -p crayon-agent-gateway -p crayon-domain`、clippy `-D warnings`、fmt、workspace 基线回归、`git diff --check`。
- 明确不做：工具执行/调度（AGT-07..10）、grant/确认（AGT-04/05）、session 状态机（AGT-03）、MCP 映射（AGT-14）。

### AGT-02 完成记录（2026-08-22）

- 实现：`crates/crayon-agent-gateway` 新 crate（`registry.rs` 405 行）；闭合 `ToolSpec`/`ParamSpec`，`ToolSpec::build` 由 capability 派生 risk、由 risk 派生 confirmation（R0/R1 无需确认，R2/R3/R4 需要确认）与 availability（R4 PreviewGated），矛盾声明在构造上不可能；`ToolRegistry` BTreeMap 确定序、容量 64、注册拒绝 InvalidName/PermanentlyDenied/DuplicateTool/Capacity/RiskMismatch/TooManyParams/InvalidParamKey；永久禁止清单 15 个闭合子串（cdp/webdriver/execute_js/eval/javascript/cookie/credential/password/payment/file_upload/file_system/filesystem/network/proxy/screenshot_capture）；v1 冻结 20 工具；`snapshot()` 行格式 `name|capability|risk|confirmation|availability|idempotent|streaming|params`；`crayon-domain` 仅追加 `AgentCapability`/`RiskLevel` 的 `wire_name()` 常量方法；根 `Cargo.toml` 加入 members。无新增第三方依赖。
- 修正：原子范围初稿把 `cast.get_state` 写成 R1，与 domain `CastRead→R0` 矛盾，已统一为"cast_read 2（均为 R0）"。
- 验证：`cargo test -p crayon-agent-gateway -p crayon-domain` 通过（registry 12 项：snapshot golden 逐行一致、capability-risk 全量自检、确认/可用性派生矩阵、重复/非法名/禁止清单 15 命中与 4 个误放行对照/容量 64 上界/参数形状/查找未知/确定序）；`cargo clippy -p crayon-agent-gateway -p crayon-domain --all-targets -- -D warnings` 通过；`cargo fmt --all -- --check` 通过；`git diff --check` 通过；基线回归 `crayon-browser-core --lib` 3/3、`--no-default-features --features legacy-dev --lib` 58/58、`crayon-profile` 42/42 不变。
- Code Review：P0 0、P1 0、P2 1（`RegistryError::RiskMismatch` 为防御性保留——`ToolSpec::build` 私有且 risk 由 capability 派生，公共路径不可能构造矛盾 spec；保留作为注册入口纵深防御，后续若有第二个构造入口即生效）。
- 未覆盖与风险：无工具执行/调度/grant/session（AGT-03/04/07..10）；snapshot golden 变更需与 Roadmap 同步评审。`AGT-02` 转为 `DONE`，解锁 `AGT-04`（另需 AGT-03）与后续工具实现任务的 registry 依赖。

## AGT-03 原子范围（client/task/session/target/generation 状态机）

- 状态：`DONE`；依赖 `AGT-01 DONE`、`FND-09 DONE`。
- 单一目标：在 `crayon-agent-gateway` 交付 CAAP session 状态机——client session 集合（有界）、task 生命周期（Queued→Running→Completed/Failed/Cancelled 终态不可逆）、target generation 跟踪与旧结果丢弃、幂等键去重、deadline 清扫、chunk seq 单调分配、有界队列背压；本任务不做 transport、grant/确认、工具执行与 MCP 映射。
- 输入：AG-002（重复 invoke/cancel、超时、旧 generation、断连、App/Profile/标签退出 → task 幂等收敛、旧结果丢弃、资源有界）、AGT-01 冻结的 `CaapRequest`/`CaapChunk`/`CaapCancel` 字段（deadline 为调用方注入 epoch ms；chunk seq 单调由本层校验）、domain `SessionGeneration`/`TabId`/`CaapError`、架构 §9（有界、幂等、逆序释放）。
- 输出与允许修改：`crates/crayon-agent-gateway/src/session.rs` + `src/session_tests.rs`、`src/lib.rs`（仅加 `pub mod session;`）、`src/registry.rs`（仅把 `is_token` 提为 `pub(crate)` 复用）、`Cargo.toml`（crate 内追加 `crayon-ipc-schema` path 依赖，无第三方新增）、本 Roadmap 状态。
- 禁止修改：AGT-01 golden 与 wire 消息、AGT-02 registry 行为与 snapshot、domain 既有类型、其他 crate；不得引入线程/时钟/IO（`now_ms` 由调用方注入）。
- 边界：
  - session  keyed by client token（同 registry 字符集 ≤64）；`MAX_SESSIONS=4`；每 session `MAX_TASKS=64`（满时先逐出最老终态 task，全为非终态才 `QueueFull`）。
  - submit：request id 在 session 内唯一（重复 → `InvalidMessage`）；幂等键首发注册；同键同指纹（tool+target+params）→ 返回既有 task（去重）；同键异指纹 → `InvalidMessage`；提交时 `deadline_ms <= now_ms` → `DeadlineExceeded` 直接终态。
  - target 必须先由调用方解析为具体 `TabId`（`ActiveTab` 解析归后续工具任务）；generation 按 TabId 跟踪，`advance_generation` 把该 tab 全部非终态 task 置为 `Failed(TargetStale)` 并返回清单；对已终态/已 stale task 的 chunk 与完成回调一律丢弃（`TargetStale` 或幂等 no-op），旧结果不得流出。
  - cancel：Queued/Running → `Cancelled` 终态；终态 task 幂等 no-op；未知 id → `InvalidMessage`；断连（close_session）把该 session 全部非终态 task 置 `Cancelled` 并整体移除。
  - chunk seq 每 task 从 0 单调分配，仅 Running 可发 chunk；final chunk 使 task → `Completed`；`sweep_expired(now)` 把到期非终态 task → `Failed(DeadlineExceeded)` 并返回清单。
  - 全部状态迁移同步完成，无锁无 await；返回的事件清单供 transport 层派发，session 层不持有回调。
- 验收与测试：AG-002。测试覆盖：session 开关与容量、request id 重复、幂等去重/异指纹拒绝、deadline 即过期与 sweep、cancel 三分支（含重复 cancel 幂等）、generation advance 使 task stale、stale task 的 chunk/complete 被拒、close_session 收敛、chunk seq 单调与非 Running 拒绝、终态不可逆、容量逐出策略、确定性伪随机（LCG）操作序列不变量（容量上界/终态不回退/seq 单调/无 stale 交付）。命令：`cargo test -p crayon-agent-gateway`、clippy `-D warnings`、fmt、workspace 基线回归、`git diff --check`。
- 明确不做：transport（AGT-12）、grant/确认（AGT-04/05）、工具执行与 app-runtime 接线（AGT-07..10）、receipt（AGT-11）、MCP 映射（AGT-14）。

### AGT-03 完成记录（2026-08-22）

- 实现：`crayon-agent-gateway` 新增 `session.rs`（442 行）：`SessionManager` 管理有界 session 集合（`MAX_SESSIONS=4`，client token 复用 registry `is_token` 校验，提为 `pub(crate)`）与每 session 64 task 容量（满时逐出最老终态，全 live 才 `QueueFull`）；task 生命周期 Queued→Running→Completed/Failed/Cancelled，终态不可逆；幂等键去重（同指纹 `Duplicate` 返回既有 id/状态，异指纹 `InvalidMessage`）；deadline 提交即检（`<=now` 直接 `Failed(DeadlineExceeded)` 终态并参与去重）与 `sweep_expired` 清扫；generation 按 TabId 跟踪，`advance_generation` 收敛该 tab 全部旧 generation 非终态 task 为 `Failed(TargetStale)`，stale task 的 start/complete/fail/chunk 一律拒绝（`TargetStale`），旧结果不可流出；cancel 三分支幂等；`close_session` 收敛并移除整个 session；chunk seq 每 task 从 0 单调分配，final chunk 完成 task；全部迁移同步完成，无锁/线程/IO/时钟，`now_ms` 调用方注入。crate 追加 `crayon-ipc-schema` path 依赖（复用 `CaapRequest`/`SchemaVersion`），无第三方新增。
- 验证：`cargo test -p crayon-agent-gateway` 25/25 通过（session 13 项：名称/容量/重复拒绝矩阵、幂等去重与异指纹、deadline 即过期与 sweep、cancel 幂等、chunk seq 生命周期、终态不可逆、generation 精确收敛与 stale 拒绝、close_session 收敛、容量逐出+全 live QueueFull、LCG 3000 步伪随机序列不变量——容量上界/终态不回退/seq 单调）；`cargo clippy --all-targets -- -D warnings` 通过；fmt、`git diff --check` 通过；基线回归 core lib 3/3、legacy-dev lib 58/58、profile 42/42 不变，workspace 全量无失败。
- Code Review：P0 0、P1 0、P2 1（idempotency fingerprint 在内存中拼接参数值如 URL——不落盘不进日志，AGT-11 receipt 必须另行脱敏，其任务行已注明"无正文/query/secret"）。
- 未覆盖与风险：transport 事件派发、grant/确认接线、工具执行归后续任务；fingerprint 仅内存态，进程重启后幂等键不保留（v1 会话级语义，符合预期）。`AGT-03` 转为 `DONE`，解锁 `AGT-04`、`AGT-06`、`AGT-11` 的 session 依赖。

## AGT-04 原子范围（grant 模型与 default-deny 授权面）

- 状态：`VERIFIED`；依赖 `AGT-02 DONE`、`AGT-03 DONE`、`PRV-08 DONE`。
- 单一目标：`crayon-agent-gateway` 新增 `grant.rs`：单次（single-use）/任务（task）/App 会话（app_session）三类 grant 的签发、校验、撤销、Profile 隔离与目标变化失效，默认 deny；不含确认 UI、transport 与工具执行。
- 输入：AG-003（grant 不跨 Profile/目标/会话且立即撤销）、AG-005（untrusted 内容不能扩大 grant）、AGT-02 registry 的 capability/risk、AGT-03 session 的 client token 口径（`is_token`）。
- 输出与允许修改：`crates/crayon-agent-gateway/src/grant.rs`、`grant_tests.rs`、`lib.rs` 模块声明、本 Roadmap。Profile 隔离用 crate 内已校验 `ProfileScope` token 表达（不引入 `crayon-profile` 依赖）；时间调用方注入（`now_ms`），无锁/线程/IO；零第三方新增。
- 边界：
  - `GrantKind = SingleUse | Task | AppSession`；SingleUse 授权即消费；Task 绑定 task id 且授权计数有界；AppSession 绑定 client session token，session 关闭即全部失效。
  - grant 绑定 `(session token, ProfileScope, capability, Option<AgentTarget>)` 四元组；authorize 必须全匹配才放行，任何不匹配/过期/撤销/未知都 deny（closed error），并提供到 `CaapError` 的稳定映射。
  - 撤销：单 grant、整个 session、整个 Profile 三级，立即生效；`invalidate_target(tab)` 把绑定该 tab 的 grant 立即失效（AG-003 目标变化）。
  - AG-005：authorize 只消费调用方给的 `(session, profile, capability, target)` 四元组，不存在任何以页面/工具输出为来源的扩大路径；grant 只能通过显式用户确认签发（本任务暴露 `issue`，调用方语义约束写入文档注释）。
  - 容量有界（`MAX_GRANTS`），满载拒绝并计数；grant id 为内部不透明 token。
- 验收与测试：`AG-003`、`AG-005` 的模型部分（确认 UI 归 AGT-05）。测试矩阵：kind 消费语义、四元组任一不匹配 deny、撤销三级立即生效、目标失效、过期（注入时钟）、容量、错误映射 golden、随机序列不变量（default-deny 永不放行未签发组合）。命令：`cargo test -p crayon-agent-gateway`、clippy `-D warnings`、fmt、workspace 基线回归、`git diff --check`。
- 明确不做：确认 UI（AGT-05）、transport（AGT-12）、工具执行（AGT-07..10）、receipt（AGT-11）、grant 持久化（进程内 v1 语义）。

### AGT-04 完成记录（2026-08-22）

- 实现：`crayon-agent-gateway` 新增 `grant.rs`（约 430 行）：`GrantKind = SingleUse/Task/AppSession`；`GrantManager.issue` 校验 session/profile/task token（复用 registry `is_token` 闭合字符集）与 TTL（1..=MAX_GRANT_TTL_MS=1h），容量 `MAX_GRANTS=128` 满载拒绝；`authorize` 按 `(session, ProfileScope, capability, target)` 四元组 default-deny 匹配，优先未撤销匹配项，过期即移除并拒绝，SingleUse 授权即消费，Task grant 有 `MAX_TASK_GRANT_USES=64` 上限；撤销三级（单 grant 幂等/`revoke_session`/`revoke_profile`）立即生效；`invalidate_target(tab)` 只失效绑定该 tab 的 grant；`sweep_expired` 清扫；`GrantStats` 有界计数；`GrantError` 闭合枚举带稳定 Display 与 `to_caap_error()` 映射。AG-005 语义由结构保证：authorize 仅消费调用方四元组，模块内不存在以页面/模型/工具输出为输入的扩大路径，`issue` 的用户确认约束写入文档注释（确认 UI 归 AGT-05）。全同步、无锁/线程/IO/时钟（`now_ms` 注入）、无第三方新增。
- 验证：`cargo test -p crayon-agent-gateway` 40/40 通过（grant 15 项：token/TTL/容量矩阵、SingleUse 消费、Task 用量上界、AppSession 过期、四元组任一不匹配 deny、未绑定目标授权任意目标、三级撤销立即生效与幂等、目标失效只杀绑定 grant 且不遮蔽新匹配、容量回收、sweep、错误 Display+CAAP 映射 golden、stats 计数、LCG 3000 步伪随机序列 default-deny 不变量）；`cargo clippy -p crayon-agent-gateway --all-targets -- -D warnings` 通过；`cargo fmt --all -- --check`、`git diff --check` 通过；基线回归 core lib 3/3、legacy-dev lib 58/58，workspace 全量无失败。
- Code Review：P0 0、P1 0、P2 1（未绑定目标的 grant 可授权该 session+profile+capability 的任意目标——这是有意的默认语义，但 AGT-05 确认 UI 摘要必须显式区分"未绑定目标"与"绑定特定 tab"，避免用户误读授权范围；已在此记录，AGT-05 验收需覆盖）。
- 未覆盖与风险：确认 UI（AGT-05）、transport 接线（AGT-12）、工具执行调度（AGT-07..10）、receipt（AGT-11）；grant 为进程内 v1 语义，无持久化；GrantId 为内部单调 id，transport 落地时如需跨进程引用须换成高熵 token（AGT-12 范围）。`AGT-04` 转为 `VERIFIED`（真机门禁不适用，纯模型任务；DONE 待 AGT-05 确认 UI 联动评审后统一处理）。

## AGT-11 原子范围（有界脱敏 action receipt）

- 状态：`VERIFIED`；依赖 `AGT-03 DONE`、`AGT-04 DONE`。
- 单一目标：`crayon-agent-gateway` 新增 `receipt.rs`：agent 已执行动作的有界、脱敏、TTL receipt 记录，支持用户预览（导出快照）与清除；不含正文、完整 query、Cookie、Authorization、token。
- 输入：AG-011（预览/清除 receipt，有界 TTL，无正文/query/secret）、PV-010（预览与实际内容一致）、AGT-03 session 的 client token 口径、AGT-02 registry 的工具名闭合集合、PRV-08 `DataClass::Diagnostic` 诊断口径与 `redact_sensitive`。
- 输出与允许修改：`crates/crayon-agent-gateway/src/receipt.rs`、`receipt_tests.rs`、`lib.rs` 模块声明、本 Roadmap。全同步、无锁/线程/IO/时钟（`now_ms` 注入）、无第三方新增；不落盘（进程内 v1 语义）。
- 边界：
  - `ActionReceipt` 字段全部为闭合 token/枚举：client session、tool 名、capability、risk、目标描述（tab id token 或 `active`）、GrantId、闭合 outcome、时间戳、错误码（可选）；不存在自由文本参数快照。
  - 容量 `MAX_RECEIPTS`（满载逐出最老）与 `RECEIPT_TTL_MS` 有界；`sweep_expired` 清扫。
  - `preview()` 返回用户可见快照，内容与内存记录逐字段一致（PV-010）；`clear_all()`/`clear_session()` 立即清除。
  - 防泄漏测试：对所有字段值断言不含正文特征与 secret 模式（复用 domain `redact_sensitive` 语义或等价断言），并验证 `to_diagnostic_event()` 产出 `DataClass::Diagnostic` 事件。
- 验收与测试：AG-011/PV-010 模型部分。矩阵：记录校验、TTL 过期与 sweep、容量逐出、预览一致性、清除、泄漏扫描、诊断事件映射、随机序列不变量。命令：`cargo test -p crayon-agent-gateway`、clippy `-D warnings`、fmt、workspace 基线、`git diff --check`。
- 明确不做：transport（AGT-12）、工具执行接线（AGT-07..10）、确认 UI（AGT-05）、持久化与跨进程导出。

### AGT-11 完成记录（2026-08-22）

- 实现：`crayon-agent-gateway` 新增 `receipt.rs`（约 300 行）：`ActionReceipt` 字段全为闭合 token/枚举（client、tool、capability、risk、目标描述 token、GrantId、闭合 outcome、可选闭合错误码、时间戳），无自由文本参数快照，正文/query/Cookie/Authorization/token 在类型上不可表达；`ReceiptStore` 容量 `MAX_RECEIPTS=256`（满载逐出最老）+ `RECEIPT_TTL_MS=24h`；`preview(now)` 用户可见快照与保留记录逐字段一致（PV-010）；`clear_all`/`clear_client` 立即清除；`sweep_expired` 清扫；`to_diagnostic_event()` 产出 PRV-08 `DataClass::Diagnostic` 事件（tool/risk/outcome/target/error_code 全闭合 token）；`ReceiptError` 闭合枚举。全同步、无锁/IO/时钟（`now_ms` 注入）、不落盘；`GrantId` 内部字段提为 `pub(crate)` 供同 crate 构造，无第三方新增。
- 验证：`cargo test -p crayon-agent-gateway` 48/48 通过（receipt 8 项：字段校验矩阵、泄漏标记扫描、TTL 过期与 sweep、容量逐出最老、预览一致性、清除、诊断事件映射 golden、LCG 3000 步容量/泄漏不变量）；`cargo clippy -p crayon-agent-gateway --all-targets -- -D warnings` 通过；fmt、`git diff --check` 通过；基线 core lib 3/3、legacy-dev 58/58 不变，workspace 全量无失败。
- Code Review：P0 0、P1 0、P2 0（AGT-03 遗留 P2 已按本任务"无正文/query/secret"口径关闭：receipt 不含参数值，fingerprint 脱敏风险不再外溢）。
- 未覆盖与风险：transport 接线（AGT-12）、工具执行记录入口（AGT-07..10 调用 `record`）、用户预览 UI 与磁盘导出（后续 BUX/PRV 任务）；receipt 进程内 v1 语义，重启即清。`AGT-11` 转为 `VERIFIED`，解锁 `AGT-12` 的 receipt 依赖（另需 `PRV-10`、`PLT-01 DONE` 已满足）。

## AGT-12 原子范围（transport 守卫层，AGT-12A 切片）

- 状态：`VERIFIED`（AGT-12A 守卫层切片）；依赖 `AGT-04 DONE`、`AGT-11 DONE`、`PRV-10 VERIFIED`、`PLT-01 DONE`。
- 路径修订说明：原允许路径 `apps/desktop-cef/agent-transport/**` 的目录尚不存在（CEF 壳位于 `browser/cef-shell`，桌面 app 装配归 CEF/QAR 后续任务），且 named pipe/UDS 的 OS 端点绑定属平台实现。本切片把 transport 无关的守卫层落在 `crayon-agent-gateway`（与 session/grant/receipt 同 crate，schema 同源），OS 端点绑定（Windows named pipe ACL、macOS UDS peer credentials 实测）保留为后续平台切片，复用 `crayon-platform-api::LocalAgentIpcEndpoint` 契约。
- 单一目标：新增 `crayon-agent-gateway/src/transport.rs`：CAAP 帧编解码（长度前缀 + 上限）、单客户端接入、每客户端限流、重放/超大/畸形拒绝与幂等 stop；不含 OS socket、确认 UI、工具执行。
- 边界：
  - `FrameCodec`：`u32 BE 长度 + payload` 流式解码；`MAX_FRAME_BYTES=65536` 上限，超限/畸形/残留字节闭合拒绝；部分帧等待更多数据。
  - `TransportGuard`：单客户端（首个接入者绑定，断开/stop 前拒绝第二者）；令牌桶限流（容量/间隔常量、`now_ms` 注入、满载拒绝）；request-id 重放拒绝（有界 seen 集合）；畸形帧 strike 计数超阈值断开。
  - `stop` 幂等并释放客户端占用；`TransportError` 闭合枚举 + `to_caap_error()` 映射。
  - 全同步、无锁/线程/IO/时钟注入、无第三方新增；帧载荷不解析、不记录。
- 验收与测试：AG-012 守卫语义部分。矩阵：编解码（完整/分片/超限/畸形/残留）、单客户端、限流（窗口边界/恢复）、重放、strike 断开、stop 幂等、错误映射 golden、LCG 不变量。命令：`cargo test -p crayon-agent-gateway`、clippy `-D warnings`、fmt、workspace 基线、`git diff --check`。
- 明确不做：OS named pipe/UDS 端点绑定与真实 peer credential/ACL 实测（后续平台切片，真机归 PLT-W04/M04 门禁）、确认 UI（AGT-05）、工具执行（AGT-07..10）、MCP 映射（AGT-14）。

### AGT-12A 完成记录（2026-08-22，transport 守卫层切片）

- 实现：`crayon-agent-gateway` 新增 `transport.rs`（约 300 行）：`FrameCodec` 流式 `u32 BE 长度前缀`解码，`MAX_FRAME_BYTES=65536` 硬上限（超限帧返回 Oversize 并丢弃 header、payload 视为 poisoned；单次 feed 超 `2*MAX` 直接 fail-closed 拒绝且不缓存）；`TransportGuard` 单客户端绑定（首者绑定、幂等重绑、第二者 `ClientBound` 拒绝）、令牌桶限流（`RATE_BURST=32`、`RATE_INTERVAL_MS=100`、`now_ms` 注入、满载 `RateLimited`）、request-id 重放拒绝（`MAX_SEEN_IDS=512` 有界窗口，语义级幂等仍由 AGT-03 session 保证）、strike 计数（`MAX_STRIKES=8` 满则断开并复位）；`stop`/`disconnect` 幂等并释放；`TransportError` 闭合枚举 + `to_caap_error()` 映射。全同步、无锁/线程/IO/时钟、无第三方新增；帧载荷不解析不记录。
- 验证：`cargo test -p crayon-agent-gateway` 60/60 通过（transport 12 项：往返/分片/背靠背/最大合法帧/超限/buffer fail-closed、单客户端矩阵、限流突发耗尽与恢复、重放有界窗口、strike 阈值断开、stop 幂等、错误 Display+CAAP golden、LCG 3000 步敌意流不变量——无 panic、strike 上界、pending 有界）；`cargo clippy -p crayon-agent-gateway --all-targets -- -D warnings` 通过；fmt、`git diff --check` 通过；基线 core lib 3/3、legacy-dev 58/58、workspace 全量无失败。
- Code Review：P0 0、P1 0、P2 1（重放窗口 512 条滑出后旧 id 可再次通过 transport 层——已注明由 AGT-03 session 幂等键提供语义级去重兜底，且窗口大小在 AGT-15 fuzz 时应结合实际 chunk 速率复核）。
- 未覆盖与风险：OS named pipe/UDS 端点绑定、真实 peer credential/ACL 实测（后续平台切片，Windows 真机归 PLT-W04 门禁）、与 session/grant 的运行时装配、MCP 映射（AGT-14）。`AGT-12A` 转为 `VERIFIED`；AGT-12 整体 DONE 待 OS 端点切片与实机门禁。

### Review P2 修复记录（2026-08-23）

- AGT-04：`Grant::is_targeted()` + `scope_summary()` 闭合作用域描述（`grant:<capability>:any-target|tab:<id>|active-tab`，无页面数据）。原 P2 的 UI 歧义风险收敛为：AGT-05 确认 UI 验收必须按该摘要渲染目标范围（新增测试 scope_summary_distinguishes_targeted_and_untargeted）。
- AGT-12A 的 P2（重放窗口滑动）维持：由 AGT-03 session 幂等键兜底，AGT-15 fuzz 复核窗口大小（理由见任务记录）。
