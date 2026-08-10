# QAR：质量、合规与发布 Roadmap

状态：等待各切片实现。发布任务按平台独立 `DONE`；一个平台通过不能替代其他平台证据。

## 原子任务

| ID | 依赖 | 目标路径/产物 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| QAR-01 | FND-03,FND-09 | CI 配置 | Win/mac/Linux 原生 runner；fast/core/security/desktop 分层和缓存 | 冷/热时长、失败定位、产物上传 | S3 |
| QAR-02 | CEF-14,MED-17,SDK-12 | `tests/e2e/desktop` | 浏览->播放->Fake Receiver->Mirror/Direct/Relay->Stop | E2E-001..E2E-004；每平台每日 | S3 |
| QAR-03 | SDK-13,PLT-W05,PLT-M05,PLT-L05 | `tests/e2e/device` | 自家接收端自动发现/码/播控/终态矩阵 | CS-010、E2E-001、E2E-002 | S4 |
| QAR-04 | MED-18,PRV-11 | `tests/security` | SSRF/rebinding/open proxy/replay/secret/IPC fuzz suite | RL/PRV/RG security 全集 | S3 |
| QAR-05 | PLT-19 | `tests/performance` | 首帧、延迟、CPU/GPU、音画、包体基线和预算报警 | CP-001、CP-006；设备/素材/口径完整 | S4 |
| QAR-06 | QAR-02,QAR-03 | `tests/stress` | 1000 session、100 切换、断网/休眠/设备重启 | E2E-005、E2E-007；资源回落 | S4 |
| QAR-07 | QAR-06 | 长稳报告 | 30 分钟 relay、8 小时 Mirror、8 小时浏览/Profile | RL-013、E2E-006；无趋势泄漏 | S4 |
| QAR-08 | PRV-13,MED-18,SDK-14 | 独立安全 Review/渗透 | 浏览器/IPC/LAN/Profile/update 攻击面报告 | P0/P1 关闭；P2 有任务 | S4 |
| QAR-09 | FND-08 | `docs/current/component-licensing.md` | 从候选技术栈开始维护 H.264/AAC/HEVC/AV1/Widevine/CDM/CEF/Cast-SDK 许可与地区/渠道结论；依赖冻结后复审 | 未放行组件不进入正式依赖/产物 | S0+legal |
| QAR-10 | QAR-09 | SBOM/NOTICE 流水线 | 每平台 SBOM、NOTICE、source mapping、签名 hash | UP-004、UP-005；依赖与产物一致 | S5 |
| QAR-11 | QAR-01 | Windows packaging | EXE/MSIX、签名、安装/卸载/升级/回滚 | UP-001..UP-003；干净 Win10/11 | S5 |
| QAR-12 | QAR-01 | macOS packaging | app/DMG/PKG、签名、公证、权限说明、更新 | UP-001..UP-003；AS/Intel | S5 |
| QAR-13 | QAR-01 | Linux packaging | 支持的 deb/rpm/AppImage/Flatpak 选型、签名/校验/更新 | UP-001..UP-003；声明发行版/Wayland | S5 |
| QAR-14 | QAR-11,QAR-12,QAR-13 | 更新与应急演练 | Stable/Beta/Dev、原子更新、失败恢复、高危 CEF 72h 评估 | UP-002、UP-003；profile schema migration | S5 |
| QAR-15 | QAR-04,QAR-07,QAR-08,QAR-10,QAR-14 | GA candidate Review | PRD/架构/测试/隐私/许可/包体/指标和已知限制对齐 | 全发布门禁；无 P0/P1 | S5 |
| QAR-16 | QAR-15 | 发布决策记录 | 平台分别 GO/NO-GO；源码 tag/二进制/SBOM/NOTICE 同步计划 | 未授权不执行发布；完成后回读验证 | S5 |

## 发布阻断条件

- 任意自动广告操作、DRM 绕过、通用 LAN proxy 或 secret 泄漏。
- 未关闭 P0/P1、未完成目标平台 S4/S5 证据。
- CEF/Cast-SDK/codec/CDM 许可、签名或源码对应关系不清。
- 无痕清理失败被静默、Release 包含 debug/test 入口。
- 不能独立升级某平台的浏览器安全内核。

## 发布状态机

```text
DRAFT -> VERIFIED -> PACKAGED -> STAGED -> APPROVED -> PUBLISHED -> POST_VERIFIED
```

任何上传、Tag、更新 manifest 切换和应用市场提交需要用户明确授权；Roadmap 完成不自动授权发布。
