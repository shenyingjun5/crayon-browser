# FND-12 基础迁移收口 Review

日期：2026-08-10
状态：`DONE`
证据级别：S2；Windows legacy app 补充 S3 构建/CLI 证据

## 结论

- 是否可合并：是。FND V0 的 workspace、Core API、测试设施、配置/本地化和 legacy 隔离达到 Roadmap 门禁。
- P0/P1/P2/P3：0/0/0/0；本次发现的三项问题均已修复并回归。
- 边界：这只证明基础迁移层完成，不代表 CEF 浏览器、Cast-SDK、平台采集、隐私 Profile、HarmonyOS 或发布链路已经完成。

## 已关闭发现

| ID | 原问题 | 修复与防回归 |
|---|---|---|
| FND-12-R1 | `security_corpus` 使用恒真断言，畸形 HLS 用例没有证明任何结果 | 成功分支验证 rewrite 输出线性有界，失败分支只接受 `HlsError::NotHls`；目标测试和全 workspace `-D warnings` 通过 |
| FND-12-R2 | legacy `--sniff-cli not-a-url` 在主线程调度前 `unwrap` panic | 新增无 panic URL 边界函数、独立单测和架构契约；真实 CLI 以 0 退出并输出结构化 `SNIFF_RESULT_JSON` 错误 |
| FND-12-R3 | 默认 `formal-product` 根包仍无条件声明 legacy 网络、CLI、解析和日志依赖 | 13 个依赖改为 optional 且只能由 `legacy-dev` 显式启用；legacy helper 同步 `cfg`；RG-005 新增失败/成功/feature 泄漏测试 |

## 专项检查

### 架构和依赖

- 默认 `get-video` 正常依赖只剩 `crayon-app-runtime`、`crayon-cast-policy`、`crayon-domain`、`crayon-ipc-schema`、`crayon-media-observer` 和 `crayon-media-probe`；`axum/reqwest/tokio/clap/...` 只在 `legacy-dev` 图中出现。
- `crayon-domain` 无网络/平台依赖；`crayon-app-runtime` 无 CEF/Tauri/OS 具体依赖；正式生产边不引用 `test-support`。
- `crayon-legacy-adapter` 只供被 workspace 排除且明确标记的 `get-video-app` 使用；Cast-SDK 尚未接入，RG-008 因此仍为 not applicable，不能写成已通过集成。
- Core API v1、current/previous schema 窗口和配置 schema 保持冻结；本次没有改变公共 wire schema、状态机或持久化格式。

### 浏览器、广告、DRM、Relay 与秘密

- 正式源码和正式 Release 产物不含 `/api/extract`、通用 `/proxy`、自动 click/play/seek、广告跳过或 remote-debug surface；相关旧能力只存在于显式 legacy 构建。
- 页面事实仍不能直接获得投屏许可；`BrowserVerified` 用户输入和播放推进门禁保持 fail closed。
- DRM/凭证继续只导致拒绝或镜像；Cookie、Authorization 和上游 URL 不进入接收端命令、公开 DTO 或正式 Relay 路由。
- 正式 Relay 仍只暴露高熵 session/resource 路由，保持 receiver/route/TTL/allow-set、逐跳 DNS/redirect 和撤销约束。

### 并发、生命周期和性能

- FND-12 的生产行为改动只发生在 legacy URL 解析边界，没有新增锁、线程、任务、socket、队列、重试或热路径日志。
- 非法 URL 在创建隐藏 WebView 及主线程 dispatch 前失败；真实 smoke 退出后 8321/8377 无残留监听。
- MED-18 已完成 Relay 的并发、停止、30 分钟长稳和首字节性能验证；本任务没有改变这些实现。

### 文件规模

- `app/src/legacy_sniff.rs::do_sniff` 约 203 行：它是只在迁移期保留的单次隐藏 WebView 事务，阶段顺序与清理契约已被冻结；本次只抽出纯 URL 边界。正式 CEF 不依赖该函数，在 CEF 替代前做行为性拆分会增加迁移回归风险，因此不在 FND-12 继续拆分。
- `src/relay/proxy.rs` 的主代理函数约 213 行：它属于 `legacy-dev` 通用代理，正式产品 Relay 已在 `crayon-relay` 按 session/router/network_guard/stream 拆分；对旧代理重构不会推进目标产品，保持 feature 隔离并由契约扫描。
- 其余 100～199 行提醒均已检查，没有发现新的状态所有权或依赖方向问题；新增 RG-005 规则已按独立 formal-root helper 拆分，未触发 100 行提醒。

## 验证

- `cargo clippy --workspace --all-targets -- -D warnings`：PASS。
- `cargo check --manifest-path app/Cargo.toml --locked --offline`：PASS。
- `cargo clippy --manifest-path app/Cargo.toml --all-targets --locked --offline -- -D warnings`：PASS。
- `cargo test --manifest-path app/Cargo.toml --locked --offline -j 1`：12/12 PASS。
- `cargo test --test legacy_contract`：9/9 PASS。
- `scripts/check.ps1 fast`、`core`、`security`：PASS；自动化只使用本地 fixture/loopback。
- `cargo build --workspace`、`cargo build --workspace --release`：PASS。
- RG-006：默认正式依赖闭包的 8 个 Release `.rlib` 全部 PASS。
- `cargo run --manifest-path app/Cargo.toml --locked --offline -- --sniff-cli not-a-url`：exit 0，结构化错误，无 panic、无残留监听。
- `cargo tree -p get-video --edges normal --depth 1`：只含 6 个正式 Core 直接依赖；同命令加 `--no-default-features --features legacy-dev` 才出现 legacy 依赖。
- `cargo fmt --all -- --check`、repo guard、`git diff --check`：PASS。

## 未覆盖与剩余风险

- macOS/Linux 未复跑 legacy Tauri app；该 app 不发布，Windows 已完成补充构建与 CLI 证据。正式三平台证据由 CEF/PLT/QAR Roadmap 提供。
- CEF 二进制、sandbox、三平台壳、Cast-SDK revision、真接收端和平台采集均未进入 FND 范围，不能由本 Review 推断完成。
- Windows legacy CLI 退出时 Chromium 报 `Failed to unregister class Chrome_WidgetWin_0 (1412)`，进程仍以 0 退出且端口已释放；正式 CEF 生命周期测试必须重新覆盖，不能沿用该结果。
