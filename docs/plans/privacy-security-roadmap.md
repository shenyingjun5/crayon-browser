# PRV：隐私与产品安全 Roadmap

状态：`FND-08 DONE`，等待 `CEF-05`。Relay 网络安全实现归 MED，本 Roadmap 负责 Profile、追踪防护、安全存储、隐私数据流和系统级安全门禁。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| PRV-01 | FND-08,CEF-04 | `crayon-profile/model` | Profile ID/type/path/lifecycle 状态机，随机目录 ID | PV-001、PV-004；非法状态/重复关闭 | S1 |
| PRV-02 | PRV-01 | `crayon-profile/ephemeral` | 临时 context、最后窗口关闭、清理清单与结果 | PV-001、PV-002、PV-003；每类存储 fixture | S3 |
| PRV-03 | PRV-01 | `crayon-profile/persistent` | 常用空间创建/隔离/销毁事务 | PV-004、PV-005；部分失败/重试 | S3 |
| PRV-04 | PRV-02,PRV-03 | `crayon-profile/path_guard` | 绝对根验证、symlink/junction/reparse 防护、启动补偿清理 | PV-006；逃逸目标零修改 | S2 |
| PRV-05 | PLT-W04,PLT-M04 | `crayon-profile/secure_store` | Windows/macOS 安全存储接口、key ID、轮换/删除/不可用状态 | PV-007；明文扫描；错误映射 | S4 |
| PRV-06 | CEF-05,FND-11 | `browser/privacy/standard` | 第三方 Cookie、存储分区、Referer、HTTPS、权限默认 | PV-008、PV-009；兼容 fixture | S3 |
| PRV-07 | PRV-06 | `browser/privacy/strict` | 高熵 API 统一降精度/限制，能力/兼容开关 | PV-009；熵/兼容；无每 Profile 随机身份 | S3 |
| PRV-08 | FND-08,FND-09 | `crayon-domain/diagnostics` | 数据分类、redaction、事件 schema、bounded producer | RL-014、PV-008、PV-010；满队列 dropped | S2 |
| PRV-09 | PRV-08 | `apps/*/diagnostics` | 默认关闭遥测、崩溃 opt-in、发送前预览、删除 | PV-008、PV-010；实际 payload 对照 | S3 |
| PRV-10 | MED-18,SDK-12 | `docs/current/threat-model.md` | 资产/信任边界/威胁/缓解/残余风险，覆盖网页、IPC、LAN、供应链 | 安全用例映射无缺口；专项 Review | S0 |
| PRV-11 | PRV-04,PRV-05,PRV-07,PRV-09,PRV-10 | `tests/security/privacy` | 磁盘/日志/DTO/网络 LeakScanner 与 profile 全存储扫描 | PV 全集、RL-014；零秘密 | S3 |
| PRV-12 | PRV-10,PRV-11 | `tools/repo-guard` | secret/debug/unsafe route/自动广告行为静态门禁 | 故意违规样本失败；Release 零例外 | S2 |
| PRV-13 | PRV-11,PRV-12 | Review/数据流文档 | 隐私影响评估、数据矩阵、平台差异和清理限制；修 P0/P1 | security/desktop tests；无虚假隐私承诺 | S3 |

## 不允许的实现

- 不允许通过清空一部分目录就宣称无痕完成。
- 不允许为不同 Profile 随机 UA/Canvas/WebGL/时区形成稳定唯一指纹。
- 不允许诊断 consumer 反压浏览、relay、Cast-SDK 或退出。
- 不允许用 Profile 名作为路径，不允许删除未验证根目录或跟随 reparse point。
