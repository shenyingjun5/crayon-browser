# CAAP v1 契约（AGT-01 冻结）

CAAP（Crayon Agent Access Protocol）v1 是 CLI/MCP 与浏览器之间的自有 Agent 协议。CLI 使用当前用户本机 IPC，MCP 是 loopback adapter；两者共享本契约的握手、envelope、错误、取消与幂等语义。Transport、tool registry、session/grant 状态机归 AGT-12/02/03/04。

## Envelope（闭合六种）

| 消息 | 方向 | 字段 |
|---|---|---|
| `CaapHello` | client → browser | `schema`（非零 u16）、`client`（token）、`capabilities`（请求集） |
| `CaapWelcome` | browser → client | `schema`、`capabilities`（授予集；不携带任何 session 材料） |
| `CaapRequest` | client → browser | `id`、`tool`（token）、`target`、`deadline_ms`（调用方注入 epoch ms）、`idempotency_key`、`params`（有界字符串表） |
| `CaapChunk` | browser → client | `id`、`seq`、`data`、`is_final`（流式/分页结果） |
| `CaapCancel` | client → browser | `id` |
| `CaapErrorReply` | browser → client | `id`、`error`（稳定错误码） |

## 边界

- token（client/tool/idempotency_key）：字符集 `[a-z0-9_.:-]`，≤64 字节。
- `params`：≤16 项，键 ≤32 字节（token 字符集），值 ≤1024 字节；参数值永远不得携带 Cookie/Authorization/token 等凭证。
- chunk `data` ≤4096 字节；`seq` 单调与缺口检测由 session 层（AGT-03）校验。
- 全部消息 `deny_unknown_fields`；schema 版本复用 FND-08 非零 `SchemaVersion`，v1 协商为精确相等。
- 构造时校验；反序列化后必须再经 `validate()` 复检。

## 能力与风险

`AgentCapability` 闭合五类：`page_read`(R1)、`navigation`(R2)、`cast_read`(R0)、`cast_control`(R3)、`semantic_action`(R4)。`AgentTarget` 闭合：`tab(TabId)` / `active_tab`。

**永久禁止能力在本类型系统中不可表达**：原始 CDP/WebDriver、任意 JavaScript、Cookie/凭证、密码/支付、文件上传、任意文件系统与网络访问。扩展能力集合是协议版本化变更。

## 稳定错误码

`CaapError` 闭合十码，wire 为 snake_case 字符串，client 只匹配码不匹配文案：`version_unsupported`、`capability_denied`、`tool_unknown`、`target_invalid`、`target_stale`、`cancelled`、`deadline_exceeded`、`queue_full`、`unauthorized`、`invalid_message`。

## Golden 与兼容窗口

`schemas/current/caap_*.json` 与 `schemas/previous/caap_*.json` 各 6 个向量。v1 为首个版本，previous 逐字节镜像 current，直到 v2 冻结；v2 起 previous 保持 v1 向量不变、current 演进。
