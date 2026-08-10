# 活跃 Roadmap

本目录只保存当前可执行的模块 Roadmap。Agent 从首批执行队列或任务表中领取一个状态为 `READY` 的任务；完成模块后把稳定结论收敛到 `docs/current/`，实施文档移入 `docs/archive/`。

当前目录共定义 132 个唯一原子任务；`docs/current/test-cases.md` 共定义 93 个唯一测试用例。

## 总入口

- [`../crayon-private-cast-browser-roadmap.md`](../crayon-private-cast-browser-roadmap.md)：总依赖、交付波次、任务状态和 Agent 领取顺序。

## 模块 Roadmap

| 代码 | Roadmap | 当前目标 | 前置 |
|---|---|---|---|
| FND | [`foundation-migration-roadmap.md`](foundation-migration-roadmap.md) | 冻结 legacy 红线、重组 workspace、建立门禁与可迁移 Core | 无 |
| CEF | [`desktop-cef-browser-roadmap.md`](desktop-cef-browser-roadmap.md) | Win/macOS/Linux CEF 壳、共享 UI、媒体观察与 IPC | FND-08 |
| MED | [`media-policy-relay-roadmap.md`](media-policy-relay-roadmap.md) | 候选、预检、纯策略和 session relay | FND-06 |
| SDK | [`cast-sdk-integration-roadmap.md`](cast-sdk-integration-roadmap.md) | 固定 Cast-SDK revision，接通发现、投屏码、能力、播控与会话 | FND-08 |
| PLT | [`desktop-platform-adapters-roadmap.md`](desktop-platform-adapters-roadmap.md) | Windows/macOS/Linux 采集、音频、编码、存储、网络、更新 | CEF-07、SDK-05 |
| PRV | [`privacy-security-roadmap.md`](privacy-security-roadmap.md) | Profile、防追踪、安全存储、威胁模型与隐私验证 | FND-08、CEF-05 |
| HM | [`harmony-browser-roadmap.md`](harmony-browser-roadmap.md) | ArkUI/ArkWeb + Native Core 技术预览与 Go/No-Go | FND-08、MED-08、SDK-05 |
| QAR | [`quality-release-roadmap.md`](quality-release-roadmap.md) | 跨平台 E2E、稳定性、许可、签名、更新和 GA | 各目标平台功能任务 |

## 首批执行队列

按依赖顺序执行，不应并行修改同一文件：

1. `FND-01` 基线与红线特征测试：当前 `DONE`。
2. `FND-02` 测试/生产物理隔离：当前 `DONE`。
3. `FND-03` repo guard 和分层检查入口：当前 `DONE`。
4. `FND-04` Legacy 合规/安全红线隔离：当前 `DONE`。
5. `FND-05` Rust workspace 与领域空壳：当前 `DONE`。
6. `FND-06A`～`FND-06D` 纯媒体能力迁移：当前 `DONE`。
7. `FND-07A` 嗅探脚本资源迁移：当前 `DONE`。
8. `FND-07B` 共享模型与状态所有权拆分：当前 `DONE`。
9. `FND-07C` 拆分 legacy Beacon 与网络地址：当前 `DONE`。
10. `FND-07D` 拆分 legacy 命令与探测编排：当前 `DONE`。
11. `FND-07E` 收口启动、Relay 与 CLI 装配：当前 `DONE`（Windows app 可复现构建、真实 app 测试、严格 Clippy 与两条 CLI smoke 已通过）。
12. `FND-08` 冻结 Core API v1 与 capability schema：当前 `DONE`。
13. `FND-09` 建立确定性 test-support：当前 `DONE`。
14. `FND-10` 把公网测试降级为手工兼容测试：当前 `DONE`。
15. `FND-11` 建立配置加载与本地化资源：当前 `DONE`。
16. `FND-12` 基础迁移收口 Review：当前 `DONE`（三项 finding 全部关闭，依赖/红线/规模/测试/Release 产物 Review 完成）。
17. `MED-01` SourceObservation 校验与 navigation 约束：当前 `DONE`。
18. `MED-02` candidate 归一化与证据合并：当前 `DONE`。
19. `MED-03` candidate 置信排序：当前 `DONE`。
20. `MED-04` candidate 生命周期（navigation/TTL/失效与有界容量）：当前 `DONE`。
21. `MED-05` probe 有界 HTTP client：当前 `DONE`。
22. `MED-06` probe MP4/HLS/DASH 预检：当前 `DONE`。
23. `MED-07` probe 保护/编码证据合并（保守错误语义）：当前 `DONE`。
24. `MED-08` cast-policy 唯一决策函数：当前 `DONE`。
25. `MED-09` relay session 模型（CSPRNG ID/TTL/ManualClock）：当前 `DONE`。
26. `MED-10` relay vault（不可序列化 secret recipe/零化/撤销）：当前 `DONE`。
27. `MED-11` relay router（loopback 控制面 + LAN 媒体面）：当前 `DONE`。
28. `MED-12` relay network_guard（IP 分类/DNS/逐跳 redirect）：当前 `DONE`。
29. `MED-13` relay MP4 GET/HEAD/Range 流式：当前 `DONE`。
30. `MED-14` relay HLS AST parser（opaque 资源表）：当前 `DONE`。
31. `MED-15` relay HLS 流式（TS/fMP4/live TTL/有界缓存）：当前 `DONE`。
32. `MED-16` relay runtime（route 绑定/并发/超时/stop 收口）：当前 `DONE`。
33. `MED-17` app-runtime delivery 编排（Planner→direct/relay/mirror）：当前 `DONE`。
34. `MED-18` 安全 Review 与收口（threat model/fuzz/30 分钟 harness）：当前 `DONE`。
35. `CEF-01A` 固定 CEF Standard revision、四平台 hash、许可和缓存/离线根契约：当前 `DONE`。
36. `CEF-01B` 冻结不含 CEF 类型/产品策略的 `BrowserEngineAdapter` 最小接口：当前 `READY`。

`FND` 模块 19 个唯一原子任务与 `MED` 模块 18 个原子任务全部 DONE（2026-08-10），FND V0 已收口。CEF bootstrap 已拆为 `CEF-01A`～`CEF-01E`，`CEF-01A DONE` 并解锁 `CEF-01B READY`；`SDK-01` 仍需固定 Cast-SDK revision 与正式接入授权，PRV/PLT/HM/QAR 再按依赖级联解锁。

`FND-07` 已按 `FND-07A`～`FND-07E` 顺序完成；FND-12 又关闭了 security corpus 恒真断言、非法 sniff URL panic 与 formal 根包 legacy 依赖泄漏。稳定结论和未覆盖项见 `docs/current/fnd-migration-review.md`。

## 状态更新规则

- 任务表只记录可复核事实；每次状态变化附 commit（如有）、命令、结果和未覆盖项。
- 新 Roadmap 必须先加入本索引并说明与现有模块的依赖和不重叠边界。
- 不允许新建同义 Roadmap；目标扩大时更新现有文档或明确建立新的稳定领域。
