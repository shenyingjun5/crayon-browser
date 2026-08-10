# MED：媒体观察、策略与 Session Relay Roadmap

状态：`MED-01/02/03/04/05/06/07/08/09/10 DONE`，`MED-11 IN_PROGRESS`。本模块不做设备协议、浏览器对象和平台采集。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| MED-01 | FND-06,FND-08 | `crayon-media-observer/observation` | `SourceObservation` 校验、大小/来源/navigation 约束 | BR-007、BR-008；非法 URL/长度/时间 | S1 DONE（2026-08-10） |

## 完成证据

- **MED-01（2026-08-10，commit 见 git log）**：`crayon-media-observer::observation` 新增 `SourceObservation`——构造即校验（空/超长 >2048/非 http(s) URL 拒绝，含 2048 边界用例），携带 tab/navigation/frame/source/双 URL/逻辑时间戳；`NavigationId` 绑定 + `is_current` 支撑导航后旧 frame/worker 迟到上报丢弃（BR-007）；iframe/Worker/MSE 来源事实保留（BR-008）；类型无正文/表单/Cookie 字段（Debug 扫描断言）。observer crate 按架构表新增对 `crayon-domain` 的依赖（仅用 `TabId`）。验证：`cargo test -p crayon-media-observer` 6/6（新增 4 条：事实完整、非法 URL/长度/边界、BR-007、BR-008）；全 workspace 严格 Clippy、`scripts/check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-02（2026-08-10，commit 见 git log）**：`crayon-media-observer::candidate::store` 新增 `CandidateStore`——同一 tab+navigation 内按归一化 URL（仅 scheme/host 大小写与默认端口归一，path/query 字节保留）合并候选并保留多源证据（PL-001：DOM/network/currentSrc 合并为一个，去重同 source+frame，evidence 上限 8）；完整签名 URL 仅存可信内存且 query 字节不丢（PL-002）；对外仅 `RedactedCandidate`（不透明 ID + `scheme://host[:port]`）；`CandidateEntry` 无 Serialize、Debug 脱敏（测试断言 query/文件名不进 Debug）；容量上限 256 满载拒绝。验证：`cargo test -p crayon-media-observer` 11/11（新增 5 条：PL-001 合并与证据、不同 URL/navigation 不合并、PL-002 query 保留与日志脱敏、归一化边界、容量有界）；全 workspace 严格 Clippy、`scripts/check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-03（2026-08-10，commit 见 git log）**：`candidate::ranking` 新增纯函数 `rank`——信号加权（currentSrc 100 / play 事件相邻 80 / 音频活动 40 / 可见面积占比 0-40 / 顶层 frame 20，均为命名常量），可见面积按集合内最大值归一（u64 中间计算防溢出）；同分按 CandidateId 升序稳定决胜（输入顺序无关）。排序只定序不过滤（广告片段/初始化分片/追踪请求保留为编排证据，过滤决策归 policy）。验证：observer 16/16（新增 5 条：BR-006 可见+最近操作胜出、currentSrc 压过附带请求、同信号/乱序输入稳定、后台音频仍第一、空输入）；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-04（2026-08-10，commit 见 git log）**：`candidate::lifecycle` 落地生命周期——`on_navigation`（旧 navigation 候选全部失效、幂等，BR-007）、`on_tab_close`（墓碑阻止关闭标签的迟到事件重建候选，新 navigation 重开恢复，BR-013）、`expire_stale`（TTL 默认 10 分钟，边界语义 now ≤ last+ttl，PL-012）；ingest 加入 admission（stale navigation/墓碑/标签表 64 上限拒绝）与满载驱逐（先过期后最旧，容量永不超 256）；时间全部调用方供给（逻辑毫秒，无墙钟）。MED-02 的满载语义由「拒绝」修正为「有界驱逐」（对应测试同步更新，容量有界不变量不变）。验证：observer 21/21（新增 5 条：BR-007、BR-013、TTL 边界、先过期后最旧驱逐、标签表有界）；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-05（2026-08-10，commit 见 git log）**：`crayon-media-probe::http` 新增 `ProbeHttpClient`——无 secret（API 无 Cookie/Authorization 入口，仅显式 UA）；禁自动 redirect（3xx 原样上抛，逐跳校验归 MED-12）；有界（connect/total 超时、range_get 字节硬上限 256KB 可配）；DNS 安全（主机名先解析、全部答案须公开可路由、公私混合整体拒绝、连接固定已验证地址防 rebinding；字面量 IP 直接分类；`is_publicly_routable` 覆盖 RFC1918/loopback/link-local/CGNAT/benchmark/文档段/组播/保留段/ULA）。错误语义 PL-014：Timeout/Connect/Transport/NonPublicAddress 等普通失败不携带数据、不提权。`allow_private_addresses` 测试钩子默认关闭。test-support `MockUpstream` 新增 `HeadRejected` 方法分流变体。验证：`cargo test -p crayon-media-probe` 36/36（http 新增 10 条：HEAD 状态头、Range 封顶、PL-003 HEAD 405→Range ftyp 回退、redirect 不跟随、loopback 字面量与 localhost 连接前拒绝、非 http 方案、超时、取消、混合 DNS、IP 分类矩阵）；test-support 26/26；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-06（2026-08-10，commit 见 git log）**：`crayon-media-probe` 新增 `hls`（播放列表检查解析：variant 的 bandwidth/resolution/codecs/URI、EXT-X-MEDIA rendition、EXT-X-KEY/SESSION-KEY 加密事实、EXT-X-MAP、ENDLIST；相对 URI 转绝对；行数上限 10k）与 `inspect`（`MediaInspector` 编排：content-type/`#EXTM3U` 内容嗅探选路，HEAD 405 回退有界 Range，PL-003；MP4 ftyp 主品牌识别只读首 4KB；DASH ContentProtection/Representation 计数；不识别 → `Unknown` 而非报错）。硬性规则实测：不下载主体（Range 录制断言）、不请求 key（key 路由 hit_count=0 断言，PL-005）、DRM 事实只作数据上报不产出直投资产（PL-006）。验证：`cargo test -p crayon-media-probe` 42/42（inspect 新增 6 条：PL-003/004/005/006、直播列表+内容嗅探、未知内容）；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-07（2026-08-10，commit 见 git log）**：`crayon-media-probe::assess` 新增 `assess_protection`——证据合并（预检结论、EME `encrypted` 信号、blob/MediaStream 来源、codec 证据），保守优先级 `DrmProtected > NoDirectUrl > KeyRequired > Unknown > Clear`：EME 信号升级表面干净的 URL（BR-011）；blob/MediaStream 不伪造直投 URL（BR-012）；AES-128/SAMPLE-AES/SESSION-KEY 一律 `KeyRequired`（当前合规姿态拒绝直投，区别于 legacy 的 AES-128 可播）；预检不确定 → `Unknown` 不静默放行；codec 证据透传、未得出保持 None 不猜测。验证：media-probe 49/49（assess 新增 7 条：clean/KeyRequired/DrmProtected/EME 升级/blob/Unknown 保守/codec 透传）；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-08（2026-08-10，commit 见 git log）**：`crayon-cast-policy::decide` 落地唯一决策函数（设计 §9.2 顺序）：播放门禁 fail-closed（PL-010，页面自报/无激活/未推进均以稳定 CoreError 拒绝）→ DRM 全局拒绝 → KeyRequired/NoDirectUrl/Unknown 只允许 Mirror 兜底 → credential-bound 不出浏览器（PL-008）→ 接收端协议/编码不兼容降级或稳定拒绝（PL-007）→ 广告连续性未知且从头播放选 Mirror（PL-009）→ 其余按 headers_class 分 Direct（无特殊头）/Relay（Referer/UA 由 session relay 代持）。Mirror 需 tab_video 能力，缺 system_audio 时带 `Degradation::NoSystemAudio` 显式降级原因（PL-011）；无采集能力则 `capabilities_unavailable` 稳定拒绝。`CastPolicyDecision` 新增 `Relay` 变体（v1 窗口内向后兼容：previous 向量仍全部可解析，RG-007 通过）并补 golden 向量。跨平台 golden（PL-013）：桌面 CEF 与 ArkWeb 受限能力下安全结论一致、仅可用模式不同。cast-policy 新增对 domain/ipc-schema/media-probe 的依赖（DTO 消费，无网络/平台）。验证：cast-policy 13/13（decide 9 条：PL-007~PL-011、PL-013、happy path、门禁矩阵）、ipc-schema 7/7（含 relay 向量）；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-09（2026-08-10，commit 见 git log）**：新建 `crayon-relay` crate（workspace 成员）。`session` 模块：`SessionToken` 128-bit CSPRNG（getrandom），hex 为路由段、Debug 脱敏、常数时间比较、不含上游 URL（RL-002）；`SessionRegistry`——创建（receiver 绑定 + 可选首请求 IP + 固定 upstream allow-set + TTL 默认 2h + generation）、`authorize` 先于任何 upstream 访问（未知 token 401 类/IP 不匹配 403 类/过期/未注册资源，RL-003）、`stop` 立即失效且幂等（RL-004）、`revoke` 五触发器（Navigation/ProfileDestroyed/AppExit 全量，RouteLost/DeviceReplaced 按设备，RL-005）、TTL expire 清退、容量 32 session/128 resource 有界。时间全部调用方供给（逻辑毫秒）。secret 随记录 Drop 零化（复用 ipc-schema SessionSecret）。验证：`cargo test -p crayon-relay` 7/7（RL-002 熵与形状、RL-003 授权矩阵、allow-set 固定、RL-004、RL-005 触发器全集、TTL 边界、容量）；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
- **MED-10（2026-08-10，commit 见 git log）**：`crayon-relay::vault` 新增 `RecipeVault`/`UpstreamRecipe`——完整上游 URL 以 `Zeroizing<String>` 保存、Drop 零化、无 Clone、无 Serialize、Debug 只含脱敏 origin/path 前缀（RL-014，LeakScanner 扫描断言）；header scope 类型级收敛：recipe 只能携带 Referer/User-Agent（Cookie/Authorization 无法表达）；`resolve` 同 origin 约束 + 非 http(s) 拒绝；`header_scope_for` 逐跳 redirect 作用域（同 origin 携带、跨 origin 剥离，RL-015）；revoke_session/revoke_all 幂等撤销（RL-004/005）；每 session 128 条有界。新增依赖 `zeroize 1`（MIT/Apache、广泛使用）。验证：crayon-relay 13/13（vault 新增 6 条：scope 解析、resolve 约束、逐跳 header scope、Debug 脱敏扫描、撤销幂等、容量有界）；严格 Clippy、`check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
| MED-02 | MED-01 | `candidate/store` | candidate 归一化、证据合并、完整 URL 内存保存、脱敏 ID | PL-001、PL-002；query 不丢；无 secret serde | S1 DONE（2026-08-10） |
| MED-03 | MED-02 | `candidate/ranking` | 当前播放、可见性、输入时间、来源置信排序 | BR-006；稳定排序/相同时间/音频 | S1 DONE（2026-08-10） |
| MED-04 | MED-02 | `candidate/lifecycle` | navigation/TTL/cancel/generation 失效与有界容量 | BR-007、BR-013、PL-012；满载 eviction | S1 DONE（2026-08-10） |
| MED-05 | FND-06,FND-09 | `crayon-media-probe/http` | 无 secret、禁自动 redirect、有界 HEAD/Range client | PL-003、PL-014；私网/混合 DNS/超时/取消 | S2 DONE（2026-08-10） |
| MED-06 | MED-05 | `probe/mp4_hls_dash` | MP4/HLS/DASH AST/容器/代表资产预检 | PL-003..PL-006；不下载主体、不请求 key | S2 DONE（2026-08-10） |
| MED-07 | MED-06 | `probe/protection_codec` | DRM/EME/加密/codec 证据合并，保守错误语义 | PL-005、PL-006、BR-011、BR-012 | S1 DONE（2026-08-10） |
| MED-08 | MED-03,MED-04,MED-07 | `crayon-cast-policy` | 唯一 `Mirror/Direct/Relay/Reject` 纯函数和 stable reasons | PL-007..PL-014；跨平台 golden 完全一致 | S2 DONE（2026-08-10） |
| MED-09 | FND-08,FND-09 | `crayon-relay/session` | session/resource/receiver/route/TTL 模型、CSPRNG ID、ManualClock | RL-002..RL-005；Drop/stop 幂等 | S1 DONE（2026-08-10） |
| MED-10 | MED-09 | `relay/vault` | 不可序列化 secret recipe、origin/path/header scope、零化/撤销 | RL-004、RL-005、RL-014、RL-015；无 clone/debug 泄漏 | S1 DONE（2026-08-10） |
| MED-11 | MED-09 | `relay/router` | loopback control + LAN media router，仅 opaque route | RL-001、RL-003、RL-008；正式路由快照 | S2 |
| MED-12 | MED-10,MED-11 | `relay/network_guard` | IP 分类、全 DNS 校验、固定地址、逐跳 redirect/scope | RL-006、RL-007、RL-015；SSRF/rebinding matrix | S2 |
| MED-13 | MED-11,MED-12 | `relay/mp4` | GET/HEAD/Range、流式背压、状态/header 映射 | RL-009、RL-012；200/206/416/断流 | S2 |
| MED-14 | MED-11,MED-12 | `relay/hls/parser` | AST 保留 tag/顺序、master/media/variant/rendition/map 资源表 | RL-010；循环/深度/总数/加密拒绝 | S2 |
| MED-15 | MED-14 | `relay/hls/stream` | TS/fMP4 二进制流、live TTL、ETag/Last-Modified、有界缓存 | RL-011..RL-013；hash/304/live 更新 | S2 |
| MED-16 | MED-13,MED-15 | `relay/runtime` | route 绑定、并发/timeout/stop/navigation/profile/app-exit 收口 | RL-004、RL-005、RL-012、RL-013 | S2 |
| MED-17 | MED-08,MED-16 | `crayon-app-runtime/delivery` | Planner -> direct/relay/mirror 编排；普通失败不提权；单次降级 | PL-014、E2E-002、E2E-004 fake；无循环 fallback | S2 |
| MED-18 | MED-04,MED-08,MED-17 | 安全 Review/文档 | threat model、fuzz corpus、性能与泄漏报告；修 P0/P1 | security check；RL 全集；30 分钟 harness | S3 |

## 关键不变量

- detector/observer 只能提供证据，不能自报 direct/relay 结论。
- `MediaCandidate.original_url` 和 recipe 只在可信内存；DTO/日志只含 opaque ID 和 redacted origin。
- Relay 不是下载器、通用代理或云代理；媒体体不落盘。
- 当前策略对需要密钥或加密 HLS 保守拒绝；变更需独立合规 Roadmap。

## 提交策略

MED-01～04、05～08、09～12、13、14/15、16/17 分别保持可回退；安全修复不得与大规模文件移动混合。每个 parser 任务必须带恶意和异常 fixture。
