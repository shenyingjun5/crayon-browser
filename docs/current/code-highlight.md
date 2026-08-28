# Code Highlight 供应链与语言契约

版本：`code-highlight-assets-v1`

状态：MRT-05 供应链冻结；MRT-06 已在 macOS 完成运行时接入与验证，Windows 真机复验待远程会话补齐。后续任务只能消费本文与 `third_party/highlight/manifest.json` 的闭包，不得自行扩大语言、模块或权限。

## 1. 选型结论

选择官方浏览器资产包 [`@highlightjs/cdn-assets@11.12.0`](https://www.npmjs.com/package/@highlightjs/cdn-assets)，固定为 BSD-3-Clause。只 vendor ESM `core.min.js`、25 个明确 grammar 和原始 LICENSE；不引入 npm runtime dependency、全语言 bundle、auto-detect、plugin、worker、theme、图片、source map 或在线 CDN。

2026-08-28 按官方仓库/包元数据比较：

| 候选 | 核验版本 | 许可/运行闭包 | 浏览器与主题特性 | 结论 |
|---|---|---|---|---|
| highlight.js | `@highlightjs/cdn-assets 11.12.0` | BSD-3-Clause；预构建浏览器包零 runtime dependency；core/grammar 可独立 ESM 加载 | class-based token；可由 MDV 自有浅/深主题统一着色；无需 WASM | **采用**。最符合固定离线闭包、按语言 lazy 与小首载 |
| Prism | `prismjs 1.30.0` | MIT；零 runtime dependency | class-based token、语言组件依赖图可拆分 | 不采用。官方明确 v1 当前只接收 security-relevant PR，v2 尚未形成可锁定替代，继续压在 v1 会增加迁移窗口 |
| Shiki | `shiki 4.4.3` | MIT；主包元数据有 8 个 runtime dependency，另有 TextMate grammar 与 JS/WASM engine 闭包 | 编辑器级保真，主题通常参与 token 输出；浏览器冷启和闭包更大 | 不采用。适合构建期或高保真展示，不符合本任务的轻量 session runtime 与 MDV 自有主题边界 |

上游事实来源为各自官方 [highlight.js 仓库](https://github.com/highlightjs/highlight.js)、[Prism 仓库](https://github.com/PrismJS/prism) 和 [Shiki 仓库](https://github.com/shikijs/shiki)；最终可发布事实以仓库内已验证 tarball、LICENSE 与 manifest 为准，不依赖网页在构建时仍可访问。

## 2. 固定闭包与包体

- npm integrity：`sha512-KvOKXODaiFmId9xaq3xc5xCL66wVLUuOngDbO9B/kewbFTqdGbn2nJxNhN3H5R1cgDTVj6R8vH0zgiNDEGjpDw==`。
- tarball SHA-256：`b8a006d30f45afe783072569f3d69c5b60c0e7b9ca28cd474e12f2584e2a3bd9`。
- Runtime JavaScript：core `20,501` bytes + grammar `102,570` bytes = `123,071` raw bytes；随包 LICENSE `1,514` bytes；受管闭包合计 `124,585` bytes，低于 `512 KiB` 上限。
- 首次高亮只加载 core 和请求语言的 dependency closure；普通 Markdown、未知语言和纯文本不得加载 core/grammar。
- MDV 浅/深主题 CSS 由 MRT-06 使用现有设计 token 编写，不复制第三方 theme，因此主题切换不需要重载 grammar。

`third_party/highlight/manifest.json` 是语言、别名、嵌套 grammar 依赖、逐文件 bytes/hash 的机器事实源。`tools/highlight/vendor.mjs` 同时把固定 hash 编译进校验器，避免只修改 manifest 就绕过离线检查。

## 3. 语言与别名

Canonical grammar 固定为：

```text
bash c cpp csharp css diff dockerfile go graphql java javascript json kotlin
markdown objectivec php powershell python ruby rust sql swift typescript xml yaml
```

常见 fenced info 别名按 manifest 精确映射，包括 `sh/shell/zsh`、`c++/cc/cxx`、`c#/cs`、`docker`、`js/jsx/mjs/cjs`、`md`、`objective-c/objc`、`ps1/pwsh`、`py`、`rb`、`rs`、`ts/tsx/mts/cts`、`html/svg` 与 `yml`。比较前只允许 ASCII 小写规范化；不做模糊匹配、扩展名推断或自动检测。

`plaintext/text/txt/plain/nohighlight` 是虚拟纯文本别名，不对应 grammar 资产。空 info、未知 info、非法/超长 token、未注册 grammar 或加载失败都保持 md4c 已转义的普通 `<code>` 文本，不猜测语言。

嵌套 dependency 也固定并经过资产扫描：Dockerfile→Bash，Markdown→XML，YAML→Ruby，JavaScript/TypeScript→CSS+GraphQL+XML，XML→CSS+JavaScript。循环闭包只允许同一 session 去重加载，不能递归重复 import。

## 4. 安全与输出边界

- MRT-06 只能调用 explicit-language 文本 API；禁止 `highlightAuto`、`highlightAll`、DOM 扫描和从文档选择模块名/路径。
- fenced source 始终是数据。不得把 source 拼进 script/module；highlight.js 返回值仍是不可信候选，只允许受审的 `span` 与固定 `hljs-*` class，经 SafeHtml policy 后进入 Browser-owned placeholder。
- 不允许 inline style、事件处理器、URL、图片、SVG、script、iframe、object、HTML passthrough 或第三方 plugin。未知 class/token 删除而不是扩权。
- 资产中没有 `fetch`、XHR、WebSocket、dynamic import、存储或 Cookie 调用。上游 core 保留两条只用于 `console.warn` 的 GitHub 文档字符串，它们不是资源引用或网络请求；CSP 仍维持 `connect-src 'none'`。
- 单 block/source/deadline/concurrency/cache/generation 继续受 `markdown-runtime-v1` 约束；grammar 自身不能扩大 budget、能力或错误集合。

## 5. 更新与离线复验

普通构建、测试和运行只读取 checked-in 文件，零 npm/网络：

```bash
node tools/highlight/vendor.mjs --check
node --test tools/highlight/vendor.test.mjs
```

维护者更新必须先在独立 Roadmap 重新比较版本/许可/安全/包体，修改脚本中的固定版本、双 hash 与逐资产 hash，再显式执行：

```bash
node tools/highlight/vendor.mjs --archive <approved-tarball.tgz>
```

`--download` 只是显式维护动作，不进入 CMake、CI 普通门禁或产品运行路径。脚本在写入前验证压缩/解压大小、SHA-512、SHA-256、tar checksum、path/type/count/entry budget、包名/版本/许可/零 runtime dependency、选择集与 nested dependency；以同目录临时树验证后再原子替换固定 vendor 根。

## 6. MRT-06 运行时接入

- CMake 在 configure 阶段锁定 manifest schema、包名、版本、许可、关闭 auto-detect、零 runtime dependency 与 25 个 grammar，并把 adapter/core/grammar 编译进只读资产 catalog；运行期不读取 vendor 文件、不访问 npm/网络。
- `RenderHighlightDocument` 复用唯一 md4c `MarkdownRenderPlan`，只为精确 allowlist fence 增加 Browser-owned inert marker；alias 在路由前归一为 canonical ID，纯文本、未知、解析/registry/marker 失败均保留原 Level A `<pre><code>`。
- `crayon://mdv/runtime/highlight/<resource-id>` 只接受无 credential/port/query/fragment 的 lower-kebab 精确资源 ID，handler 再按不可变 catalog 精确命中；未知 ID 返回 404。
- 页面使用 `IntersectionObserver` 在 block 接近 viewport 时才加载 adapter/core/dependency closure/grammar；module promise 在页面 session 内去重。亮暗主题仅由 MDV 自有 `hljs-*` token CSS 切换，不重载 grammar。
- adapter 只调用 `highlight(source, {language, ignoreIllegals: true})`。返回候选先受 2 MiB、64 层、32768 节点预算约束，再只以 `createTextNode`/`createElement("span")` 重建；仅保留 `hljs-[a-z0-9_-]+` class，不使用第三方 `innerHTML`。
- 编辑 revision 或文档 generation 变化会换新 node ID；异步结果落位前复核 DOM 仍连接、node ID 与原始 `textContent` 均未变化，迟到结果直接丢弃。
