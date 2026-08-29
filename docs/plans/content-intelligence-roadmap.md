# CNT 页面数据、Markdown 与第二阶段模型 Roadmap

- 状态：C1 执行中；`CNT-01..03 DONE`，`CNT-04 READY`
- 任务数：16
- C1 开始门禁：`CEF-15`、`BUX-18`、`SDK-14`、`MED-19`、`PRV-08`
- M2 开始门禁：`CNT-10`、`AGT-16`、`PRV-13`

## 1. 范围

- C1：确定性当前页快照、结构化内容、Markdown、预览/复制/保存，为用户和 Agent R1 共用。
- `CNT` 只拥有 verified `PageSnapshot`、正文/Markdown 和基础 revision；Action/Form/Media/Risk Map、action_id、前置条件、effect 和面向动作的 ChangeSet 由 `ACT` Roadmap 拥有并复用该数据面。
- M2：模型/provider 决策后，提供用户确认的文档总结与基于合法文本来源的视频总结。
- 非目标：批量爬取、后台站点遍历、隐藏字幕接口、媒体下载、未授权 ASR、模型参与权限/DRM/投屏安全决策。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| CNT-01 | DONE | CEF-15,BUX-18,SDK-14,MED-19,PRV-08 | `crayon-content-contract/**`,`crayon-page-data/**` | 定义 `PageSnapshot`、结构块、provenance、revision、截断与资源上限 | `CT-001`,`CT-002`; schema/golden | C1 |
| CNT-02 | DONE | CNT-01,CEF-01B | `browser/engine-api/**`,`apps/desktop-cef/**`,`crayon-browser-gateway/**` | 在跨引擎接口增加有界 snapshot stream/cancel，并实现 Renderer 分块采集与 Browser 来源/navigation验证 | `CT-001`,`CT-002`,`CT-007`; interface contract/integration | C1 |
| CNT-03 | DONE | CNT-02 | `crayon-page-data/**`,`crayon-app-runtime/**` | snapshot owner、generation 缓存、取消、分页和旧结果丢弃 | `CT-002`,`CT-007`; integration | C1 |
| CNT-04 | READY | CNT-03 | `crayon-content-extract/**` | 确定性主正文、阅读顺序和结构块识别 | `CT-003`,`CT-008`; fixture/unit | C1 |
| CNT-05 | TODO | CNT-04 | `crayon-content-markdown/**` | 标准 Markdown 转换与稳定转义 | `CT-003`,`CT-004`; golden | C1 |
| CNT-06 | TODO | CNT-05 | `crayon-content-markdown/**` | 列表、引用、代码、表格、链接和图片引用规范化 | `CT-004`,`CT-005`; golden/security | C1 |
| CNT-07 | TODO | CNT-03,CNT-06 | `crayon-page-data/**`,`tests/perf/content/**` | 字段索引、增量 revision、流式/背压和 C1 性能基线 | `CT-006`,`CT-008`; benchmark/soak | C1 |
| CNT-08 | TODO | CNT-06,CNT-07,PRV-08 | `apps/desktop-cef/**`,`crayon-platform-api/**` | 本地预览、复制、保存、取消、覆盖和失败反馈 | `CT-005`,`CT-006`; UI integration | C1 |
| CNT-09 | TODO | CNT-08 | `tests/**`,`test-support/**` | 正确性、安全、导航竞争、超大页面、资源释放与 E2E | `CT-001..008`; E2E/security/perf | C1 |
| CNT-10 | TODO | CNT-09 | `docs/current/**`,`docs/plans/**` | C1 独立 Review 与 Agent data-plane 接口冻结 | CT-001..008；P0/P1=0 | C1 |
| CNT-11 | TODO | CNT-10,AGT-16,PRV-13 | ADR,`crayon-model-contract/**`,`docs/current/**` | 决定本地/云端/BYOK/provider、地区、费用、保留、密钥和数据发送契约 | `CT-009`; ADR/contract；未决策不开网络 | M2 |
| CNT-12 | TODO | CNT-11 | `crayon-model-adapter/**`,`crayon-profile/**` | provider registry、安全存储、origin/redirect、发送前 payload preview 和 Fake provider | `CT-009..011`; security/integration | M2 |
| CNT-13 | TODO | CNT-12 | `crayon-content-ai/document/**`,`crayon-app-runtime/**` | 当前文档摘要、要点、大纲/问答，绑定 snapshot/hash 与引用 | `CT-010..013`; Fake provider | M2 |
| CNT-14 | TODO | CNT-12,MED-07 | `crayon-content-ai/video/**`,`crayon-app-runtime/**` | 基于用户可见字幕/转录或用户文本的视频总结输入契约；无文本时明确拒绝 | `CT-010`,`CT-014`; 无媒体下载/隐藏接口 | M2 |
| CNT-15 | TODO | CNT-13,CNT-14 | `apps/desktop-cef/**`,locales,tests | AI UI、provider/字段预览、引用、取消、错误和本地 Markdown 降级 | `CT-011..014`; UI/E2E | M2 |
| CNT-16 | TODO | CNT-15 | threat model,Review,`docs/current/**` | 模型数据流、成本/隐私/安全/性能 Review 和 feature Go/NoGo | CT-009..014；P0/P1=0 | M2 |

## 3. 数据不变量

- page snapshot 与 Markdown 是无模型也可用的基础能力。
- 页面脚本不能触发文件写入、Agent grant 或模型网络请求。
- Agent R1 grant 不等于模型发送授权；每次 provider payload 单独预览/确认。
- 模型不接收 Cookie、Authorization、浏览历史、完整敏感 query、隐藏 DOM、跨源正文或其他标签。
- 视频总结首期不下载媒体/音轨、不绕 DRM、不调用隐藏字幕 API；无合法文本来源即明确不支持。
- 模型结果为 untrusted，不能触发 CAAP 工具、改变投屏策略或写回页面。

## CNT-01 原子范围（PageSnapshot schema 冻结）

- 状态：`DONE`；依赖（C1 门禁五项全部满足：CEF-15/BUX-18/SDK-14/MED-19/PRV-08）。
- 路径说明：Roadmap 允许路径含 `crayon-content-contract/**` 与 `crayon-page-data/**` 两个名字；为避免单 schema 双 crate，契约类型落在新 crate **`crayon-page-data`** 的 `snapshot.rs`（CNT-03 的 owner/cache 亦在此 crate 扩展），不建空壳 `crayon-content-contract`。
- 单一目标：冻结 `PageSnapshot` wire schema——navigation 引用（TabId+SessionGeneration）、脱敏 URL/title、闭合 nine-kind 内容块、provenance 恒等声明、revision 与 truncation 显式信息、compact/standard 两级资源上限；serde deny_unknown_fields 全覆盖 + 构造校验/解码复检 + current/previous golden。本任务不做采集、正文识别与 Markdown 转换。
- 输入：CT-001（字段/顺序/schema 正确、节点/字节有界）、CT-002 模型部分（超大/畸形拒绝）、CT-003 类型部分（危险 URL 在 schema 层被拒）、PRD §4.3、FND-08 golden 机制与 SchemaVersion、domain TabId/SessionGeneration。
- 输出与允许修改：新 crate `crates/crayon-page-data/{Cargo.toml,src/snapshot.rs,src/snapshot_tests.rs}`、根 Cargo.toml members、`schemas/current|previous/page_snapshot_*.json` golden、本 Roadmap。零第三方新增（serde/serde_json 沿用）。
- 禁止修改：domain/FND-08 既有契约与 golden、其他 crate；不得出现采集逻辑、DOM/HTML/CDP/对象指针形态字段；不得引入网络/IO。
- 边界：
  - 内容块闭合九类：heading(1..=6)/paragraph/list_item(depth ≤8, 有序号可选)/link/image/table/code_block/divider/quote；inline 结构不做独立块类型。
  - URL 白名单仅 `http://`/`https://` 绝对地址（≤2048 字节、无控制字符）——`javascript:`/`data:`/`blob:` 等在 validate 即拒绝，不可存储；image 只存引用元数据（alt/src），不含加载结果。
  - 上限：standard 级 block ≤4096 个、单块文本 ≤16384 字节、总量 ≤1 MiB；compact 级 512/2048/128 KiB；table 行 ≤256 列 ≤32、cell ≤1024 字节、code ≤32768 字节、title ≤512 字节。
  - provenance 为恒等声明：`verified_by` 必须等于 `"browser_process"`（页面伪造来源在 validate 被拒）；truncation 必须显式携带 omitted_blocks/omitted_bytes/reasons（闭合枚举 limit_block_count/limit_total_bytes/limit_depth）。
  - 解码后 `validate()` 复检全覆盖；截断时快照仍须满足同一上限集。
- 验收与测试：CT-001、CT-002/003 模型部分。矩阵：golden 往返逐字节一致与 previous 镜像、九类块 roundtrip、两级上限差异、危险 URL 拒绝矩阵、伪造 provenance/畸形/未知字段/超限拒绝、确定性伪 fuzz 不 panic。命令：`cargo test -p crayon-page-data`、clippy `-D warnings`、fmt、workspace 回归、`git diff --check`。
- 明确不做：Renderer 分块采集（CNT-02）、snapshot owner/generation 缓存（CNT-03）、正文识别（CNT-04）、Markdown（CNT-05/06）。

### CNT-01 完成记录（2026-08-30）

- 实现：将既有 WIP 补成可构建的 `crayon-page-data` workspace crate，冻结 `PageSnapshot` v1。Envelope 以 `TabId + SessionGeneration + revision` 形成稳定导航绑定，构造时固定 `SchemaVersion::CURRENT` 与 `browser_process` provenance；wire 解码执行二次 `validate()`。内容块为 heading/paragraph/list_item/link/image/table/code_block/divider/quote 九类闭合集合；URL 仅接受无 userinfo、无控制/空白/反斜杠的绝对 HTTP(S) 引用。standard/compact 的 block、单文本、总文本预算以及 table/code/list/title/URL 形状上限均有命名常量。
- 截断与兼容：truncation 使用闭合 `TruncationReason` 列表，拒绝缺省原因、零 omitted、非截断却声明 omitted、重复原因和未知枚举；所有 struct/variant 拒绝未知字段。新增 current/previous `page_snapshot_v1.json`，逐字节镜像并由 roundtrip 测试锁定。v1 不携带采集时间、DOM/HTML/CDP、selector、对象指针或独立权限材料；采集、来源验证、缓存和 Markdown 均未越界进入本任务。
- 测试：新增 7 项 CT-001/002/003 契约测试，覆盖九类块、golden、两级预算差异、危险 URL、伪造 provenance、未知字段、形状/截断异常和 512 组确定性畸形输入不 panic。
- 验证：`cargo fmt --package crayon-page-data -- --check`、`cargo fmt --all -- --check` 通过；`cargo clippy -p crayon-page-data --all-targets -- -D warnings` 通过；`cargo test -p crayon-page-data` 7/7；`cargo test -p crayon-browser-core --lib` 3/3；`cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib` 58/58；`cargo test --workspace` 全部通过（Relay 2 个长稳测试按既有配置 ignored）；`bash scripts/check.sh fast` 通过；`bash scripts/check.sh security` 沙箱内首次因 loopback bind 返回 `Operation not permitted`，在获批的沙箱外同命令重跑通过；`git diff --check` 通过。
- Code Review：按 v0.8 顺序复核需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试与可维护性。Review 关闭既有 WIP 的缺失 link 第九类块、开放 truncation bitmask、URL/Unicode 控制字符、可公开伪造 provenance helper 和 URL 校验热路径分配问题；最终 P0/P1/P2 = 0/0/0。`validate_block` 约 101 行触发一级提醒，保持为九类闭合 enum 的单一穷尽校验与字节计数入口，无锁/IO/回调；拆散会增加形状校验与总预算记账漂移风险。
- 未覆盖与风险：Renderer 分块采集、Browser 来源/navigation 验证与取消归 `CNT-02`；owner/cache/旧 generation 归 `CNT-03`。无平台或真机门禁；`CNT-02` 依赖满足，转为 `READY`。

## CNT-02 原子范围（有界 snapshot stream 与 Browser 校验）

- 状态：`DONE`；依赖 `CNT-01 DONE`、`CEF-01B DONE`。
- 路径映射：Roadmap 中 `apps/desktop-cef/**` 映射现仓库 `browser/cef-shell/**`；`crayon-browser-gateway/**` 映射 Browser process 下的新 `browser/cef-shell/src/browser/page_snapshot_gateway/**`，不新建同义空壳 crate。
- 单一目标：建立一次当前导航快照请求从 Renderer 分块采集到 Browser 验证后向调用方流出的有界、可取消通道。跨引擎接口只暴露闭合结构事实与终止状态；Browser process 绑定请求、tab、navigation、renderer process/frame 与严格递增 sequence，页面消息不能自称可信来源。
- 输入：CNT-01 `PageSnapshot` 九类闭合结构与预算、CT-001/002/007、engine-api 强类型与异步回调约束、CEF Renderer/Browser 生命周期和现有 generation fencing 模式。
- 输出与允许修改：`browser/engine-api/**` 增加 `SnapshotRequestId`、request/chunk/terminal/sink 与 `StartSnapshot`/`CancelSnapshot`；`browser/cef-shell/src/renderer/page_snapshot_collector/**` 增加单线程、主 frame、可见事实分块采集器；`browser/cef-shell/src/browser/page_snapshot_gateway/**` 增加 Browser-issued request/source/navigation/sequence/预算校验和有界 drain；根 CMake 仅接入上述目标；本 Roadmap 记录证据。
- 禁止修改：`crayon-page-data` v1 schema、App runtime、UI/locales、缓存/分页 owner、正文识别和 Markdown；不得携带 DOM/HTML/CDP/selector、Cookie、Authorization、隐藏或跨源正文；不得加入 CEF 类型到 engine-api。
- 边界：每请求最多 64 个 chunk、每 chunk 最多 64 个事实/64 KiB、在途验证队列最多 16 个 chunk；sequence 从 0 严格递增且恰有一个 terminal。取消、导航、tab 关闭和 teardown 幂等，清除未消费数据并禁止晚到 callback；满载 fail closed 返回显式 backpressure，不阻塞、不重试。Renderer 只接收主 frame、当前 navigation、可见且同源的已规范化事实；Browser 再校验可信 IPC source process/frame、request/tab/navigation 和所有预算。
- 验收与测试：engine-api contract 覆盖有界 stream、异步 callback、取消/关闭/导航/stop fence；Renderer unit 覆盖分块、超限、隐藏/跨源/子 frame、取消/teardown；Browser integration 覆盖页面伪造、错误 process/frame、旧 navigation、乱序/重复/final 后消息、超限/backpressure 与 drain。运行独立 CMake configure/build/ctest、共享层回归、clang-format、`git diff --check`。
- 明确不做：Snapshot owner、generation cache、跨请求分页和旧结果替换（CNT-03）；DOM 主正文算法（CNT-04）；Markdown/UI/文件写入（CNT-05..08）；真实 CEF IPC 编解码接线及平台真机门禁不在本纯契约/状态机任务内。

### CNT-02 完成记录（2026-08-30）

- 实现：engine-api 新增 `SnapshotRequestId`、standard/compact 请求模式、九类闭合结构事实、首块 document metadata、chunk/terminal/sink 以及 `StartSnapshot`/`CancelSnapshot`。单 chunk 为 64 facts/64 KiB、单流 64 chunks，且复用 CNT-01 的 4096/512 facts、1 MiB/128 KiB 总预算；UTF-8、控制字符、URL、标题、列表、表格、code/language 和所有 kind 专属字段均 fail closed。Fake adapter 锁定异步 callback、唯一 terminal、取消/导航/close/stop fence 与 sink 生命周期契约。
- Renderer：新增单线程 `PageSnapshotCollector`，只接收当前 navigation、指定 main frame、可见且同源的规范化事实；按事实数和字节预算分块，空正文仍先发送 Browser 可验证的 URL/title metadata。取消丢弃 partial chunk，teardown 禁止晚到 callback；热路径使用常数预算记账，不逐事实复制整块。
- Browser：新增 `PageSnapshotGateway`，Browser-issued request 绑定 tab/navigation、可信 renderer process/frame 和 Browser 当前 URL；拒绝页面自报来源、错误 process/frame、旧导航/URL、乱序/重复、畸形、超限和 terminal 后消息。队列为 16 event 且为每个 active request 预留 terminal 容量，满载显式 backpressure；取消/导航/关闭清除未消费 chunk，retired request 窗口固定 128，shutdown 释放全部状态。
- 测试：engine-api contract 与 header/forbidden scan、Renderer unit、Browser integration 覆盖 CT-001/002/007 的结构/预算、异步流、metadata、隐藏/跨源/子 frame、伪造来源、旧 navigation、乱序/重复、取消、backpressure、bounded retirement、teardown/shutdown。最终目标集 `browser_engine_contract|headers_compile|page_snapshot_collector|page_snapshot_gateway` 为 4/4 通过。
- 验证：`cmake -S . -B .cache/build/cnt02 -G Ninja -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF`、`cmake --build .cache/build/cnt02` 通过；`ctest --test-dir .cache/build/cnt02 --output-on-failure` 59/59 通过（含 `cef_build_graph_contract`）；最终 Review 修正后目标集再次 4/4；`bash scripts/check.sh fast` 沙箱内因 9 项 loopback bind 返回 `Operation not permitted`，沙箱外同命令重跑通过；沙箱外 `bash scripts/check.sh security` 通过；clang-format dry-run 与 `git diff --check` 通过。
- Code Review：按 v0.8 完成需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性审查。Review 期间关闭缺失 document URL/title、Browser URL 复核、completed-without-metadata、开放文本控制字符/畸形 UTF-8、compact 总预算、列表/表格/code 形状、热路径复制和无界 retired 集合；最终 P0/P1/P2 = 0/0/0。
- 未覆盖与风险：真实 CEF IPC 编解码接线随平台 adapter 集成推进；本任务交付的是平台中立接口和 Renderer/Browser 可独立验证状态机，无真机门禁。Snapshot owner、generation cache、分页和旧结果替换进入 `CNT-03 READY`。

## CNT-03 原子范围（snapshot owner 与 generation 分页缓存）

- 状态：`DONE`；依赖 `CNT-02 DONE`。
- 单一目标：为 Browser 验证完成的 `PageSnapshot` 建立唯一 owner，按 tab/generation/revision 接收或丢弃结果，并向当前页消费者提供有界、可取消、不会跨导航漂移的分页读取。page-data 拥有纯状态机与分页值类型，app-runtime 只负责线程安全装配，不复制缓存规则。
- 输入：CNT-01 `PageSnapshot`/`NavigationBinding`/revision，CNT-02 cancel/terminal 与 generation fence，CT-002/007，app-runtime 既有“锁内不做外部调用”纪律。
- 输出与允许修改：`crayon-page-data/src/owner.rs` 与独立测试，公开 bounded owner/read/page/error/stats；`crayon-app-runtime/src/page_snapshot_runtime.rs` 与独立测试，装配 `Arc<Mutex<SnapshotOwner>>` 并提供导航、publish、begin/next/cancel/close/shutdown 用例；对应 Cargo 依赖和本 Roadmap。
- 禁止修改：PageSnapshot v1 wire schema/golden、CNT-02 C++ contract、正文识别、Markdown、UI、文件/网络 IO；不得缓存 DOM/HTML/CDP、不得新增持久化或后台线程。
- 边界：最多 16 个 tab snapshot、32 个 active read、每页 1..=256 blocks、128 个 bounded retired read ID；新 generation 原子清除旧 snapshot/read，旧 generation/revision 永不覆盖；同 revision 相同值幂等，不同值拒绝；pagination 固定绑定 tab+generation+revision，publish 新 revision 立即使旧 read stale。容量先逐出无 active read 的最旧 tab，否则显式 backpressure；cancel/close/shutdown 幂等且释放 clone。
- 验收与测试：CT-002/007 integration，覆盖正常/空快照、多页、非法页大小、旧 generation/revision、同 revision 冲突、新 revision/导航竞争、取消重复、close/shutdown、tab/read/retired 容量和 mutex poison 恢复；运行 package test/clippy/fmt、workspace/fast/security 回归和 `git diff --check`。
- 明确不做：增量字段索引与性能 soak（CNT-07）、正文提取（CNT-04）、跨进程 IPC 编解码、持久化恢复、UI/文件写入。

### CNT-03 完成记录（2026-08-30）

- 实现：`crayon-page-data` 新增纯内存 `SnapshotOwner`，以 tab/generation/revision 为唯一缓存键语义；旧 generation/revision 丢弃、同 revision 同值幂等而异值拒绝，新导航或新 revision 原子失效进行中的 read。分页 read 固定绑定创建时的 navigation 与 revision，页大小限制 1..=256 blocks；cancel、close、shutdown 均幂等释放状态并保留有界终态原因。
- 资源与装配：缓存最多 16 tabs、32 active reads、128 retired reads；容量满时仅 LRU 逐出无 active read 的 tab，否则显式 `CapacityExceeded`。`PageSnapshotRuntime` 作为 app-runtime 唯一线程安全装配入口，用 `Mutex<SnapshotOwner>` 保护纯内存状态，锁内无回调、网络、文件 IO 或等待，并在 mutex poison 后恢复状态访问。
- 测试：新增 10 项 owner/runtime 测试，覆盖 CT-002/007 的空快照、多页与终止、非法页大小、旧 generation/revision、同 revision 冲突、导航/新 revision 竞争、重复取消、close/shutdown、tab/read/retired 容量、LRU 和 mutex poison 恢复。
- 验证：`cargo clippy -p crayon-page-data -p crayon-app-runtime --all-targets -- -D warnings`、`cargo fmt --all -- --check` 通过；`cargo test -p crayon-page-data` 14/14；`cargo test -p crayon-app-runtime --lib` 11/11；`cargo test --workspace` 全部通过（Relay 2 个长稳测试按既有配置 ignored）；`bash scripts/check.sh fast` 与 `bash scripts/check.sh security` 通过；`git diff --check` 通过。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试与可维护性；最终 P0/P1/P2 = 0/0/0。所有集合和单页 clone 均有常数上限，状态机不保存 DOM/HTML/CDP，不新增线程、持久化或 IO。
- 未覆盖与风险：增量字段索引、流式性能基线归 `CNT-07`；正文识别进入 `CNT-04 READY`。跨进程 IPC 编解码、UI 与文件写入仍不在本任务范围；无平台或真机门禁。
