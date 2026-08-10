# CEF：Desktop 浏览器壳 Roadmap

状态：等待 `FND-08`。目标平台 Windows、macOS、Linux；每项以目标路径、测试 ID 和证据作为验收，不以单平台截图替代。

## 原子任务

| ID | 依赖 | 目标路径 | 实现输出 | 测试/验收 | 证据 |
|---|---|---|---|---|---|
| CEF-01 | FND-08 | `cmake/`、`browser/engine-api` | CEF revision、CMake preset、`BrowserEngineAdapter` 最小接口；记录许可/hash | 三平台 configure；RG-005；接口无产品策略 | S1 |
| CEF-02 | CEF-01 | `browser/cef-shell/src/process` | Browser/render/GPU 子进程入口与 sandbox 开关；正式构建强制 sandbox | 三平台启动/退出；sandbox smoke；无业务代码在 main | S3 |
| CEF-03 | CEF-02 | `src/browser/window` | 单窗口/标签生命周期、导航、前后退、刷新、停止、缩放 | BR-001、重复关闭、崩溃恢复；资源无泄漏 | S3 |
| CEF-04 | CEF-03 | `src/browser/context` | 临时/持久 `CefRequestContext` factory，Profile ID 不用名称作路径 | BR-002、PV-001、PV-004 基础；context 隔离 | S3 |
| CEF-05 | CEF-04 | `src/browser/permission` | 摄像头/麦克风/通知/定位/剪贴板/下载按站点控制 | allow/deny/remember/session tests；默认最小权限 | S3 |
| CEF-06 | CEF-02,FND-08 | `src/ipc`、`crayon-ipc-schema` | length-prefixed IPC、session secret、schema/大小/进程校验 | RG-007；畸形/超大/错误 secret/旧版本 | S2 |
| CEF-07 | CEF-06 | `src/browser/core_client` | Core 子进程启动、健康、崩溃、有界关闭与重连 | 启动失败/崩溃/超时/退出；无 orphan | S3 |
| CEF-08 | FND-11,CEF-03 | `browser/shared-ui` | 地址栏、标签、投屏按钮、错误/权限壳和本地化，不接真实设备 | UI unit；locale parity；键盘/缩放/无障碍 smoke | S3 |
| CEF-09 | CEF-06 | `src/renderer/media_observer` | 独立 document-start 资源：media events、可见性、frame/navigation ID；无自动交互 | BR-003..BR-013；尤其 BR-009、BR-010 | S2 |
| CEF-10 | CEF-09 | `src/browser/input_proof` | Browser process 可信输入、前台标签和播放推进交叉校验 | BR-003、BR-004、BR-005、BR-007；页面伪造全部失败 | S2 |
| CEF-11 | CEF-09 | `src/browser/network_observer` | ResourceRequest/response observation，仅允许字段并有大小/速率上限 | BR-008、BR-011、BR-012；敏感 header/正文不进入 DTO | S2 |
| CEF-12 | CEF-10,CEF-11 | `src/browser/observation_gateway` | DOM/network observation 合并并发送 Core，generation fencing | PL-001、PL-002；导航迟到事件；背压/dropped | S2 |
| CEF-13 | CEF-08,CEF-12 | `shared-ui/features/cast` | `Idle/Browsing/Eligible/Selecting/Planning/Casting` 视图绑定 | 状态 UI contract；未播放禁用；错误不假成功 | S3 |
| CEF-14 | CEF-05,CEF-07,CEF-12,CEF-13 | `tests/e2e/desktop/browser` | 三平台本地 fixture E2E harness、截图/日志脱敏产物 | BR-001..BR-014 适用项；无公网 | S3 |
| CEF-15 | CEF-14 | 文档/Review | 三平台 CEF 壳总 Review、性能/包体/启动基线，修 P0/P1 | desktop build + E2E + repo guard；V1 CEF 部分完成 | S3 |

## 接口冻结

`BrowserEngineAdapter` 只包含导航、标签、Profile、权限、输入事实和 observation 订阅；不得暴露 CEF 对象给 UI/Core。新增接口必须先写 contract test 和 Harmony 可实现性说明。

## 每项通用验证

- C++ format/static analysis、目标 test target、目标平台 build。
- 变更 renderer/browser IPC 时执行畸形消息、大小上限、旧 navigation 和 secret 泄漏测试。
- 三平台实现允许分任务完成，但共同接口变更由一个 owner 先合并，平台 Agent 不各自改 schema。
