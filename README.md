<p align="center">
  <img src="assets/brand/generated/windows/png/app-icon-128.png" width="128" height="128" alt="Crayon Browser icon">
</p>

<h1 align="center">Crayon Browser</h1>

<p align="center">
  蜡笔 AI Agent 投屏浏览器 · AI Agent &amp; Cast Browser
</p>

Crayon Browser 是一款面向 AI Agent 定制的桌面浏览器，同时提供常规网页浏览与局域网投屏能力。桌面端统一使用 Chromium/CEF 架构；Windows 是当前首发开发环境，macOS 为第二桌面平台，HarmonyOS 电脑为后续技术预览。

> [!IMPORTANT]
> 项目仍处于开发阶段，尚未提供可供普通用户安装的正式版本。Windows CEF 多进程壳、sandbox 和品牌资源已经过本机验证；完整标签栏、地址栏、起始页、浏览器日用功能与投屏 UI 仍按 Roadmap 开发中。

## 产品边界

- 用户在当前页面主动开始播放后，才可以发起投屏。
- 投屏只使用局域网内的 Direct/Relay 媒体投送，设备发现、投屏码、连接和控制复用固定 revision 的 Cast-SDK。
- 浏览器不实现 WebRTC sender、标签页/窗口/系统音频采集、编码或镜像传输；无 Direct/Relay 路由时只交接独立蜡笔投屏客户端。
- 不下载视频、不绕过 DRM、不跳过广告、不批量抓取站点，也不提供代理池或反检测指纹能力。
- Agent 能力统一经过版本化协议、tool registry、capability guard 和用户授权；不向外暴露 raw CDP/WebDriver、任意 JavaScript、Cookie、Authorization 或通用文件/网络工具。
- 真实模型 provider 和依赖模型的视频/文档总结属于第二阶段，模型不参与权限、风险或投屏安全决策。

## 规划能力

| 领域 | 目标 | 当前状态 |
|---|---|---|
| 桌面浏览器 | 标签、导航、Profile/无痕、书签、历史、下载、权限与设置 | Windows 基础功能开发中 |
| 局域网投屏 | 设备发现、投屏码、Direct/Relay、播放控制和会话监督 | SDK/Fake 闭环已完成；真实接收端门禁待验证 |
| 页面数据 | 当前页确定性快照、Markdown 和有界增量 | 等待浏览器/投屏/隐私前置 |
| Agent 接入 | 自有协议、CLI、入站 MCP、只读页面工具和授权操作 | 已规划，尚未开放正式接口 |
| Workflow | 成功任务候选、用户确认保存、Challenge 人工接管和受控自愈 | 后续阶段 |
| Capability Hub | 入站 Agent 能力与出站 Partner API/MCP 分离路由 | 后续阶段 |

任务状态的唯一事实来源是 [模块 Roadmap 索引](docs/plans/README.md)，不能根据本表推断某项代码已经完成。

## 架构

```text
产品 UI / 应用编排
        ↓
跨引擎 Browser API / App Runtime
        ↓
共享 Domain · Media · Cast Policy · Relay · Agent Protocol
        ↓
CEF / Cast-SDK facade / Windows & macOS adapters
```

CEF、Win32、AppKit 和 ArkWeb 类型只允许存在于对应 adapter/shell。只有 `crayon-cast-adapter` 可以依赖 Cast-SDK；CLI/MCP 不能直接调用 CEF、Cast-SDK 或 Relay。

## 平台

| 平台 | 内核 | 优先级 |
|---|---|---|
| Windows 10/11 x64 | CEF | 当前首发开发与验证平台 |
| macOS x64/arm64 | CEF | 第二桌面平台；独立构建/实机门禁 |
| HarmonyOS 电脑 | ArkUI/ArkWeb | 后续技术预览 |
| Linux | — | 当前不在产品和开发范围 |

## 快速开始

### Rust workspace

需要 Rust 1.85 或更高版本。

```powershell
git clone --recurse-submodules https://github.com/shenyingjun5/crayon-browser.git
cd crayon-browser
cargo test --workspace
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check.ps1 fast
```

根 package 为 `crayon-browser-core`。历史提取/relay 代码只存在于显式 `legacy-dev` feature，不属于正式浏览器产品入口：

```powershell
cargo test -p crayon-browser-core --no-default-features --features legacy-dev --lib
```

### Windows CEF 壳

需要 Windows x64、Visual Studio 2022/Build Tools（MSVC）和 CMake。CEF 版本及校验值固定在 [CEF distribution 契约](docs/current/cef-distribution.md)；将解压后的 CEF Standard 根目录设置为 `CRAYON_CEF_ROOT`：

```powershell
$env:CRAYON_CEF_ROOT = 'D:\path\to\cef_binary_150..._windows64'
cmake --preset windows-cef-debug
cmake --build --preset windows-cef-debug --config Debug
ctest --preset windows-cef-debug -C Debug --output-on-failure
```

本仓库不把下载缓存和构建产物提交到 Git。当前没有正式 installer，不能把开发构建当作发布版本。

## 仓库结构

| 路径 | 责任 |
|---|---|
| `browser/engine-api` | 不泄漏 CEF/ArkWeb 类型的跨引擎 C++ 契约 |
| `browser/cef-shell` | Windows/macOS CEF 进程、窗口和平台适配 |
| `browser/shared-ui` | 浏览器 UI 规范、design token、glyph 与本地化资源 |
| `crates/` | Domain、Cast adapter/policy、媒体观察、Relay、IPC 与 App Runtime |
| `third_party/cast-sdk` | 固定 revision 的 Cast-SDK git submodule |
| `test-support` | 确定性时钟、fixture、Fake facade 和泄漏扫描设施 |
| `docs/current` | 当前权威架构、测试、Review、品牌与 UX 契约 |
| `docs/plans` | 可执行原子任务 Roadmap |
| `app`、`demo`、根 `src` | 显式隔离的历史迁移代码，不是正式 CEF 产品入口 |

## 开发规则

开始开发前依次阅读：

1. [AGENTS.md](AGENTS.md)
2. [当前契约索引](docs/current/README.md)
3. [当前 PRD](docs/crayon-private-cast-browser-prd.md)
4. [当前架构](docs/current/architecture.md)
5. [技术方案](docs/crayon-private-cast-browser-technical-design.md)
6. [模块 Roadmap 索引](docs/plans/README.md)
7. [测试标准](docs/current/testing-standard.md) 与 [Code Review 标准](docs/current/code-review-standard.md)

实质性开发必须领取一个依赖满足的原子任务。完成定义包含实现、实际测试证据、文档同步和独立 Code Review；未运行的验证必须明确写为未运行。

## 贡献与安全

- 提交应保持模块边界、小型可审查 diff、无硬编码秘密，并包含适用的行为测试和 Roadmap 更新。
- 不要在 issue、日志、fixture 或文档中提交 Cookie、Authorization、Token、私有签名 URL 或本机绝对路径。
- 安全问题不要附带真实用户数据或生产凭证；在仓库建立正式安全报告通道前，请先联系仓库所有者，不要公开披露可直接利用的细节。
- Cast-SDK 外部仓库、发布、Tag、部署和应用市场操作需要单独授权。

## 许可证

本仓库当前尚未包含开源许可证。公开可见不等于授予复制、修改或分发权；许可证确定前，保留所有权利。
