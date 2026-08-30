# CNT 页面数据、Markdown 与第二阶段模型 Roadmap

- 状态：C1 已收口；`CNT-01..10 DONE/VERIFIED`（CNT-08 VERIFIED、CNT-10 DONE 2026-08-30）；M2 等 `AGT-16/PRV-13` 与 provider ADR
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
| CNT-04 | DONE | CNT-03 | `crayon-content-extract/**` | 确定性主正文、阅读顺序和结构块识别 | `CT-003`,`CT-004`; fixture/unit | C1 |
| CNT-05 | DONE | CNT-04 | `crayon-content-markdown/**` | 标准 Markdown 转换与稳定转义 | `CT-003`,`CT-005`; golden | C1 |
| CNT-06 | DONE | CNT-05 | `crayon-content-markdown/**` | 列表、引用、代码、表格、链接和图片引用规范化 | `CT-005`,`CT-006`; golden/security | C1 |
| CNT-07 | DONE | CNT-03,CNT-06 | `crayon-page-data/**`,`tests/perf/content/**` | 字段索引、增量 revision、流式/背压和 C1 性能基线 | `CT-006`,`CT-008`; benchmark/soak | C1 |
| CNT-08 | VERIFIED | CNT-06,CNT-07,PRV-08 | `apps/desktop-cef/**`,`crayon-platform-api/**` | 本地预览、复制、保存、取消、覆盖和失败反馈 | `CT-005`,`CT-006`; UI integration | C1 |
| CNT-09 | DONE | CNT-08 | `tests/**`,`test-support/**` | 正确性、安全、导航竞争、超大页面、资源释放与 E2E | `CT-001..008`; E2E/security/perf | C1 |
| CNT-10 | DONE | CNT-09 | `docs/current/**`,`docs/plans/**` | C1 独立 Review 与 Agent data-plane 接口冻结 | CT-001..008；P0/P1=0 | C1 |
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

## CNT-04 原子范围（确定性主正文与阅读顺序）

- 状态：`DONE`；依赖 `CNT-03 DONE`。
- 验收编号校正：原任务行写 `CT-003,CT-008`，但权威 `docs/current/test-cases.md` 中主正文、阅读顺序、空页/导航页/重复节点/无限列表是 `CT-004`；`CT-008` 是预览导航/标签/Profile 生命周期，generation 栅栏由 `CNT-03` 提供、UI 验收归 `CNT-08`。本任务按 `CT-003,CT-004` 执行并已同步任务行。
- 单一目标：新增纯 Rust、无 IO 的 `crayon-content-extract`，从 Browser 已规范化的可见语义事实中确定性选择主内容区域，按稳定 section/column/row/source 顺序输出 CNT-01 的闭合 `ContentBlock`，并显式统计隐藏、跨源、敏感、非正文、重复、危险引用和预算丢弃。
- 输入：CNT-02 Browser 验证后的主 frame/current navigation/同源可见事实语义，CNT-01 九类 block/两级预算，CT-003/004；输入不包含 DOM/HTML/selector/CEF handle、表单值、脚本源码、Cookie、Authorization 或页面存储。
- 输出与允许修改：新 crate `crates/crayon-content-extract/**`、workspace member 和本 Roadmap。公开闭合 region/privacy/content fact、稳定 reading key、提取结果/排除计数和错误；不修改 `PageSnapshot` v1 schema。
- 禁止修改：Renderer/Browser IPC、page-data owner/app-runtime、Markdown、UI/locales、文件/网络；不得引入 DOM parser、站点规则、模型、第三方依赖或持久化。
- 边界：standard/compact 最多消费 4096/512 source facts，超出部分计入 budget omission；只保留 visible + same-origin + public facts。主区域按闭合 semantic region、有效文本量、结构块数和 link density 的常数有界评分选择，稳定 tie-break 使用 region id；navigation/header/footer/complementary 不得成为主正文。重复 node id 只取 reading key 最小的一项；危险 link/image URL、空/超限形状不输出。排序只使用整数 key，不读取布局对象或运行时 locale。
- 验收与测试：fixture/unit 覆盖 CT-003/004 的密码/隐藏/跨源/危险 URL、长文/多栏、空页、纯导航、重复节点、无限列表、区域 tie-break、九类结构与两级预算；package test/clippy/fmt、workspace/fast/security 回归和 `git diff --check`。
- 明确不做：Markdown 序列化/转义（CNT-05/06）、增量 revision/字段索引/streaming benchmark（CNT-07）、预览导航/UI/无障碍（CNT-08）、真实 DOM/CEF 接线或平台真机门禁。

### CNT-04 完成记录（2026-08-30）

- 实现：新增 `crayon-content-extract` 纯 Rust crate，输入仅为 Browser 规范化的闭合语义 fact；以 main/article/unknown 闭合 region、有效文本量、结构块数和 link density 进行常数有界评分，稳定 tie-break 选择主区域，并按 section/column/row/source key 输出 CNT-01 九类 `ContentBlock`。navigation/header/footer/complementary 永不成为主正文，unknown 的纯链接区域同样明确排除。
- 安全与预算：仅处理 visible + same-origin + public fact；隐藏、跨源、敏感控件、危险 URL、空/畸形/超限结构、重复 node id 和非正文区域均不输出且分别计数。standard/compact 输入上限 4096/512，输出同时执行对应 block、单字段与总文本预算；被截事实数和字节数显式记录。API 不可表达 DOM/HTML/selector/脚本/表单值/CEF handle，无 IO、线程、锁、网络、站点规则或第三方依赖。
- 测试：10 项 fixture/unit 覆盖 CT-003/004 的敏感/隐藏/跨源/危险 URL、长文多栏稳定排序、空页、显式/unknown 纯导航、重复节点、无限列表、区域 tie-break、九类结构、畸形形状和两级 source/total-byte 预算。
- 验证：`cargo test -p crayon-content-extract` 10/10；`cargo clippy -p crayon-content-extract --all-targets -- -D warnings`、`cargo fmt --all -- --check` 通过；`cargo test --workspace` 全部通过（沙箱内首次因既有 loopback 测试 `Operation not permitted`，沙箱外重跑通过，Relay 2 个长稳测试按既有配置 ignored）；`bash scripts/check.sh fast` 与 `bash scripts/check.sh security` 通过；`git diff --check` 通过。
- Code Review：按 v0.8 完成需求/边界、正确性、架构/API、安全/隐私、性能、测试和可维护性审查；期间补齐总文本预算、omitted bytes 与 unknown 纯链接导航拒绝；CNT-05 跨任务接口复核又关闭 source list depth `0` 与 PageSnapshot `1..=8` 不一致的问题并补回归。最终 P0/P1/P2 = 0/0/0。排序/分组均受 4096/512 上限约束，无锁内调用、递归、日志或整树重复序列化。
- 未覆盖与风险：真实 Renderer/Browser 语义 fact 接线不在本纯算法任务；Markdown 转换进入 `CNT-05 READY`，列表/表格/链接等规范化归 `CNT-06`，预览生命周期和无障碍归 `CNT-08`。无平台或真机门禁。

## CNT-05 原子范围（确定性 Markdown 与基础转义）

- 状态：`DONE`；依赖 `CNT-04 DONE`。
- 验收编号校正：原任务行写 `CT-003,CT-004`；权威测试表中 Markdown 可复现、Unicode、转义和 HTML/script 注入是 `CT-005`，因此本任务按 `CT-003,CT-005` 执行。CT-004 主正文/阅读顺序已由 CNT-04 完成。
- 单一目标：新增 `crayon-content-markdown`，将已验证 `PageSnapshot` 的九类 block 以固定空行、换行和统一 CommonMark 标点转义生成确定性 UTF-8 Markdown，并返回绑定 navigation/revision 的不可变文档值。
- 输入：CNT-01 `PageSnapshot`/九类闭合 block/两级预算，CNT-04 稳定 block 顺序，CT-003/005；不重新读取页面、DOM 或文件。
- 输出与允许修改：新 crate `crates/crayon-content-markdown/**`、workspace member和本 Roadmap；current/previous basic golden 逐字节镜像。零第三方新增。
- 禁止修改：PageSnapshot schema/owner、正文提取、Renderer/Browser、UI/locales、文件/剪贴板/网络；不得输出原始 HTML，不得调用 md4c/Markdown Runtime 或模型。
- 边界：所有页面文本按统一字符扫描转义 Markdown/HTML 起始标点，CR 已被 schema 拒绝，LF/tab 保留；heading/paragraph/divider 直接映射，列表/引用/链接/图片/表格使用安全基础表示，code 使用四空格缩进避免 fence 注入。standard/compact 输出分别限制 1536 KiB/192 KiB，超限显式失败且不返回 partial 文本。空快照输出空串，非空输出恰一个末尾 LF。
- 验收与测试：CT-003/005 golden/unit 覆盖九类 block、Unicode、全部转义标点、HTML/script、换行、空页、确定性重复渲染、current/previous golden 与输出预算；package test/clippy/fmt、workspace/fast/security 和 `git diff --check`。
- 明确不做：列表层级/有序编号、quote continuation、动态 code fence/language、GFM table、link/image URL 去 query/fragment 与引用去重（CNT-06）；流式/增量（CNT-07）；预览/复制/保存（CNT-08）。

### CNT-05 完成记录（2026-08-30）

- 实现：新增零第三方依赖的 `crayon-content-markdown` crate，从已验证 `PageSnapshot` 生成绑定 TabId/generation/revision 的不可变 `MarkdownDocument`。九类 block 均有确定性基础表示，页面文本按单次字符扫描统一转义反斜杠、CommonMark 标点和 HTML tag 边界；code 使用四空格缩进，避免页面内容闭合 fence。
- 稳定性与预算：block 之间固定两个 LF，空表示（如空 image alt）不生成伪段落，空快照输出空串，非空结果恰一个末尾 LF。standard/compact 输出硬上限 1536 KiB/192 KiB，在提交 block 前检查并以 `OutputTooLarge` fail closed，不返回 partial 文本；渲染不含 URL、DOM/HTML、文件/剪贴板/网络/模型或日志。
- Golden 与测试：current/previous `basic.md` 逐字节镜像，锁定 Unicode、九类 block、换行与安全基础表示；6 项 unit/golden 覆盖 CT-003/005 的 script/HTML、全部 Markdown 标点、空页、重复渲染、metadata 绑定、空 alt、末尾 LF 和输出扩张超预算。
- 验证：`cargo test -p crayon-content-markdown` 6/6；`cargo clippy -p crayon-content-markdown --all-targets -- -D warnings`、`cargo fmt --all -- --check` 通过；`cargo test --workspace` 全部通过（Relay 2 个长稳测试按既有配置 ignored）；`bash scripts/check.sh fast` 与 `bash scripts/check.sh security` 通过；`git diff --check` 通过。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、安全/隐私、性能、测试与可维护性；关闭空 block 分隔、末尾多 LF 和预算追加时机问题，最终 P0/P1/P2 = 0/0/0。单 block 临时缓冲受 PageSnapshot 1 MiB/128 KiB 总预算约束，无递归、锁、IO 或不受控增长。
- 未覆盖与风险：列表层级/有序号、动态 fence/language、GFM 表格和 link/image 引用规范化进入 `CNT-06 READY`；流式/增量和 UI/文件能力不在本任务。无平台或真机门禁。

## CNT-06 原子范围（复合结构与引用规范化）

- 状态：`DONE`；依赖 `CNT-05 DONE`。
- 验收编号校正：原任务行写 `CT-004,CT-005`；CT-004 的正文/顺序已由 CNT-04 完成，复合结构超量、重复、超长与稳定截断属于权威 `CT-006`。本任务按 `CT-005,CT-006` 执行。
- 单一目标：在 `crayon-content-markdown` 内把 CNT-05 安全基础表示升级为规范化 Markdown：嵌套有序/无序列表、逐行引用、不会被正文闭合的动态 code fence/language、矩形 GFM table，以及默认移除 query/fragment 的 link/image 引用；保持 basic golden 兼容入口。
- 输入/输出：仅消费 CNT-01 已验证的九类 `ContentBlock`，主 `render_snapshot` 输出规范化 Markdown；`render_basic_snapshot` 保留 CNT-05 逐字节行为。允许修改 `crates/crayon-content-markdown/**` 与本 Roadmap，零新增网络/IO/模型能力。
- 禁止修改：page-data schema/owner、content-extract、Renderer/Browser、UI/locales、文件/剪贴板/网络；不得输出原始 HTML、userinfo、query、fragment 或加载图片。
- 边界：列表 indent 和 continuation 从 depth/marker 宽度确定；code fence 长度为正文最大连续 backtick + 1 且至少 3；table 首行为 header、固定 `---` delimiter，cell 中 pipe 转义、换行规范为空格；link/image destination 仅保留既有安全 HTTP(S) URL 的 scheme/authority/path，并转义括号/反斜杠。沿用 CNT-05 输出硬上限，超限整体失败；空 alt 仍不生成图片引用。
- 验收与测试：CT-005/006 normalized golden/security 覆盖嵌套/有序/多行列表、引用、正文内 fence、language、Unicode table/pipe/newline、链接/图片 query/fragment/userinfo、重复引用、空 alt、超量和 deterministic repeat；package test/clippy/fmt、workspace/fast/security、`git diff --check`。
- 明确不做：字段索引/增量 revision/streaming 与性能 soak（CNT-07）、UI/复制/保存（CNT-08）、Markdown 反解析、远程资源加载或本地 MDV Runtime 接线。

### CNT-06 完成记录（2026-08-30）

- 实现：主 `render_snapshot` 升级为规范化 Markdown，`render_basic_snapshot` 保留 CNT-05 basic golden 行为。list 按 depth 输出两空格层级、有序 ordinal 与 marker 对齐 continuation；quote 逐行前缀；code fence 取正文最大连续 backtick + 1（至少 3）并保留闭合 language；table 输出矩形 GFM header/delimiter/body，cell pipe 转义、换行归一为空格。
- 引用与安全：link/image 使用内联引用，destination 默认删除 query/fragment 并转义反斜杠/括号；PageSnapshot 已拒绝 userinfo/危险 scheme，renderer 不加载 URL 或图片。空 alt 不输出引用；重复引用确定性重复呈现，无全局 dedup/cache 状态。沿用 CNT-05 整体输出预算，动态 fence/table 扩张仍在提交 block 前 fail closed。
- Golden 与测试：新增 `normalized.md` golden，并保持 current/previous basic golden 逐字节不变；9 项 unit/golden 覆盖 CT-005/006 的 Unicode/HTML 转义、嵌套有序多行列表、正文内四 backtick、language、GFM table/pipe/newline、link/image query/fragment、重复引用、空 alt、预算与重复渲染。
- 验证：`cargo test -p crayon-content-markdown` 9/9；`cargo clippy -p crayon-content-markdown --all-targets -- -D warnings`、`cargo fmt --all -- --check` 通过；`cargo test --workspace` 全部通过（Relay 2 个长稳测试按既有配置 ignored）；`bash scripts/check.sh fast` 与 `bash scripts/check.sh security` 通过；`git diff --check` 通过。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、安全/隐私、性能、测试和可维护性；修正 basic table 兼容回归，最终 P0/P1/P2 = 0/0/0。所有扫描受 PageSnapshot 预算约束，无网络、资源加载、HTML、锁、IO、递归或正文日志。
- 未覆盖与风险：字段索引、增量 revision、stream/backpressure 与 C1 性能基线进入 `CNT-07 READY`；UI/复制/保存仍归 CNT-08。无平台或真机门禁。

### CNT-05/06 Windows golden 行尾回归修复记录（2026-08-30）

- 根因：CNT Markdown 的 `basic.md`/`normalized.md`/previous golden 使用 `include_str!` 做逐字节契约比较，但根 `.gitattributes` 只为 `crates/**/tests/*.txt` 固定 LF；Windows `core.autocrlf=true` checkout 将这些 `*.md` 变为 CRLF，导致渲染器固定 LF 输出与工作区 fixture 不等。Git blob 本身仍为 LF，失败不来自 Markdown 行为变化。
- 修复：为 `crates/crayon-content-markdown/tests/golden/**` 明确 `text eol=lf`，保持 byte-exact golden 契约，不在测试中归一化或放宽断言。
- 验收：Windows `core.autocrlf=true` 下重新写入后，`basic.md`/`normalized.md`/previous `basic.md` 的 raw CRLF 计数均为 0，且 raw hash 与既有 Git blob 相同；`cargo test -p crayon-content-markdown` 9/9、`cargo test --workspace`、`./scripts/check.ps1 fast`、`./scripts/check.ps1 security`、`cargo clippy -p crayon-content-markdown --all-targets --no-deps -- -D warnings`、`cargo fmt --all -- --check`、`git diff --check` 均通过。按 v0.8 独立复核需求/边界、正确性、架构/API、安全/隐私、性能、测试和可维护性，P0/P1/P2 = 0/0/0；未覆盖与剩余风险：无。状态仍为 `CNT-05/06 DONE`，本记录只修跨平台 fixture checkout 契约。

## CNT-07 原子范围（索引、基础增量与性能基线）

- 状态：`DONE`；依赖 `CNT-03 DONE`、`CNT-06 DONE`。
- 验收映射：CT-006 覆盖复合字段超量与有界输出；CT-008 中 navigation/close/Profile 旧结果失效由 CNT-03 owner 提供，本任务补 revision stream 的 stale/cancel/backpressure；Agent 侧授权 Profile stream 与最终性能口径仍归 AGT-06/15。
- 单一目标：在 `crayon-page-data` 为已验证快照建立可复用字段索引，生成绑定同一 tab/generation 的基础 revision delta，并以固定窗口的有序 chunk/ack/cancel 状态机交付；新增本地 100KB benchmark 与确定性 revision soak，冻结 C1 预算。
- 边界澄清：CNT 只生成“公共数据块前后缀差分”的单 splice/replace-all，不定义 action_id、语义 effect 或动作 ChangeSet；后者仍由 ACT Roadmap 独占。
- 输出与允许修改：`crates/crayon-page-data/src/{index,delta}.rs` 及独立测试；`tests/perf/content/**` 本地 Harness；根 `Cargo.toml`/`Cargo.lock` 仅用于注册 Harness；本 Roadmap。禁止修改 PageSnapshot v1 wire schema/golden、owner 行为、Markdown/正文算法、UI、Renderer/Browser、网络/文件生产能力。
- 索引/增量预算：九类 block kind 各自保存有序 position，索引总 position 数等于 block 数且最多 4096/512；delta 要求 tab/generation 相同且 revision 严格递增，以最长公共 prefix/suffix 生成至多一个 splice。insert+delete ≤512 blocks 时发 splice，超过则 replace-all；记录 reused/inserted/deleted/serialized byte estimate，计数饱和且不含正文日志。
- 流式/背压：每 chunk 最多 64 blocks，总 chunk ≤64；最多 4 个 unacked chunk，满载返回 `Backpressure`，不阻塞/重试/扩容。sequence 从 0 严格递增，ack 必须按序且不可重复；删除-only/no-change 仍发送一个 metadata terminal chunk。cancel 幂等并释放 pending payload；generation/revision fence 不匹配立即 stale，terminal 后不可重开。
- 性能基线：固定本地约 100KB fixture，缓存字段索引 P95 ≤50ms，结构 delta + normalized Markdown P95 ≤500ms；记录 sample count、first chunk、complete、估算序列化字节与复用率。Harness 不使用公网、随机时钟或第三方 benchmark 依赖；阈值是 C1 防回归门禁，不宣称对外竞品优势。
- 验收：package unit 覆盖九类索引、空/同值/insert/delete/replace、旧 generation/revision、chunk sequence/terminal/ack/backpressure/cancel/容量；Harness 覆盖 100KB P95 与 10,000 步 bounded soak。命令：page-data test/clippy/fmt、perf package test、workspace/fast/security、`git diff --check`。
- 明确不做：跨 Profile Agent 授权、CAAP transport/deadline（AGT-06/15）、高频语义 ChangeSet（ACT-10）、UI event-loop/RSS 平台采样（QAR-05）、UI/复制/保存（CNT-08）。

### CNT-07 完成记录（2026-08-30）

- 实现：新增九类 `BlockKind` 的 revision 索引与 payload byte estimate；同一 tab/generation 且 revision 严格递增时，以最长公共 prefix/suffix 生成单一 `Splice`，变化超过 512 blocks 时退化为 `ReplaceAll`，同值 revision 仅发送 metadata。delta 记录起点、删除/插入、复用块数和估算序列化字节，不修改 PageSnapshot wire schema。
- 流式与生命周期：`DeltaStream` 每 chunk 最多 64 blocks、最多 4 个未确认 chunk，按序 sequence/ack，满载显式 `Backpressure`；generation/revision 不匹配立即 stale 并释放 payload，cancel 幂等。删除-only/no-change 仍发送一个 terminal metadata chunk；terminal 交付时立即释放内部 inserted payload，之后不可重开。
- 性能与测试：新增零第三方 benchmark 依赖的 `crayon-content-perf-tests`。固定约 100KB/100-block fixture、40 样本实测 index P95 5us、first chunk P95 6us、delta + normalized Markdown P95 2.555ms、估算序列化 1086 bytes、复用率 99%，均低于 50ms/500ms 门槛；10,000 revision bounded soak 通过。page-data 23/23，覆盖九类/空索引、同值/insert/delete/replace、旧 generation/revision、sequence/terminal/ack/backpressure/cancel 与 terminal payload 释放。
- 验证：`cargo fmt -p crayon-page-data -p crayon-content-perf-tests -- --check` 通过；`cargo clippy -p crayon-page-data -p crayon-content-perf-tests --all-targets --no-deps -- -D warnings` 通过；`cargo test -p crayon-page-data -- --test-threads=1` 23/23；`cargo test -p crayon-content-perf-tests -- --test-threads=1 --nocapture` 2/2；普通隔离 clone 应用同一 staged patch 后 `bash scripts/check.sh fast` 全部通过（guard/format/formal-workspace/legacy 58/58，Relay 2 个既有长稳测试 ignored）；主工作区 `bash scripts/check.sh security` 通过；`git diff --cached --check` 通过。主工作区全量 fmt/clippy/workspace 的失败由并发未提交的 ACT semantic schema/golden 改动触发，隔离验证确认 CNT-07 patch 本身全绿。
- Code Review：按 v0.8 复核需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试和可维护性；关闭 terminal 后内部 payload 延迟释放问题，最终 P0/P1/P2 = 0/0/0。实现无锁、无 IO/网络、无正文日志、无阻塞/重试，所有集合受 PageSnapshot 与固定窗口约束。
- 未覆盖与风险：Agent 跨 Profile stream/transport/deadline 仍归 AGT-06/15，语义动作 ChangeSet 归 ACT-10，UI/RSS 平台采样归 QAR-05；本地预览、复制、保存、取消与覆盖进入 `CNT-08 READY`。无平台或真机门禁。


## CNT-08 原子范围（本地预览、复制、保存、取消、覆盖和失败反馈）

- 状态：`VERIFIED`（导出控制器模型层 + 全量回归）；依赖 `CNT-06/07 DONE`、`PRV-08 DONE`。
- 单一目标：把 page→Markdown 转换结果的本地导出流（预览/复制/保存/取消/覆盖/失败反馈）冻结为可测的 view-model 控制器，并复用 MDV-06 原子保存。
- 输入与输出：输入为转换后的 Markdown payload（上游有界）与页面标题；输出仅限 `browser/shared-ui/page-tools/include/crayon/browser_page_tools/page_markdown_export.h`、`src/page_markdown_export.cc`、`tests/page_markdown_export_test.cc`、CMake 接线与本 Roadmap（`browser/shared-ui/**` 为 desktop-cef UI 共享层，属允许路径 `apps/desktop-cef/**` 的映射范围）。
- 语义：payload ≤1MiB 超界 fail closed 且不残留会话；`SuggestFilename` 对标题做非法字符折叠、空白收敛、128B 字节界内 UTF-8 截断与强制 `.md` 后缀、空回退 `page.md`；save-as 前探测存在→进入覆盖确认态，仅显式 `ConfirmOverwrite` 放行（CT-007 不静默覆盖）；取消清空会话与 pending 态零残留；保存失败映射到闭合 `kFailed` 并透出 MDV 残留 temp 路径；文件路径由用户选择目录 + 控制器只校验文件名部分。
- 验收：CT-005/CT-006 契约侧：预览/复制/保存/取消/覆盖/失败全状态、超界 payload 拒绝、非法文件名拒绝、取消无残留。
- 明确不做：CEF UI 实机接线与截图证据（归后续装配验证切片）、文件对话框平台实现（平台层已有契约）、复制系统调用（UI 层取 payload）。

### CNT-08 完成记录（2026-08-30）

- 实现：`PageMarkdownExportController` 组合 MDV-06 `MdvSaveController`（page-tools → mdv 私有依赖登记）；CMake 新增 `page_markdown_export_contract` 测试。
- 验证（macOS arm64，`CRAYON_CEF_ROOT` 指向已校验离线根）：`cmake --preset macos-arm64-cef-debug` configure 成功；全量 build 成功；`TEMP=/tmp ctest --preset macos-arm64-cef-debug` **72/72 通过**（含新增 page_markdown_export_contract 5 场景与既有 page_tools_contract）。
- Code Review：按 v0.8 复核；修正一处测试期望（调用方提供用户目录时仅校验文件名部分，目录+文件名为合法目标）。P0/P1/P2 = 0/0/0。
- 未覆盖与风险：真实 UI 接线（mdv handler/菜单入口/剪贴板系统调用）与真机截图证据未做；payload 由上游 content-markdown 管线产出（CNT-05/06 已冻结边界）。`CNT-09 READY`。

## CNT-09 原子范围（C1 E2E / security / perf 总矩阵）

- 状态：`DONE`（2026-08-30）；依赖 `CNT-08 VERIFIED`。
- 单一目标：在不改变 C1 生产契约的前提下，新增确定性测试 Harness，把 CT-001..008 已分散验证的 schema、Browser-normalized facts、正文筛选、snapshot owner、R1 读取、Markdown、导航/Profile/关闭释放和超量拒绝串成可独立执行的 E2E/security/perf 总矩阵。
- 允许修改：`tests/e2e/content/**`、`tests/security/content/**`、`tests/perf/content/**`、`test-support/**`、Workspace 测试成员、CMake 测试配置与本 Roadmap/索引；若总矩阵先复现已有 C1 缺陷或 Windows 构建契约阻塞，只允许对对应入口/checkout 属性做最小根因修复并保留回归证据。禁止修改：PageSnapshot/CAAP schema 与 golden、Markdown/提取算法、CNT-08 UI 行为、模型/provider/网络能力。
- 边界：fixture 仅构造 Browser-normalized facts，不引入 DOM/HTML/CEF handle；不使用公网、固定长 sleep、真实用户文件或凭证。security 覆盖隐藏/敏感/跨源、危险 URL/伪造 provenance、跨 Profile/后台/旧 generation、输出与 target 容量、敌意输入不 panic；E2E 覆盖正常/空页/长文、确定性 Markdown、导航竞争、取消/关闭/shutdown 后零旧正文；perf 复用 CNT-07 100KB P95 与 10,000 revision bounded soak。
- 验收：新增 E2E/security crate 独立通过；`crayon-content-perf-tests` 2/2；CNT-08 `page_markdown_export_contract` 与 C++ 全 CTest 通过；相关 clippy/fmt、workspace/fast/security、`git diff --check` 全通过。测试失败若揭示生产缺陷，先保留复现再做最小修复并回填允许路径。
- 明确不做：真实 CEF UI/剪贴板/文件对话框/真机截图（后续装配）、RSS/事件循环平台采样与长稳（QAR-05）、Agent transport/CLI/MCP（AGT-12C/13/14）、C1 总接口冻结与 GO/NO-GO（CNT-10）。

### CNT-09 完成记录（2026-08-30）

- E2E：新增 `crayon-content-e2e-tests`，以 Browser-normalized facts 串联 main-content extraction → `PageSnapshot` → `PageSnapshotRuntime` → grant/R1 → normalized Markdown；3/3 覆盖正常/空页/长文、隐藏/敏感/跨源排除、query/fragment 去除、确定性重复、导航竞争、跨 Profile、close/shutdown 释放。
- Security/perf：新增 `crayon-content-security-tests` 4/4，覆盖伪造 provenance/未知字段/危险 URL、2000 组确定性 hostile facts、跨 Profile/后台/旧 generation、16-target 容量和超长 selection；既有 `crayon-content-perf-tests` 2/2，Windows 40 样本 index P95 27us、first chunk P95 4us、100KB complete P95 20.606ms、复用率 99%，10,000 revision bounded soak 通过。
- Windows C++：`cmake -S . -B .cache/build/cnt09 -G Ninja -DCRAYON_BUILD_TESTS=ON -DCRAYON_ENABLE_CEF=OFF`、`cmake --build .cache/build/cnt09` 通过；`ctest --test-dir .cache/build/cnt09 --output-on-failure` 61/61，通过项含 `page_markdown_export_contract`、page snapshot collector/gateway、Markdown/Highlight/KaTeX/Mermaid。
- 复现与修复：E2E 先复现导航后旧 generation 被错误分类为 `SourceUnavailable`，最小调整 app-runtime fence 顺序为 generation 优先，保留回归；Windows configure 复现 KaTeX/Mermaid manifest 因 `core.autocrlf=true` checkout 漂移，`.gitattributes` 固定 3 个 KaTeX 文本和 104 个 Mermaid `.mjs` 为 LF，恢复后逐项 bytes/hash 与既有 manifest 完全一致（vendor Git blob 未改变）；MinGW `-Werror` 复现 4 个 header 缺直接 `<cstdint>` 与 KaTeX 测试临时 string 引用，补 header 自包含和测试循环类型后全绿。
- 总验证：相关严格 Clippy、`cargo fmt --all -- --check`、`cargo test --workspace`、`scripts/check.ps1 fast`、`scripts/check.ps1 security`、`git diff --check` 全通过。clang-format dry-run `NOT_RUN`：Windows 环境 `Get-Command clang-format` 原始错误为 `The term 'clang-format' is not recognized`；C++ 改动仅 4 个直接 include 与 1 个循环变量类型，已由全量 MinGW `-Wall -Wextra -Wpedantic -Werror` 构建覆盖。
- Code Review：按 v0.8 复核 CT-001..008 映射、fixture 信任边界、generation/Profile/资源释放、输出预算、锁内工作、性能口径和 Windows 构建契约；上述复现项全部关闭，最终 P0/P1/P2=`0/0/0`。
- 未覆盖与风险：真实 CEF UI 菜单/剪贴板/文件对话框/截图仍是 CNT-08 已记录的产品装配风险；RSS/事件循环平台采样与长稳归 QAR-05。CNT-09 测试总矩阵已完成，`CNT-10 READY` 执行 C1 独立总 Review/接口冻结。

## CNT-10 原子范围（C1 独立 Review 与 Agent data-plane 接口冻结）

- 状态：`DONE`；依赖 `CNT-09 DONE`。
- 单一目标：独立审查 CT-001..008 的 Browser facts → extract → PageSnapshot/owner/delta → Markdown/export → Agent R1 数据面，冻结 current 接口、预算、信任和生命周期语义并给出 C1 GO/NO-GO。
- 输入与输出：只读审查 CNT-01..09 生产代码、测试与完成证据；输出限 `docs/current/content-data-plane.md`、current/计划索引和本 Roadmap，不修改生产行为或 schema/golden。
- 验收：Windows C++ build/CTest、C1 Rust unit/E2E/security/perf、workspace/format/security 证据；按 v0.8 Review，P0/P1=0；明确未覆盖和 M2 门禁。
- 明确不做：真实 CEF 导出 UI 装配、Agent transport/CLI/MCP、模型/provider/网络能力、PageSnapshot/R1 协议扩张。

### CNT-10 完成记录（2026-08-30）

- 契约：新增 current `content-data-plane-v1`，冻结单一事实管线、九类 block、Browser provenance、Profile/前台/tab/generation/revision fence、分页/delta/backpressure、Markdown 和五个 Agent R1 逻辑工具；协议/预算/错误变化必须另建版本化原子任务并更新 golden。
- Review：按 v0.8 审查需求/信任边界、正确性、依赖/API、mutex 生命周期、安全/隐私、热路径/日志、测试与维护性。CT-001..008 映射闭合，C1 `GO`，P0/P1/P2=0/0/0。
- Rust 验证：page-data 23/23、content-extract 10/10、content-markdown 9/9、C1 E2E 3/3、security 4/4、perf 2/2；100KB 40 样本 index P95 21us、first chunk 4us、delta+Markdown complete P95 10.568ms、serialized 1086 bytes、reuse 99%；10,000 revision soak 通过。
- Windows C++：现有 `.cache/build/cnt09` 的完整 `ctest --output-on-failure` 61/61，232.33s；`cef_build_graph_contract` 206.88s、Mermaid 22.08s，其余 content 关键项 `page_markdown_export_contract`、`page_snapshot_collector`、`page_snapshot_gateway` 均通过。前置 `cmake --build` 两次因工具 124s/242s 总时限终止且无编译错误输出，因此本轮 build 记 `TIMEOUT`，不冒充新 build 通过；CTest 使用 CNT-09 已成功构建的同一目录产物。
- 未覆盖与风险：CNT-08 真实 CEF 菜单/剪贴板/文件对话框/截图仍需产品装配；完整 snapshot 在 runtime mutex 内复制重建继续由 AGT-15/QAR-05 监测 UI delay/RSS/长稳。M2 必须等待 AGT-16、PRV-13 和 provider ADR，不因 C1 GO 提前联网。
