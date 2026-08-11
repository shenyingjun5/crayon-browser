# 蜡笔 AI Agent 投屏浏览器测试标准

- 版本：v0.7
- 日期：2026-08-11

## 1. 分层

| 层级 | 入口 | 目标 | 默认预算 |
|---|---|---|---:|
| L0 Repo Guard | `tools/repo-guard` | 依赖、规模、硬编码、测试隔离、许可和 source lock | 30 秒 |
| L1 Unit | 各 crate/target 独立测试 | 纯函数、状态机、parser、边界和释放 | 2 分钟 |
| L2 Contract/Integration | `tests/contracts`, `tests/integration` | schema、IPC、CEF、Cast-SDK、Markdown、CAAP、语义动作、Workflow、Hub | 5 分钟 |
| L3 Security | `tests/security` | SSRF、rebind、重放、secret、action、challenge、connector 与旧会话 | 10 分钟 |
| L4 Desktop E2E | `tests/e2e/desktop` | Windows/macOS 浏览、Cast、Agent、语义动作与适用 feature Preview | 按套件 |
| L5 Device/Platform | `tests/e2e/device` | 真实接收端、网络拓扑、系统生命周期和安装包 | 按矩阵 |
| L6 Long-run/Release | 专项 Harness | 性能、长稳、升级、回滚、SBOM | 单独执行 |

## 2. 确定性设施

- `ManualClock`：TTL、超时、重试、generation 和回收，不使用固定长 `sleep`。
- `MockUpstream`：MP4/HLS/DASH fixture、Range、redirect、断流、慢流和 DNS 结果。
- `FakeCastSdk`：发现、投屏码、能力、Direct/Relay、控制、旧事件和 route lost。
- `PlatformFake`：安全存储、本地网络、生命周期、更新、文件对话框和外部客户端交接。
- `PageFixture`：当前页快照、结构提取、Markdown、安全和资源上限。
- `FakeAgentClient`：CAAP/CLI/MCP handshake、invoke/stream/cancel、错误 secret、重放、限流、grant、确认和旧 generation。
- `SemanticPageFixture`：Page/Action/Form/Media/Risk Map、ChangeSet、action_id、多信号漂移、precondition 和 effect。
- `FakeWorkflowStore`：trace redaction、candidate、checkpoint、个人 Skill、版本/健康/回滚与 Profile 隔离。
- `FakePartnerConnector` / `RouteFixture`：trust、OAuth/scope、SSRF/DNS、tool injection、health/熔断、route reason 和 fallback。
- `FakeModelProvider`：仅用于 M2 的 payload readback、正常、流式、超时、取消、配额和畸形响应；第一阶段不得访问真实 provider。
- `LeakScanner`：日志、DTO、磁盘、诊断和安装包中的 secret/测试资源扫描。

自动化不得访问真实模型 provider、公共网络或第三方影视站，也不得引入 WebRTC、采集器或编码器。FakeAgent/FakeModel 只存在于 test-support/独立测试构建图。

## 3. 当前平台矩阵

| 平台 | PR | 每日/候选版 | 重点 |
|---|---|---|---|
| Windows 10/11 x64 | build/unit/contract | E2E/device/package | CEF、DPAPI、本地网络/防火墙、多网卡、更新、客户端交接 |
| macOS x64/arm64 | build/unit/contract | E2E/device/package | CEF、Keychain、本地网络权限、签名/公证、更新、客户端交接 |
| HarmonyOS 电脑 | 非 PR 阻塞 | 技术预览专项 | PC 窗口/键鼠、ArkWeb、HUKS、LAN Direct/Relay |

Linux 不在当前矩阵；不能用 Linux runner 结果代替 Windows/macOS 证据。HarmonyOS 手机/平板结果不能代替鸿蒙电脑证据。

## 4. 必测维度

- 正常、空输入、非法输入、容量边界、重复调用、取消、超时、旧结果和恢复。
- 导航、关闭标签、设备切换、网络切换、睡眠唤醒、Profile 销毁和 App 退出。
- start/stop 幂等、线程/socket/token/cache/临时文件释放和队列满载行为。
- Cookie、Authorization、完整签名 URL、浏览历史和 session secret 的日志/DTO/网络/磁盘泄漏。
- Direct/Relay 安全；无路由时 `ExternalClientHandoff` 需要确认且不创建 Cast-SDK/Relay/WebRTC 会话。
- Markdown 快照/输出确定性、导航绑定、危险 URL、超大页面、取消、保存失败和峰值资源。
- CAAP previous/current、握手、tool registry、grant/确认、stream/cancel/deadline、幂等、重放、本机 ACL、MCP/CLI 同义性和 Release surface。
- Agent 读页覆盖 first chunk/complete、CPU/RSS、UI event-loop delay、序列化字节、增量复用和背压；所有 fixture 本地确定。
- 语义动作覆盖 action_id 失效、多信号唯一性、风险单调、前置条件、效果验证、未知副作用和人工接管。
- Workflow 覆盖 verified-only 学习、redaction、用户预览保存、challenge 不绕过、checkpoint、技能隔离/健康/回滚和高风险禁止修复。
- Hub 区分入站 MCP/出站 connector，覆盖 route_reason、fallback 重授权、信任/签名/kill switch、OAuth、SSRF、描述注入、熔断和审计。
- M2 覆盖发送前预览与真实 payload 一致、provider origin/redirect、取消/失败、本地 Markdown 降级、引用和合法视频文本来源。

## 5. 性能与长稳

- 浏览器：冷启动、首导航、标签切换、崩溃恢复和 Profile 清理。
- 投屏：发现、连接、Direct/Relay 首帧、控制、停止和 Relay 附加延迟。
- Relay：并发、慢接收端、断流、缓存、30 分钟 VOD/live 与停止后回落。
- Markdown：不同节点/文本规模的时延、峰值内存、取消响应和输出上限。
- Agent：CAAP handshake、缓存命中 R1、100KB 页面、分页/增量、并发和恶意慢 consumer；未完成对照前不宣称优于具体浏览器。
- Semantic/Workflow：地图和 ChangeSet 的时延/字节、effect wait、重复任务步骤/耗时/传输下降、技能运行健康和修复误匹配率。
- Hub/Connector：路由计算、连接并发、限流/熔断、响应预算和取消后的资源回落。
- 长稳：8 小时 Direct/Relay、浏览/Profile；不测试浏览器镜像音画同步或编码性能。

## 6. 证据规则

- 记录完整命令、退出码、测试数量、耗时、平台/设备版本和原始失败摘要。
- 未运行写 `NOT_RUN`；超时写 `TIMEOUT`，不能推断通过或失败。
- 真机/Harness 任务只有实际证据才能 `DONE`；单平台结果不替代另一平台。
- 当前核心基线仍以根 `AGENTS.md` 记录为准；入口变化必须同步 Roadmap 和本标准。
- 在入口尚未创建前，Roadmap 必须列出底层真实命令；不得引用不存在的脚本作为证据。

## 7. 当前用例集

当前权威用例为 [test-cases.md](test-cases.md) 中 186 个唯一 ID。任务必须引用适用 ID，新增/删除用例时同步总 Roadmap 与计划索引。
