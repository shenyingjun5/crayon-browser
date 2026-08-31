# QAR 质量与发布 Roadmap

- 状态：Windows 首发核心门禁以 `QAR-01W/02AW/03W/04W/05AW/06W/07W/08AW/09/11W/12W/14W/15W/16W` 推进；macOS 特有 M slices 后续；`QAR-02B/05B/08B` 为第二期 feature 门禁
- 任务数：18
- 当前发布候选：Windows 10/11 x64；macOS arm64 特有打包/签名/公证/生命周期门禁后续独立验证
- 非目标：Linux 包、浏览器采集/编码/WebRTC。Agent、Workflow、Partner Connector、Partner Cast 和 M2 模型 feature 各自有独立 Go/NoGo，不以关闭某 feature 冒充其已通过门禁，也不以其 NO-GO 阻塞已达标的浏览器/LAN 投屏核心。

## 1. 任务表

| ID | 状态 | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 阶段 |
|---|---|---|---|---|---|---|
| QAR-01W | TODO | FND-03,FND-09 | CI 配置 | Windows 原生 runner；fast/core/security/desktop 分层和缓存；macOS runner 后续 01M | 冷/热时长、失败定位、产物上传 | R1 |
| QAR-02AW | TODO | REL-02,CNT-21W,PLT-19W,MDV-20W,MRT-09W | `tests/e2e/desktop/**` | Windows 浏览、网页 Markdown、本地 MDV、Direct/Relay/交接核心 E2E | `E2E-001..004`,`CT-001..008`,`MD-001..013`; Windows 每日 | R1 |
| QAR-02B | TODO | QAR-02AW,AGT-14 | `tests/e2e/desktop/**` | 第二期 CAAP/CLI/MCP 只读 Preview E2E | `AG-013`,`AG-014`; feature 默认关闭 | V5 |
| QAR-03W | TODO | SDK-13,PLT-W05 | `tests/e2e/device/**` | Windows Desktop Host + ADB 自家接收端发现、投屏码、Direct/Relay、控制和终态矩阵 | `CS-010`,`E2E-001`,`E2E-002` | R1 |
| QAR-04W | TODO | QAR-01W,FND-12 | 覆盖率/变更门禁 | changed-lines、关键 crate、schema/ABI golden 门禁 | 阈值与豁免可审计 | R1 |
| QAR-05AW | TODO | CEF-15,SDK-14,CNT-21W,MRT-09W,PLT-19W | 性能 harness | Windows 启动、导航、Direct/Relay、网页 Markdown、本地 MDV UI delay/RSS | perf report；无无界增长 | R1 |
| QAR-05B | TODO | QAR-05AW,AGT-16 | 性能 harness | 第二期 CAAP first-chunk/增量与所选 semantic/Workflow/Hub 预算 | feature perf report | V5 |
| QAR-06W | TODO | QAR-02AW,QAR-03W,QAR-05AW | 稳定性 harness | Windows 重复导航、设备切换、网络切换、睡眠唤醒和退出 | `E2E-005`; 资源归零 | R1 |
| QAR-07W | TODO | QAR-06W | 长稳报告 | Windows 30 分钟 Relay、8 小时 Direct/浏览/Profile | `RL-013`,`E2E-006`; 无趋势泄漏 | R1 |
| QAR-08AW | TODO | PRV-13AW,QAR-02AW | 安全/隐私门禁 | Windows 一期网页 Markdown、MDV、Profile、媒体/Relay/接收端威胁模型、LeakScanner、SSRF/rebinding/replay | 一期核心安全用例；P0/P1=0 | R1 |
| QAR-08B | TODO | QAR-08AW,PRV-13B,QAR-02B,AGT-16 | 安全/隐私门禁 | 第二期 CAAP replay/prompt injection/confused deputy 与所选 Workflow/Partner/model 专项证据 | 所选 feature 全安全用例；P0/P1=0 | V5 |
| QAR-09 | TODO | QAR-01W,PLT-W05,BRD-04 | Windows packaging | 安装、签名、更新、卸载、回滚、防火墙提示；验证 EXE、安装器、快捷方式和任务栏均消费 `app-icon-v1` | `UP-001..003`,`BI-003`,`BI-004`; clean VM；安装/升级/回滚后图标无旧缓存或黑边 | R1 |
| QAR-10 | TODO | QAR-01M,PLT-M05,BRD-04 | macOS packaging | 第一期 arm64 包、签名、公证、更新、卸载和回滚；验证 bundle/Dock/Finder 均消费 `app-icon-v1`；只有原生 x64 门禁完成才产出并宣称 Universal/Intel 支持 | `UP-001..003`,`BI-006`; clean VM；系统遮罩、安装/升级/回滚图标复核 | R1 |
| QAR-11W | TODO | QAR-09 | SBOM/许可 | Windows 候选的 CEF、Cast-SDK、Rust/C++ 依赖 SBOM 与许可证产物 | source/revision/hash 可追踪 | R1 |
| QAR-12W | TODO | QAR-09 | 升级/回滚矩阵 | Windows Profile/schema/SDK 兼容、失败回滚、数据保留与清理 | `UP-001..003`; previous/current | R1 |
| QAR-14W | TODO | QAR-11W,QAR-12W | 发布演练 | Windows 候选包离线安装、升级、回滚与事故演练 | 演练记录；无外部发布 | R1 |
| QAR-15W | TODO | QAR-07W,QAR-08AW,QAR-14W | 发布清单 | Windows 指标/支持矩阵、三闭环开关、第二期 feature OFF、macOS NOT_IN_RELEASE、已知限制与回滚 Runbook | 全门禁证据可追踪 | R1 |
| QAR-16W | TODO | QAR-15W,REL-04 | Go/NoGo Review | 对 Windows x64 一期核心候选做发布决策；macOS 和第二期 feature 保持 NOT_IN_RELEASE | P0/P1=0；结论明确 | R1 |

## 2. 门禁原则

- 第一期 Windows 候选开启的三大闭环只受 `QAR-02AW/05AW/08AW` 核心门禁约束；`QAR-02B/05B/08B` 在第二期 feature 开启前执行，不能反向阻塞一期。
- Windows 和 macOS 分别记录构建、安装、升级、回滚和真实设备证据，不能互相替代；当前 W slices 可以独立形成 Windows 候选，不等待 M slices。
- 外部客户端交接只验证下载/启动请求和错误反馈；浏览器不得被当作镜像 sender 测试。
- `E2E-005/006` 只覆盖 Direct/Relay 与浏览/Profile 生命周期，不包含 WebRTC 或采集编码预算。
- Markdown 是确定性本地处理；性能与安全门禁不依赖远程模型或公共网络。
- 未选择发布的 Workflow/Partner/Partner Cast feature 保持关闭并记录 NOT_IN_RELEASE；选择发布时其 `WFL-16`、`HUB-16` 或 `SDK-16` 及对应 AC/WF/HB/CS 证据必须完成。
- 第一期 macOS 支持矩阵默认为 Apple Silicon；若 QAR-15 宣称 Intel/Universal 可运行支持，必须在原生 x64 硬件补齐长稳，Rosetta 只可作为构建/短 smoke 证据。
- 本 Roadmap 不执行发布、推送、Tag、部署或应用市场提交；这些外部动作仍需用户明确授权。

## 3. Windows-first 与 macOS 后续 slices（REL-05，2026-08-31）

- 表中的 `W`/`AW` 是既有 QAR 顶层任务的 Windows 首发执行 slice，不增加任务总数。它们逐项记录 Windows 证据，并可独立到达 `QAR-16W`。
- macOS 后续使用对应 `01M/02AM/03M/04M/05AM/06M/07M/08AM/10/11M/12M/14M/15M/16M`；复用共享测试但必须补签名/公证、Keychain、原生生命周期、安装/升级/回滚与真实设备证据。M slice 不阻塞 W，也不能改写 W。
- `QAR-09` 是 Windows packaging；`QAR-10` 只属于 macOS 后续。`QAR-11W/12W/14W/15W/16W` 不再依赖 QAR-10；macOS 对应聚合只消费 M slices。
- Windows 候选仍必须完成完整 Release artifact scan、SBOM/许可、30 分钟 Relay、8 小时 Direct/浏览/Profile、clean VM 安装/升级/回滚和 P0/P1=0；平台顺序变化不降低门禁。

## 4. 分拆说明（REL-01，2026-08-30）

- 原 `QAR-02` 混合核心 E2E 与 Agent Preview，拆为 `02A/02B`；一期核心不再依赖 AGT-14。
- 原 `QAR-05` 混合核心性能与 CAAP/后续 feature，拆为 `05A/05B`；一期只冻结浏览、投屏、网页 Markdown 和本地 MDV 预算。
- 原 `QAR-08` 混合核心与 Agent/Workflow/Partner 安全，拆为 `08A/08B`；核心 Review 消费 PRV-13A，第二期消费 PRV-13B。
- 分拆不降低任何已选择 feature 的门禁：第二期 feature 只有完成对应 B 任务并经 QAR-16 单独 Go 后才能开启。
