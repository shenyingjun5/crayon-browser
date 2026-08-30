# 第一期生产装配审计

- 契约：`release-v1-assembly`
- 日期：2026-08-30
- 任务：`REL-02`
- 结论：本地 Markdown 已进入真实 CEF 产品调用链，但仍含生产 fixture 初始化缺口；网页 Markdown 的真实 CEF snapshot 段已由 `CNT-17` 接通，Rust 生产编排和导出 UI 仍不可达；LAN 投屏尚未进入 `CrayonBrowser.app`。

本文只记录生产 reachability。类、函数、静态库、测试或历史 Roadmap 状态存在，不等同于用户从真实 CEF 入口可以使用。

## 1. macOS CEF 装配根

REL-02 审计后，`CNT-17` 已把 snapshot CEF adapter、collector 与 gateway 加入 macOS/Windows 产品 target；其余断点保持不变。下列独立目标仍未被 `crayon_browser` 链接：

- `crayon::cef-shell-media-observer`、`crayon::cef-shell-network-observer`、`crayon::cef-shell-observation-gateway`、`crayon::cef-shell-input-proof`；
- `crayon::browser-page-tools`、`crayon::browser-cast-view`；
- Rust `crayon-app-runtime`、`crayon-cast-adapter`、`crayon-relay`、`crayon-content-*` 与平台 adapter。

因此 CEF 产品当前没有 C++/Rust 装配桥，也没有 Agent、Workflow、Hub、Partner、model provider、HarmonyOS 或远程监听进入 App bundle 的生产路径。

## 2. 网页生成 Markdown

当前调用图：

```text
真实 CEF 当前页
  ---> Browser-issued snapshot request / Renderer DOM adapter       [CNT-17 DONE]
  ---> PageSnapshotCollector -> CEF IPC -> PageSnapshotGateway      [CNT-17 DONE]
  -X-> verified facts -> C++/Rust bridge -> PageSnapshotRuntime     [CNT-18]
  -X-> content-extract -> PageSnapshot -> content-markdown          [CNT-18]
  -X-> page-tools preview / clipboard / save dialog                 [CNT-19]
  -X-> 用户可见 Markdown
```

证据与断点：

| 层 | 当前事实 | 后续任务 |
|---|---|---|
| 用户入口 | `browser/shared-ui/page-tools` 只生成静态库；`PageMarkdownExportController` 无生产调用方，CEF target 未链接它 | `CNT-19` |
| Renderer | `CefPageSnapshotRenderer` 已用 `VisitDOM` 生成有界可见主 frame facts，并经版本化 IPC 发送 | `CNT-17 DONE` |
| Browser | `TabController`/`CefPageSnapshotBridge` 已签发并校验 request/source/navigation/sequence/backpressure，提供 drain/cancel seam | `CNT-17 DONE` |
| Core | `PageSnapshotRuntime`、extract 与 Markdown 已有实现；非测试生产代码没有实例化 `PageSnapshotRuntime`，也没有 CEF 到 Rust DTO/FFI | `CNT-18` |
| 导出 | 预览、复制、保存、覆盖和错误模型存在，但没有真实菜单、剪贴板或文件对话框调用链 | `CNT-19` |
| 真实回归 | 现有 CT 测试证明模型契约，不证明真实 CEF 页面可导出 | `CNT-20` |
| 模块收口 | 跨平台、隐私与产品状态仍未复核 | `CNT-21` |

## 3. 网页媒体 LAN 投屏

当前调用图：

```text
真实 CEF 媒体/网络/用户输入
  -X-> MediaObserver + NetworkObserver + InputProofGate             [PLT-M05b1]
  -X-> ObservationGateway -> candidate/probe/policy                 [PLT-M05b2]
  -X-> CastFeatureView + receiver picker + CastUsecase/SDK pump     [PLT-M05b3]
       |-> Direct -> ADB-connected Crayon receiver                  [PLT-M05b4]
       |-> Relay  -> opaque LAN route -> receiver                   [PLT-M05b5]
       `-> Reject / confirmed ExternalClientHandoff                 [PLT-M05b6]
```

证据与断点：

| 层 | 当前事实 | 后续任务 |
|---|---|---|
| 观察与可信门禁 | CEF-09..12 的 observer/gateway/gate 均为独立静态库；生产 App 未链接，符号除实现自身外无生产调用方 | `PLT-M05b1` |
| 候选与策略 | Rust observer/probe/policy 已测试，但没有消费真实 CEF 事实的生产 owner | `PLT-M05b2` |
| UI/SDK/状态 | `CastFeatureViewModel` 与 `CastUsecase` 均存在；前者未链接 CEF，后者只在测试中构造，CEF App 也未链接 Rust/Cast-SDK | `PLT-M05b3` |
| Direct 真机 | SDK standalone Harness 历史证据不能替代 Desktop Host；用户授权使用任一 ADB 在线手机运行正式接收端验证 | `PLT-M05b4` |
| Relay 真机 | Browser 尚无 Relay runtime/route/session 产品装配 | `PLT-M05b5` |
| 拒绝/交接 | 产品尚未连接 DRM/credential reject UI 与 macOS `ExternalClientHandoff` adapter | `PLT-M05b6` |
| 资源稳定性 | Browser/Renderer/SDK/Relay/platform watcher 尚无 100 次同进程闭环 | `PLT-M05c` |

ADB 设备在线只证明实验室设备可达，不证明接收端版本、Direct、Relay 或控制已通过。真机记录必须包含所选设备、Android/接收端构建、网络拓扑、媒体 fixture、首帧/控制/停止和资源清理结果。

## 4. 本地 Markdown 编辑

真实生产调用图已存在：

```text
CEF BrowserApp
  -> RegisterMdvSchemeHandlerFactory
  -> TabController delegates
  -> MdvEntryController
       -> native file dialog / file URL / drag / context menu
       -> GateLocalLoad + bounded file read
  -> MdvEditController
       -> RenderP0MarkdownDocument
       -> MdvRuntimeState -> crayon://mdv/app.html
       -> transform / dirty confirmation / atomic save / save-as
```

关键证据位于 `browser/cef-shell/src/macos/app.cc:131-219`、`browser/cef-shell/src/browser/mdv/cef_mdv_entries.cc:145-305` 与 `cef_mdv_editing.cc:150-560`。入口要求用户手势；页面 query 限制在 `crayon://mdv/`；保存复用原子写/冲突模型。

审计同时发现一个发布缺口：macOS/Windows `BrowserApp` 以 `BuildFixtureSnapshot()` 初始化生产 `MdvRuntimeState`，而 `cef_mdv_handler.cc:52-66,343-394` 在生产源内嵌测试示例。手动直达 `crayon://mdv` 时可暴露该 fixture，并违反“生产源不含 fixture”的仓库规则。该断点由 `MDV-25` 单独移除；`MRT-09` 再执行 P0 runtime 总 Review。

## 5. 第一期与第二期默认开关

当前没有一套可由页面或配置任意放大的运行时 feature flag。发布装配以 CEF target 的 source/link allowlist 为事实源，默认值冻结如下：

| 能力 | 当前产品默认 | 第一期候选默认 | 开启门禁 |
|---|---|---|---|
| 本地 Markdown/MDV | ON（含待修 fixture 缺口） | ON | `MDV-25`、`MRT-09`、核心 QAR |
| 网页 Markdown | OFF（不可达） | 完成链路后 ON | `CNT-17..21`、`PRV-13A`、核心 QAR |
| LAN Direct/Relay/交接 | OFF（不可达） | 完成链路后 ON | `PLT-M05b1..b6/M05c`、`PRV-13A`、核心 QAR |
| Agent/CLI/入站 MCP | OFF | OFF / `NOT_IN_RELEASE` | 第二期 AGT + QAR B |
| Workflow/Challenge/Site Skill | OFF | OFF / `NOT_IN_RELEASE` | 第二期 WFL + QAR B |
| Capability Hub/Partner | OFF | OFF / `NOT_IN_RELEASE` | 第二期 HUB/SDK-15/16 + QAR B |
| model/provider/AI 总结 | OFF；无 provider 装配 | OFF / `NOT_IN_RELEASE` | 第二期 CNT-11..16 + PRV-13B + QAR B |
| HarmonyOS | OFF | OFF / `NOT_IN_RELEASE` | HM 独立技术预览 |

一期能力未完成其门禁时必须保持不可达，不能通过配置强开。第二期能力若未来进入 CEF source/link list，必须先更新所属 Roadmap、发布 allowlist 与 QAR-15/Release scan；类库在 workspace 中可构建不等于产品开关为 ON。

## 6. 审计命令

- `rg` 枚举 CEF target sources/links、关键符号的非测试调用方和 CMake/Cargo 依赖。
- `cargo tree -p crayon-app-runtime -e normal` 验证 Rust 领域依赖存在，但不代表 CEF 产品链接。
- `adb devices -l` 验证后续真机资源：本次有 4 台设备处于 `device` 状态；未运行投屏。
- `cargo run -p repo-guard -- scan --root .`、`git diff --check` 与文档计数/链接检查作为 REL-02 完成门禁。

## 7. 审计结论

- 网页 Markdown：`PARTIALLY_ASSEMBLED_NOT_USER_REACHABLE`，`CNT-17 DONE`，按 `CNT-18 -> 19 -> 20 -> 21` 严格串行。
- LAN 投屏：`NOT_REACHABLE`，按 `PLT-M05b1 -> ... -> b6 -> M05c` 严格串行。
- 本地 Markdown：`REACHABLE_WITH_GAP`，先完成 `MDV-25`，再由 `MRT-09`/REL/QAR 收口。
- 第二期 feature：`OFF`，没有生产 CEF 调用链、远程 listener 或模型 provider。

本审计不修改生产行为，不声称任何新增功能、真机、性能、长稳或发布门禁通过。
