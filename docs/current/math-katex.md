# KaTeX 数学语法与供应链契约

版本：`math-katex-assets-v1`

日期：2026-08-28

状态：MRT-07 冻结；尚未改变 MDV 生产语法。MRT-08 只能消费本文与 `third_party/katex/manifest.json` 的闭包，不得自行扩大定界符、命令、模块或权限。

## 1. 选型与版本

采用官方 [`katex@0.18.4`](https://github.com/KaTeX/KaTeX/releases/tag/v0.18.4)，MIT，固定上游 tag `v0.18.4`/commit `49dc3d986747fd7d3bb25b597bcb98b071ae6035`。该版本晚于 `0.18.2` 的 settings prototype-pollution 修复；升级必须重新审查 output policy、CSS class、命令集、字体、包体和 MR-005，不跟随 `latest`。

官方 npm 包声明 `commander@^8.3.0`，但它只被 CLI 使用。选择的 `dist/katex.mjs` 是自包含 ESM，无 import/runtime dependency；Commander、`cli.js`、Node adapter 与全部 contrib 都不进入产品闭包。

来源与完整性：

- npm tarball：`https://registry.npmjs.org/katex/-/katex-0.18.4.tgz`
- npm integrity：`sha512-IMPntbRLOU+eu88XDiFKqQ8Akhr9Tv7jDMXqPhjG9SI1JMA4DIgXk4x9k4skJz2NZJXBRbC+2pYBLj9olqcZow==`
- tarball SHA-256：`0090b1ebccc77d1402ec95e85ee539e1da514d6cd6934156c00baf39dcb0e3aa`
- 官方安全/option 事实源：[Security](https://katex.org/docs/security)、[Options](https://katex.org/docs/options)、[Supported Functions](https://katex.org/docs/supported)

## 2. Markdown 数学语法

数学是 Level B 扩展，不属于 CommonMark/GFM。MRT-08 只能在现有 md4c callback/fact 层识别下列两种形式；extension 未启用、识别失败或渲染失败时，Level A 原文及定界符保持可见。

### 2.1 行内

唯一形式为 `$...$`：

```md
质能关系是 $E = mc^2$。
```

规则：

- opener/closer 都是未转义的单个 `$`，不能属于 `$$`；奇数个紧邻前导反斜杠表示转义，偶数个不转义。
- opener 后、closer 前必须是非空白字符；内容非空，不跨 `LF`、段落或 block 边界，不允许未转义的嵌套 `$`。
- opener 前若是 ASCII 字母/数字/下划线则不启动，closer 后若是 ASCII 字母/数字/下划线则不闭合，避免 `US$5`、`x$y` 等词内误判；显式 `$5$` 仍是合法公式。
- code span、fenced/indented code、链接 destination、HTML-like 原文和已被其他 extension 拥有的 source range 内不识别。
- `\$` 在普通文本或公式 source 中表示字面美元，不形成新定界符。

### 2.2 块级

支持独占行多行形式：

```md
$$
E = mc^2
$$
```

以及单行形式：

```md
$$ E = mc^2 $$
```

规则：

- opening `$$` 必须是根级行前 0～3 个空格后的首个非空白 token；不识别列表、引用、表格、代码块或其他容器内的 `$$`。
- 多行 opening/closing 行除 0～3 个前导空格与 `$$` 外不得有内容；公式 source 不包含定界行，不允许跨空段，closing 缺失时整段保持普通 Markdown。
- 单行形式要求 closing `$$` 后仅有空白，source 非空；同一行内额外未转义 `$$` 使该候选失败关闭。
- 单行形式去掉定界符内侧的 ASCII space/tab 后得到 source；多行形式移除 opening/closing 行并以 `LF` 连接中间行，不保留 closing 前的最后换行，其他行内缩进逐字节保留。
- block source 可含普通换行但不得含 NUL/受禁控制字符。`\(...\)`、`\[...\]`、裸 `\begin{...}`、AsciiMath 与 Mermaid 内部公式不触发本扩展。

### 2.3 预算与冲突

- 单公式 UTF-8 source `<= 64 KiB`、token `<= 8192`、花括号深度 `<= 64`；文档节点数与总 source 仍受 `markdown-runtime-v1` 的 1024 节点/2 MiB 总预算。
- range 按 source offset 排序且不得重叠；block 优先于 inline，code/link/既有 extension range 优先于 math。任何歧义、越界、UTF-8/source-byte 不一致都 fail closed 为 Level A 文本。
- 扫描必须线性、有界；不得使用灾难性回溯 regex、第二 Markdown parser 或 DOM 文本反推。

## 3. 固定渲染 policy

MRT-08 调用 KaTeX 时每个公式创建新的 null-prototype 空 macro 对象，固定 option：

```text
output="htmlAndMathml"
throwOnError=true
strict="error"
trust=false
globalGroup=false
maxSize=16
maxExpand=256
displayMode=<仅由 inline/block fact 决定>
macros=<每次 render 新建的空对象>
```

`throwOnError=true` 是刻意选择：官方说明异常/错误 tooltip 可能包含原始 LaTeX；adapter 只把异常转换为 Browser-owned 本地化错误码/卡片，不显示上游 message、stack、路径或整篇文档。公式原文只能作为已转义的局部 fallback，由 MDV 自己落位。

Browser-owned preflight 按 ASCII command name 大小写无关拒绝：

```text
href url includegraphics htmlClass htmlId htmlStyle htmlData
def gdef edef xdef let futurelet newcommand renewcommand providecommand global
csname endcsname expandafter noexpand
```

并拒绝所有以 `html` 开头的 command（包括未来版本新增的名字），而不只依赖当前枚举。

第一组会引入 URL/资源/HTML attribute；第二组会创建、持久化或间接构造宏并扩大审计面。`trust=false` 与 fresh macro object 是第二道门禁，不能代替 preflight。文档、Profile、tab、公式之间不共享 macro 状态；不提供用户自定义宏配置。

## 4. 输出安全边界

KaTeX 返回的 HTML/MathML 始终是不可信候选，不能直接写 `innerHTML`。MRT-08 必须：

- 在 detached parser 中解析后，以 Browser-owned DOM API 重建；tag/attribute/class/style 均为版本化闭合白名单，未知项整公式拒绝。
- 只允许 KaTeX 0.18.4 必需的 HTML、MathML 与内联 SVG geometry；禁止 `script/style/link/meta/base/iframe/object/embed/img/audio/video/form`、事件属性、`href/src/srcset/action`、外部 URL、`data:`/`file:`/`javascript:`、`foreignObject` 与 CSS `url()/@import/expression/var()`。
- class 必须属于 KaTeX 0.18.4 受审前缀/枚举；style 逐 declaration 解析，只接受 KaTeX 布局所需的有限属性与有限数值/unit，不按字符串透传。
- `annotation encoding="application/x-tex"` 中的公式内容仍作为文本节点重建；不得从候选读取权限、路径、module、option 或 resource ID。
- candidate HTML/DOM node/depth/attribute/style/output bytes 有独立命名上限；解析/预算/落位/generation 失败只回退当前公式，不影响普通 Markdown 或其他公式。

具体 tag/class/style 集必须由 MRT-08 的真实 0.18.4 golden 生成并人工审查后写入生产 policy；本文不以“KaTeX 官方称安全”替代 Browser-owned sanitizer。

## 5. 离线资产闭包

`third_party/katex/manifest.json` 是机器事实源，共 23 个受管资产、`885,374` bytes（上限 2 MiB）：

- `katex.mjs`：`601,882` bytes；自包含 ESM。
- 确定性 `katex.min.css`：`22,593` bytes。由官方 CSS 精确移除 WOFF/TTF fallback，只留下 20 条 WOFF2 URL；不改变其他 selector/declaration。
- 20 个官方 WOFF2 字体：`259,792` bytes。
- MIT LICENSE：`1,107` bytes。

不包含 `katex.min.js` UMD、CLI、Commander、auto-render、copy-tex、mhchem、mathtex-script-type、render-a11y、源码、类型、测试、文档、demo、source map、WOFF、TTF 或网络 fallback。普通构建/运行不执行 npm，不读取 tarball，不访问公网。

## 6. 更新与复验

离线门禁：

```bash
node tools/katex/vendor.mjs --check
node --test tools/katex/vendor.test.mjs
```

经独立 Roadmap 批准的更新才可执行：

```bash
node tools/katex/vendor.mjs --archive <approved-katex.tgz>
```

`--download` 仅是显式维护入口。工具在写入前验证双 archive hash、gzip/tar checksum、path/type/count/size、package identity/exports/CLI-only dependency、选择源文件 hash、ESM closure、CSS transform/font URL 闭包与所有输出 hash/bytes；先在同目录临时树离线复验，再原子替换固定 vendor root。missing/extra/tamper、symlink、未知 MIME/font、外链 CSS 或预算超限均 fail closed。

## 7. MRT-08 接口

MRT-08 只实现本文已经冻结的 facts、adapter、资源路由、output policy、lazy/cache/generation 和局部错误 UI。不得新增定界符、宏、命令、contrib、字体、网络、文件或持久化状态。普通/无公式文档必须零 KaTeX runtime/CSS/font 请求；字体只经 `crayon://mdv` manifest 精确路由按 CSS 需要加载。
