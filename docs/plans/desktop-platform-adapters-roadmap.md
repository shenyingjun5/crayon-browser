# PLT Windows/macOS 平台适配 Roadmap

- 状态：规划中
- 任务数：7
- 平台：Windows、macOS
- 非目标：Linux、屏幕/标签页/系统音频采集、编码器、WebRTC sender

## 1. 任务表

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| PLT-01 | TODO | FND-09 | `crates/crayon-platform-api/**` | 定义安全存储、本地网络、生命周期、更新和外部客户端交接接口 | `CP-004`,`CP-W01`,`CP-M01`; unit | V1 |
| PLT-02 | TODO | PLT-01,FND-10 | `crates/crayon-platform-api/**`, `crates/crayon-platform-capabilities/**` | 定义 `secure_store`、`local_network`、`lifecycle`、`update`、`external_client_handoff` 能力模型 | `CP-004`; schema/golden | V1 |
| PLT-W04 | TODO | PLT-02,CEF-12,SDK-08 | `platform/windows/**` | 实现 DPAPI、本地网络/防火墙、多网卡、睡眠唤醒、更新与下载/启动独立投屏客户端 | `CP-W01`; Windows integration | V4W |
| PLT-W05 | TODO | PLT-W04,CEF-15,SDK-14,PRV-12 | `apps/desktop-cef/**`, `platform/windows/**` | Windows 产品装配与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-W01`; Windows device | V4W |
| PLT-M04 | TODO | PLT-02,CEF-01E,CEF-12,SDK-08 | `platform/macos/**` | 实现 Keychain、本地网络权限、生命周期、更新与下载/启动独立投屏客户端 | `CP-M01`; macOS integration | V4M |
| PLT-M05 | TODO | PLT-M04,CEF-15,SDK-14,PRV-12 | `apps/desktop-cef/**`, `platform/macos/**` | macOS 产品装配、签名/公证与 Direct/Relay/外部客户端交接验收 | `E2E-001..005`,`CP-M01`; macOS device | V4M |
| PLT-19 | TODO | PLT-W05,PLT-M05 | `docs/current/**`, `docs/plans/**`, `tests/**` | Windows/macOS 平台边界、生命周期和发布前独立 Review | 平台矩阵；Review P0/P1=0 | V5 |

## 2. 外部客户端交接契约

- 交接入口只在 Direct/Relay 不可用或用户主动选择时出现。
- 浏览器先解释需要独立客户端，再由用户确认下载或打开。
- adapter 只返回 `download_started`、`launch_requested`、`not_installed`、`cancelled` 或可诊断错误；不得返回“镜像投屏已开始”。
- 浏览器不向外部客户端传递 Cookie、Authorization、浏览历史或任意页面控制权限。
- 外部客户端拥有自己的安装、授权、采集、编码和镜像生命周期；这些能力不进入本仓库。

## 3. 完成门禁

- Windows/macOS 的网络切换、多网卡、睡眠唤醒、退出和重复调用均能幂等恢复或释放。
- 安全存储、更新与客户端交接失败有明确用户反馈和诊断，但诊断不泄密。
- 生产构建图不存在 Linux、采集、编码或 WebRTC sender 依赖。
- 真实平台验证记录实际 OS、构建、接收端和未覆盖项。
