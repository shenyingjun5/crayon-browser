# BRD 品牌图标资产 Roadmap

- 状态：`BRD-01..04 DONE`
- 任务数：4
- 目标平台：Windows、macOS、HarmonyOS 电脑
- 输入：用户确认的“浏览器窗口 + 蜡笔”图标；原始 PNG 仅作参考源，不直接进入正式安装包。

## 1. 边界

- 本 Roadmap 只拥有品牌母版、确定性派生资产、资产门禁和下游接入契约。
- 不实现 CEF/ArkWeb 壳、安装器、签名、公证、应用市场发布或运行时状态图标。
- App 图标不得复用为投屏按钮、Agent grant、MCP、托盘状态或错误状态图标；这些属于产品 UI 的独立语义 glyph。
- 不增加 AI 星光、机器人、Cast 波纹或文字；保持最多“浏览器 + 蜡笔”两个隐喻。

## 2. 原子任务

| ID | 状态 | 依赖 | 允许修改 | 实现输出 | 验收 | 证据 |
|---|---|---|---|---|---|---|
| BRD-01 | DONE | 无 | `assets/brand/source/reference-v1.png`、`docs/current/brand-assets.md`、本 Roadmap/索引 | 锁定参考源 SHA-256、尺寸、来源声明、保留/调整/禁用规则与平台组合原则 | BI-001；原图可追溯；明确不直接发布；无本机临时路径 | S0 |
| BRD-02 | DONE | BRD-01 | `assets/brand/source/*.svg` | 重建 `master`、`micro`、`monochrome` 三个 SVG；去黑角；保留浏览器+蜡笔；macOS 可切换系统遮罩底板 | BI-002、BI-005；静态 SVG、无嵌入位图/脚本/外部资源；16～1024 视觉检查 | S1 |
| BRD-03 | DONE | BRD-02 | `assets/brand/manifest.json`、`tools/brand-assets/**`、`assets/brand/generated/**` | 无第三方包的 Node 生成/验证工具；导出 Windows PNG/ICO、macOS iconset/ICNS、Harmony PNG 和 contact sheet | BI-003..008；两次生成 hash 一致；尺寸/alpha/container/manifest 全通过 | S1 |
| BRD-04 | DONE | BRD-03 | current/CEF/HM/QAR Roadmap、检查入口 | 把资产验证加入 fast/all；声明 CEF-01D/01E、HM-02、QAR-09/10 的消费与包内验证门禁；完成独立 Review | `scripts/check.ps1 fast`；`scripts/check.sh fast` 或等价 Node 门禁；P0/P1=0 | S1 |

## 3. 下游接入

- `CEF-01D`：消费 `generated/windows/app.ico` 与 Windows PNG，不重新生成或手工修改。
- `CEF-01E`：消费 `generated/macos/AppIcon.iconset`/`app.icns`，在 macOS runner 复核 `iconutil`/包内资源。
- `HM-02`：按目标 DevEco SDK 模板消费 `generated/harmony`，不得复用 Windows 遮罩假设。
- `QAR-09/10`：在 clean VM/真实 macOS 包验证 EXE、快捷方式、任务栏、Dock、安装/卸载、升级/回滚图标。

## 2A. 完成记录

- `BRD-01`（2026-08-11）：参考源复制到仓库路径，`Get-FileHash -Algorithm SHA256` 得到 `aa807f170a73b5d8130b03f45ad36228cf45c97037dcf73d1363400b668db870`；只记录仓库相对路径，原始临时路径未进入源码/文档。原图 `1254×1254`、24-bit RGB、无 alpha，明确只作视觉参考。Review P0/P1/P2/P3=0；未覆盖项为 SVG 与平台产物，转入 BRD-02/03。
- `BRD-02`（2026-08-11）：完成 `app-icon-master.svg`、`app-icon-micro.svg` 与 `app-glyph-monochrome.svg`；源文件不含脚本、外链资源或嵌入位图。已人工检查 1024 母版、macOS 方形底板和 16/20/24/32/40/48/64/128/256 小尺寸 contact sheet，浏览器与蜡笔语义可辨，透明圆角无原参考图的黑色烘焙角。Review P0/P1/P2/P3=0；平台容器和确定性 hash 转入 BRD-03。
- `BRD-03`（2026-08-11）：`node tools/brand-assets/generate.mjs` 生成 27 个受管文件，renderer 为 `Chromium 151.0.7922.77`；`node tools/brand-assets/verify.mjs` 的 BI-001..008 全部通过。最终连续两次生成的 `manifest-lock.json` SHA-256 均为 `AAEC10EA36BCD3223F73DE6F2A7F88FF526497789926766C6949C34B67965D67`；contact sheet 覆盖 16～1024 并已在明/暗背景人工复核，`.gitattributes` 固定 hash 输入为 LF、平台容器为 binary。Review 发现并关闭 1 个 P1（受管删除路径未拒绝父级 symlink/junction），补充 3 项路径逃逸/reparse/reset 单测后 P0/P1/P2/P3=0；平台打包消费门禁转入 BRD-04。
- `BRD-04`（2026-08-11）：PowerShell 与 shell 新增独立 `brand-assets` 模式，并接入 `fast/all`；`scripts/check.ps1 brand-assets` 与 Git Bash `scripts/check.sh brand-assets` 均通过 3 项路径安全单测及 BI-001..008（27 文件）。首次整体 fast 在范围外 SDK 能力代码编译阶段失败，未修改该并行任务；其恢复后重跑 `scripts/check.ps1 fast`，guard、format、brand-assets-unit、brand-assets、formal-workspace、legacy-unit 全部通过。CEF-01D/01E、HM-02、QAR-09/10 已固定消费路径与真包验证门禁。最终 Review P0/P1/P2/P3=0，未覆盖项仅为下游任务中的真实 Windows/macOS/HarmonyOS 包内显示验证。

## 4. 完成门禁

- 参考源、矢量源和生成产物均有 hash 与版本；生成资产不得从 OS 临时目录读取。
- 小尺寸使用专用 `micro`，不把 1024 位图机械缩小作为完成。
- 透明角不存在黑边、白边或 key-color fringe；macOS 版本不预烘焙错误系统遮罩。
- 任何品牌变化必须更新源、manifest、golden hash、视觉证据与版本号，不能只替换某个平台文件。
