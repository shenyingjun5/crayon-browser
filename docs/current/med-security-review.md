# MED 模块安全评审（MED-18，2026-08-10）

范围：`crayon-media-observer`、`crayon-media-probe`、`crayon-cast-policy`、`crayon-relay`、`crayon-app-runtime/delivery`（MED-01..MED-17 全部提交）。评审顺序按 `code-review-standard.md`：需求/边界 → 正确性 → 架构/API → 并发/生命周期 → 安全/隐私 → 性能 → 测试 → 可维护性。

## 1. 威胁模型对照（技术方案 §14 → 实现 → 测试）

| 威胁 | 实现控制 | 测试证据 |
|---|---|---|
| 恶意网页触发投屏 | 播放门禁 fail-closed（页面自报不可信，BrowserVerified 才放行） | cast-policy `decide` 门禁矩阵（PL-010） |
| LAN 开放代理 | 无任意 URL 路由；opaque session/resource；控制面 loopback + secret | router RL-001/RL-003/RL-008 测试 |
| SSRF/DNS 重绑定 | 逐跳 allow-set + IP 分类 + 解析后固定地址 | network_guard RL-006/RL-007 测试 |
| Cookie 泄露 | recipe 类型层面无 Cookie/Authorization；Debug 脱敏；LeakScanner | vault RL-014 测试；v1 契约 secret deny |
| 假接收端 | receiver 绑定 + 可选首请求 IP + 设备级撤销 | session RL-003、runtime route-lost 测试 |
| 重放控制命令 | token 常数时间比较、TTL、stop 即失效 | session RL-002/RL-004、router 过期测试 |
| 页面越权调用 native | 控制面 secret + deny_unknown_fields + body 上限 | router 控制面测试 |
| Profile 数据残留 | recipe URL Zeroizing、撤销即零化 | vault 撤销测试（零化经 Zeroizing 类型保证） |
| 更新供应链 | 本模块无更新通道代码 | 不适用（QAR/PLT 范围） |
| 指纹保护反而唯一 | 本模块无指纹代码 | 不适用（PRV 范围） |

## 2. RL 用例全集覆盖

| 用例 | 覆盖位置 |
|---|---|
| RL-001 无 legacy 路由 | `crates/crayon-relay/tests/router.rs` |
| RL-002 128-bit CSPRNG token | `tests/session.rs` |
| RL-003 授权先于 upstream | `tests/router.rs`（fetcher 零调用断言） |
| RL-004 stop 即失效/清空 | `tests/session.rs`、`tests/runtime.rs` |
| RL-005 五触发器撤销 | `tests/session.rs`、`tests/runtime.rs` |
| RL-006 逐跳私网拒绝 | `tests/network_guard.rs` |
| RL-007 校验后固定地址 | `tests/network_guard.rs`（resolver seam） |
| RL-008 猜测/穿越/超长/方法 | `tests/router.rs`、控制面 413/422 |
| RL-009 200/206/416/HEAD/suffix | `tests/mp4.rs` |
| RL-010 opaque 改写 | `tests/hls_parser.rs`、`tests/hls_stream.rs` |
| RL-011 二进制字节一致 | `tests/hls_stream.rs` |
| RL-012 并发有界/断流超时 | `tests/mp4.rs`、`tests/runtime.rs` |
| RL-013 长稳内存 | `tests/longrun.rs`（30 分钟 harness，结果见 Roadmap 证据） |
| RL-014 无泄漏 | `tests/vault.rs`（LeakScanner）；relay crate 生产代码零日志语句 |
| RL-015 逐跳 header scope | `tests/network_guard.rs`、`tests/vault.rs` |

## 3. Fuzz 语料

`crates/crayon-relay/tests/security_corpus.rs`：畸形/超长/ NUL /非法 UTF-8 播放列表、未闭合引号、空 URI、超长属性（100KB）、超长行（1MB）、token/资源 ID/Range 边界语料——全部不 panic、有界拒绝。作为 cargo-fuzz 种子集保留。

## 4. 性能与泄漏报告

- relay 首字节附加延迟（loopback，50 次取 p50）：direct 929µs → relay 1651µs，**附加 ≈ 722µs**（`relay_first_byte_overhead_probe`，断言 <50ms）。
- 内存：MP4/HLS body 全程流式（无全量入内存）；播放列表缓存 ≤64 条 × ≤256KB；30 分钟 harness（约 6.9GB 流量）RSS 30,820KB → 33,796KB，平台期稳定，不随流量增长。
- 泄漏：crayon-relay 生产代码无任何日志语句；所有含 URL 的类型 Debug 脱敏；`SessionSecret`/token 常数时间比较 + Drop 零化。

## 5. Review 发现

- P0：无。P1：无。P2：无。
- P3（不阻塞，已记录）：
  1. DASH relay serving 不在 v1（Relay+DASH 结构化降 Mirror，MED-17 已记录）；后续 DASH 任务需先建 schema 任务。
  2. `GuardedFetch` 每跳新建 reqwest Client（连接不复用），对分片级流量有轻微开销；如性能门禁需要，后续任务引入 pinned-client 缓存。
  3. `is_well_formed_range` 在 mp4 模块私有，安全语料以行为级覆盖；如需更强保证可在 MED 后续任务抽公共 parser。

## 6. 验证命令（全部实际运行）

- `cargo test --workspace`：通过（见 check.sh all 的 formal-workspace 步）。
- `scripts/check.sh all` / `security`：通过。
- `cargo clippy --workspace --all-targets -- -D warnings`：通过。
- `cargo test -p crayon-relay --test security_corpus`：4/4。
- `cargo test -p crayon-relay --test longrun relay_first_byte_overhead_probe -- --ignored`：通过（数据见 §4）。
- `cargo test -p crayon-relay --test longrun -- --ignored --nocapture`（30 分钟）：**通过**（1800.12s）——2,618,643 轮、约 6.9GB 流量，RSS 30,820KB → 33,796KB（+2.9MB，采样平台期），内存与流量无相关性，停止后不增长。
