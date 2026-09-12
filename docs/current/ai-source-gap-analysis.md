# AI Source Producer Gap Analysis（MRT-19）

状态：v1 gap analysis；只产出后续任务触发条件，不含实现。
上游：WFL-10 store（已闭合）、CNT-11 provider ADR（已闭合）、MRT-09 P0 Runtime（已闭合）。
下游：CNT-12（model adapter）、CNT-13（document summarisation）、CNT-15（AI UI）。

## 1. 候选 Markdown 冻结

| 维度 | 已闭合 | 缺口 | 后续任务 |
|---|---|---|---|
| 来源 | WFL-10 store 中 `Candidate` → `Enabled` 的 SiteSkill | — | — |
| 内容 | Recipe 步骤摘要 + origin + version（WFL-08 生成门） | — | — |
| 冻结点 | `SkillPreviewController::confirm()` 单次释放 `Arc<Recipe>` | — | — |
| 发送 | 用户显式操作（CNT-15 AI UI），产品不主动推送 | UI 装配 | CNT-15 |

**结论**：候选生成链已闭合；缺口仅在产品 UI 层（CNT-15）。

## 2. 发送预览

| 维度 | 已闭合 | 缺口 | 后续任务 |
|---|---|---|---|
| WFL-09 preview | name/origin/version/steps/risk/data-flow 完整披露 | — | — |
| provider 预览 | CNT-11 `ModelProviderConfig` endpoint/model/timeout | — | — |
| 发送前 UI | 展示将要发送的 Markdown + 目标 endpoint + 费用 | UI 装配 | CNT-15 |

**结论**：数据模型闭合；缺口在 CNT-15 UI 层。

## 3. Provenance

| 维度 | 已闭合 | 缺口 | 后续任务 |
|---|---|---|---|
| 来源追踪 | Recipe 指纹（FNV-1a origin+name+version+steps） | — | — |
| 请求追踪 | `ModelRequest.prompt + markdown` 绑定 snapshot/hash | hash 绑定实现 | CNT-12/13 |
| 审计 | CNT-11 `ConnectorAuditEvent`（provider hash, 无正文） | — | HUB-15 |

**结论**：指纹体系已闭合；请求级 hash 绑定归 CNT-12/13。

## 4. 取消与用户保存边界

| 维度 | 已闭合 | 缺口 | 后续任务 |
|---|---|---|---|
| WFL-12 取消 | `RunCancel` 协作取消 + deadline 超时 | — | — |
| 模型请求取消 | — | HTTP 层取消 | CNT-12 |
| 保存 | 用户显式确认 → WFL-10 store；产品永不自动保存 | — | — |
| 无痕 | WFL-10 `clear_all` + CNT-11 零保留 | — | — |

**结论**：取消/保存语义闭合；HTTP 层取消归 CNT-12。

## 5. 后续任务触发条件

| 触发 | 任务 | 前置 |
|---|---|---|
| provider HTTP adapter | CNT-12 | CNT-11 ✅ |
| 摘要/大纲/问答 | CNT-13 | CNT-12 |
| AI UI | CNT-15 | CNT-13/14 |
| 总审 | CNT-16 | CNT-15 |
