# 蜡笔隐私投屏浏览器 Code Review 标准

## 1. 目标

Review 必须确认改动满足任务和架构、不会破坏现有行为，并在跨平台、并发、生命周期、媒体热路径、安全、隐私、许可和可验证性方面可控。Formatter/Lint 处理机械问题，人工 Review 重点判断正确性和风险。

## 2. 必查维度

### 2.1 需求与原子任务

- 是否只实现一个 Roadmap 任务，依赖和不做项是否遵守。
- 正常、失败、取消、超时、重复、旧 generation、恢复和清理是否完整。
- 是否修共同根因，还是在多个入口复制补丁。
- 是否夹带无关重构、全仓格式化、依赖升级或生成文件变化。

### 2.2 架构与依赖

- 文件是否位于 `architecture.md` 规定模块，依赖方向是否正确。
- CEF/ArkWeb/OS API 是否泄漏到 shared Core；产品策略是否出现 OS/设备型号分支。
- 是否绕过 `crayon-cast-adapter` 直接调用 Cast-SDK 内部 crate、SOAP 或 receiver URL。
- 状态、规则、错误码和配置是否有唯一事实来源。
- 新抽象是否对应稳定领域概念；禁止无意义 wrapper 和杂物模块。

### 2.3 正确性、状态与生命周期

- 状态迁移、边界值、代际、缓存 TTL 和异步顺序是否正确。
- start/stop、导航、标签关闭、设备替换、route lost、Profile 销毁和 App exit 是否幂等。
- 部分初始化失败是否逆序清理 socket、task、listener、capture、encoder、temp file、token 和 secret。
- 错误是否保留 operation/stage/code，是否存在假成功、吞错或错误被后续状态覆盖。

### 2.4 并发与死锁

- 明确共享状态所有者、锁/线程/队列和全局锁序。
- 不在锁内 await、IO、IPC、外部 callback、平台调用、join 或阻塞等待。
- 检查 callback -> stop 与 stop -> callback、A->B/B->A、旧 session 回调和释放后访问。
- 队列、缓存、连接、timer、重试有界；取消路径和唤醒条件明确。

### 2.5 媒体性能

- 采集、逐帧、逐分片、socket 和渲染热路径无不必要分配、复制、JSON、字符串和锁竞争。
- 无默认逐帧/逐 segment 日志；昂贵诊断在 enable 判断后构建。
- 慢上游/接收端、满队列和编码器背压行为明确，主业务不被辅助诊断反压。
- 性能结论给出设备、素材、时长、统计口径和前后数据。

### 2.6 硬编码与模型

- 不散落端口、超时、重试、容量、UA、协议字符串、codec 和错误码。
- 用户文案进入本地化资源；平台/设备差异通过 capability。
- 无凭证、Cookie、Authorization、签名 URL、私有地址和本机路径。
- 多布尔/字符串状态是否应使用能表达约束的类型。

### 2.7 浏览器、广告与 DRM

- 页面事件是否只作为不可信线索，Browser process 是否验证可信输入和播放推进。
- 是否存在自动点击、自动播放、广告过滤/快进/跳过、`currentTime`/速率/可见性修改。
- EME/DRM/protected surface 是否只识别和拒绝，不请求 key/license 或绕过。
- 网页本机播放能力与投屏能力是否分开表达。

### 2.8 Relay、网络与秘密

- LAN 是否只暴露 tokenized session/resource 路由，无任意 URL proxy/control API。
- token 熵、receiver/route/TTL/upstream allow-set、撤销触发器是否完整。
- URL、DNS、每跳 redirect、header scope、方法、长度、并发和超时是否校验。
- 检查 SSRF、DNS rebinding、开放代理、路径穿越、token 猜测、重放和 DoS。
- Cookie/Authorization/完整 URL/query/token 是否可能进入 DTO、日志、磁盘、receiver 或云端。

### 2.9 Profile 与隐私

- Profile/空间隔离、关闭清理、失败提示和启动补偿是否正确。
- 路径删除是否验证根目录、类型和 symlink/junction。
- 安全存储是否使用平台 adapter，密钥是否有删除/轮换路径。
- 遥测是否默认关闭、字段最小、用户可预览。

### 2.10 API、协议与跨平台

- 公共 API 是否最小、强类型、线程/所有权/错误语义明确。
- IPC/C ABI/Cast-SDK 升级是否保持当前与前一版本兼容。
- C ABI 是否使用不透明 handle、显式长度和所有权，不跨边界传容器/异常。
- 不支持能力是否通过 capability + stable error 显式表达，不能静默成功。

### 2.11 测试与交付

- 新行为和 bug 回归是否有测试；测试是否验证公共行为而非内部实现。
- 生产/测试物理隔离，Release 不含测试资产和 debug 入口。
- 是否实际运行任务要求的 Format/Lint/Test/Build/Harness/Device 命令。
- 测试是否避免固定 sleep、公共网络、真实账号和残留状态。
- 文档、schema、fixture、SBOM/NOTICE 是否按影响同步。

### 2.12 规模提醒

- 函数 100/200 行、生产文件 2000/3000 行、测试文件 2000/3000 行按 `AGENTS.md` 处理。
- 规模本身不是 P0/P1；Reviewer 必须指出具体职责混合、所有权或测试风险。
- 生成/vendor 代码检查生成源、revision、许可和接入风险。

## 3. 问题等级

| 等级 | 含义 | 合并要求 |
|---|---|---|
| P0 | 严重安全/隐私事故、数据破坏、核心不可用、稳定死锁/崩溃、DRM/广告红线 | 必须修复并重新验证 |
| P1 | 明确功能错误、架构破坏、协议不兼容、竞态/泄漏、显著性能或许可问题 | 原则上本次修复 |
| P2 | 可维护性下降、重要测试缺失、重复规则或可预见风险 | 修复或记录后续任务 ID |
| P3 | 非关键表达、命名和局部优化 | 不阻塞 |

每个 P0/P1/P2 必须包含文件/行、触发场景、实际影响、证据和修复方向。`Question` 表示证据不足；`Nit` 不得阻塞。

## 4. Review 顺序

1. 确认任务、范围、依赖和权威契约。
2. 阅读相关架构、调用方、实现和测试，不能只看 Diff。
3. 检查正确性、状态、架构、公共 API、并发和安全。
4. 检查媒体热路径、隐私、跨平台、文件规模和交付。
5. 运行风险匹配的验证。
6. 按严重度输出发现，再列验证和未覆盖项。
7. 修复 P0/P1 后重新 Review；不能沿用修复前结论。

## 5. 合并条件

- 任务验收全部满足，达到要求的证据级别。
- 无未处理 P0/P1；P2 延期有理由和后续 ID。
- 架构、Cast-SDK 边界、协议兼容和平台 capability 正确。
- 无已知死锁、稳定竞态、资源/秘密泄漏或无界增长。
- Test/Build/Lint/Harness 与风险匹配，未覆盖项明确。
- Roadmap 状态、验证证据和必要 current 文档已经更新。

## 6. 结论模板

```markdown
# <TASK-ID> Code Review

## 结论
- 是否可合并：
- P0/P1/P2/P3：
- 核心判断：

## 发现
### [P1] <标题>
- 位置：`path:line`
- 场景：
- 影响：
- 证据：
- 修复方向：

## 专项检查
- 架构/Cast-SDK 边界：
- 并发/生命周期：
- Relay/安全/隐私：
- 性能/日志：
- 跨平台/API/兼容：
- 文件规模：

## 验证
- `<实际命令>`：PASS/FAIL/TIMEOUT/NOT_RUN

## 未覆盖与风险
- <内容>
```
