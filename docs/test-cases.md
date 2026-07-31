# get-video 测试用例

> 版本：v0.1 · 更新日期：2026-07-30
> 说明：所有公网 URL 均已用 curl 实测可达（HTTP 200 / 206，见各项标注）。
> 约定：**[在线]** 依赖公网资源，可能随时间失效；**[夹具]** 本地构造，确定性测试，CI 必须可跑。

## 1. 提取层（L1 静态解析）用例

| # | 用例 | 输入 | 预期 |
|---|---|---|---|
| E1 **[在线]** | 真实页面内嵌 `<source src>` MP4 | `https://www.w3schools.com/html/html5_video.asp`（HTML 内含 `src="mov_bbb.mp4"`，相对路径） | 提取出 `https://www.w3schools.com/html/mov_bbb.mp4`，protocol=mp4，相对地址被正确转绝对 |
| E2 **[夹具]** | 页面内嵌 m3u8 直链（含转义形式 `https:\/\/...`） | 本地 HTML：内联 JS 变量 `var url = "https:\/\/cdn.example.com\/a\/b.m3u8";` | 转义还原，提取出正常 URL |
| E3 **[夹具]** | URL 编码形式（`https%3A%2F%2F...m3u8`） | 本地 HTML | 解码后提取 |
| E4 **[夹具]** | JSON-LD VideoObject | 本地 HTML：`<script type="application/ld+json">{"@type":"VideoObject","contentUrl":"...mp4"}</script>` | 提取 contentUrl 及标题 |
| E5 **[夹具]** | maccms 风格播放器配置（`player_aaaa` 内嵌 JSON） | 本地 HTML 仿 maccms 页面 | 从配置 JSON 提取 m3u8 |
| E6 **[夹具]** | 无视频页面 | 纯文本 HTML | 返回空 formats，不报错 |
| E7 **[夹具]** | 多个重复/多清晰度链接 | 含 3 个相同 m3u8 + 不同 quality 关键词的页面 | 去重后按清晰度排序 |

> 正则基准可对照 LibreTV `js/config.js` 的 `M3U8_PATTERN = /\$https?:\/\/[^"'\s]+?\.m3u8/g`；LibreTV 仓库本身不含真实视频 URL（内置源已清空，只剩 `example.com` 占位），无法直接提供提取用例。

## 2. relay 服务用例

### 2.1 m3u8 重写（核心，规则对标 LibreTV `functions/proxy/[[path]].js`）

| # | 用例 | 输入 | 预期 |
|---|---|---|---|
| R1 **[在线]** | Master 播放列表 + 相对路径子列表 | `https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8`（200，`audio/mpegurl`，含 240p~1080p 五档，子列表为 `url_0/...m3u8` 相对路径） | 每个 `#EXT-X-STREAM-INF` 的下一行改写为 `/proxy/<编码绝对URL>`；**保留多码率结构**，不自动选码率（与 LibreTV 的差异点） |
| R2 **[在线]** | 媒体播放列表 + 相对分片 + EXT-X-BYTERANGE | `https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8`（200，分片为 `main.ts` + `#EXT-X-BYTERANGE`） | 分片行改写为代理地址；BYTERANGE 标签原样保留 |
| R3 **[在线]** | Master 含 EXT-X-MEDIA（音频/字幕组 URI） | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8`（200，含 `EXT-X-MEDIA:TYPE=AUDIO/SUBTITLES,URI="..."`） | `EXT-X-MEDIA` 的 `URI="..."` 同样改写为代理地址 |
| R4 **[在线]** | AES-128 加密流（**非 DRM，应可播**） | `https://playertest.longtailvideo.com/adaptive/oceans_aes/oceans_aes.m3u8`（200；子列表含 `#EXT-X-KEY:METHOD=AES-128,URI="oceans.key"`，相对 key 地址） | `EXT-X-KEY` 的 `URI` 改写为代理地址，key 经代理可拉取；`drm` 标记 = **false** |
| R5 **[夹具]** | EXT-X-MAP（fMP4 初始化段） | 本地 m3u8 含 `#EXT-X-MAP:URI="init.mp4"` | MAP 的 URI 改写为代理地址 |
| R6 **[夹具]** | 递归嵌套 master | master 指向另一个 master | 正常递归；超过 5 层拒绝 |
| R7 **[夹具]** | Content-Type 非 mpegurl 但内容为 `#EXTM3U` | mock 上游返回 `text/plain` + m3u8 内容 | 按内容判定，仍走重写逻辑 |
| R8 **[夹具]** | 带 query 的分片地址（`seg.ts?token=abc&x=1`） | mock 上游 | query 保留，编码/解码不丢失参数 |

### 2.2 Range 与传输

| # | 用例 | 输入 | 预期 |
|---|---|---|---|
| R9 **[在线]** | MP4 Range 透传 | `https://www.w3schools.com/html/mov_bbb.mp4` 或 `https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/720/Big_Buck_Bunny_720_10s_1MB.mp4`（均实测 `-r 0-1023` 返回 **206**） | relay 返回 206 + `Content-Range` + `Accept-Ranges`；播放器可拖动 |
| R10 **[夹具]** | 上游不支持 Range | mock 上游忽略 Range 返回 200 | relay 透传 200，不伪造 206 |
| R11 **[在线]** | 直播流（无 EXT-X-ENDLIST，持续刷新） | `https://cph-p2p-msl.akamaized.net/hls/live/2000341/test/master.m3u8`（200） | 不重写内容以外的处理；分片流式转发不缓冲全量 |
| R12 **[夹具]** | 大文件流式 | mock 上游吐 100MB | relay 内存占用平稳（stream 转发，不全量入内存） |

### 2.3 防盗链与头部

| # | 用例 | 输入 | 预期 |
|---|---|---|---|
| R13 **[夹具]** | 校验 Referer 的上游 | mock 上游：无 Referer 返 403，带指定 Referer 返 200 | relay 带 `?referer=` 参数时伪造成功（200），不带时透传失败（403） |
| R14 **[夹具]** | 校验 UA 的上游 | mock 上游按 UA 区分 | relay 默认浏览器 UA 可通过 |
| R15 **[夹具]** | 敏感响应头净化 | mock 上游返回 `set-cookie` / `x-frame-options` / `content-security-policy` | relay 响应中这些头被删除 |
| R16 **[夹具]** | CORS | 浏览器跨域请求 + OPTIONS 预检 | 响应含 `Access-Control-Allow-Origin: *`，预检 204 |

### 2.4 安全

| # | 用例 | 输入 | 预期 |
|---|---|---|---|
| S1 **[夹具]** | SSRF 内网地址 | `/proxy/http%3A%2F%2F127.0.0.1%2F...`、`192.168.x`、`10.x`、`169.254.169.254`（云元数据） | 一律 400（黑名单对标 LibreTV `server.mjs` 的 `isValidUrl`） |
| S2 **[夹具]** | 非 http 协议 | `/proxy/file%3A%2F%2F%2Fetc%2Fpasswd`、`ftp://...` | 400 |
| S3 **[夹具]** | 畸形编码 URL | 非法 percent-encoding | 400，不 panic |

## 3. DRM 检测用例

| # | 用例 | 输入 | 预期 |
|---|---|---|---|
| D1 **[夹具]** | FairPlay（HLS） | 本地 m3u8：`#EXT-X-KEY:METHOD=SAMPLE-AES,URI="skd://...",KEYFORMAT="com.apple.streamingkeydelivery"`（Apple 官方 fps 样本已下线/403，用合成夹具） | `drm: true`，不产出 relay 地址 |
| D2 **[夹具]** | Widevine（HLS KEYFORMAT） | `KEYFORMAT="urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"` | `drm: true` |
| D3 **[在线]** | DASH 多 DRM | `https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey/Manifest_1080p.mpd`（200，实测含 `ContentProtection` cenc + PlayReady `urn:uuid:9a04f079-...`） | `drm: true` |
| D4 **[夹具]** | DASH VOD（无 DRM） | `https://dash.akamaized.net/akamai/bbb_30fps/bbb_30fps.mpd`（200，`application/dash+xml`，无 ContentProtection）也可作在线对照 | `drm: false` |
| D5 **[在线]** | AES-128（非 DRM 对照，同 R4） | oceans_aes | `drm: false`——**关键区分**：`METHOD=AES-128` 且 key 可公开拉取 ≠ DRM，应可播 |

## 4. 播放闭环验收（每里程碑必跑）

| # | 用例 | 操作 | 预期 |
|---|---|---|---|
| P1 | HLS 点播闭环 | 提取 R1 → `ffplay "http://127.0.0.1:8321/proxy/<编码URL>"` 或 `/player` 测试页 | 出画面，音画正常，可切换码率 |
| P2 | MP4 拖动闭环 | relay 地址喂给 mpv/ffplay，拖动进度条 | 拖动后 2s 内恢复播放（依赖 R9） |
| P3 | AES-128 闭环 | R4 的 relay 地址播放 | key 经代理获取，正常解密播放 |
| P4 | 直播闭环 | R11 relay 地址播放 3 分钟 | 持续出画面，播放列表刷新正常 |
| P5 | 防盗链闭环 | R13 mock 源 | 仅带 referer 的 relay 地址可播 |

## 5. 失效预案

> 2026-07-30 实测备注：R11/P4 主源 `cph-p2p-msl.akamaized.net/hls/live/2000341/test/master.m3u8` 的 master 仍返回 200，但其子列表 `master_1.m3u8` 返回 404（源站问题，直连同样 404）——已按本节替补原则改用 `https://test-streams.mux.dev/pts_shift/master.m3u8` 验证通过。

在线用例失效时的替补原则：

- HLS 测试流替补池：`test-streams.mux.dev`（hls.js 官方维护的一批）、`devstreaming-cdn.apple.com`、`demo.unified-streaming.com`，选当前可达者替换；
- MP4 替补池：`test-videos.co.uk`、`www.w3schools.com`（`commondatastorage.googleapis.com` 在本环境不可达，已弃用）；
- CI 中以 **[夹具]** 用例为硬门槛，**[在线]** 用例允许标记 skip。
