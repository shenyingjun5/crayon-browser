# 蜡笔 AI 投屏浏览器当前目标架构

## 1. 架构结论

产品以共享 UI、浏览器引擎适配和应用编排为入口，下设三个互相隔离的领域：媒体/投屏、网页内容/模型、Agent 安全访问；Cast-SDK facade 与平台能力适配仍是唯一外部设备/系统边界。桌面端共享 CEF，HarmonyOS 使用 ArkWeb；设备协议和播控复用 Cast-SDK，不在本仓库重复实现。

```mermaid
flowchart TB
    UI["shared-ui：浏览、内容、AI 与投屏交互"] --> ENGINE["browser-engine-api"]
    ENGINE --> CEF["cef-shell：Win/macOS/Linux"]
    ENGINE --> ARK["harmony-shell：ArkWeb"]
    UI --> APP["app-runtime：状态机与用例编排"]
    CEF --> APP
    ARK --> APP
    APP --> MEDIA["crayon-media：观察、候选、策略、relay"]
    CEF --> SNAP["content-gateway：有界语义快照"]
    ARK --> SNAP
    SNAP --> CONTENT["crayon-content：提取、Markdown、卡片"]
    APP --> CONTENT
    CONTENT --> MODEL["model-adapter：用户确认后的 provider"]
    AGENT["CLI/MCP：loopback、默认关闭"] --> GATE["agent-gateway：registry、grant、receipt"]
    GATE --> APP
    APP --> CAST["crayon-cast-adapter"]
    CAST --> SDK["Cast-SDK 固定源码 / 公开 facade"]
    APP --> PLATFORM["platform-api"]
    PLATFORM --> WIN["platform/windows"]
    PLATFORM --> MAC["platform/macos"]
    PLATFORM --> LNX["platform/linux"]
    PLATFORM --> HM["platform/harmony"]
    MEDIA --> RECEIVER["蜡笔接收端媒体地址/WebRTC"]
    SDK --> RECEIVER
```

## 2. 目标仓库目录

```text
get-video/
├── AGENTS.md
├── Cargo.toml                         # Rust workspace；不放产品依赖细节
├── cmake/                             # CEF/C++ 共用构建 helper
├── config/
│   ├── product-defaults.toml          # 非秘密产品默认值
│   ├── feature-schema.json            # capability/feature schema
│   └── policy-schema.json             # 策略签名数据 schema
├── browser/
│   ├── shared-ui/                     # TypeScript UI、状态机视图、本地化资源
│   ├── engine-api/                    # C++ BrowserEngineAdapter 稳定接口
│   ├── cef-shell/                     # Desktop CEF browser/render process
│   │   ├── include/
│   │   ├── src/browser/
│   │   ├── src/renderer/
│   │   ├── src/content/                # 语义快照采集与 Browser 校验；不含模型实现
│   │   ├── src/ipc/
│   │   └── tests/
│   └── harmony-shell/                 # ArkUI/ArkWeb；Harmony Roadmap 启动后创建
├── crates/
│   ├── crayon-domain/                 # 共享 ID、错误、能力、状态，不依赖平台/网络
│   ├── crayon-media-observer/         # SourceObservation、候选关联
│   ├── crayon-cast-policy/            # 唯一 Mirror/Direct/Reject 决策器
│   ├── crayon-media-probe/             # MP4/HLS/DASH/DRM/codec 有界预检
│   ├── crayon-relay/                   # session relay、HLS/DASH、SSRF
│   ├── crayon-content/                 # 确定性提取、Markdown、结构化数据、阅读卡片
│   ├── crayon-agent-gateway/           # tool registry、grant、任务代际、receipt
│   ├── crayon-profile/                 # Profile 生命周期与清理编排
│   ├── crayon-cast-adapter/            # Cast-SDK facade 的唯一产品适配层
│   ├── crayon-app-runtime/             # 产品用例、状态机、Core API
│   ├── crayon-ipc-schema/              # 版本化 Browser/Core schema
│   └── crayon-legacy-adapter/           # 迁移期兼容；完成后删除
├── platform/
│   ├── api/                            # capture/codec/store/network/lifecycle/update 接口
│   ├── windows/
│   ├── macos/
│   ├── linux/
│   └── harmony/
├── third_party/
│   └── cast-sdk/                       # 固定 source revision 的独立 submodule 边界
├── integrations/
│   └── ai-providers/                   # 经批准的模型 adapter；凭证来自 secure store
├── apps/
│   ├── desktop/                        # CEF 正式装配根；含默认关闭的 agent CLI/MCP
│   ├── harmony/                        # Harmony 正式装配根
│   └── legacy-tauri/                   # 当前 app 迁入；仅回归/迁移，不发布
├── test-support/                       # 仅测试依赖：clock、mock upstream、fake receiver
├── tests/
│   ├── contracts/                      # IPC、策略、Cast-SDK facade golden tests
│   ├── integration/                    # 本地 upstream/relay/receiver/Fake model
│   ├── e2e/                            # 浏览器到接收端闭环
│   ├── fixtures/                       # 许可清晰、无秘密的媒体/manifest
│   └── security/                       # SSRF、token、重放、泄漏
├── tools/
│   ├── repo-guard/                     # 模块、文件、硬编码、测试隔离门禁
│   └── receiver-simulator/
├── scripts/                            # 跨平台入口脚本；核心逻辑放 tools
└── docs/
    ├── current/
    ├── plans/
    └── archive/
```

目录按迁移 Roadmap 分阶段创建，禁止一次性创建空目录或占位模块。根 `src/`、`app/`、`demo/` 在兼容迁移完成前保留；正式 CEF 闭环和回归门禁完成后才移动到 `apps/legacy-tauri/` 或删除。

## 3. 模块职责

| 模块 | 唯一职责 | 可以依赖 | 禁止承担 |
|---|---|---|---|
| `crayon-domain` | 强类型 ID、错误、能力和状态 | serde 等基础库 | 网络、平台、UI、Cast-SDK |
| `media-observer` | 可信输入关联、Observation -> Candidate | domain | 投屏方式决策、设备控制 |
| `cast-policy` | Mirror/Direct/Reject 纯决策 | domain、probe DTO | 网络请求、平台 API、UI |
| `media-probe` | 有界格式/DRM/可访问性证据 | domain、HTTP adapter | 用户体验和设备控制 |
| `relay` | 授权 session、资源注册、媒体流 | domain、probe | 任意 URL API、设备发现 |
| `content` | `PageSnapshot` -> 正文/Markdown/结构化数据/阅读卡片的确定性转换 | domain | CEF/ArkWeb、文件选择、模型网络、Agent 权限 |
| `model-adapter` | 用户确认后把最小内容请求映射到批准 provider | content DTO、secure-store 接口 | 浏览器 DOM、Profile 路径、投屏/安全决策 |
| `agent-gateway` | 工具 registry、capability grant、确认、任务代际、receipt | domain、app-runtime 用例接口 | 直接调用 CEF/Cast-SDK/relay/平台 API、任意脚本 |
| `profile` | 无痕/常用空间生命周期与清理结果 | domain、平台接口 | CEF/ArkWeb 具体对象 |
| `cast-adapter` | 浏览器语义到 Cast-SDK facade 映射 | domain、Cast-SDK 公开 facade | SOAP/DLNA 协议副本、网页逻辑 |
| `app-runtime` | 用例编排和唯一产品状态机 | 上述领域接口 | CEF/OS 具体调用、协议实现 |
| `ipc-schema` | 版本、消息和兼容协商 | domain | 业务实现 |
| `platform/*` | 权限、采集、编码、安全存储、更新 | platform-api | 产品策略和站点规则 |

## 4. Cast-SDK 集成边界

固定使用 Cast-SDK 已公开的稳定 facade：

- 发现：`start_discovery`、`stop_discovery`、`refresh_discovery`、`list_devices`。
- 连接：`connect_device`、`disconnect_device`、`resolve_device_by_cast_code`。
- 能力：`list_devices_with_capabilities`、`assess_cast`、receiver app capability。
- URL 投送：通过公开 URL/HLS facade 或经 SDK 批准的统一 remote media API。
- 控制：session handle 绑定的 play/pause/seek/volume/stop。
- 会话监督：监听 current session、route lost、receiver stop/end 和 stale generation。

浏览器拥有：

- CEF/ArkWeb 页面、Cookie、Profile、用户输入和媒体观察。
- `SourceObservation`、`MediaCandidate`、广告连续性/DRM 策略。
- 网页授权 relay 的 secret vault 和生命周期。
- 标签页采集与 WebRTC 镜像。

网页内容/Agent 边界：

- Renderer 只产生有界 `PageSnapshot` 线索；Browser process 校验顶层标签、Profile、origin、navigation/generation、字段和大小。
- `crayon-content` 不知道 Cookie、Authorization、CEF、ArkWeb、Cast-SDK 或 provider secret；模型是可选消费者，不是正文或安全规则所有者。
- `agent-gateway` 只能调用 `app-runtime` 已存在的产品用例。CLI/MCP 不得直接创建第二套导航、投屏、relay 或 Profile 生命周期。
- 页面文本、无障碍树、模型输出和外部 client 输入都标记为不可信，不能生成 capability grant 或取消用户确认。
- MCP transport 默认关闭、只绑定 loopback、使用短期高熵 secret；loopback、allow-list 和 redaction 只是组合门禁的一部分，不单独构成安全边界。

源码基线：

| 项 | 固定值 |
|---|---|
| Repository | `https://github.com/shenyingjun5/Cast-SDK.git` |
| Revision | `44c3a99871aa1e68cbda71eacefbb41d23a747a8` |
| Submodule | `third_party/cast-sdk` |
| Machine-readable lock | `config/cast-sdk-source.toml` |

- Windows、macOS 桌面端由 `crayon-cast-adapter` 直接编译并调用 `cast-sender-service::SenderCommandService`。
- HarmonyOS 从同一 submodule revision 构建 `sender/harmonyos/sdk-arkts` 和 `native-bridge`，通过 ArkTS `CastSenderClient` 映射同一产品 facade。
- 本轮不处理 Linux 发送端 SDK；Linux 不进入 SDK-01～SDK-14 的依赖或完成门禁。

集成规则：

- `.gitmodules`、gitlink 和 `config/cast-sdk-source.toml` 必须指向同一远端和精确 commit；禁止 branch、tag 漂移或开发者本机绝对路径。
- submodule 作为独立源码边界，不参与本仓库生产源码、文件规模和依赖扫描；真正依赖只允许从 `crayon-cast-adapter` 建立。
- SDK 升级使用独立原子任务完成 gitlink、source lock、API diff、contract test、构建和回滚记录。
- UI、CEF、ArkWeb 和 media 模块不得直接依赖 Cast-SDK。
- SDK 缺少能力时返回稳定 unsupported 或建立 Cast-SDK Roadmap；不得在浏览器仓库拼协议补洞。

## 5. 核心状态机

```text
Idle
 -> Browsing
 -> PlaybackEligible
 -> SelectingReceiver
 -> Planning
 -> StartingMirror | StartingDirect
 -> Casting
 -> Stopping
 -> Browsing

任意活动态 -> Failed -> Browsing/Idle
导航、标签关闭、route lost、receiver stop、Profile 销毁 -> Stopping
```

状态写入只由 `crayon-app-runtime` 完成。CEF/ArkWeb、relay、Cast-SDK 和平台 adapter 只能产生带 session/generation 的事实事件；旧 generation 事件必须丢弃。

内容任务由 `crayon-content` use-case owner 管理 `Idle -> Snapshotting -> Extracting -> Previewing -> OptionalModel -> Completed/Cancelled/Failed`，但不得直接改变 Cast 状态。Agent 任务由 `crayon-agent-gateway` 管理 `Requested -> AwaitingGrant -> Running -> Completed/Cancelled/Failed`；它只能通过 `app-runtime` 事件请求状态迁移。导航、Profile 销毁、grant 撤销和 App 退出会同时使相关旧 generation 失效。

## 6. 配置和能力

- `ProductConfig`：端口范围、超时、容量、更新渠道等非秘密默认值，可由签名配置覆盖。
- `PlatformCapabilities`：browser engine、tab video/audio、hardware codec、secure store、local discovery、protected surface。
- `ReceiverCapabilities`：只来自 Cast-SDK，不由 UI 或站点规则猜测。
- `CastPolicyInput`：用户播放证明、候选证据、平台能力、接收端能力、广告连续性和播放门禁状态。
- `ContentCapabilities`：semantic snapshot、local export、secure model store、reader card 与 receiver document capability；未知能力显式降级。
- `AgentCapabilityGrant`：Profile/tab/tool/risk/target/generation/expiry 的强类型授权，默认不可持久化。
- 秘密、Cookie 和 Authorization 不进入上述可序列化诊断模型。

## 7. 迁移不变量

1. 先用特征测试记录现状，再做纯移动，再做行为修复；三者不混在同一提交。
2. 当前 53 项 core 测试不得减少；迁移内联测试只能改变位置，不能删断言。
3. 正式 App 不得依赖 legacy 任意 URL proxy、隐藏 WebView 自动交互和 beacon 端口。
4. 任何时刻至少保留一条可运行的端到端开发链路；正式 CEF 达到门禁前不删除 Tauri 迁移源。
5. 公共错误、协议和状态变更必须版本化并有前后兼容测试。
