# CNT 网页提取与 Markdown Roadmap

- 状态：规划中
- 任务数：10
- 开始门禁：`CEF-15`、`SDK-14`、`MED-19`、`PRV-08` 均完成
- 非目标：AI/模型、Agent、批量爬取、后台抓取、网页卡片投屏

## 1. 目标

在浏览器与局域网投屏主链路完成后，为用户当前打开的页面提供确定性 Markdown 提取。所有处理由用户主动触发并在本地完成，结果支持预览、复制和保存。

## 2. 任务表

| ID | 状态 | 依赖 | 允许修改路径 | 交付目标 | 验收/测试 | 阶段 |
|---|---|---|---|---|---|---|
| CNT-01 | TODO | CEF-15,SDK-14,MED-19,PRV-08 | `crates/crayon-content-contract/**`, `docs/current/**` | 定义页面快照、提取结果、诊断和资源上限契约 | `CT-001`,`CT-002`; schema/golden | C1 |
| CNT-02 | TODO | CNT-01 | `apps/desktop-cef/**`, `crates/crayon-browser-gateway/**` | 在 Renderer/Browser 边界生成受限、可取消的当前页快照 | `CT-001`,`CT-007`; unit/integration | C1 |
| CNT-03 | TODO | CNT-02 | `crates/crayon-browser-gateway/**`, `crates/crayon-app-runtime/**` | 建立用户触发、导航绑定、旧结果丢弃的提取编排 | `CT-002`,`CT-007`; integration | C1 |
| CNT-04 | TODO | CNT-03 | `crates/crayon-content-extract/**` | 确定性识别标题、主正文和结构块 | `CT-003`,`CT-008`; fixture/unit | C1 |
| CNT-05 | TODO | CNT-04 | `crates/crayon-content-markdown/**` | 将结构块稳定转换为标准 Markdown | `CT-003`,`CT-004`; golden | C1 |
| CNT-06 | TODO | CNT-05 | `crates/crayon-content-markdown/**` | 支持列表、引用、代码、表格、链接和图片引用的安全规范化 | `CT-004`,`CT-005`; golden/security | C1 |
| CNT-07 | TODO | CNT-05,PRV-08 | `apps/desktop-cef/**`, `crates/crayon-platform-api/**` | 本地复制与保存，处理取消、覆盖、失败和敏感路径 | `CT-005`,`CT-006`; integration | C1 |
| CNT-08 | TODO | CNT-06,CNT-07 | `apps/desktop-cef/**` | Markdown 预览/阅读视图和明确的原页返回路径 | `CT-003`,`CT-006`; UI integration | C1 |
| CNT-09 | TODO | CNT-08 | `tests/**`, `test-support/**` | 覆盖正确性、安全、取消、导航竞争、超大页面与性能预算 | `CT-001..008`; E2E/perf/security | C1 |
| CNT-10 | TODO | CNT-09 | `docs/current/**`, `docs/plans/**` | 独立 Code Review、验收证据与剩余风险登记 | 全部 CNT 验收；Review P0/P1=0 | C1 |

## 3. 不变量

- 只处理当前标签页、当前导航代次和用户主动触发的快照。
- 页面脚本不能直接要求文件写入，也不能伪造可信提取完成事件。
- 输入大小、节点数、深度、文本长度、图片/链接数量、执行时间和输出大小都有界。
- 导航、关闭标签、取消或超时后，旧结果不得覆盖新页面。
- Markdown 中的 URL 做规范化和危险 scheme 过滤；不内联 Cookie、Authorization 或页面存储。
- 输出确定、可重复；自动化不依赖公共网络或第三方站点。
- 当前模块不含模型接口、Prompt、供应商配置、密钥或 Agent 工具。

## 4. 完成定义

- `CT-001..008` 有确定性 fixture 和实际执行证据。
- Windows/macOS 完成预览、复制、保存和取消路径验证。
- 超大/恶意页面不会造成无界内存、栈深度、CPU 或输出增长。
- 文档明确已覆盖和未覆盖的 HTML 语义，不宣称保真还原网页视觉样式。
