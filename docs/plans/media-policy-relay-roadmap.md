# MED：媒体观察、策略与 Session Relay Roadmap

状态：`MED-01 DONE`，`MED-02 IN_PROGRESS`。本模块不做设备协议、浏览器对象和平台采集。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| MED-01 | FND-06,FND-08 | `crayon-media-observer/observation` | `SourceObservation` 校验、大小/来源/navigation 约束 | BR-007、BR-008；非法 URL/长度/时间 | S1 DONE（2026-08-10） |

## 完成证据

- **MED-01（2026-08-10，commit 见 git log）**：`crayon-media-observer::observation` 新增 `SourceObservation`——构造即校验（空/超长 >2048/非 http(s) URL 拒绝，含 2048 边界用例），携带 tab/navigation/frame/source/双 URL/逻辑时间戳；`NavigationId` 绑定 + `is_current` 支撑导航后旧 frame/worker 迟到上报丢弃（BR-007）；iframe/Worker/MSE 来源事实保留（BR-008）；类型无正文/表单/Cookie 字段（Debug 扫描断言）。observer crate 按架构表新增对 `crayon-domain` 的依赖（仅用 `TabId`）。验证：`cargo test -p crayon-media-observer` 6/6（新增 4 条：事实完整、非法 URL/长度/边界、BR-007、BR-008）；全 workspace 严格 Clippy、`scripts/check.sh all`、`git diff --check` 通过。Code Review P0/P1/P2/P3 均为 0。
| MED-02 | MED-01 | `candidate/store` | candidate 归一化、证据合并、完整 URL 内存保存、脱敏 ID | PL-001、PL-002；query 不丢；无 secret serde | S1 |
| MED-03 | MED-02 | `candidate/ranking` | 当前播放、可见性、输入时间、来源置信排序 | BR-006；稳定排序/相同时间/音频 | S1 |
| MED-04 | MED-02 | `candidate/lifecycle` | navigation/TTL/cancel/generation 失效与有界容量 | BR-007、BR-013、PL-012；满载 eviction | S1 |
| MED-05 | FND-06,FND-09 | `crayon-media-probe/http` | 无 secret、禁自动 redirect、有界 HEAD/Range client | PL-003、PL-014；私网/混合 DNS/超时/取消 | S2 |
| MED-06 | MED-05 | `probe/mp4_hls_dash` | MP4/HLS/DASH AST/容器/代表资产预检 | PL-003..PL-006；不下载主体、不请求 key | S2 |
| MED-07 | MED-06 | `probe/protection_codec` | DRM/EME/加密/codec 证据合并，保守错误语义 | PL-005、PL-006、BR-011、BR-012 | S1 |
| MED-08 | MED-03,MED-04,MED-07 | `crayon-cast-policy` | 唯一 `Mirror/Direct/Relay/Reject` 纯函数和 stable reasons | PL-007..PL-014；跨平台 golden 完全一致 | S2 |
| MED-09 | FND-08,FND-09 | `crayon-relay/session` | session/resource/receiver/route/TTL 模型、CSPRNG ID、ManualClock | RL-002..RL-005；Drop/stop 幂等 | S1 |
| MED-10 | MED-09 | `relay/vault` | 不可序列化 secret recipe、origin/path/header scope、零化/撤销 | RL-004、RL-005、RL-014、RL-015；无 clone/debug 泄漏 | S1 |
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
