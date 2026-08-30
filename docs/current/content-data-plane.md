# Content Data Plane v1

- 契约：`content-data-plane-v1`
- 状态：C1 `GO`（CNT-10，2026-08-30）
- 范围：当前网页的 Browser 验证事实、`PageSnapshot`、确定性 Markdown、generation-scoped owner、增量/背压，以及经授权的 CAAP R1 读取。

## 1. 所有权与依赖方向

```text
CEF/engine adapter normalized facts
  -> crayon-content-extract
  -> crayon-page-data::PageSnapshot
  -> crayon-app-runtime::PageSnapshotRuntime
     -> local preview/export
     -> crayon-agent-gateway::ContentReadPort (R1)
        -> structured snapshot / deterministic Markdown
```

- Renderer/engine adapter 只产出有界、归一化事实；DOM、CEF handle、脚本和跨源正文不得进入 Core。
- `crayon-content-extract` 选择主内容和阅读顺序，并排除隐藏、敏感、跨源及危险 URL 事实。
- `crayon-page-data` 是 snapshot schema、provenance、revision、分页、delta 和缓存状态的唯一所有者。
- `crayon-app-runtime` 绑定 Profile、前台目标、tab、generation 和 snapshot owner；UI、CLI/MCP 不得绕过它直接读取引擎。
- `crayon-agent-gateway` 只做 grant/target/output guard 和稳定错误映射；Markdown 复用同一 snapshot，不建立第二套抓取管线。

本地 `crayon://mdv` 的文件查看/编辑/runtime extension 是独立用户能力，不属于本契约，也不得作为 Agent 文件读取能力暴露。

## 2. 冻结数据与信任边界

`PageSnapshot` v1 的稳定字段为：schema version、output level、tab/generation、当前 URL、标题、revision、Browser provenance、显式 truncation、九类闭合 content block。反序列化后必须再次 `validate()`；unknown field、非 current schema、伪造 provenance、危险 URL、非法 shape 和超预算全部拒绝。

唯一可信 provenance token 是 `browser_process`。页面正文、标题、链接文本和图片 alt 始终是不可信内容：它们不能改变 grant、目标、Profile、generation、风险、投屏策略或后续工具调用。

闭合 block 类型：heading、paragraph、list item、link、quote、code block、image reference、table、divider。图片只保留引用，不在内容管线加载。Markdown 输出转义 HTML/Markdown 元字符，并默认移除来源 URL 的 query/fragment。

## 3. 预算与背压

| 面 | v1 上限/语义 |
|---|---|
| Standard snapshot | 4096 blocks、总文本 1 MiB、单普通 block 16 KiB |
| Compact snapshot | 512 blocks、总文本 128 KiB、单普通 block 2 KiB |
| URL / title | 2048 B / 512 B |
| Code / table / list | code 32 KiB；table 256×32、cell 1024 B；list depth 8 |
| Snapshot owner | 16 cached tabs、32 active reads、128 retired read ids、page 1..256 blocks |
| Delta | 单次变化 512 blocks；chunk 64 blocks；最多 4 个 unacked chunk |
| Markdown | Standard 1.5 MiB；Compact 192 KiB；超限不返回部分文档 |
| Agent R1 | targets 64、selection 16 KiB、snapshot serialized output 2 MiB；调用方 limit 必须在闭合上限内 |

Collector 主动截断必须填写 omitted blocks/bytes 和去重后的 reason；Core 不允许静默截断。Delta 只在同 tab/generation 且 revision 严格前进时成立；大变化退化为 replace-all。背压、容量和超限均显式失败，不阻塞、不扩容、不自动重试。

## 4. 生命周期与错误语义

- navigation generation 前进会使旧 snapshot、live read 和旧结果失效；同 revision 不同内容为 conflict。
- Profile 不匹配、后台 target、旧 generation、关闭 tab 和 shutdown 均不得返回正文。
- read cancel、tab close、Profile 切换和 shutdown 释放持有的分页/目标状态；终态不可重开。
- Agent 每次 R1 调用先经 `GrantManager::authorize(PageRead)`；单次/任务 grant 的消费计数只在成功授权时推进。
- Browser-owned rejection 映射到稳定 CAAP error；错误和诊断不携带页面正文、URL query、selection 或底层对象。

## 5. Agent R1 冻结接口

R1 内容面固定为五个逻辑工具：列出当前 Profile 的可读目标、标题、当前选择、结构化 snapshot、确定性 Markdown。所有内容结果保留 tab/generation fence；明确 tab 必须与返回 tab 一致，ActiveTab 由 Browser runtime 解析。

R1 不表达 Cookie、Authorization、密码/支付值、隐藏 DOM、跨源 iframe 正文、任意 JavaScript/CDP、任意文件、网络代理或后台批量抓取。CLI/MCP 只能经 CAAP adapter 使用同一语义，不能增加旁路 API。

任何修改 `PageSnapshot` wire、block 闭合集、provenance、预算、generation/revision、R1 工具形状或错误码的变更，都必须建立独立原子任务并同步 current/previous golden；不能在模型、Workflow 或 UI 任务中顺手扩张。

## 6. CNT-10 Review 结论

- CT-001..008 的 schema、提取、Markdown、导航竞争、Profile/后台拒绝、取消/关闭、容量、安全与性能证据闭合。
- Windows C++ collector/gateway/export 与共享层完整 CTest 通过；Rust unit/E2E/security/perf 通过。
- C1 结论：`GO`。本地页面数据与 Agent R1 可以作为后续 CLI/MCP、语义动作和 Workflow 的稳定只读数据面。
- P0/P1/P2：0/0/0。

未覆盖但不阻断 C1 接口冻结：CNT-08 的真实 CEF 菜单/剪贴板/文件对话框呈现仍需产品装配验证；R1 完整 snapshot 在 runtime mutex 内复制重建的成本由既有上限与当前 P95 覆盖，继续纳入 AGT-15/QAR-05 的 UI delay/RSS/长稳矩阵。模型 provider、发送预览与 AI 总结属于 CNT-11+，在 ADR/AGT-16/PRV-13 完成前保持关闭。
