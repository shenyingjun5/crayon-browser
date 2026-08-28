# Markdown Runtime Extension Framework v1 契约

- 契约版本：`markdown-runtime-v1`
- 日期：2026-08-28
- 所属任务：`MRT-01`
- 适用范围：浏览器内用户侧 `crayon://mdv`；不适用于网页事实提取、CNT、CAAP、MCP、Cast/receiver 或 AI provider

本文冻结 Markdown Runtime v1 的逻辑 schema、安全边界、匹配、预算、生命周期和兼容规则。实现语言、类名和序列化格式可以变化，但不得改变本文的可表达能力与失败语义。

## 1. 目标与非目标

Runtime 在不建立第二 Markdown parser 的前提下，为现有 C++17/md4c 安全渲染链提供闭合扩展点：

```text
用户选择的 Markdown 字节
  -> md4c CommonMark/GFM 解析与安全 HTML
  -> Browser-owned ExtensionNode 事实
  -> 编译期 Extension Registry 精确匹配
  -> 专用 adapter/renderer
  -> 类型化输出 policy gate
  -> MDV 页面局部落位
```

v1 不提供：

- 运行时安装、下载、发现或更新插件；
- 文档/AI 提供的 manifest、renderer、模块路径、URL、capability 或 `trusted`；
- 第二 Markdown parser、通用 JS/WASM 执行器、任意脚本或 native process；
- 本地文件读取、网络访问、导出、投屏、Agent 工具或 Browser 特权；
- 对未启用扩展语法的抢先解释。

## 2. 兼容层级与优先级

兼容顺序固定为：

1. **Level A：CommonMark/GFM**。现有标准 HTML 输出优先且必须逐字节保持；扩展失败不能破坏它。
2. **Level B：成熟生态扩展**。Mermaid、Code Highlight、KaTeX、Graphviz 等必须逐项经 Roadmap 启用。
3. **Level C：蜡笔 Runtime**。ECharts、Presentation 等仅在独立契约通过后启用；TV/Cast/AI 仍是跨域 gap analysis。

标准解析始终先执行。Extension Registry 不修改 md4c 的 block/inline 归属，也不以扩展优先级覆盖 Level A。Registry 内不存在“先注册者获胜”或文档指定优先级：同一 `kind + matcher` 只能有一个 owner；冲突双方均禁用并返回 `registry_conflict`。

未知、禁用、冲突、超界、加载失败或输出被拒绝时：

- fence 保留现有安全 code block；
- inline/block/container 保留现有安全文本/HTML；
- 其他 block 与整篇文档继续渲染；
- 不自动改用另一 extension，不联网补包，不重放副作用。

## 3. 所有权与信任边界

| 组件 | 拥有 | 不拥有 |
|---|---|---|
| `browser/shared-ui/markdown` | md4c、Level A 安全 HTML、source revision 与 ExtensionNode 事实 | registry、动态资源、CEF、平台 API |
| `browser/shared-ui/markdown-runtime` | 编译期 registry、manifest 校验、预算、route、cache key、generation 与错误状态 | parser、本地文件、Browser 特权、第三方算法 |
| 专用 extension adapter | 精确语法到类型化 renderer 输入的转换 | 任意模块/URL、其他 extension、grant |
| `browser/shared-ui/mdv` | 会话、主题、占位/错误 UI、页面局部落位 | Agent 工具、Cast 协议、任意文件路由 |
| CEF MDV adapter | manifest 资源读取、严格同源路由、Renderer 生命周期 | 扩展语义、通用文件 handler、网络 fallback |

Markdown 字节、ExtensionNode source、第三方 renderer 输出、错误文本和未来 AI 候选均不可信。只有 Browser 构建产物内的 current manifest/registry 是可信配置；“来自本地文件”不等于 trusted。

## 4. Render Plan 与 ExtensionNode v1

逻辑 schema ID 为 `crayon.markdown-runtime/render-plan/v1`：

```json
{
  "schema": "crayon.markdown-runtime/render-plan/v1",
  "document_generation": 7,
  "source_revision": 19,
  "safe_html": "<p>...</p><pre><code>...</code></pre>",
  "extension_nodes": [
    {
      "kind": "fence",
      "node_id": "n-0000000000000001",
      "matcher": "mermaid",
      "source_utf8": "flowchart LR\nA-->B\n",
      "source_bytes": 19,
      "source_revision": 19
    }
  ]
}
```

### 4.1 字段约束

| 字段 | 约束 |
|---|---|
| `schema` | 必须精确为 current 或受支持的 previous schema ID；未知 major 拒绝 |
| `document_generation` | Browser 会话内单调递增的无符号整数；打开/关闭/导航/替换文档推进 |
| `source_revision` | 当前文档编辑版本；任何字节变化推进 |
| `safe_html` | 现有 md4c + HTML policy 的 Level A 输出；extension 不能回写或放宽 policy |
| `kind` | 闭合枚举：`inline`、`block`、`fence`、`container` |
| `node_id` | Browser 生成、当前 Render Plan 内唯一的不透明 ID；页面不得解释其结构 |
| `matcher` | parser/adapter 产生且已规范化的精确 token；不是 regex、glob、模块名或 URL |
| `source_utf8` | 经 UTF-8 与预算校验的最小 extension 源文本；不得附加宿主文件路径、外围正文或其他元数据（用户源码自身出现的普通路径文本仍只按不可信文本处理） |
| `source_bytes` | `source_utf8` 的实际 UTF-8 字节数，必须逐字节一致 |
| node `source_revision` | 必须等于 plan revision；不相等视为 stale |

ExtensionNode v1 禁止附带：`manifest`、`extension_id`、`module`、`path`、`url`、`capabilities`、`options`、`trusted`、HTML、SVG、脚本、文件句柄或平台对象。

Render Plan、ExtensionNode 与 Manifest 均为 closed object：除 current/previous golden 明列的字段外，未知字段一律拒绝；不能用“忽略未知字段”承载未来权限或模块信息。

四类 node 只是闭合事实类型，不代表默认启用语法。MRT-02 只能在不改变 `safe_html` 的同时产生事实；某类没有已审核 adapter 时必须是零分发。

本 schema 是 parser/runtime 之间的 fallback plan，不是直接交给页面的 DOM。Registry 命中后，Runtime/MDV assembly 才能为 node 生成唯一的 Browser-owned inert placeholder；未命中或失败时继续使用 `safe_html` 中对应的 Level A code/text。页面不得按 source offset、DSL 或 CSS 猜测落位目标。md4c 公共 callback 不提供容器场景下连续可靠的原文区间，因此 v1 不伪造 byte range；编辑定位由当前 revision 内的 node ID 与后续 Browser-owned assembly 映射负责。

### 4.2 node ID 与页面定位

`node_id` 只在当前 `document_generation + source_revision` 内有效。它不能跨编辑、导航、Profile 或窗口复用，不能包含路径/source/hash/extension 名称，也不能成为长期 DOM selector。页面落位必须同时核对 node ID 和三重 generation，找不到唯一占位时丢弃结果。

## 5. Manifest 与 Registry v1

逻辑 manifest schema ID 为 `crayon.markdown-runtime/manifest/v1`。Manifest 由构建期受管源生成并编译进产品；文档、网页、AI、MCP、配置文件和下载内容均不能提供或覆盖它。

```json
{
  "schema": "crayon.markdown-runtime/manifest/v1",
  "id": "mermaid",
  "version": "11.17.2",
  "node_kind": "fence",
  "matchers": ["mermaid"],
  "output": "svg",
  "asset_manifest": "mermaid-runtime-v1",
  "policy_version": "svg-policy-v1",
  "capabilities": {
    "network": "deny",
    "file": "deny",
    "dynamic_code": "deny",
    "external_process": "deny",
    "export": "deny",
    "interaction": "deny"
  }
}
```

### 5.1 Manifest 字段

| 字段 | 规则 |
|---|---|
| `schema` | current/previous golden 精确值 |
| `id` | 编译期唯一、ASCII 小写 kebab token；不能由 matcher 推导模块路径 |
| `version` | 锁定的实现/资产版本；不能使用 range、tag、`latest` 或空值 |
| `node_kind` | 四类闭合枚举之一 |
| `matchers` | 非空、去重的精确 token 列表；禁止 regex/glob/前缀/大小写折叠/附加参数 |
| `output` | 闭合枚举：`safe-html`、`svg`、`canvas`、`error` |
| `asset_manifest` | 可为空或引用另一个构建期受管资产 manifest ID；不得是路径或 URL |
| `policy_version` | 对应 Browser-owned 输出 policy；缺失或未知时 extension 禁用 |
| `capabilities` | 六个 key 必须完整出现；缺失按 deny，未知 key 拒绝 manifest |

v1 永久禁止 `network/file/dynamic_code/external_process`，`export` 当前也必须为 `deny`。`interaction` 默认且 P0 manifest 必须为 `deny`；后续 ECharts/Presentation 若经独立 Roadmap、安全契约和 Browser-owned page-local policy 审核，可在不获得 Browser 特权的前提下使用闭合值 `page-local`。其他值拒绝，文档不能覆盖。

Manifest 不含 renderer 代码路径。`id -> adapter factory` 是编译期 C++ registry 的闭合映射；manifest ID 存在但 factory 缺失、factory 未登记 manifest、版本不匹配或 policy 缺失时均禁用该 extension。

### 5.2 精确 matcher 与冲突

- CommonMark 解析后的 fence info 只在专用 adapter 定义的规范化步骤后比较；当前 Mermaid matcher 只接受精确小写 `mermaid`。
- `Mermaid`、`mermaid extra`、空 token、含控制字符 token 均不命中 Mermaid。
- Registry key 为 `node_kind + U+0000 + matcher`；同 key 多 owner 时两者均不可路由。
- 不允许文档提供 extension ID、优先级、alias 或 fallback chain。
- Registry 初始化必须先完整验证再发布；不能把半个有效 registry 暴露给渲染线程。

## 6. 类型化输出与 policy gate

Extension 返回的是不可信候选，不是可直接写 DOM 的可信结果：

| 输出 | Browser-owned 处理 | 禁止 |
|---|---|---|
| `safe-html` | 重新经过扩展专用 HTML allowlist；与普通 Markdown HTML policy 分版本 | inline handler、script、危险 URL/CSS、任意 iframe/object |
| `svg` | 解析后经过独立 SVG policy；ID/fragment 限当前 block | script、event、foreignObject、外部 URL、危险 scheme、`@import`、CSS `url()` |
| `canvas` | 页面代码从已校验的纯数据创建受控 canvas；结果不序列化为 HTML | renderer 返回 HTMLElement、callback、function、网络资源 |
| `error` | 本地化错误卡片 + 可选安全源码；原始详情只保留有界诊断码 | stack、绝对路径、完整正文、URL/token |

只有 `ready` 且 generation 全匹配的输出可替换唯一占位。policy 拒绝等价于当前 block `output_rejected`，不能降级为放宽 policy。

## 7. 能力与资源策略

所有能力默认且当前均 deny：

- 无网络、DNS、socket、fetch/XHR、远程字体/图片或 CDN fallback；
- 无任意本地文件、目录枚举、文件上传、最近文档或磁盘 cache；
- 无 `eval`、`Function`、动态任意 import、文档脚本、插件脚本或 native process；
- 无导出、保存、剪贴板、投屏、窗口/系统设置、CAAP/MCP/Agent 能力；
- 无跨 block 或跨文档 DOM 查询；page-local UI 也必须由后续独立契约显式启用。

Runtime 资产只从构建期 manifest 的精确相对路径读取。CEF 路由必须先规范化并拒绝目录、`..`、反斜杠、NUL、编码/二次编码分隔符、query、fragment、大小写别名、未知 MIME 和 manifest 外路径；普通构建与运行完全离线。

## 8. 预算模型

实现必须提供一个不可由文档覆盖的 `RuntimeBudgetV1`，至少包含下列命名上限：

| 预算 | 语义 |
|---|---|
| `max_nodes_per_document` | 单 Render Plan 的 extension node 总数 |
| `max_source_bytes_per_node` | 单 node 的 UTF-8 source 字节 |
| `max_total_extension_source_bytes` | 单文档所有 node source 总字节 |
| `max_extension_nesting_depth` | inline/block/container 事实嵌套深度 |
| `render_deadline_ms` | 单次 renderer 从调度到结果的 deadline |
| `max_concurrent_renders` | 当前文档同时执行的 renderer 数 |
| `max_pending_renders` | 有界等待队列；满载时拒绝新项而非扩容 |
| `max_cache_entries/max_cache_bytes` | 会话内存 cache 双上限 |
| `max_error_bytes` | 单 block 对用户/诊断可见错误文本上限 |

所有值必须是有限正数，由对应实现任务的 benchmark 冻结为命名常量；`0 = unlimited`、负数、缺失、文档 override 和运行时自动扩容均非法。达到 node/source/depth 上限时只保留 Level A fallback；达到队列/cache 上限时返回 `capacity_exceeded` 并记录不含正文的 dropped counter。

## 9. Generation、状态机与取消

每个请求绑定：

```text
document_generation + source_revision + extension_generation
```

- `document_generation`：打开、替换、导航、关闭文档时推进。
- `source_revision`：编辑、reload 或冲突解决改变源码时推进。
- `extension_generation`：主题、policy、manifest/runtime 版本、Renderer 重启或 extension disable 时推进。

状态闭合为：

```text
unrequested -> queued -> loading -> rendering -> ready
                       \-> failed
任意未终态 -> cancelled | stale
```

允许重试只创建带新 request ID 的新请求；旧请求保持终态，不能复活。取消/deadline/导航后即使第三方调用不可中止，回调也只能被丢弃。页面销毁、Renderer crash 或 App 退出按 `cancel -> detach callbacks -> clear cache/resources` 逆序收敛，且可重复调用。

错误码闭合为：

```text
invalid_node
unknown_kind
disabled
registry_conflict
budget_exceeded
capacity_exceeded
asset_unavailable
load_failed
render_failed
timeout
cancelled
stale
output_rejected
```

错误不携带源码、外围正文、绝对路径、URL query、Cookie、Authorization、token、stack 或第三方内部对象。

## 10. Lazy、cache 与性能

- 无匹配 node 时不得读取 manifest 资产、创建 worker、初始化 runtime 或改变普通 Markdown 首屏路径。
- 匹配后才请求专用 extension；viewport lazy 由后续任务实现，但不能改变 generation/预算语义。
- cache 仅在当前 Browser 会话内存中存在，不写磁盘。key 至少包含：`extension id + locked version + source hash + theme + normalized options + policy version`，并绑定 Profile/文档隔离域。
- key、日志和指标不保存原始 source。hash 不能作为跨 Profile 的文档标识。
- 文档关闭/导航、Profile/无痕关闭、Renderer 终止、manifest/policy 变化或内存压力必须清除相关项。
- 不在 UI/IO 线程同步加载大型资产，不在锁内加载/渲染/回调；队列、cache、错误与指标均有界。

## 11. Locale 与无障碍

- 用户可见 extension 名称、加载/失败/禁用提示和动作全部来自 locale，不使用文档文本作为控件名称。
- 占位、ready、error 的状态变化以有界 live-region 表达，避免每次编辑高频播报。
- SVG/canvas 默认是不可交互的图像语义；必须有本地化名称和来自安全文本的有界描述，不能把完整 DSL 当作 accessible name。
- 键盘、焦点、全屏、查看源码等交互由各自任务补充；未实现时不得暴露不可用按钮。
- error 卡片可查看安全源码，但不得显示 stack、路径或内部 policy 细节。

## 12. Golden 与兼容窗口

current golden：

- `crayon.markdown-runtime/render-plan/v1`
- `crayon.markdown-runtime/manifest/v1`
- 本文 §4 的 valid render plan、§5 的 valid manifest、§13 reject vectors

v1 首次冻结时没有 previous golden。以后变更遵循：

1. 增加可选字段且默认 deny/保持 fallback，可在同 major 下新增 current，并把前一份保留为 previous。
2. 删除/重命名字段、扩大 capability、改变 matcher/fallback/generation/policy 语义必须新 major 和独立 Roadmap。
3. 实现最多同时接受 current + 紧邻 previous；更旧或未知 schema fail closed，不猜测迁移。
4. manifest、registry、adapter、资产与 policy 版本必须作为一个兼容集合验证；不能混用不同 generation。
5. golden 修改必须同步 schema/example/reject vectors、测试 ID、Roadmap、Release scan 与回滚版本。

## 13. MR-001 可执行向量

### 13.1 接受

| ID | 输入 | 预期 |
|---|---|---|
| `RP-V1-VALID-FENCE` | §4 current plan | 接受；保留 safe HTML；产生一个 fence fact |
| `MF-V1-VALID-SVG` | §5 current manifest | 接受；registry key 为 `fence\0mermaid` |
| `RP-V1-EMPTY` | current plan，`extension_nodes=[]` | 接受；零 registry/asset/runtime 读取 |
| `RP-V1-FOUR-KINDS` | 构造四个唯一 node，kind 分别为四类且 source/bytes 合法 | schema 接受；只有已有专用 parser adapter 且已登记 matcher 的 node 可由生产解析链发射/路由 |

### 13.2 拒绝或安全回退

| ID | 变异 | 预期 |
|---|---|---|
| `RP-UNKNOWN-SCHEMA/KIND` | 未知 schema 或 kind | plan 拒绝 / node `unknown_kind`，Level A fallback 保留 |
| `RP-DUPLICATE-ID` | 同 plan 两个相同 node ID | 两 node 均不路由，`invalid_node` |
| `RP-BYTE-MISMATCH` | bytes 与 UTF-8 source 不一致，或 source 非法 UTF-8 | node 拒绝，不修猜内容 |
| `RP-STALE-REVISION` | node revision 或返回 generation 不一致 | `stale`，结果不落位 |
| `RP-OVER-BUDGET` | node/单项/总量/深度超过上限 | 对应 node/剩余 node fallback；无无界分配 |
| `RP-DOCUMENT-MANIFEST` | node 增加 manifest/module/path/url/option/capability/trusted | 拒绝额外字段，不能注册或扩权 |
| `MF-UNKNOWN-FIELD` | manifest 增加 module/url/renderer/trusted 或未知 capability | manifest 拒绝，extension disabled |
| `MF-WILDCARD-MATCHER` | matcher 为 regex、glob、前缀、大小写 alias 或含参数 | manifest 拒绝 |
| `MF-DUPLICATE-MATCHER` | manifest 内重复，或两个 manifest 占同 key | registry conflict；双方禁用，不按顺序选胜者 |
| `MF-UNLOCKED-VERSION` | `latest`、range、空 version | manifest 拒绝 |
| `MF-CAPABILITY-ALLOW` | network/file/dynamic_code/external_process/export 非 deny，或未审核 manifest 请求 `page-local`/未知 interaction | v1 manifest 拒绝 |
| `MF-UNKNOWN-OUTPUT/POLICY` | output 或 policy version 未知 | extension disabled |
| `MF-ASSET-ROUTE` | asset ID 为路径/URL，或 manifest 资产出现穿越/query/fragment | manifest/请求拒绝，无文件或网络读取 |
| `OUT-ACTIVE-CONTENT` | HTML/SVG 含 script/event/foreignObject/外链/CSS URL | `output_rejected`；不能放宽 policy |
| `REG-PARTIAL-PUBLISH` | 一项校验失败后尝试使用其他已插入项 | 整个新 registry 不发布，继续 previous 或全部关闭 |

MR-001 的实现测试必须逐项实例化这些向量，并断言失败不修改 Level A HTML、不产生资产/网络/文件读取、不留下 pending/cache 项。

## 14. 与后续任务的接口

- `MRT-02`：实现 §4 facts，证明 CommonMark/GFM golden 逐字节不变。
- `MRT-03`：实现 §5 registry 与 §13 manifest/冲突向量。
- `MRT-04`：实现 §7～10 的 asset loader、预算、cache、generation 和清理。
- `MDV-14..20`：只注册/消费 Mermaid 专用 manifest/adapter，不复制通用 framework。
- `MRT-05..09`：Code Highlight/KaTeX 各自增加专用语法、安全与供应链契约，不扩大 v1 capability。

任何后续任务若需要文档 manifest、动态模块、网络、文件、脚本、导出、投屏、Agent 或跨文档交互，必须先新建独立 Roadmap 并升级契约；不得在 adapter 内绕过本文件。
