# 蜡笔隐私投屏浏览器 Code Review 标准

- 版本：v0.5
- 日期：2026-08-11

每个原子任务实现并验证后必须独立 Review。顺序固定为：需求/边界、正确性、架构/API、并发/生命周期、安全/隐私、性能、测试、可维护性。

## 1. 需求与边界

- 任务 ID、依赖、允许/禁止路径、验收、非目标和实际 diff 一致。
- Roadmap 目标没有被表述成已实现事实；历史完成证据没有被改写。
- Windows/macOS 是当前桌面范围；HarmonyOS 只按电脑 PC 形态；Linux 无当前实现/发布承诺。
- 浏览器/局域网投屏先于 Markdown；AI/模型/Agent/CLI/MCP 没有当前代码入口。

## 2. 架构与 API

- 依赖方向为 UI/应用编排→领域接口→Core/Cast-SDK facade→平台 adapter。
- CEF、ArkWeb 和系统 API 不泄漏到共享领域层。
- 只有 `crayon-cast-adapter` 调 Cast-SDK；App 不拼 SOAP、DLNA metadata、CastExtension 或 receiver URL。
- 公共 schema/状态机/持久化/安全边界变化先有独立 Roadmap、previous/current golden 和迁移方案。
- 状态唯一所有，不直接修改其他模块的集合、锁、缓存或 generation。

## 3. 浏览器与投屏

- 页面消息不能替代 Browser process 的可信输入与播放推进验证。
- observer 只提供事实，policy 才产生 `Direct/Relay/ExternalClientHandoff/Reject`。
- Direct/Relay 仅限 LAN；Relay route opaque、会话/设备/TTL/allow-set 绑定且不是通用代理。
- `ExternalClientHandoff` 需要用户确认，不创建 receiver handle、Relay token、WebRTC transport、采集器或编码器，不显示“投屏已开始”。
- 新代码不得引用历史 `Mirror`、`tab_video`、`system_audio`、硬编码能力作为浏览器镜像授权；兼容读取只在 `MED-19` 明确范围内。
- DRM/EME/受保护表面只允许识别和拒绝，不提取 key/license 或绕过保护。

## 4. 页面内容与 Markdown

- 只处理用户触发的当前 tab/navigation/generation 快照。
- 节点、深度、文本、URL、时间、输出和文件写入都有界且可取消。
- 隐藏表单值、跨源正文、Cookie、Authorization、页面存储和危险 scheme 不进入结果。
- 导航/关闭/取消后的旧结果不能覆盖新页面。
- 当前实现不含模型、Prompt、provider endpoint/key、Agent tool、CLI 或 MCP transport。

## 5. 并发与生命周期

- 持锁期间无网络/文件 IO、await、外部 callback、IPC、join 或不可控平台调用。
- 多锁有固定锁序；callback/timer/worker 的 stop/release 不反向死锁。
- 队列、缓存、连接、并发、重试和日志均有界，满载与 dropped 计数明确。
- start/stop、导航、网络/设备切换、睡眠唤醒和退出幂等，旧 generation 事件无副作用。
- 资源按 owner 逆序释放；清理失败明确报告。

## 6. 安全与隐私

- Cookie、Authorization、浏览历史、完整签名 URL、token 和 key 不出可信内存边界。
- URL、redirect、DNS、路径、IPC 消息长度/数量覆盖 SSRF、rebind、开放代理、重放和穿越。
- 删除前验证显式根、规范路径、符号链接/目录联接和目标数量；失败停止，不扩大范围。
- Debug/日志/错误不泄密；诊断非阻塞且不参与正确性。
- 无痕清理 best-effort 不能被写成已清除。

## 7. 性能与可维护性

- 导航、Relay 分片、socket 和渲染热路径无默认高频日志、不必要复制/JSON/格式化和锁竞争。
- 慢上游/接收端、满队列和背压行为明确；辅助诊断不反压主业务。
- magic 端口、超时、重试、容量、协议、错误码、URL、UA 和路径进入命名配置/常量/枚举。
- 生产代码无 fixture、Mock/Fake、故障注入和测试依赖。
- 函数/文件规模遵守根 `AGENTS.md` 提醒，不为行数制造无边界 wrapper。

## 8. 测试与证据

- 新行为有正常、失败、空/边界、重复、取消、超时、旧结果、恢复和释放测试。
- Bug 修复先有失败复现；自动化使用本地 fixture、确定性时钟和 mock upstream。
- 实际执行 Format、Lint、Unit、Integration、Build 和适用 Harness；记录命令、数量、耗时、平台及未覆盖项。
- P0/P1 必须关闭；P2 延期记录理由和后续任务 ID。

## 9. Review 输出模板

```markdown
任务：<TASK-ID> <名称>

范围：<文件、行为、明确不做>

验证：
- `<实际命令>`：PASS/FAIL/TIMEOUT/NOT_RUN

发现：
- P0：<数量与结论>
- P1：<数量与结论>
- P2：<数量与结论/延期 ID>

未覆盖与风险：
- <没有则写“无”>

结论：APPROVE | CHANGES_REQUIRED | BLOCKED
```
