# get-video 设计方案

> 版本：v0.3 · 状态：设计稿（仅方案，未进入开发）
> 参考项目：LibreTV（`/home/user/work/LibreTV`）

## 1. 定位与边界

**本模块只做两件事：视频地址解析 + 本地 relay 流服务。** 它是投屏应用（Rust + Tauri PC 客户端）内部的一个独立模块，**不负责投屏协议**（DLNA/Chromecast 等由应用侧实现）。

- **输入**：一个网页 URL。
- **输出（一）**：解析出的视频流地址列表（协议、清晰度、所需请求头、DRM 标记）。能直接在线播放的直链，应用侧可拿去直接推送播放。
- **输出（二）**：一个本地 HTTP relay 服务。对有防盗链、跨域限制或需要改写 m3u8 的流，提供 `http://127.0.0.1:<port>/proxy/...` 形式的本地可播放地址，任何播放器（或投屏设备经局域网）都能直接拉流。

**验收标准**：模块自己起一个 HTTP 服务，本地播放器（ffplay / mpv / hls.js 测试页）能通过 relay 地址正常播放提取到的视频——即视为打通。

**DRM 原则：不碰。** 检测到版权保护（DRM 加密）的内容，标记拒播，不解密、不绕过。

## 2. 为什么不用 yt-dlp

**许可证没问题，工程上不匹配。**

- yt-dlp 采用 [Unlicense](https://github.com/yt-dlp/yt-dlp)（公共领域捐献），可商用、可闭源分发、无需署名，许可零障碍。
- 但放进 Tauri 桌面应用有三个工程问题：
  1. **体积与打包**：需捆绑 Python 运行时或独立二进制（十几 MB）；
  2. **更新节奏冲突**：yt-dlp 的价值在上千个 extractor 的高频更新（网站改版即失效，社区周更），捆绑进应用 = 频繁发版，或运行时自动更新第三方二进制（供应链与审核风险）；
  3. **能力错位**：Tauri 自带 webview，网页嗅探我们有更顺手的牌（见 L2）。
- **定位**：yt-dlp 仅作开发期参考实现与「正确性基准」——自研解析器的结果与它对拍验证，不进正式产品。

## 3. 自研提取引擎（三层架构）

输入网页 URL，输出统一视频流列表。三层逐级升级，能快则快：

### L1 静态快路径（Rust）

- reqwest 拉取页面 HTML（桌面浏览器 UA、gzip、超时控制）；
- 依次尝试：
  - 正则扫描直链：`.m3u8` / `.mp4` / `.mpd`，含转义还原（`https:\/\/`、`%3A%2F%2F`）；
  - `<video src>` / `<source src>` / `data-src` 标签；
  - JSON-LD `VideoObject.contentUrl`；
  - 常见播放器内嵌配置 JSON（如 maccms 类站点的 `player_aaaa` 变量）；
- 去重、按协议分类（hls / dash / mp4），从 URL 或上下文文本推测清晰度。
- 命中即返回，秒级内。不命中进入 L2。

### L2 隐藏 webview 网络嗅探（Tauri 差异化能力）

- Tauri 创建**不可见窗口**真实加载目标页面，等 JS 执行、播放器初始化；
- 拦截网络请求（注入 JS 劫持 `fetch`/`XHR`/`<video>` src 赋值，或自定义协议拦截），抓取 `.m3u8` / `.mp4` / `.mpd` 请求及其实际请求头（Referer、UA、Cookie 上下文）；
- 相当于**内置无头浏览器**，覆盖 JS 动态渲染、懒加载播放器；
- 嗅探超时（如 15s）未抓到媒体请求 → 判定该页无公开视频或需登录，如实返回。

> 2026-08-01 实现备注（demo/ 已验证）：注入方式为 `initialization_script`（document start），五路 hook：`fetch`、`XMLHttpRequest.open`、`HTMLMediaElement.src` setter、`MutationObserver`（video/source 的 src/data-src）、`PerformanceObserver` 兜底；命中经 `event.emit('sniff-found')` 上报（远程页 IPC 需在 capabilities 配 `remote.urls` 放行），另有 Image beacon 兜底通道；收集窗口 12s 上限、首个命中后再等 3s 收尾；结束后关窗，对每条结果跑 DRM 检测与清晰度推测并生成 relay_url。Linux 构建基线为 WebKitGTK 2.40+，使用 Tauri/wry 官方依赖链，不再保留 2.38 兼容层。
>
> 2026-08-02 追加：第六路 **Worker hook**——包装 `window.Worker`，classic worker 改为「嗅探 shim + importScripts(原脚本)」的 blob worker（shim 在 worker 作用域 hook fetch/XHR，命中 postMessage 回主线程）；module worker / blob: 脚本 / 异常回退原构造器。解决央视频类「WASM + Worker 内拉流」站点主线程 hook 不可见的盲区；`yangshipin.cn/tv/home` 直播流已实测嗅出并经 relay 播放验证。

### L3 站点专用解析器 + 规则热更新

- 对重点站点（B站、微博、各 maccms 资源站等）写专用 extractor：走站点公开 API 或页面内嵌 state JSON（`__INITIAL_STATE__`、`__playinfo__` 等），返回结构化多清晰度列表；
- 解析规则以**远程规则包**（签名 JSON / JS 片段）下发，启动时检查更新——网站改版只更规则包，不全量发版；
- 解析器同步携带该站所需 Referer/UA，供 relay 伪造防盗链头。

### 统一返回结构

```json
{
  "title": "视频标题",
  "webpage": "输入的原始 URL",
  "source": "static | webview | site-api",
  "formats": [
    {
      "url": "https://.../index.m3u8",
      "protocol": "hls",
      "quality": "1080p",
      "drm": false,
      "headers": { "Referer": "https://...", "User-Agent": "..." },
      "relay_url": "http://127.0.0.1:8321/proxy/<编码URL>?referer=..."
    }
  ]
}
```

`relay_url` 由模块直接拼好，调用方拿来即用。

## 4. DRM 检测与拒播策略

任何一层解析出结果后，先过 DRM 检测，命中即标记 `drm: true`，调用方拒播：

| 检测点 | 特征 |
|---|---|
| HLS（m3u8） | `#EXT-X-KEY:METHOD=...` 且非 `NONE`；`KEYFORMAT` 含 FairPlay / Widevine / PlayReady 标识；`SAMPLE-AES` |
| DASH（mpd） | `<ContentProtection>` 元素、Widevine/PlayReady UUID |
| 站点名单 | 已知全 DRM 站点直接前置标记 |

**只做检测，不做解密，不接 CDM。**

## 5. 本地 relay 服务（Rust + axum）

模块内嵌 HTTP 服务，默认监听 `127.0.0.1:8321`（可配 `0.0.0.0` 供局域网设备拉流）。核心是 `GET /proxy/<编码URL>`，设计借鉴 LibreTV serverless 版并补齐其短板：

- **m3u8 内容重写**：Content-Type 含 `mpegurl` 或内容以 `#EXTM3U` 开头时逐行处理——分片行、`#EXT-X-KEY`/`#EXT-X-MAP`/`#EXT-X-MEDIA` 的 URI 转绝对地址后改写回 `/proxy/<编码URL>`，并透传 referer 参数；主播放列表（多码率）保留结构，清晰度选择交给播放器。递归深度限 5 层。（实现补充：改写后的地址追加装饰性文件名后缀 `/proxy/<编码URL>/<文件名>`，兼容对 URL 扩展名严格校验的播放器，如打了 CVE-2023-6604 补丁的 ffmpeg；服务端解析时忽略后缀。）
- **Range 透传**（LibreTV 两个版本都缺，必须补）：客户端 `Range` 头转发上游，206/`Content-Range`/`Accept-Ranges` 原样回传——mp4 拖动进度条依赖它。（实现修正：**m3u8 不透 Range**——重写后字节必然变化，上游 206 的 Content-Range 基于原始长度，回传会导致客户端截断正文；播放列表一律全量拉取并重写后整体返回。Range 透传只对分片/mp4 等非重写内容生效。）
- **防盗链伪造**：UA/Referer 按提取结果中的 `headers` 设置（经 query 参数传递），缺省取目标 origin；不转发客户端 Cookie。
- **响应头净化**：删 `content-security-policy`、`set-cookie`、`x-frame-options`，避免重复 CORS 头；统一加 `Access-Control-Allow-Origin: *` 并处理 OPTIONS 预检（hls.js 测试页需要）。
- **真流式**：非 m3u8 内容 stream + 转发，不全量进内存；超时与有限重试（实现口径：连接超时 10s + 读空闲超时 10s，建连/发送失败重试 2 次；**不用请求总超时**——会掐断大文件与慢速分片流）。
- **SSRF 防护**（沿用 LibreTV 思路）：仅 http/https，屏蔽 localhost、`127.*`、`10.*`、`192.168.*`、`172.16-31.*`、`169.254.*` 等内网/保留段。
- **辅助路由**：
  - `GET /api/extract?url=...` → 提取接口（供命令行/测试页/应用侧调试）；
  - `GET /player?url=...` → 内置 hls.js 极简播放页（仅作联调与验收用，不是产品 UI）；
  - `GET /health` → 存活检查。

## 6. 模块接口（供应用侧集成）

对外暴露两种集成方式，应用侧任选：

1. **Rust crate 直接调用**：`extract(url) -> Result<VideoInfo>`、`relay::start(config) -> RelayHandle`；
2. **HTTP API**：模块以 sidecar/常驻进程方式运行，应用侧走 `/api/extract` 与 `/proxy`。

投屏对接方式（应用侧的事，此处只约定数据）：直推场景消费 `formats[].url`；需要中继的场景消费 `formats[].relay_url`（服务绑定局域网地址时，relay_url 自动用 PC 的局域网 IP 生成）。

## 7. LibreTV 参考结论（调研摘要）

调研对象：`/home/user/work/LibreTV`。

- **本地版 `server.mjs`（Express）**：`/proxy/:encodedUrl` 透明流式管道（stream+pipe），只转 UA，sha256(PASSWORD)+时间戳鉴权，SSRF 内网黑名单。**不重写 m3u8、不透 Range**——我们的 relay 必须补上这两点。
- **Serverless 版 `functions/proxy/[[path]].js`**：**m3u8 内容重写**是核心参考——分片行与 KEY/MAP 的 URI 转绝对地址后改写回代理路径；主播放列表自动选最高码率（我们改为保留多码率，选择权交给播放器）。
- **能力边界**：LibreTV 只对接现成采集 API，不做任意网页嗅探——提取引擎是我们自研的核心差异。
- 可复用思路：`/proxy/ + encodeURIComponent(url)` 约定、SSRF 黑名单、极简依赖哲学。

## 8. 模块划分与里程碑

```
src-tauri/src/（或独立 crate get-video/）
├── extract/
│   ├── mod.rs           # 提取编排：L1→L2→L3、DRM 检测、结果归一
│   ├── static_parse.rs  # L1 正则/标签/JSON-LD
│   ├── webview_sniff.rs # L2 隐藏窗口嗅探（已在 demo/ 的 Tauri 2 demo 中实现验证，待并入正式壳）
│   └── rules.rs         # L3 规则包加载/校验（本轮仅本地 JSON 骨架；热更新未实现）
├── drm.rs               # DRM 特征检测
└── relay/
    ├── mod.rs           # axum 服务生命周期
    └── proxy.rs         # m3u8 重写、Range、防盗链、SSRF
```

| 里程碑 | 内容 | 验收（本地播放器实测） |
|---|---|---|
| **M1** | L1 静态解析 + relay（m3u8 重写/Range/防盗链）+ `/api/extract` + 测试播放页 | 内嵌 m3u8 的测试页：提取成功，ffplay/mpv 经 relay 播放流畅，mp4 可拖动 |
| **M2** | L2 webview 嗅探 | JS 动态加载的视频页可提取并经 relay 播放 |
| **M3** | L3 规则包热更新 + 首批站点解析器 + DRM 检测标记 | 规则远程更新生效；DRM 内容正确标记拒播 |

每个里程碑的验收统一为：**本地起服务 → 提取 → 本地播放器经 relay 地址播放成功**。

## 9. 验证方案（测试矩阵）

详细测试用例（含实测可用的公网测试流地址与本地夹具设计）见 [docs/test-cases.md](./test-cases.md)。核心验证点：

| 验证点 | 方法 | 预期 |
|---|---|---|
| L1 快路径 | 内嵌 m3u8 直链的测试页 | 秒级返回 m3u8 地址 |
| m3u8 重写 | curl relay 地址 | 分片行变为 `/proxy/` 开头的本地地址 |
| Range | `curl -H "Range: bytes=0-1023"` 请求 mp4 relay | 206 + Content-Range |
| 防盗链 | 对校验 Referer 的源，带/不带 referer 对比 | 带 referer 返回 200 |
| 播放闭环 | ffplay / mpv / 测试页 hls.js 播 relay 地址 | 正常出画面、可拖动 |
| DRM 标记 | 已知 DRM 内容 | `drm: true`，不产出可播地址 |
| SSRF | 请求内网地址 | 400 拒绝 |

## 10. 范围外（明确不做）

- 投屏协议（DLNA/Chromecast/AirPlay 的发现与推送）——应用侧负责；
- 视频下载、转码、分轨合并；
- 任何形式的 DRM 解密或绕过（红线）；
- 产品级播放 UI（内置播放页仅用于联调验收）；
- 视频源搜索/频道化内容功能。

## 11. 合规与免责

- 不绕过付费墙，不处理 DRM 内容；检测到即标记拒播。
- 提取能力面向用户合法有权访问的公开页面；用户对自己输入的 URL 及播放行为负责。
- 模块不分发、不存储任何视频内容，仅作协议转换与本地中转。
