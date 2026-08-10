# HM HarmonyOS 电脑浏览器 Roadmap

- 状态：后续技术预览
- 任务数：12
- 目标设备：鸿蒙电脑，PC 形态
- 非目标：手机、平板、AVScreenCapture、AVCodec、WebRTC sender

## 1. 任务表

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| HM-01 | TODO | FND-12 | `apps/harmony/**`, `platform/harmony/**` | 建立 DevEco/ArkUI PC 窗口、键盘、鼠标与多任务工程基线 | build；PC-form harness | VH |
| HM-02 | TODO | HM-01 | `apps/harmony/**` | 实现 ArkWeb 导航、标签页、下载和权限基础能力 | Harmony browser integration | VH |
| HM-03 | TODO | HM-02,MED-04 | `platform/harmony/**` | ArkWeb 媒体观察与可信 Browser-side 验证 adapter | `MO-001..007`; integration | VH |
| HM-04 | TODO | HM-02,PLT-02 | `platform/harmony/**` | 定义 ArkWeb、本地 LAN、PC 窗口/输入、文件与外部客户端交接能力 | capability golden | VH |
| HM-05 | TODO | HM-03,HM-04 | `crates/crayon-harmony-ffi/**`, `platform/harmony/**` | 建立有界、可取消、版本化的 Rust C ABI | ABI/unit/fuzz | VH |
| HM-06 | TODO | HM-05 | `apps/harmony/**`, `platform/harmony/**`, build files | 集成 Rust 静态库/HAR 并建立可重复构建 | build/package | VH |
| HM-07 | TODO | HM-06,SDK-08 | `platform/harmony/**`, `crates/crayon-cast-adapter/**` | 接入固定版本 OHPM/HAR Cast-SDK，完成发现、连接与控制 | Fake/real adapter tests | VH |
| HM-08 | TODO | HM-07,MED-19 | `apps/harmony/**`, `crates/crayon-app-runtime/**` | 复用 Direct/Relay/Reject/ExternalClientHandoff 编排 | `PL-007..009`,`PL-015`; integration | VH |
| HM-09 | TODO | HM-06,PRV-08 | `platform/harmony/**` | HUKS、本地网络、Profile、生命周期和清理失败报告 | privacy/security tests | VH |
| HM-10 | TODO | HM-08,HM-09 | `apps/harmony/**`, `platform/harmony/**` | PC 窗口、键鼠快捷键、文件对话框和外部客户端交接产品装配 | PC-form UI/device | VH |
| HM-11 | TODO | HM-10,SDK-14 | `tests/**`, `apps/harmony/**` | 鸿蒙电脑 Direct/Relay/外部交接真实端到端验证 | `E2E-001..005`; device | VH |
| HM-12 | TODO | HM-11 | `docs/current/**`, `docs/plans/**` | PC 形态技术预览 Review、性能/隐私证据和 Go/NoGo | Review P0/P1=0 | VH |

## 2. PC 形态验收

- 使用真实鸿蒙电脑或明确模拟电脑窗口、键盘、鼠标、多任务和本地文件能力的指定 Harness。
- 验证多窗口/多标签、快捷键、焦点、缩放、外接显示器和睡眠唤醒的适用场景。
- 手机或平板运行结果不能替代 PC 形态证据。
- 浏览器只负责 LAN Direct/Relay；无媒体路由时交接给独立客户端，不实现采集、编码或 WebRTC。

## 3. 开始条件

HarmonyOS 电脑工作在 Windows/macOS 的共享协议、Cast-SDK facade、`MED-19` 语义和隐私契约稳定后启动，不阻塞桌面首发。
