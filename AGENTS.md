<!-- rulepack-simple type=development -->

# 蜡笔 AI Agent 投屏浏览器 Agent 规则

本文件只保存仓库级、跨任务长期有效的规则。任何 Agent 修改代码、测试、构建、依赖或 Roadmap 前必须完整读取；更深目录的 `AGENTS.md` 只能补充专有约束，不能放松本文件。详细架构、验证和 Review 规则分别以 `docs/current/architecture.md`、`testing-standard.md`、`code-review-standard.md` 为准。

## 1. 产品边界

- 产品：面向 AI Agent 定制的“蜡笔 AI Agent 投屏浏览器”。桌面范围为 Windows/macOS CEF；HarmonyOS 仅做鸿蒙电脑 PC 形态技术预览；Linux 当前不在范围。
- 顺序：浏览器基础与 LAN Direct/Relay 投屏 → 确定性页面快照/Markdown → Agent 协议、CLI/MCP、语义操作 → Workflow/Challenge/个人 Site Skill → Capability Hub/Partner Connector → 第二阶段模型能力。
- 浏览器只做 LAN Direct/Relay 媒体投送，不实现 WebRTC、标签页/窗口/系统音频采集、编码或镜像传输；无可投路由时只交接独立蜡笔投屏客户端。
- 设备发现、投屏码、连接、能力评估、DLNA/CastExtension、播放控制和会话监督复用 Cast-SDK；浏览器不得复制协议栈。
- MCP 是自有版本化 Agent 协议的 adapter，不是第二套业务实现。CLI/MCP 必须共享握手、工具、错误、取消、幂等、generation 与审计语义。
- 非目标包括视频下载、内容聚合、站点批量抓取、广告跳过、DRM 绕过、验证码求解、反检测指纹、代理池和云端媒体代理。第一阶段 Release 不接真实模型 provider。

## 2. 事实来源与阅读顺序

开发前依次读取：

1. 本文件。
2. `docs/current/README.md` 指向的当前 PRD、架构、测试、安全和 Review 契约。
3. `docs/plans/README.md`、总 Roadmap 与所属模块 Roadmap。
4. 任务涉及的生产代码、测试和相邻调用方。
5. 只有需要历史背景时才读归档；归档不得覆盖 current 契约。

权威顺序：当前 PRD/架构/协议/安全契约 > 当前模块 Roadmap > 代码与测试确认的现状 > 历史文档。Roadmap 描述不等于已实现，必须核对 Git 和真实代码。

## 项目记忆

<!-- project-memory:begin -->
- 稳定事实：Workspace 为 `crayon-browser-core` + `crates/*`（Rust）与 `browser/*`（C++17 共享层 + CEF 壳）；Roadmap 的 `src/**` 映射 `browser/cef-shell/src/**`。
- 平台顺序（用户决策，2026-08-22）：macOS 先行跑通，Windows 负责最终产品装配与平台回归；具体完成状态只看平台/Roadmap 证据。
- Rosetta 边界：arm64 Mac 上的 x64 产物只做短启停 smoke；x64 长稳必须使用原生 x64 硬件。
- Keychain 边界（用户决策，2026-08-23）：Chromium Cookie 加密使用产品语义 `use-mock-keychain`；macOS Keychain 只由 `SecureStore` 在用户真实保存/读取机密时访问。
- Cast-SDK 只允许 `crayon-cast-adapter` 依赖；浏览器发现 SDK 公共能力缺口时先走 SDK Roadmap/API/版本交付，不在本仓临时复制实现。
<!-- project-memory:end -->

项目记忆只记录已确认且会持续影响后续工作的事实，标明来源和日期；不记录临时命令、任务进度、易变测试数量、猜测或秘密。

## 3. Roadmap 驱动

- 实质性开发只能从活跃 Roadmap 领取一个原子任务；开始前补齐状态、单一目标、输入、依赖、允许/禁止路径、边界、验收命令和明确不做项。
- 状态只用 `TODO`、`READY`、`IN_PROGRESS`、`BLOCKED`、`IMPLEMENTED`、`VERIFIED`、`DONE`。一次只能有一个本人领取的任务处于 `IN_PROGRESS`。
- `IMPLEMENTED` 只表示代码完成；`VERIFIED` 表示规定自动化验证已有证据；涉及平台/设备门禁的任务只有真机或指定 Harness 通过后才能 `DONE`。
- 完成记录必须列出实际命令、结果、未覆盖项和风险；依赖未完成不得偷跑，未运行不得写成通过。
- 一个原子任务只有一个主要变化原因，通常一个可审查提交、可独立回退；行为变化与大规模纯移动分开。
- 新增跨模块协议、公共 API、状态机、持久化 schema、安全边界，或改变两个以上稳定领域所有权时，先建立/修订独立 Roadmap 并评审。
- 预计超过 2 个工程日、约 10 个生产文件或约 1000 行净新增生产代码，或需要独立性能/兼容/长稳/多平台矩阵时，也应拆分 Roadmap。

## 4. 架构与所有权

- 依赖方向固定：产品 UI/编排 → 领域接口 → 共享 Core/Cast-SDK facade → 平台 adapter。CEF、ArkWeb 和 OS 类型只能进入对应 adapter/shell。
- 状态必须有唯一 owner；模块不得直接修改别的模块的内部集合、锁或缓存。共享策略使用能力模型，不散落平台/型号判断。
- `lib.rs`/`main.rs` 只做装配、re-export 和生命周期入口；禁止无边界的 `utils.rs`、`common.rs`、`manager.rs`、`misc.rs`。
- Agent 访问统一经过版本化 tool registry、capability guard 和 app-runtime 用例；CLI/MCP 不得直连 CEF、ArkWeb、CDP、Cast-SDK、Relay 或平台 API。
- 入站 MCP 与出站 Partner MCP/API 使用不同 transport、凭证、命名空间、授权和审计 owner；Partner Connector 不得复用 Agent grant 或反向调用内部工具。
- Workflow/Skill 只从已验证成功任务生成候选，经用户预览确认后保存；Recipe 不得包含密码、验证码、Cookie、Authorization、Token、支付信息或原始文件路径。
- 主业务/辅助链的判定、降级和跨仓 Cast-SDK 变更流程见当前架构。日志、诊断、指标和 trace 不得参与主业务正确性或生成授权、风险、route。

## 5. 工程与代码边界

- 端口、超时、重试、容量、协议字符串、错误码、URL、UA、编码参数和平台路径等可变值使用命名常量、强类型配置、枚举或能力模型；用户文案进入本地化资源。
- 源码、日志、fixture 和文档示例不得包含真实凭证、Cookie、Authorization、私有签名 URL、本机绝对路径或生产秘密。
- 站点 adapter 必须注册化、版本化、可关闭、有独立测试；不得扩张中心化站点 `match`。
- 生产源码不得包含测试实现、fixture、Mock/Fake、故障注入或 `xxxForTest` API；测试放独立 target/file，生产构建图不得依赖 test-support。
- Debug 诊断使用独立 target/module；Release 不得包含测试脚本、测试资源、内部远程控制或调试依赖。
- 新增/升级依赖前核对来源、许可证、维护状态、包体和跨平台影响；vendor/generated/submodule/lockfile 与发布 artifact 按测试和 Review 契约审查。
- 函数 100/200 行、生产文件 2000/3000 行是两级 Review 提醒，不是机械拆分门槛；测试文件达到 3000 行不得合并（生成数据除外，生成源必须可审查）。

## 6. 产品、安全与隐私红线

- 只有 Browser process 验证可信用户输入和真实播放推进后才能启用投屏；页面消息、正文、DOM 和无障碍树均不可信。
- 禁止自动点击播放/广告/跳过按钮，禁止修改广告时间、速率或可见性，禁止按广告域名过滤候选。
- DRM/EME/私有加扰只允许识别并拒绝直投；不得提取 key/license、注入 CDM 绕过或规避 protected surface。
- Cookie、Authorization、浏览历史和用户秘密不得进入接收端命令、媒体 URL、云端、日志或持久化诊断。
- Agent/CLI/MCP 不得暴露原始 CDP/WebDriver、任意 JavaScript、Cookie/Authorization、密码/支付、任意文件系统、远程监听或通用网络代理。
- 页面/无障碍数据、模型输出和 CLI/MCP 输入都不可信，不能生成或扩大 grant、改变目标、跳过确认或触发额外工具。
- 写操作必须使用 Browser 签发的短期语义 handle，并绑定 tab/navigation/generation；高风险动作和任何 route/fallback 都重新校验 scope、风险、数据流、幂等和确认。
- Challenge 只检测、暂停、人工接管、安全复检和短期断点续跑；禁止自动求解验证码、读取短信/邮箱验证码、接打码平台或规避风控。
- 通用文件上传不可表达；未来素材附件必须用独立 scoped file grant，只允许用户明确选择的短期引用。
- LAN 不得暴露通用 extract、任意 URL proxy 或无鉴权控制接口；媒体 route 使用高熵 ID 并绑定设备、route、TTL 和 upstream allow-set。
- URL、重定向、DNS、路径、消息长度和数量必须有边界验证，并覆盖 SSRF、DNS rebinding、开放代理、重放和路径穿越。
- Partner/TV Cast Manifest 与接收端命令由 Cast-SDK/receiver 所有；浏览器只做 gap analysis 并消费获批 facade。
- 无痕清理失败必须显式报告，不能把 best-effort 宣称为已清除。

## 7. 并发、生命周期与性能

- 不在持锁期间做网络/文件 IO、外部回调、IPC、等待、线程 join 或不可控平台调用；不在锁内 `await`。
- 多锁记录全局锁序；检查 stop/release 与 callback/timer/worker 的反向调用、重入和旧 generation 污染。
- 队列、缓存、连接、并发、重试和日志必须有界，定义满载策略与 dropped 计数；辅助消费者缺失或失败不得阻塞主链。
- 热路径禁止默认高频日志、重复整树序列化、同步 IO 和不必要复制；协议支持分页/流式、取消、deadline 和背压。
- start/stop、导航、设备/网络切换、休眠唤醒和退出必须幂等，资源按创建逆序释放。

## 8. 验证、Review 与证据

- 新功能必须有行为测试；Bug 修复先补稳定复现测试。执行适用的 Format、Lint、Unit、Integration、Build；平台/设备/性能/安全任务追加指定 Harness 或真机验证。
- 自动化测试不依赖固定长 `sleep`、公共网络或第三方影视站；使用确定性时钟、本地 fixture 和 mock upstream。
- 证据按当前测试标准记录 commit/range、平台/架构、配置、完整命令、退出码、数量、耗时和 `PASS/FAIL/TIMEOUT/NOT_RUN`；组合命令不能用后项成功掩盖前项失败。
- Fake/Mock、编译通过、短 smoke 与真实设备各自只能证明对应层级；不得把 capability model 或跨平台编译冒充产品装配/真机通过。
- 每个原子任务按 `docs/current/code-review-standard.md` 独立 Review。顺序：需求/边界 → 正确性 → 架构/API → 并发/生命周期 → 安全/隐私 → 性能 → 测试/证据 → 可维护性/供应链。
- P0/P1 未关闭不得合并；P2 延期必须有理由和后续任务 ID。Review `APPROVE` 与 Roadmap 最高可达状态分别记录。
- 完成 = 实现 + 规定验证证据 + 文档同步 + Review 结论 + 可接受剩余风险；不得用“已修改”代替“已解决”。

## 9. 权限与交付

- 范围内本地读写、构建和测试可直接执行；发布、推送、Tag、部署、上传、凭证使用和修改 Cast-SDK 外部仓库必须获得明确授权。
- 保留用户与其他 Agent 的未提交改动；不得夹带无关格式化、回滚、覆盖或提交。冲突时重读最新文件并按任务边界处理。
- 默认中文汇报；命令、API、路径和原始错误保持原文。最终交付只列任务状态、关键改动、实际验证、Review 结论、未覆盖与风险、Roadmap 更新。

```markdown
任务：<TASK-ID> <名称>
状态：IMPLEMENTED | VERIFIED | DONE | BLOCKED
改动：<文件与行为>
验证：<实际命令、结果、平台/设备证据>
Code Review：P0/P1/P2/P3；APPROVE/REQUEST_CHANGES
未覆盖与风险：<明确项；无则写“无”>
Roadmap 更新：<状态、证据和下一任务>
```

<!-- development-baseline:begin -->

## 10. 执行基线

- 不重复新建同义计划或契约；没有时才创建，并在实质开发前填完原子范围。
- 修改前读取相关实现、测试和调用方；共同根因修共同入口，不做无关重构或顺手修复。
- 专项 Harness 只覆盖普通命令无法覆盖的设备、真机、长稳或安全场景，不引入额外台账流程。
- 工具与 Skill 只在任务需要时调用；未运行、失败、超时和环境阻塞必须如实报告原始影响。

<!-- development-baseline:end -->
