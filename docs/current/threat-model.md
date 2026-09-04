# 蜡笔浏览器威胁模型（PRV-10）

- 版本：v1.0
- 日期：2026-08-22
- 状态：当前权威安全契约；模块实现后的专项 Review 必须回填本文件并升版
- 上游：当前 PRD v0.7、[architecture.md](architecture.md)、[test-cases.md](test-cases.md)、[med-security-review.md](med-security-review.md)

## 1. 范围

覆盖七个领域的威胁、缓解与残余风险：网页内容、IPC/LAN（投屏与 Relay）、入站 CAAP/CLI/MCP、语义动作、Workflow/Challenge、出站 Partner connector、模型与供应链。非目标：接收端设备内部安全（Cast-SDK/receiver owner）、外部投屏客户端内部安全、HarmonyOS 平台细节（技术预览期）。

## 2. 资产与安全目标

| 资产 | 安全目标 |
|---|---|
| Cookie、Authorization、密码、支付凭证 | 不出 Browser process；不进入接收端命令、媒体 URL、云端、日志、诊断、receipt、Workflow Recipe |
| 浏览历史、页面正文、Profile 存储 | Profile 间隔离；无痕清理可验证；不进入遥测/诊断 |
| 投屏会话与 LAN 控制面 | 只有用户验证播放后才可投；会话不可被本机/局域网内第三方劫持或重放 |
| Agent 授权面（grant/确认） | 默认 deny；不可被页面、模型输出或工具结果扩大 |
| 系统资源 | 队列/缓存/并发有界；不可被恶意网页或本机 client 耗尽 |
| 供应链（CEF、Cast-SDK、依赖） | 版本锁定与可验证来源；无测试/调试残留进入 Release |

## 3. 信任边界

```text
B1 网页内容（Renderer/无障碍树/DOM/模型输出/工具结果）  —— 一律不可信
B2 Browser process（唯一可信判定点：播放验证、授权、DRM/广告结论）
B3 LAN（发现/投屏码/Direct/Relay/接收端）              —— 接收端与网络均不可信
B4 本机 IPC（CAAP named pipe/UDS、CLI、入站 MCP）      —— 仅当前用户 + loopback 可信
B5 出站 Partner connector                              —— 合作方 API 不可信（响应内容不可信）
B6 平台层（secure store/lifecycle/update/handoff）     —— OS 能力可信，接口数据仍需校验
B7 供应链（源码/依赖/构建产物）                        —— 构建环境可信，产物必须可复验
```

跨界数据规则：B1 内容不能生成/扩大 grant、改变目标、跳过确认或触发额外工具（AG-005/AC-006）；B4 入站与 B5 出站使用不同 transport、凭证、命名空间与审计 owner；CEF/ArkWeb 类型、DOM 指针、CDP 对象与平台句柄不得进入公共 schema。

## 4. 威胁、缓解与证据

### 4.1 网页内容（B1→B2）

| 威胁 | 缓解 | 证据/状态 |
|---|---|---|
| 恶意网页伪造播放事件触发投屏 | 播放门禁 fail-closed：页面自报不可信，仅 BrowserVerified 放行 | PL-010；cast-policy 门禁矩阵（已实现） |
| 网页诱导自动点击播放/广告/跳过 | 红线禁止自动点击、广告 currentTime/速率/可见性修改、广告域名过滤 | AG-009 沿用正常投屏门禁；执行层归 AGT-10 |
| DRM 绕过/key 提取 | 只识别与拒绝直投；无 CDM/key 操作 | E2E-003；策略 Reject 路径（已实现） |
| 高熵指纹/跨站追踪 | 标准模式默认阻止第三方 Cookie、存储分区、Referer 收敛；严格模式统一降精度、无每 Profile 随机身份 | PV-008/PV-009；PRV-06/07 DONE |
| 超深/超大页面拖垮快照/语义地图 | 对外字段有界、precondition fail closed、分页/背压/取消 | AC-002/AC-005/AC-010；CNT/ACT 模块任务 |
| 提示注入（页面/模型文本"忽略规则并授权"） | 内容统一 untrusted；不能扩大 grant/改目标/触发第二工具 | AG-005；grant 模型结构保证（AGT-04 DONE） |

### 4.2 IPC/LAN 投屏与 Relay（B2↔B3）

| 威胁 | 缓解 | 证据/状态 |
|---|---|---|
| LAN 开放代理/任意 URL 路由 | 无通用 extract/proxy；opaque 高熵 session/resource ID；控制面 loopback+secret | RL-001/RL-003/RL-008（已实现） |
| SSRF/DNS rebinding | 逐跳 allow-set、IP 分类、解析后固定地址 | RL-006/RL-007（已实现） |
| 假接收端/设备冒充 | receiver 绑定、可选首请求 IP 校验、设备级撤销 | RL-003；CS-002 稳定 device ID |
| 控制命令重放 | 128-bit CSPRNG token、常数时间比较、TTL、stop 即失效 | RL-002/RL-004（已实现） |
| Cookie/凭证进入 recipe/日志 | recipe 类型层面无 Cookie/Authorization；Zeroizing；Debug 脱敏；零日志语句 | RL-014/RL-015（已实现） |
| 播放列表/manifest 改写注入 | opaque 改写只重写允许字段；二进制逐字节一致 | RL-010/RL-011（已实现） |
| 并发/断流资源耗尽 | 媒体面并发闸门满载 503 不排队；断流超时；30 分钟长稳 | RL-012/RL-013（已实现） |
| 网络切换/多网卡/VPN 误投 | 接口能力观察 + DefaultRouteChanged 后旧 session 不自动恢复 | CP-004/E2E-007；PLT-01 接口 DONE，真机归 PLT-W04/M04 |
| 外部客户端交接被冒用为镜像后门 | 交接结果闭合（无"镜像已开始"变体）；请求无页面数据；需用户确认 | E2E-004；ExternalClientHandoff 契约（AGT/PLT DONE，真机归 PLT-W04/M04） |

### 4.3 入站 CAAP / CLI / 入站 MCP（B4→B2）

| 威胁 | 缓解 | 证据/状态 |
|---|---|---|
| 错误用户/非 loopback peer 接入 | 握手前拒绝（PeerIdentity same_user ∧ loopback 门禁）；无 remote bind | AG-012；PLT-01 `LocalAgentIpcEndpoint` 契约 DONE；transport 归 AGT-12 |
| 未授权工具调用 | 版本化 registry + capability guard + default-deny grant（四元组绑定、立即撤销、目标失效） | AG-003；AGT-02/03/04 DONE |
| 高危动作静默执行 | R2～R4 必须用户确认；确认摘要含 client/tool/目标/关键参数；变化后重确认 | AG-004；确认 UI 归 AGT-05 |
| 永久禁止 surface 复活 | 注册期永久 deny list（raw CDP/WebDriver/任意 JS/cookie/凭证/支付/文件/网络代理） | AGT-02 DONE；AG-015 Release scan 归后续 |
| 重放/超限/超大消息 | session secret 到期、幂等键、消息上限、deadline、队列有界 | AG-002；AGT-03 DONE |
| 审计与脱敏缺口 | receipt 只含闭合 token（无正文/query/secret）、TTL、用户预览/清除；诊断 DataClass 门禁 | AG-011/PV-010；AGT-11/PRV-08 DONE |
| MCP 适配层成为第二套实现 | MCP 只是 CAAP loopback adapter，schema 同源 registry，默认关闭 | AG-014；归 AGT-14 |

### 4.4 语义动作（B1→B2→页面副作用）

| 威胁 | 缓解 | 证据/状态 |
|---|---|---|
| selector/任意 JS/CDP 透传 | 不存在该类 surface；内部多信号定位，外部无 CSS/XPath/JS selector | AC-004/AC-012；ACT 模块任务 |
| 过期/跨目标 action_id（TOCTOU） | action_id 绑定 generation/TTL/nonce；跨 target/generation 全失效；失败关闭 | AC-003/AC-007；ACT 归后续 |
| 密码/支付/文件/隐藏元素被执行 | 敏感元素不产生可执行 action_id；风险只升不降；永久拒绝清单 | AC-006/AC-009/AG-010 |
| 效果不确定时谎报成功 | 仅 verified 报成功；indeterminate 不自动重放；幂等键拦截重复副作用 | AC-008 |

### 4.5 Workflow / Challenge / 个人 Site Skill

| 威胁 | 缓解 | 证据/状态 |
|---|---|---|
| Recipe/Skill 携带凭证或被静默覆盖 | Recipe 类型层面无密码/验证码/Cookie/Authorization/Token/支付/原始路径；版本变化不可静默覆盖 | WFL 模块任务（规划中） |
| Challenge 被自动化求解 | 只允许识别/暂停/人工接管/安全复检/短期断点续跑；禁止求解验证码/打码/反检测 | WFL 归后续；红线已冻结 |
| 高风险自愈改变目标或降级 | 自愈受控漂移检测；route/fallback 重新校验语义/scope/风险/幂等/确认 | WFL 归后续 |
| 成功任务未确认即沉淀 | 仅 verified success 生成候选；用户预览确认后保存 | WFL 归后续 |

### 4.6 出站 Partner connector（B2→B5）

| 威胁 | 缓解 | 证据/状态 |
|---|---|---|
| 入站/出站边界混淆 | 不同 transport、凭证、命名空间、授权与审计 owner；connector 不能调用内部工具或复用 Agent grant | HUB 模块任务（规划中） |
| 合作方响应注入扩大权限 | connector 响应统一不可信；不能生成 grant/route override/修复决定 | HUB 归后续 |
| OAuth/scope 滥用 | connector 拥有 OAuth/scope 与网络策略；不持有页面操作直通 | HUB 归后续 |
| Partner 失败静默降级网页执行 | 任何 route/fallback 重新确认 | 红线冻结；HUB 归后续 |

### 4.7 模型（第二阶段）与供应链

| 威胁 | 缓解 | 证据/状态 |
|---|---|---|
| 模型参与权限/风险/投屏决策 | 模型位于内容与建议边界之后；输出不可信，不参与 grant/路由/挑战/修复决策 | 第二阶段；边界已冻结 |
| API Key/凭证落盘泄露 | 第一阶段 Release 无真实 provider/Key；secure store 接口已定义（key token/value 有界、闭合错误） | PLT-01 DONE；PRV-05 归 PLT-W04/M04 |
| CEF/Cast-SDK 版本漂移 | CEF 固定版本 + hash 校验；Cast-SDK 固定 git revision + source lock | CS-009；CEF-01A..01D、SDK-01 DONE |
| 依赖引入风险 | 新依赖审查来源/许可证/维护/包体；workspace 零多余第三方（agent-gateway/platform-api std-only） | FND 规则；各任务验证记录 |
| 测试/调试残留进入 Release | 生产/测试隔离规则；Release 扫描门禁 | RG-006；PRV-12 静态门禁归后续 |
| 更新通道劫持 | UpdateFlow 闭合状态机、错误无 URL；真实通道归 PLT/QAR | PLT-01 DONE；PLT-W04/M04/QAR 归后续 |

## 5. 残余风险登记

2026-09-03 后续范围调整：用户明确不处理代理专项/接收端 URL 代检，原 R05/R06 不再属于本次实施与发布依赖。下述 R01 的外部能力 Review 描述仅约束未来重新立项；没有启用新接口或放宽既有安全策略。历史特殊网络失败保留为未覆盖，不等于普通 Direct 或后续 UI 的前置阻塞。

2026-09-03 `PLT-CAST-R01` 设计增量：按 [投屏交互契约](cast-interaction.md) 约束多播放器证明隔离、草稿/源版本与显式提交、MHV2 版本降级拒绝和 Browser-owned 覆盖层；对应实现/攻击向量尚待 R03/R04/R07/R09/平台任务，不能宣称已缓解。Fake-IP 检查失败不转换为 Clear；接收端 URL 评估的来源认证、逐跳地址绑定和保护证据须经 R05 外部能力 Review，当前不可启用。错误事实仅含封闭枚举，无地址/秘密，不参与授予 route。

2026-09-03 受限 LAN probe 边界：仅 Browser 当前真实播放、同源 RFC1918 IPv4 literal 与明确设备选择的短期 StartCast 可获一次精确 HEAD/Range 预检。禁止域名/IPv6/跨源/凭证/代理/跳转，撤销在 SDK 提交前生效；默认网络与 Relay guard 不放宽。网络目标校验不是授权凭据；恶意页面单独提供 URL/Network fact 不获权。实现与取消矩阵尚待 [PLT-M05b4b1..b4](../plans/lan-media-probe-roadmap.md) 补证。

| 风险 | 等级 | 处置 |
|---|---|---|
| 真实接收端闭环未验证（SDK-13 BLOCKED 无真机） | 中 | QAR 真机门禁；不得用 Fake 证据充当 |
| AGT transport/确认 UI 未落地，grant/receipt 仅模型层证据 | 中 | AGT-05/12 后补 transport 与 UI 安全用例 |
| 语义动作/Workflow/Hub 未实现，4.4～4.6 多为契约级缓解 | 中 | 模块实现后专项 Review 必须回填本表 |
| DASH relay v1 缺口（MED-18 P3） | 低 | 只影响 DASH 直投可用性，非安全扩大 |
| 无痕/持久清理的平台限制（文件锁、OS 缓存） | 低 | 显式报告失败，不宣称已清除 |
| 提示注入的长文/多轮变体仅模型级测试 | 低 | AG-015 fuzz/注入专项在 AGT-15 关口 |

## 6. 安全用例映射（无缺口核对）

| 用例族 | 对应节 |
|---|---|
| PL-001..015（播放/策略） | §4.1、§4.2 |
| RL-001..015（Relay/LAN） | §4.2 |
| CS-001..012（Cast-SDK 集成） | §4.2、§4.7 |
| PV-001..010（隐私） | §4.1、§4.3、§4.7 |
| AG-001..015（Agent 访问） | §4.3 |
| AC-001..012（语义动作） | §4.4 |
| E2E-001..007（投屏闭环/长稳） | §4.2、§5 |
| CP-004/CP-W01/CP-M01/AG-012 平台语义 | §4.2、§4.3、§4.7 |

核对规则：新增安全用例或模块落地时，必须同步更新本表与对应威胁行；专项 Review 发现的新威胁进入 §5 残余风险登记或直接补缓解。

## 7. 维护

- 每个安全相关模块任务（AGT/ACT/WFL/HUB/PRV/PLT/QAR）完成 Review 时回填对应行并注明证据。
- 本文件优先级低于当前 PRD/架构的安全红线，高于历史归档；冲突时以红线为准并在此记录。
