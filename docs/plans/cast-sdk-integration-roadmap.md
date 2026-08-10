# SDK：Cast-SDK 集成 Roadmap

状态：等待 `FND-08`。本机 Cast-SDK 工作树只用于审阅，正式集成不得依赖任何开发者本机绝对路径。

## 边界

只通过稳定 facade 复用发现、投屏码、连接、能力、远程媒体投送、播控和会话监督。网页、Cookie、relay recipe、CEF 与产品 UI 不进入 Cast-SDK。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| SDK-01 | FND-08 | `third_party/cast-sdk`、依赖决策 | 建立固定 git revision 的 submodule；记录 commit、license、升级/回滚 | RG-008；干净 checkout 可复现，无本机 path | S1 |
| SDK-02 | SDK-01 | `crayon-cast-adapter/Cargo.toml` | 只引入必要 facade crate；feature/依赖树/包体基线 | cargo tree；仅 adapter 依赖 SDK；无 tauri/automation 泄漏 | S1 |
| SDK-03 | SDK-02 | `cast-adapter/api` | 产品侧 `CastFacade` trait、强类型 DTO/error；不暴露 SDK 内部类型 | compile/serde/error contract；CS-008 | S1 |
| SDK-04 | SDK-03,FND-09 | `cast-adapter/fake`（test only） | Fake facade 事件/能力/session/generation | CS-001..CS-009 可确定性编排；Release 无 fake | S2 |
| SDK-05 | SDK-03 | `cast-adapter/service` | `SenderCommandService` 生命周期、线程/回调/stop 封装 | start/drop/crash/restart；无 orphan/锁内 callback | S2 |
| SDK-06 | SDK-05 | `cast-adapter/discovery` | start/stop/refresh/list 快照和增量事件 | CS-001、CS-002；多网卡、同名、过期、重复 | S2 |
| SDK-07 | SDK-05,SDK-06 | `cast-adapter/pairing` | connect/disconnect/cast code 状态映射 | CS-003；成功/过期/错误/取消/route lost | S2 |
| SDK-08 | SDK-05 | `cast-adapter/capability` | receiver assessment -> `ReceiverCapabilities`，TTL/generation | CS-004；未知/变化/旧缓存；policy golden | S2 |
| SDK-09 | SDK-05,SDK-08,MED-08 | `cast-adapter/delivery` | Direct/HLS/relay URL 只经 facade 投送；Mirror descriptor 协议确认 | CS-005；无 SOAP/URL 拼接；unsupported 明确 | S2 |
| SDK-10 | SDK-09 | `cast-adapter/control` | handle-bound play/pause/seek/volume/stop | CS-006；非法值、重复、旧 handle、超时 | S2 |
| SDK-11 | SDK-09,SDK-10 | `cast-adapter/session` | listener、自然结束、receiver stop、route lost、替换 | CS-007；旧事件不能停止新会话 | S2 |
| SDK-12 | SDK-07,SDK-08,SDK-10,SDK-11 | `crayon-app-runtime/cast_usecase` | UI/runtime 状态与 SDK 事件编排，撤销 relay/mirror | Fake E2E V2；每个终态资源清理 | S2 |
| SDK-13 | SDK-12 | 真接收端 Harness | 自动发现、投屏码、能力、投送、控制、终态 | CS-010、E2E-001、E2E-002；记录接收端版本/网络 | S4 |
| SDK-14 | SDK-02,SDK-12,SDK-13 | Review/升级说明 | API contract、依赖/许可、错误映射、并发生命周期 Review | 全 CS；无 P0/P1；锁定 SDK revision | S4 |

## SDK 缺口处理

若 `SenderCommandService` 未公开浏览器所需能力：

1. 在本 Roadmap 记录缺口和调用场景。
2. 在 Cast-SDK 建独立 Roadmap、测试和公共 API 评审。
3. SDK 发布/固定新 revision 后执行 `SDK-01/02/09` 升级验证。
4. 浏览器仓库不得调用 private crate、复制 SOAP/CastExtension、缓存设备 IP 或建立临时协议分支。

## Review 专项

- callback 是否在锁外；session generation 是否贯穿所有控制。
- Cast-SDK diagnostics 不得携带网页 URL、Cookie、relay secret。
- SDK runtime 失败不能阻塞浏览器退出或 Profile 清理。
