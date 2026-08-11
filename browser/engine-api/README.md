# Browser Engine API

`browser/engine-api` 是桌面产品编排与具体网页引擎之间的最小 C++17 契约。生产头文件不包含平台、引擎、投屏、站点或测试类型；本模块自身也不启动线程、计时器、文件/网络 IO。

## 所有权与调用语义

- 调用方拥有 `BrowserEngineAdapter` 和 `EngineEventSink`，并保证 sink 活到 `Stop()` 完成。
- `Start()` 绑定唯一 sink；相同 sink 的重复调用幂等，运行中替换 sink 必须稳定拒绝。
- adapter 实例是单次生命周期；`Stop()` 后再次 `Start()` 必须稳定拒绝，重启使用新实例，避免旧 generation 状态复活。
- 命令返回值只表示“已接受”或稳定拒绝，不表示导航、创建或权限操作已经完成。
- 实现不得在命令调用栈内同步回调。异步结果由引擎事件线程送到 sink；具体线程由后端文档声明。
- `Stop()`、重复关闭/销毁/退订必须幂等。`Stop()` 返回后、adapter 析构后、退订后均不得再回调对应事件。
- 取消、deadline、崩溃恢复和具体队列上限由后续 operation owner/后端任务定义；本接口不伪造完成或超时。

## 强类型和安全边界

- Profile/Tab/PermissionRequest/Subscription ID 只能由 `TryCreate` 构造，空值、超长值、空白和路径字符 fail closed。
- 仅接受 ASCII DNS/IPv4 authority 的 `http`/`https` URL，端口限制为 `1..65535`；拒绝 userinfo、畸形 host/port、控制字符和超长 URL。IPv6/IDN 必须在引入共享 URL parser 的独立契约中扩展，不能由后端静默放宽；内部空白页由后端实现拥有，不扩张公共 URL scheme。
- zoom 只允许 `0.25..5.0` 的有限值。所有枚举在后端入口再次通过 `IsValid` 校验，未知值稳定拒绝。
- 事件只含 opaque ID、当前 URL、稳定状态和最小事实，不得携带 Cookie、Authorization、响应正文、页面存储、引擎 handle、接收端命令或投屏策略。
- Profile ID 是 opaque identity，不是目录名；平台后端必须用独立受控映射生成存储路径。

## CEF 与 HarmonyOS 电脑映射

| 契约 | Windows/macOS CEF | HarmonyOS PC ArkWeb | 能力处理 |
|---|---|---|---|
| adapter 生命周期 | Browser process owner + CEF UI thread | ArkUI 页面 owner + UI task | 均可实现；线程细节留在后端 |
| Profile | 独立 request context | 独立 ArkWeb controller/data partition 能力 | 无法隔离时报告 `unsupported`，不得假装隔离 |
| Tab/导航/历史/zoom | browser host/frame API | ArkWeb controller API | 均可实现；结果只经事件 sink |
| 权限请求/决定 | Browser process permission callback | ArkWeb permission callback/native bridge | 平台不支持的权限稳定拒绝 |
| 可信输入事实 | Browser process 原生输入回调 | ArkUI/native input bridge | 页面消息不能产生 trusted fact |
| observation 订阅 | Renderer/Browser observation gateway | ArkWeb/native bridge | topic 不可表达时 capability 降级 |

本表只是接口可实现性说明，不是 HarmonyOS 真机证据。ArkWeb 的具体 API、PC 窗口行为和数据隔离必须在 HM Roadmap 中验证。

## 明确不做

- 不创建可运行浏览器、CEF/ArkWeb adapter、窗口、Profile 存储或产品 UI。
- 不提供 DOM、HTML、CDP、selector、任意 JavaScript、截图、Cookie、Authorization 或通用网络能力。
- 不包含媒体策略、Relay、Cast-SDK、Agent grant、Workflow 或模型逻辑。
- observation 只冻结订阅和最小事件外形；页面快照、媒体 payload 与语义动作由后续独立 contract 扩展。

## 独立验证

```powershell
cmake -S browser/engine-api -B .cache/build/engine-api -G Ninja -DCRAYON_ENGINE_API_BUILD_TESTS=ON
cmake --build .cache/build/engine-api
ctest --test-dir .cache/build/engine-api --output-on-failure
```
