# PLT：Desktop 平台适配 Roadmap

状态：等待 `CEF-07`、`SDK-05`。共享接口冻结后，Windows/macOS/Linux 可由不同 Agent 并行；每个平台任务只修改自己的目录和 contract fixture。

## 共享任务

| ID | 依赖 | 目标路径 | 输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| PLT-01 | FND-08 | `platform/api` | capture/codec/store/network/lifecycle/update 接口和 ownership/thread contract | fake tests、C ABI/static assertions；无 OS 类型 | S2 |
| PLT-02 | PLT-01 | `platform/api/capabilities` | 启动 capability snapshot、变化事件和稳定错误 | PL-011、PL-013；unknown/denied/degraded | S2 |
| PLT-03 | PLT-01,FND-11 | `platform/api/config` | 画质/帧率/码率/延迟预算强类型配置与边界 | 非法/极值/默认/receiver constraint | S1 |

## Windows

| ID | 依赖 | 目标路径 | 输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| PLT-W01 | PLT-01,CEF-07 | `platform/windows/capture` | WGC 目标区域/帧/颜色/resize 与生命周期 | CP-002、CP-003、CP-W01；遮挡/全屏/多屏 | S4 |
| PLT-W02 | PLT-W01 | `platform/windows/audio` | WASAPI loopback、设备切换、静音/无音频状态 | CP-001、CP-003、CP-W01；音画时间戳 | S4 |
| PLT-W03 | PLT-W01,PLT-W02,PLT-03 | `platform/windows/codec` | MF/D3D11 H.264 capability、零拷贝/回退、IDR/stop | CP-001、CP-006、CP-W01；30 分钟、资源释放 | S4 |
| PLT-W04 | PLT-01 | `platform/windows/system` | DPAPI、本地网络/防火墙、多网卡、休眠、更新接口 | PV-007、E2E-007、CP-004 | S4 |
| PLT-W05 | PLT-W01,PLT-W02,PLT-W03,PLT-W04,SDK-09 | `apps/desktop/windows` | 正式装配 + WebRTC/接收端闭环 | E2E-001、E2E-005、Windows matrix | S4 |

## macOS

| ID | 依赖 | 目标路径 | 输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| PLT-M01 | PLT-01,CEF-07 | `platform/macos/capture` | ScreenCaptureKit 画面与权限状态；不绕 protected surface | CP-002、CP-003、CP-005、CP-M01 | S4 |
| PLT-M02 | PLT-M01 | `platform/macos/audio` | 合法系统音频路径、权限/不可用显式状态、时间戳 | CP-001、CP-003、CP-M01 | S4 |
| PLT-M03 | PLT-M01,PLT-M02,PLT-03 | `platform/macos/codec` | VideoToolbox H.264、颜色空间、IDR、硬编资源 | CP-001、CP-006、CP-M01；Intel/AS | S4 |
| PLT-M04 | PLT-01 | `platform/macos/system` | Keychain、本地网络权限、route、休眠、公证更新接口 | PV-007、E2E-007、CP-004 | S4 |
| PLT-M05 | PLT-M01,PLT-M02,PLT-M03,PLT-M04,SDK-09 | `apps/desktop/macos` | 签名开发 App + 投屏闭环 | E2E-001、E2E-005、权限恢复 | S4 |

## Linux

| ID | 依赖 | 目标路径 | 输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| PLT-L01 | PLT-01,CEF-07 | `platform/linux/capture` | Wayland PipeWire portal；X11 capability 分开 | CP-002、CP-003、CP-L01；用户取消 portal | S4 |
| PLT-L02 | PLT-L01 | `platform/linux/audio` | PipeWire 音频 node 与时钟；无音频降级 | CP-001、CP-003、CP-L01 | S4 |
| PLT-L03 | PLT-L01,PLT-L02,PLT-03 | `platform/linux/codec` | VA-API/V4L2 probe、driver deny/allow、软件回退门禁 | CP-001、CP-006、CP-L01；GPU matrix | S4 |
| PLT-L04 | PLT-01 | `platform/linux/system` | Secret Service、mDNS/firewall/multi-NIC、sleep、更新接口 | PV-007、E2E-007、CP-004 | S4 |
| PLT-L05 | PLT-L01,PLT-L02,PLT-L03,PLT-L04,SDK-09 | `apps/desktop/linux` | 支持发行版正式装配；Wayland/X11 声明 | E2E-001、E2E-005；package smoke | S4 |

## 收口

| ID | 依赖 | 输出 | 验收 |
|---|---|---|---|
| PLT-19 | PLT-W05,PLT-M05,PLT-L05 | 跨平台差异报告、性能/权限/包体基线、Review | CP 全集适用项；策略结论一致；各平台无 P0/P1 |

## 平台任务通用门禁

- 真实动态视频左上角毫秒时间码 + 实际声音测延迟，不能用内部阶段点代替。
- 权限拒绝、永久拒绝、撤销、系统设置恢复和 App 重启分别验证。
- 采集/编码对象、GPU surface、audio client、线程和 callback 必须在 stop 后回收。
- H.264/AAC 能力实现不等于许可放行，QAR-09 前不得进入正式产物承诺。
