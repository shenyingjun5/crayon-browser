# 活跃 Roadmap

本目录只保存当前可执行的模块 Roadmap。Agent 从首批执行队列或任务表中领取一个状态为 `READY` 的任务；完成模块后把稳定结论收敛到 `docs/current/`，实施文档移入 `docs/archive/`。

当前目录共定义 128 个唯一原子任务；`docs/current/test-cases.md` 共定义 93 个唯一测试用例。

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
11. `FND-07E` 收口启动、Relay 与 CLI 装配：当前 `IN_PROGRESS`。

`FND-07` 是迁移 Epic，实际按 `FND-07A`～`FND-07E` 顺序执行；当前只移动 legacy relay 启动、CLI/UI-test 编排与装配收口，并补齐可复现的 Tauri build 前置资源。

## 状态更新规则

- 任务表只记录可复核事实；每次状态变化附 commit（如有）、命令、结果和未覆盖项。
- 新 Roadmap 必须先加入本索引并说明与现有模块的依赖和不重叠边界。
- 不允许新建同义 Roadmap；目标扩大时更新现有文档或明确建立新的稳定领域。
