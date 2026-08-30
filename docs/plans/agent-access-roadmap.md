# AGT CAAP / CLI / MCP Agent 访问 Roadmap

- 状态：`A0 权限内核完成（AGT-01..05/11）`；`AGT-08/AGT-10 DONE`（2026-08-24）、`AGT-12B VERIFIED`（Windows/macOS OS 组合回归闭合，AGT-12 整体待产品装配）；`AGT-06/09/15 VERIFIED`（2026-08-30）；`AGT-07 等 CNT-08`
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
| AGT-05 | VERIFIED | AGT-04,CEF-08 | `apps/desktop-cef/**/agent-confirm/**`,locales | 确认 UI：client、工具、route、目标、参数摘要、数据披露、到期和无障碍 | `AG-004`; UI integration | A0 |
| AGT-06 | VERIFIED | CNT-03,AGT-03 | `crayon-page-data/**`,`crayon-agent-gateway/page_stream/**` | generation-scoped 快照缓存、分页/流式/增量、索引、背压和性能 instrumentation | `AG-006`,`AG-015`; benchmark/soak | A1 |
| AGT-07 | TODO | AGT-04,AGT-06,CNT-08 | `crayon-agent-gateway/tools/content/**`,`crayon-app-runtime/**` | R1 target/标题/选区/结构化页面/Markdown 读取工具 | `AG-006`; 跨 Profile/后台/过期/超量拒绝 | A1 |
| AGT-08 | DONE | AGT-04,SDK-08 | `crayon-agent-gateway/tools/cast_read/**` | R0/R1 接收端能力和投屏状态读取，不返回 IP/URL/token | `AG-007`; adapter tests | A1 |
| AGT-09 | VERIFIED | AGT-05,CEF-07,ACT-07,ACT-11 | `crayon-agent-gateway/tools/navigation/**`,`crayon-app-runtime/**` | R2 打开/切换/关闭标签、导航、后退、刷新、滚动及人工接管结果 | `AG-008`; scheme/redirect/download/popup/cancel | A2 |
| AGT-10 | DONE | AGT-05,SDK-12,MED-19 | `crayon-agent-gateway/tools/cast_control/**` | R3 选择设备、开始/暂停/seek/停止；沿用正常投屏门禁 | `AG-009`; 目标变化重确认；不控制外部镜像客户端 | A2 |
| AGT-11 | VERIFIED | AGT-03,AGT-04 | `crayon-agent-gateway/receipt/**`,diagnostics | 有界脱敏 action receipt、TTL、用户预览/清除 | `AG-011`,`PV-010`; 无正文/query/secret | A0 |
| AGT-12 | VERIFIED | AGT-04,AGT-11,PRV-10,PLT-01 | `apps/desktop-cef/agent-transport/**`,`crayon-platform-api/**` | Windows named pipe/macOS UDS CAAP transport；当前用户 ACL、限流、单客户端、stop | `AG-012`; 恶意本机 client/replay/oversize | A1 |
| AGT-13 | TODO | AGT-05,AGT-07,AGT-08,AGT-12 | `apps/agent-cli/**`,docs/tests | R0/R1 CLI Developer Preview；机器可读结果、版本、cancel | `AG-013`; 无交互不绕确认 | A1 |
| AGT-14 | TODO | AGT-05,AGT-07,AGT-08,AGT-12 | `apps/mcp/**`,MCP contracts | 只读 MCP Developer Preview，将 initialize/list/call/cancel 映射到 CAAP | `AG-014`; schema 同源、loopback only | A1 |
| AGT-15 | VERIFIED | AGT-06,AGT-09,AGT-10,ACT-12 | `crayon-agent-gateway/tools/semantic/**`,`tests/security/agent/**`,`tests/perf/agent/**` | 把 R4 Action Map/action_id/effect 接入 CAAP，并完成提示注入/fuzz/恶意 client/性能专项 | `AG-005`,`AG-010`,`AG-015`; 不复制 locator/runtime；永久禁止 surface 零命中 | A2 |
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

## AGT-12B 原子范围（CAAP 连接运行时与平台端点闭环）

- 状态：`VERIFIED`；依赖 `AGT-12A VERIFIED`、`PLT-W04 DONE`、`PLT-M04 DONE`。
- 单一目标：把平台端点已验证的当前用户连接流接入 AGT-12A 守卫，交付一次只服务一个本机 client 的 CAAP 连接运行时：首帧必须为精确版本 `CaapHello`，握手后只接收经 schema 复检的 `CaapRequest`/`CaapCancel`，并将 `CaapWelcome`/稳定错误回复按同一有界帧格式写回。
- 输入与输出：允许修改 `crates/crayon-platform-api/src/local_agent_ipc.rs`（只新增已验证连接流抽象）、`crates/crayon-platform-windows/src/local_agent_ipc.rs`、`crates/crayon-platform-macos/src/local_agent_ipc.rs`（只实现该抽象）、`crates/crayon-agent-gateway/src/transport.rs(+tests)`、对应 crate manifest 与本 Roadmap。平台 adapter 只提供 peer/字节流事实，CAAP 解析、版本、限流、重放与 strike 仍唯一归 gateway。
- 边界：OS peer gate 必须先于读取任何 handshake 字节；连接流 read/write/close 有闭合错误且不携带 peer/正文；每次 read 缓冲 ≤64KiB；Hello 前的 request/cancel、重复 Hello、未知/畸形 JSON、版本不匹配、超大帧稳定拒绝；request id 进入 AGT-12A 有界 replay 窗口，cancel 不创建新授权；stop/disconnect 幂等并释放连接占用，不持锁执行阻塞 IO，不记录 payload。
- 验收与测试：`AG-012`。纯内存连接矩阵覆盖分片 handshake、版本/能力协商、握手顺序、request/cancel、重放、限流、畸形/超大 strike、EOF/写失败与 stop；Windows x64 以真实 named pipe 完成 current-user connect → Hello/Welcome → Request/Cancel → disconnect，并复验第二 client/ACL/远程拒绝事实；macOS UDS 端点复用 `PLT-M04c` 的真实 peer credential 证据，组合运行时需在 macOS 回归。命令：目标 crate test/clippy/fmt、workspace 回归、`scripts/check.ps1 fast/security`、`git diff --check`。
- 明确不做：工具执行与 grant 签发（AGT-07..10/后续 app-runtime 装配）、CLI/MCP（AGT-13/14）、远程监听、CEF/Rust 进程级启动装配、macOS 以 Windows 结果冒充真机。

### AGT-12B 完成记录（2026-08-30）

- 实现：`crayon-platform-api` 新增 OS 已验证的 `LocalAgentIpcConnection` 有界字节流契约，端点必须在读取 Hello 前完成 peer gate；Windows named pipe 与 macOS UDS 分别实现 read/write/幂等 close，Windows client 构造器只接受本机 `\\.\pipe\crayon-agent-<token>`，远程 pipe 在 OS 调用前拒绝，Windows 身份查询失败主动断开，macOS socket bind 后显式收紧 `0600`。gateway 新增 `CaapConnection`：首帧精确 Hello、能力交集 Welcome、握手后闭合 Request/Cancel、AGT-12A 限流/重放/strike、版本/IO/超大/EOF fail-closed、payload 零日志；测试专用 raw stream 构造保持 crate 私有，生产只能经平台端点进入。
- Windows x64 验证：`cargo test -p crayon-agent-gateway -p crayon-platform-api -p crayon-platform-windows`：gateway 92/92、platform-api 17/17 + contract 7/7、platform-windows 27/27；其中真实 named pipe 完成 current-user connect → Hello/Welcome → Request/Cancel → 第二 client 拒绝 → disconnect/stop，远程 pipe 名在 CreateFileW 前拒绝。`cargo build -p crayon-agent-gateway -p crayon-platform-api -p crayon-platform-windows --release` 通过；同一真实 named pipe 用例 `--release` 1/1 通过。
- macOS arm64 验证（2026-08-30，macOS 26.6.2 build 25G83）：新增与 Windows 对称的真实 UDS + CAAP 集成测试，仅在 macOS dev-dependency 接入 `crayon-platform-macos`。真实 `/tmp/crayon-agent-agt12b-<pid>.sock` 经 `0600` 权限与 `getpeereid` 同用户门禁后完成 Hello/Welcome（PageRead 能力交集）→ Request(41) → Cancel(41) → connection/endpoint stop，并断言 socket 文件清除、零残留；Debug/Release 各 1/1 通过。完整相关回归：gateway 92/92、platform-api 17/17 + contract 7/7、platform-macos 34/34（串行 Keychain 测试稳定通过）；Release 三 crate build 通过。
- 质量门禁：`cargo clippy -p crayon-agent-gateway -p crayon-platform-api -p crayon-platform-windows --all-targets --no-deps -- -D warnings` 零告警；`cargo fmt --all -- --check`、`git diff --check` 通过；核心基线 3/3、legacy-dev 58/58；`scripts/check.ps1 security` passed（guard/relay unit/security 全绿）。全依赖 Clippy 被未改动的 `crates/crayon-page-data/src/snapshot.rs:506` 既有 `nonminimal_bool` 阻断；`cargo test --workspace` 与 `scripts/check.ps1 fast` 的 guard/format/brand 步骤通过，但 formal-workspace 被未改动的 `crayon-content-markdown` 两个 Windows CRLF golden（实际 LF、fixture CRLF）阻断，未夹带修复。
- Code Review：按 v0.8 从需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试、可维护性复核；修正 Review 中发现的 4 项（远程 pipe 构造旁路、IO/版本失败未立即释放、Windows peer 查询失败未 disconnect、UDS 文件权限依赖 umask）。最终 P0/P1/P2=`0/0/0`；阻塞 IO 不在锁内，连接/endpoint 单一所有，payload/peer/参数不进入错误或日志，8KiB read 与 64KiB frame/有界 replay 保持热路径上界。
- 未覆盖与风险：Windows named pipe 与 macOS UDS+CAAP 的 OS 组合回归均已闭合；CEF 产品进程 accept loop、生命周期 stop 与 session/grant/tool dispatch 装配仍未实现，AGT-12 因此保持 `VERIFIED` 而非 `DONE`。后续 AGT-12C 在 CEF 产品装配收口后才能解锁 AGT-13/14；PLT-M05b 的真实接收端 Direct/交接验收是独立设备任务，不由本回归替代。

## AGT-08 原子范围（R0/R1 投屏只读工具）

- 状态：`DONE`（2026-08-24）；依赖 `AGT-04 VERIFIED`、`SDK-08 DONE`。
- 单一目标：`crayon-agent-gateway` 新增 `tools/cast_read.rs`：`cast.list_receivers`（R0）与 `cast.get_state`（R0）的网关侧实现面——闭合脱敏 DTO、读取源端口 trait、边界校验与确定性快照；IP/媒体 URL/route token/session 材料在类型上不可表达。不含 transport、确认 UI 与 app-runtime 装配。
- 输入：AG-007（不返回 IP/媒体 URL/token；同名设备；旧 route；无会话；使用 SDK 最新 generation）、AGT-02 冻结的 cast_read 工具声明、SDK-08 的 `ReceiverCapabilities`（domain 层，可直接复用）与 generation/TTL 语义。
- 输出与允许修改：`crates/crayon-agent-gateway/src/tools/cast_read.rs`、`tools/cast_read_tests.rs`、`src/tools/mod.rs`（新建）、`lib.rs` 仅加模块声明、crate `tests/` 快照 golden、本 Roadmap。零第三方新增。
- 禁止修改：registry/session/grant/receipt/transport 行为与其 golden、CAAP schema、cast-adapter/facade、其他 crate；不得引入网络/IO/线程/时钟；DTO 不得出现地址、URL、route/resource token、页面标题字段。
- 边界：
  - `ReceiverSummary { device_id, name, capabilities(复用 domain `ReceiverCapabilities`) }` 与 `CastStateSnapshot { state(闭合六态), receiver_id(可选), position_ms, duration_ms, generation }`；无会话返回 Idle 快照而非错误。
  - 读取源以端口 trait 注入（生产由后续 app-runtime 实现，测试用 fixture）；工具层校验容量 ≤64、id/name 非空有界且无控制字符，违规稳定拒绝并映射闭合 `CaapError`。
  - 快照为确定性行格式（name 中 `\`/`|` 转义），golden 锁定；generation 随快照透出供调用方做代际围栏。
- 验收与测试：AG-007。矩阵：同名双设备并列、脱敏 golden 锁定（含负向断言无 ip=/url=/token= 形态）、容量与非法条目拒绝、无会话 Idle、generation 透出、错误映射 golden、LCG 不变量。命令：`cargo test -p crayon-agent-gateway`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：投屏控制（AGT-10）、transport 接线（AGT-12）、CLI/MCP 映射（AGT-13/14）、app-runtime 端口生产实现。

### AGT-08 完成记录（2026-08-24）

- 实现：`crayon-agent-gateway` 新增 `tools/cast_read.rs`（约 250 行）+ `tools/mod.rs`：`CastReadSource` 端口 trait 注入实时数据（生产实现归后续 app-runtime 装配，测试用 fixture），工具层零缓存；`list_receivers(source, generation)` 校验容量 ≤64、device_id/name 非空有界（≤128 字节、无控制字符）后产出 `ReceiverSummary { device_id, name, capabilities }`，capabilities 直接复用 domain `ReceiverCapabilities`（七字段闭合，含 SDK-08 定稿的保守合成语义——未评估即 false/0）；按 device_id 确定排序，与发现顺序无关；`get_state(source)` 返回 `CastStateSnapshot { state(闭合五态 idle/connecting/playing/paused/stopped), receiver_id(可选), position_ms, duration_ms, generation }`，无会话返回 Idle 快照而非错误。IP/媒体 URL/route/resource token/session 材料/页面标题在类型上不可表达；generation 随两个快照透出供调用方代际围栏。快照行格式确定性锁定（`\`/`|` 转义）；`CastReadError` 闭合三态带稳定 `to_caap_error()` 映射（SourceUnavailable→CapabilityDenied、InvalidDeviceData→InvalidMessage、CapacityExceeded→QueueFull）。零第三方新增；全同步、无锁/IO/时钟。
- 验证：`cargo test -p crayon-agent-gateway` 67/67 通过（cast_read 新增 6 项：同名双设备并列且 id 序确定、golden 逐字节一致含转义用例并负向断言无 ip/http/token 形态、容量 65 拒绝+控制字符名拒绝+超长 id 拒绝、无会话 Idle 单行快照、generation 前后可区分供围栏、错误映射 golden）；clippy `-D warnings` 零告警；fmt 通过；workspace 全量无失败；`git diff --check` 通过。
- Code Review：按标准八维复核。P0 0、P1 0、P2 0——端口 trait 使 gateway 不依赖 cast-adapter/facade（依赖方向合规）；脱敏由类型字段集保证而非运行时过滤。
- 未覆盖与风险：端口的生产端实现与 SDK generation 的实时接线归后续 app-runtime 任务（AGT-07 同期装配）；同名设备仅以 opaque id 区分，UI 层需自行展示区分信息；transport/CLI/MCP 归 AGT-12..14。`AGT-08` 转为 `DONE`。

### Review P2 修复记录（2026-08-23）

- AGT-04：`Grant::is_targeted()` + `scope_summary()` 闭合作用域描述（`grant:<capability>:any-target|tab:<id>|active-tab`，无页面数据）。原 P2 的 UI 歧义风险收敛为：AGT-05 确认 UI 验收必须按该摘要渲染目标范围（新增测试 scope_summary_distinguishes_targeted_and_untargeted）。
- AGT-12A 的 P2（重放窗口滑动）维持：由 AGT-03 session 幂等键兜底，AGT-15 fuzz 复核窗口大小（理由见任务记录）。

## AGT-10 原子范围（R3 投屏控制工具）

- 状态：`DONE`（2026-08-24）；依赖 `AGT-05 VERIFIED`、`SDK-12 DONE`、`MED-19 DONE`。
- 单一目标：`crayon-agent-gateway` 新增 `tools/cast_control.rs`：R3 五命令（select_receiver/start/pause/seek/stop）的网关侧模型——闭合命令 DTO、控制端口 trait、**确认上下文围栏**（执行必须携带用户确认时的上下文指纹，设备/媒体代际任一变化即拒绝并要求重新确认）与门禁结果透传；实际播放/DRM/广告/policy 门禁由 app-runtime 正常用例裁决，本层不复制。不控制外部镜像客户端会话。不含 transport、grant 签发接线与 UI。
- 输入：AG-009（R3 确认且沿用播放/DRM/广告/policy；设备/媒体/route 中途变化重确认；外部镜像客户端不受控）、AGT-04 grant 四元组与 `scope_summary` 口径、AGT-05 确认视图模型与上下文指纹语义、`tools/cast_read.rs` 的闭合状态词汇。
- 输出与允许修改：`crates/crayon-agent-gateway/src/tools/cast_control.rs`、`cast_control_tests.rs`、`src/tools/mod.rs` 仅加模块声明、本 Roadmap。零第三方新增；golden 用内联快照即可。
- 禁止修改：registry/session/grant/receipt 行为、CAAP schema、cast-adapter/facade、`tools/cast_read.rs` 既有行为；不得引入网络/IO/线程/时钟；不得在工具层实现或绕过任何 DRM/广告/策略判定。
- 边界：
  - `CastCommand` 闭合五类；receiver_id 走闭合校验（非空有界无控制字符）；seek 仅 u64 毫秒。
  - `execute_confirmed(port, ConfirmedCommand)` 是唯一执行入口——`ConfirmedCommand { command, confirmed_context }` 由调用方在用户 Confirm 后构造；当前上下文与确认上下文 `(session_state, receiver_id, media_generation)` 任一不等 → `ContextStale`（要求重新 Present+Confirm），不存在绕过路径。
  - `external_client_handoff=true` 的上下文对一切命令稳定拒绝（ExternalClientNotControllable）；无活动会话时除 select_receiver 外全部拒绝（NoSession）。
  - 门禁结果透传：端口返回的 `CoreError`（DRM/Policy/ReceiverIncompatible 等）原样进入 `GateRejected`，工具层不解释不改写。
- 验收与测试：AG-009 模型部分。矩阵：五命令 wire 锁定、确认围栏三分支（receiver/media_generation/state 变化）、外部交接不可控、无会话拒绝、非法 receiver、门禁结果透传、错误映射 golden、LCG 不变量（任何执行路径要么上下文全等要么稳定拒绝）。命令：`cargo test -p crayon-agent-gateway`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：transport 接线（AGT-12）、CLI/MCP 映射（AGT-13/14）、grant 签发与确认 UI 装配（app-runtime）、receipt 记录入口接线。

### AGT-10 完成记录（2026-08-24）

- 实现：`crayon-agent-gateway` 新增 `tools/cast_control.rs`（约 200 行）：`CastCommand` 闭合五类（select_receiver/start/pause/seek/stop，wire 名锁定）；**确认围栏为唯一执行入口**——`execute_confirmed(port, ConfirmedCommand)` 要求携带用户确认时的 `CastContext { session_state, receiver_id, media_generation, external_client_handoff }` 完整快照，检查序固定为 命令形状 → 外部交接 → 会话存在性 → 上下文全等，任一不等即 `ContextStale` 强制重新 Present+Confirm，裸执行入口在类型层不存在；外部镜像客户端会话对全部五命令稳定拒绝 `ExternalClientNotControllable`；无活动会话时除 select_receiver（建立会话的合法起点）外拒绝 `NoSession`；receiver id 校验升级为共享的 `valid_id`（非空有界无控制字符且禁空白，读侧同步采用——列出的 id 必然可选）；正常播放/DRM/广告/policy 门禁裁决由端口实现方返回，工具层以 `GateRejected(CoreError)` 原样透传不改写不解释；`CastControlError` 闭合六态带稳定 `to_caap_error()` 映射。零第三方新增；全同步、无锁/IO/时钟。
- 验证：`cargo test -p crayon-agent-gateway` 75/75 通过（cast_control 新增 8 项：五命令 wire 锁定、上下文全等时经端口执行并留痕、三分支变化各自触发 ContextStale 且零执行、外部交接五命令全拒、四命令无会话矩阵+select 从 Idle 合法、非法 receiver 在触达端口前拒绝、四种 CoreError 门禁裁决逐字透传、错误映射 golden）；clippy `-D warnings` 零告警；fmt 通过；workspace 全量无失败；`git diff --check` 通过。
- Code Review：按标准八维复核。P0 0、P1 0、P2 0——R3 必经确认由类型系统保证（无裸 execute）；id 禁空白为读/控两侧一致契约，若真实发现栈产出带空白 id 将在读侧 fail closed（已在文档注释声明取舍）。
- 未覆盖与风险：grant 签发、确认 UI 与 receipt 记录的端到端装配归 app-runtime 后续任务；ContextStale 后的重确认流由调用方驱动（AGT-05 模型已支持）。`AGT-10` 转为 `DONE`。


### AGT-05 原子范围（确认 UI 视图模型与本地化）

- 状态：`VERIFIED`；依赖 `AGT-04 VERIFIED`、`CEF-08 VERIFIED`。
- 路径说明：Roadmap `apps/desktop-cef/**/agent-confirm/**` 的目录尚不存在；按既有映射惯例落在 `browser/shared-ui/agent-confirm`（共享层视图模型），CEF shell 呈现归后续装配。
- 单一目标：R2～R4 工具调用确认的共享视图模型——展示 client/工具/risk/目标作用域（消费 AGT-04 `scope_summary` 口径）/参数摘要/数据披露/到期，Confirm/Deny 两步流；任何上下文变化（导航/设备/参数指纹）强制重新确认；无障碍经本地化 label key 全覆盖。
- 边界：
  - 参数摘要只含闭合字符集 key + 值长度 + 敏感标志；敏感 key（password/payment/cookie/token/file 等）值完全遮蔽，普通值显示长度掩码——原始值永不进入 UI 模型。
  - 到期用注入时钟；过期后 Confirm 稳定拒绝；Deny 终态。
  - 上下文指纹（navigation/device/params）任一变化即失效待确认态，必须重新 Present；不存在绕过路径。
  - 全部文案走 locales（en/zh parity），无障碍 label key 与字段一一对应。
- 验收与测试：`AG-004` 模型部分（UI 呈现与实机无障碍归 BUX/QAR）。矩阵：Present 校验、Confirm/Deny/过期、指纹变化重确认、敏感遮蔽、locale parity、风暴不变量。命令：独立 configure/build/ctest、共享层回归、`git diff --check`。
- 明确不做：真实 grant 签发接线（app-runtime）、CEF 呈现与无障碍实机验证（QAR/BUX）、transport。

### AGT-05 完成记录（2026-08-23）

- 实现：新增 `browser/shared-ui/agent-confirm`（header/impl/CMake/契约测试各 1）。`AgentConfirmRequest` 闭合字段——client/tool/capability/risk（wire token）、target_scope（消费 AGT-04 `scope_summary` 口径，含 any-target/tab:id 区分）、参数摘要（**只含 key+值长度+敏感标志，原始值永不进入模型**）、数据披露标志、到期时间；`Fingerprint()` 覆盖 identity+params（值为长度标记）供上下文变化检测。`AgentConfirmModel` 两步流：Present 校验（token 闭合字符集、scope ≤256、params ≤16、过期即拒）→ Confirm 仅在未过期 pending 态放行 / Deny 终态；`OnContextChanged(fingerprint)` 任一变化使 pending/confirmed 全部失效强制重新 Present（AG-004 变化后重确认）；Tick 主动过期。敏感 key 家族（password/payment/card/cookie/auth/token/file 等）全遮蔽。locales 新增 10 个无障碍 label key（title/client/tool/risk/target/params/disclosure/expires/allow/deny，en/zh 49/49 全等），parity 入契约测试。
- 验证：`cmake -S . -B .cache/build/agt05` 零告警；`agent_confirm` 1/1（7 组：Present 校验矩阵、Confirm/Deny/边界过期、上下文变化强制重确认含 confirmed 失效、敏感遮蔽与指纹无原始值、token 矩阵、locale parity、5000 步风暴）；共享层回归 39/39；`git diff --check` 通过。
- Code Review：P0 0、P1 0、P2 1——Fingerprint 为确定性拼接字符串（仅元数据无值），碰撞理论上可由超长 key 构造；确认判定还依赖 expires/token 校验兜底，若后续需要密码学强度指纹归 gateway 层（AGT-12 transport 已有 secret 通道）。
- 未覆盖与风险：真实 grant 签发接线（app-runtime 用例）、CEF 呈现与键盘/读屏实机验证（QAR/BUX）、R2～R4 与 AGT-04 grant 的端到端串联（后续装配）。`AGT-05` 转为 `VERIFIED`。**A0 波次（AGT-01..05/11）全部完成。**


## AGT-06 原子范围（generation-scoped 页面流 fan-out 与背压）

- 状态：`VERIFIED`；依赖 `CNT-03 DONE`、`AGT-03 DONE`。
- 单一目标：在 `crayon-agent-gateway/page_stream/**` 冻结 snapshot 的授权 fan-out 流层：profile-scoped 订阅、generation fence、有界队列与 drop-oldest 背压、有界 instrumentation。
- 输入与输出：输入为 CNT-03 verified `PageSnapshot` 与 grant 层 `ProfileScope`；输出仅限 `crates/crayon-agent-gateway/src/page_stream/**`、`lib.rs` 模块登记、Cargo 依赖登记与本 Roadmap。pull 分页/缓存/取消由 `SnapshotOwner`（CNT-03）继续拥有，本层不复制。
- 边界与预算：≤8 并发 client、每 client 队列 ≤16 chunks（溢出 drop-oldest 并计数 gap，seq 单调可检测）、client id ≤64B 闭合字符集；纯状态，无锁/IO/系统时钟；计数器为有界诊断，不参与正确性。
- 验收：AG-006/AG-015 契约侧：fan-out 只达匹配 tab/generation 订阅者且按序、跨 client 隔离、慢 consumer 溢出丢弃可检测、generation 推进只取消旧订阅、profile 关闭零内容泄漏、cancel 幂等、容量/重复/shutdown 稳定拒绝。
- 明确不做：grant 校验（AGT-04 所有权，订阅由授权层调用）、CAAP chunk 编码（AGT-01 schema 已冻结）、transport（AGT-12）、R1 工具（AGT-07）。

### AGT-06 完成记录（2026-08-30）

- 实现：`page_stream/mod.rs`：`StreamClientId`（闭合校验）、`PageStreamHub`（subscribe/publish/next_chunk/cancel/advance_generation/close_profile/shut_down/stats），`StreamChunk{seq,snapshot}` 与 `StreamStats` 有界计数。
- 验证：`cargo test -p crayon-agent-gateway` 82/82（新增 stream 7 场景）；`cargo clippy -p crayon-agent-gateway -p crayon-page-data --all-targets -- -D warnings` 通过；`cargo fmt --check` 通过；`bash scripts/check.sh security` 全绿。
- Code Review：按 v0.8 复核。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：真实 CAAP 传输与 grant 接线归 AGT-12/07；soak/benchmark（AG-015 的长稳）归 QAR harness。`AGT-07 等 CNT-08`、`AGT-09 READY`。


## AGT-09 原子范围（R2 导航工具）

- 状态：`VERIFIED`（工具面 + 用例层）；依赖 `AGT-05/CEF-07/ACT-07/ACT-11` 全部完成。
- 单一目标：冻结 R2 导航工具面（7 个闭合动词）与 app-runtime 执行用例：确认绑定、危险 scheme 前置拒绝、per-tab generation fence、有界 tab 表与确定性 receipt。
- 输入与输出：输出仅限 `crates/crayon-agent-gateway/src/tools/navigation.rs(+tests)`、`crates/crayon-app-runtime/src/navigation_usecase.rs(+tests)`、模块登记、依赖登记与本 Roadmap。
- 语义：`Navigate/OpenTab` 需 http(s) URL（`is_safe_url` 拒绝 javascript/file/data/userinfo/控制字符等一切危险目标）；每动词闭合字段集（意外字段拒绝）；确认 reference ≤128B 闭合字符集、缺失=CapabilityDenied；Scroll 步长 ∈ (0, 10_000px]；用例持有 ≤64 tab 的 generation 表：未知 tab / 旧 generation / 停机引擎在引擎前拒绝；引擎拒绝（危险 redirect/download/阻塞）为终态，不隐式重试；OpenTab 容量在引擎前 fence。
- 验收：AG-008 契约侧：危险 scheme/超量/未知 tab/旧 generation/停机全部 fail closed 且不达引擎；正常动词恰好一次派发；receipt 只含 authority/形状，query 不外泄。
- 明确不做：redirect/download 的引擎侧细节（CEF adapter）、grant/确认 UI（AGT-04/05）、transport（AGT-12）。

### AGT-09 完成记录（2026-08-30）

- 实现：gateway `tools/navigation.rs`（请求校验门 + `NavigationPort` trait + CAAP 错误映射 + receipt）；app-runtime `navigation_usecase.rs`（`NavigationEngine` port、generation fence、tab 生命周期与容量）。
- 验证：`cargo test -p crayon-agent-gateway -p crayon-app-runtime` 129 项全通过（新增 navigation 8 场景）；`cargo clippy --all-targets -- -D warnings` 通过；`cargo fmt --check` 通过。
- Code Review：按 v0.8 复核；修正两处（OpenTab 意外 tab 字段未拒绝；receipt 优先级 url>scroll>tab）。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：`NavigationEngine` 的真实 CEF adapter 归后续装配切片；redirect/download 语义由引擎拒绝统一表达。下一任务 `WFL-01 READY`（依赖 ACT-12/AGT-03）。

## AGT-15 原子范围（R4 semantic action CAAP adapter 与专项门禁）

- 状态：`VERIFIED`；依赖 `AGT-06 VERIFIED`、`AGT-09 VERIFIED`、`AGT-10 DONE`、`ACT-12 DONE`。
- 单一目标：在 agent gateway 把冻结的 `act.invoke` CAAP 请求收敛为一个闭合、可审计的 semantic action port 调用，并返回 ACT 冻结的 terminal `EffectReport`；补齐提示注入、敏感/隐藏/跨源/过期拒绝、恶意输入与有界性能专项。不复制 action handle、locator、precondition、risk 或 effect runtime。
- 输入与允许修改：`crates/crayon-agent-gateway/src/tools/semantic.rs(+tests)`、`tools/mod.rs`、gateway manifest；`tests/security/agent/**`、`tests/perf/agent/**` 与 workspace members；本 Roadmap。输入只消费 AGT-01 `CaapRequest`、AGT-02 冻结的 `act.invoke(action_id!,args?)`、ACT-03 `ActionHandleId` 与 ACT-08 `EffectReport`/幂等 key。
- 禁止修改：CAAP current/previous golden、AGT-02 registry snapshot、ACT schema/handle/locator/precondition/risk/effect 实现、CEF/平台/Cast/Relay；不得新增 selector、任意 JS/CDP、密码/支付/file action、第二工具调度、远程 transport、日志或持久化。
- 边界：只接受 tool=`act.invoke` 且参数键精确为必填 `action_id` 与可选 `args`；action id、幂等 key 复用 ACT 强类型校验，argument UTF-8 ≤512B 且禁止 NUL/非空白控制字符；页面/参数中的指令只作为 opaque untrusted data，一次请求最多调用一次 `SemanticActionPort`，不能改 target、扩大 grant 或触发第二工具。Browser/app-runtime port 唯一拥有当前 verified node/risk/precondition/confirmation/effect；其闭合拒绝映射到稳定 CAAP 错误。terminal effect 序列化必须 ≤一个 CAAP chunk，`indeterminate` 是成功传输的终态结果但不可重放。
- 验收与测试：`AG-005`、`AG-010`、`AG-015`。覆盖正常 effect、错误映射、参数闭合/边界、提示注入不解释、密码/支付/file/隐藏/跨源/过期/旧 generation 拒绝且 port 零副作用、同幂等 key 只执行一次、LCG mutation/fuzz 无 panic、永久禁止 surface scan、最大合法参数与批量拒绝保持调用数/输出字节有界。命令：目标 crate/security/perf tests、clippy `-D warnings`、fmt、workspace、fast/security、`git diff --check`。
- 明确不做：Action Map 生产/读页（AGT-07/CNT-08）、真实 CEF locator/executor 装配、grant/确认 UI 签发、CLI/MCP（AGT-13/14）、AGT-12C 产品 accept loop、墙钟 benchmark/soak（QAR）。

### AGT-15 完成记录（2026-08-30）

- 实现：gateway 新增 `tools/semantic.rs`，将冻结的 `act.invoke(action_id!,args?)` 从经复检的 `CaapRequest` 收敛为 `SemanticInvokeRequest`；复用 ACT 强类型 `ActionHandleId`/`IdempotencyKey`，参数键闭合、argument ≤512B 且控制字符受限。唯一 `SemanticActionPort` 不暴露 selector/DOM/JS/CDP/文件/网络能力，Browser-owned port 继续唯一拥有 verified facts、risk/precondition/confirmation/effect；terminal `EffectReport` 只产生一个 final CAAP chunk。新增 agent security/perf workspace Harness，无第三方依赖。
- 验证：`cargo test -p crayon-agent-gateway` 97/97；`cargo test -p crayon-agent-security-tests -p crayon-agent-perf-tests` 5/5 + 2/2；其中 2000 组 LCG hostile 参数、13 类永久禁止 surface、提示注入 opaque、敏感/隐藏/跨源/旧 generation 零执行、AGT-03 同幂等 key 只派发一次、1024 次线性调用与最大参数单 chunk 全通过。`cargo clippy -p crayon-agent-gateway -p crayon-agent-security-tests -p crayon-agent-perf-tests --all-targets --no-deps -- -D warnings`、`cargo fmt --all -- --check`、`cargo test --workspace`、`scripts/check.ps1 fast`、`scripts/check.ps1 security`、`git diff --check` 全通过；Windows workspace Markdown golden 无 CRLF 回归。
- Code Review：按 v0.8 独立复核需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性；增加 typed request 进入 adapter 时的 `validate()` 纵深复检。P0/P1/P2=`0/0/0`。纯同步无锁/IO/日志；argument 与 effect 都有硬上限；页面/参数指令不参与授权、目标或第二工具选择。
- 未覆盖与风险：真实 CEF locator/executor、grant/确认 UI、session/tool dispatch/receipt 的产品进程装配仍归 AGT-12C/后续装配；Action Map 生产仍等 `CNT-08/AGT-07`；墙钟 benchmark/soak 归 QAR。因这些平台装配尚未闭合，`AGT-15` 为 `VERIFIED` 而非 `DONE`。
