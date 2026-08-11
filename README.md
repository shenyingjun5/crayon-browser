# 蜡笔 AI Agent 投屏浏览器

本仓库正在建设一款专门为 AI Agent 定制的桌面浏览器，同时提供完整浏览体验和局域网投屏能力。当前正式目标平台为 Windows、macOS（CEF）；HarmonyOS 仅面向鸿蒙电脑 PC 形态技术预览；Linux 暂不规划。

产品不是旧版“视频地址提取器/通用代理”的继续扩张。当前边界是：

- 用户先在当前页主动播放，再通过固定 Cast-SDK facade 选择 LAN Direct/Relay。
- Direct/Relay 不可用时只交接独立蜡笔投屏客户端；浏览器不做 WebRTC、屏幕/标签页/系统音频采集或编码。
- 当前页数据、确定性 Markdown、语义地图、CAAP、CLI/入站 MCP 和授权操作是 Agent-native 核心。
- Workflow 只从已验证成功任务生成候选，并由用户预览保存为个人 Site Skill；验证码/风控只检测、暂停和人工接管。
- Capability Hub 严格区分 Agent 到蜡笔的入站 MCP 与蜡笔到合作方的出站 API/MCP connector。
- 文档/视频总结等依赖模型的能力在第二阶段；模型不参与权限、风险、投屏或路由安全决策。

## 权威文档

开发前按以下顺序阅读：

1. [`AGENTS.md`](AGENTS.md)
2. [`docs/current/README.md`](docs/current/README.md)
3. [当前 PRD](docs/crayon-private-cast-browser-prd.md)
4. [当前架构](docs/current/architecture.md)
5. [技术方案](docs/crayon-private-cast-browser-technical-design.md)
6. [总 Roadmap](docs/crayon-private-cast-browser-roadmap.md) 与 [模块 Roadmap 索引](docs/plans/README.md)
7. [测试标准](docs/current/testing-standard.md)、[测试用例](docs/current/test-cases.md) 与 [Code Review 标准](docs/current/code-review-standard.md)

当前 PRD/架构/安全契约优先于 Roadmap，Roadmap 优先于历史文档。任务状态不代表代码已实现，领取任务前必须检查真实代码、测试和 Git 状态。

## 当前进度

- 45 个原子任务已完成：Foundation 19、`MED-01..19`、`CEF-01A`、`SDK-01..06`。
- 当前优先任务为 `SDK-07` 与 `CEF-01B`。
- 活跃范围共 196 个任务、153 个权威测试 ID。
- 历史 Tauri、提取器、站点矩阵和通用 `/api/extract`/`/proxy` 文档仅用于迁移背景，不是正式产品 API 或新增功能依据。

## 工程执行

- 实质性开发必须从 [`docs/plans/README.md`](docs/plans/README.md) 领取一个依赖已满足的原子任务。
- 每次只把一个任务置为 `IN_PROGRESS`，实现后执行任务规定的 Format、Lint、Unit、Integration、Build 和适用 Harness。
- 完成任务必须记录实际命令、结果、未覆盖项、独立 Code Review 和 Roadmap 状态。
- 不得修改或发布 Cast-SDK 外部仓库、推送、Tag、部署或使用凭证，除非获得用户明确授权。

历史代码可能仍保留在兼容/迁移边界中；不得把历史行为解释为当前产品范围，也不得在其上继续新增正式能力。
