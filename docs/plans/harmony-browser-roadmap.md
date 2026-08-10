# HM：HarmonyOS ArkWeb 技术预览 Roadmap

状态：等待共享 API。目标是验证一个与桌面产品语义一致、实现后端不同的 HarmonyOS 形态；不承诺 CEF 移植或完整 GA。

## Go/No-Go 问题

- ArkWeb 能否在不破坏页面的情况下提供用户播放、媒体元素和必要网络观察证据。
- AVScreenCapture 是否能合法、稳定取得目标画面和系统音频；高级安全模式的限制如何降级。
- Rust Core + Cast-SDK facade 是否能通过受支持 NDK/C ABI 稳定接入。
- 本地网络、后台、HUKS、HAP 签名和应用市场规则是否允许产品闭环。

## 原子任务

| ID | 依赖 | 目标路径 | 输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| HM-01 | FND-08 | `browser/harmony-shell` | DevEco/HAP 最小工程、ArkUI 页面、构建说明 | 模拟器/真机启动；无 CEF/桌面依赖 | S4 |
| HM-02 | HM-01 | `harmony-shell/arkweb` | 地址输入、导航、登录、前后退、页面/应用消息通道 | BR-001、BR-002 子集；origin/schema/大小校验 | S4 |
| HM-03 | HM-02 | `arkweb/media_observer` | document-start observer、navigation/frame/generation | BR-003..BR-013 适用项；无自动交互 | S4 |
| HM-04 | HM-02,HM-03 | `arkweb/capability` | 高级安全模式 WebRTC/WASM/WebGL/UDP/媒体能力矩阵 | capability 真机报告；缺失显式降级 | S4 |
| HM-05 | FND-08 | `platform/harmony/native-api` | Core C ABI/NAPI、handle/长度/所有权/线程契约 | ABI fuzz、错误/释放/重复调用 | S2 |
| HM-06 | HM-05 | `platform/harmony/core-build` | Rust 交叉编译 Spike；失败时 C++ shim 决策记录 | TLS/socket/thread/panic/包体/符号 | S4 |
| HM-07 | SDK-05,SDK-06,HM-06 | `crayon-cast-adapter/harmony-ffi` | 通过唯一 adapter 向 ArkTS 暴露发现/码/能力/播控 facade | CS-001..CS-008、真机 CS-010；platform 不直依赖 SDK | S4 |
| HM-08 | MED-08,HM-03,HM-06 | `apps/harmony/runtime` | 共享 observation/policy/runtime 状态语义 | PL-013 golden、Fake E2E V2 | S4 |
| HM-09 | HM-01 | `platform/harmony/secure-store-network` | HUKS、本地网络权限/发现、IPv4/IPv6、后台状态 | PV-007、E2E-007 真机 | S4 |
| HM-10 | HM-01,HM-04 | `platform/harmony/capture` | AVScreenCapture 画面/音频权限与 protected surface | CP-001..CP-005、CP-H01 | S4 |
| HM-11 | HM-10 | `platform/harmony/codec` | AVCodec H.264 capability、时间戳、IDR、停止回收 | CP-001、CP-006、CP-H01；30 分钟 | S4 |
| HM-12 | HM-07,HM-08,HM-10,HM-11 | `apps/harmony` | 技术预览端到端：浏览->播放->设备->投屏->停止 | E2E-001、E2E-003、E2E-004、E2E-005 适用项 | S4 |
| HM-13 | HM-12 | HAP/测试 | 签名开发 HAP、权限/后台/包体/Release 测试隔离 | RG-006、UP-001、UP-004 子集 | S5-preview |
| HM-14 | HM-12,HM-13 | Go/No-Go 报告/Review | 完整浏览器、轻量浏览器或遥控器形态决策；缺口/成本/商店结论 | 无 P0/P1；真机证据可复核 | S4 |

## 决策门槛

- `GO full`：浏览、播放证据、采集音频、Cast-SDK 和隐私清理全部达到桌面同语义。
- `GO lite`：ArkWeb 浏览和 Direct/Relay 可用，但标签页音频/采集受限；产品显式只提供可支持模式。
- `REMOTE only`：ArkWeb/采集不满足浏览器承诺，仅保留设备发现与遥控，不称投屏浏览器。
- `NO-GO`：安全/权限/审核边界无法满足，停止产品化，不用非公开 API 绕过。
