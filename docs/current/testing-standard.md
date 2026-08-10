# 蜡笔隐私投屏浏览器测试标准

## 1. 测试目标

测试必须证明产品行为、隐私、安全、协议和跨平台能力，而不只是证明函数被调用。自动化不依赖第三方影视站、真实账号或公共网络；真实站点仅用于法务批准后的兼容 smoke，不能成为 CI 成功条件。

## 2. 测试层级

| 层级 | 目录 | 目标 | 默认时限 |
|---|---|---|---:|
| L0 Repo Guard | `tools/repo-guard` | 目录依赖、文件规模、硬编码、测试隔离、许可 | 30 秒 |
| L1 Unit | 各模块独立测试文件 | 纯模型、parser、策略、状态机、错误映射 | 2 分钟/模块 |
| L2 Contract | `tests/contracts` | IPC、C ABI、Cast-SDK facade、能力与 golden vector | 5 分钟 |
| L3 Integration | `tests/integration` | mock upstream、relay、fake receiver、Profile 存储 | 10 分钟 |
| L4 Desktop E2E | `tests/e2e/desktop` | CEF 导航、播放、设备、投屏、停止 | 20 分钟/平台 |
| L5 Device/Platform | `tests/e2e/device` | 真机采集、音频、权限、接收端和网络拓扑 | 按矩阵 |
| L6 Stress/Longrun | `tests/stress` | 资源上限、切换、8h 长稳、弱网 | 夜间/发布 |
| L7 Release | `tests/release` | 签名、公证、安装、升级、卸载、SBOM、产物隔离 | 候选版本 |

## 3. 测试代码边界

- 生产与测试物理隔离，具体规则以根 `AGENTS.md` 为准。
- Rust 私有测试实现放相邻 `*_tests.rs`，生产文件仅保留 `#[cfg(test)] mod ...;`。
- 浏览器脚本测试 fixture 不得嵌回 C++/Rust 字符串；生产脚本作为独立受版本控制资源，测试从测试 bundle 加载 fixture。
- `test-support` 只允许 dev/test target 依赖，生产依赖图必须为零。
- 测试 fixture 必须记录来源、许可、hash 和生成方式；禁止真实 Cookie、Authorization 和签名媒体 URL。

## 4. 确定性设施

必须提供：

- `ManualClock`：控制 TTL、重试、导航失效和会话超时。
- `MockUpstream`：MP4/HLS/DASH、Range、重定向、慢响应、断流、DNS 目标分类。
- `FakeReceiver`：Cast-SDK facade 行为、能力、状态、stale generation、route lost。
- `BrowserFixtureServer`：video/audio、iframe、MSE、Worker、广告编排、DRM signal 和用户手势页面。
- `PlatformFake`：capture/codec/store/network/lifecycle/update capability。
- `LeakScanner`：日志、DTO、诊断包、磁盘目录中的 URL/token/Cookie/Authorization 扫描。

禁止用固定长 `sleep` 等待异步结果；使用事件、虚拟时钟、有限 deadline 和明确失败原因。

## 5. 每个任务最低覆盖

- 正常路径。
- 无效/空输入与最大边界。
- 超时、取消、重复调用和幂等。
- 旧 session/generation 结果。
- 部分初始化失败后的逆序清理。
- 适用时：网络切换、权限撤销、休眠恢复、设备断开。
- 安全相关：未授权、越权、重放、输入长度、敏感信息泄漏。

## 6. 跨平台矩阵

| 平台 | 基础 CI | E2E | 真机/硬件门禁 |
|---|---|---|---|
| Windows 10/11 x64 | 每次 PR | 每日 | Intel/AMD/NVIDIA、WGC/WASAPI、签名安装 |
| macOS 当前/前一版，Apple Silicon/Intel | 每次 PR | 每日 | ScreenCaptureKit、系统音频、权限、公证 |
| Linux 支持发行版 | 每次 PR | 每日 | Wayland/PipeWire、VA-API、Portal、包格式 |
| HarmonyOS | schema/unit 每次 PR | 模拟器每日 | ArkWeb、AVScreenCapture、AVCodec、HUKS、本地网络真机 |

## 7. 性能与资源口径

- 标签页投屏首帧：用户确认投屏到接收端首个可见视频帧。
- 端到端延迟：浏览器测试页显示毫秒时间码并播放可识别声音，接收端画面/声音实测；内部阶段点不能代替。
- relay 首字节：接收端发起请求到收到首字节，不包含上游预检。
- 内存：稳定播放 30 分钟/8 小时后的工作集与 session 结束后回落；不得随媒体总时长线性增长。
- 热路径日志关闭时接近零成本；测试 dropped、采样和上限。

## 8. 命令分层目标

以下入口由基础工程 Roadmap 创建后成为标准门禁：

```text
scripts/check.ps1 fast       # repo guard + format + unit + contract
scripts/check.ps1 core       # 全 Rust core/integration
scripts/check.ps1 desktop    # 当前平台 CEF build + E2E smoke
scripts/check.ps1 security   # relay/IPC/secret/SSRF
scripts/check.ps1 release    # 当前平台发布产物
scripts/check.ps1 all        # CI 全量，不用于每个小任务
```

在入口尚未创建前，Roadmap 必须列出底层真实命令。Agent 不得引用不存在的脚本作为已完成证据。

## 9. 证据分级

- `S0 Static`：文档/静态检查。
- `S1 Unit`：目标模块单测通过。
- `S2 Integration`：本地集成/契约通过。
- `S3 Platform`：目标 OS 的正式构建和 E2E 通过。
- `S4 Device`：真实接收端/硬件/网络矩阵通过。
- `S5 Release`：签名发布产物与升级链路通过。

任务必须声明需要的最低证据级别。只有达到该级别且 Code Review 通过才可以 `DONE`。

## 10. 基线失败管理

- 首先证明失败是否在未修改基线可复现。
- 既有失败单独记录，不得写成“本次通过”，也不得顺手扩大修改。
- 本次新增失败必须修复或回退。
- 因权限/设备/网络未执行的项目标记 `NOT_RUN`，不能改成 `PASS`。
