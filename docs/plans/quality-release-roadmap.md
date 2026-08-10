# QAR 质量与发布 Roadmap

- 状态：规划中
- 任务数：15
- 发布平台：Windows、macOS
- 非目标：Linux 包、浏览器采集/编码/WebRTC、AI/模型发布门禁

## 1. 任务表

| ID | 状态 | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 阶段 |
|---|---|---|---|---|---|---|
| QAR-01 | TODO | FND-03,FND-09 | CI 配置 | Windows/macOS 原生 runner；fast/core/security/desktop 分层和缓存 | 冷/热时长、失败定位、产物上传 | V5 |
| QAR-02 | TODO | CEF-14,MED-19,SDK-12 | `tests/e2e/desktop/**` | 浏览→播放→Fake Receiver→Direct/Relay→Stop，以及外部客户端交接 | `E2E-001..004`; 两平台每日 | V5 |
| QAR-03 | TODO | SDK-13,PLT-W05,PLT-M05 | `tests/e2e/device/**` | 自家接收端发现、投屏码、Direct/Relay、控制和终态矩阵 | `CS-010`,`E2E-001`,`E2E-002` | V5 |
| QAR-04 | TODO | QAR-01,FND-12 | 覆盖率/变更门禁 | changed-lines、关键 crate、schema/ABI golden 门禁 | 阈值与豁免可审计 | V5 |
| QAR-05 | TODO | CEF-15,SDK-14,CNT-10 | 性能 harness | 启动、导航、Direct/Relay 首帧、Relay 开销、Markdown 与交接 UI 预算 | perf report；无无界增长 | V5 |
| QAR-06 | TODO | QAR-02,QAR-03,QAR-05 | 稳定性 harness | 重复导航、设备切换、网络切换、睡眠唤醒和退出 | `E2E-005`; 资源归零 | V5 |
| QAR-07 | TODO | QAR-06 | 长稳报告 | 30 分钟 Relay、8 小时 Direct/浏览/Profile | `RL-013`,`E2E-006`; 无趋势泄漏 | V5 |
| QAR-08 | TODO | PRV-13,QAR-02 | 安全/隐私门禁 | 威胁模型、LeakScanner、SSRF、replay、路径和旧会话专项 | 全安全用例；P0/P1=0 | V5 |
| QAR-09 | TODO | QAR-01,PLT-W05 | Windows packaging | 安装、签名、更新、卸载、回滚和防火墙提示 | `UP-001..003`; clean VM | V5 |
| QAR-10 | TODO | QAR-01,PLT-M05 | macOS packaging | universal 包、签名、公证、更新、卸载和回滚 | `UP-001..003`; clean VM | V5 |
| QAR-11 | TODO | QAR-09,QAR-10 | SBOM/许可 | CEF、Cast-SDK、Rust/C++/Ark 依赖 SBOM 与许可证产物 | source/revision/hash 可追踪 | V5 |
| QAR-12 | TODO | QAR-09,QAR-10 | 升级/回滚矩阵 | Profile/schema/SDK 兼容、失败回滚、数据保留与清理 | `UP-001..003`; previous/current | V5 |
| QAR-14 | TODO | QAR-11,QAR-12 | 发布演练 | Windows/macOS 候选包、离线安装、升级、回滚与事故演练 | 演练记录；无外部发布 | V5 |
| QAR-15 | TODO | QAR-07,QAR-08,QAR-14 | 发布清单 | 指标、支持矩阵、已知限制、客服/诊断和回滚 Runbook | 全门禁证据可追踪 | V5 |
| QAR-16 | TODO | QAR-15 | Go/NoGo Review | Windows/macOS 分平台发布决策和剩余风险签字 | P0/P1=0；结论明确 | V5 |

## 2. 门禁原则

- Windows 和 macOS 分别记录构建、安装、升级、回滚和真实设备证据，不能互相替代。
- 外部客户端交接只验证下载/启动请求和错误反馈；浏览器不得被当作镜像 sender 测试。
- `E2E-005/006` 只覆盖 Direct/Relay 与浏览/Profile 生命周期，不包含 WebRTC 或采集编码预算。
- Markdown 是确定性本地处理；性能与安全门禁不依赖远程模型或公共网络。
- 本 Roadmap 不执行发布、推送、Tag、部署或应用市场提交；这些外部动作仍需用户明确授权。
