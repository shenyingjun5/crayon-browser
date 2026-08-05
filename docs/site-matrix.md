# 站点覆盖测试矩阵（2026-08-02）

测试方法：extract-cli（快速提取）→ 未命中自动 sniff-cli（深度嗅探）→ 央视家族候选自动解码探针实测。

测试环境：`./target/debug/get-video-app`（debug 构建），每次运行前 `pkill` 清理实例避免 8321/8377 端口冲突，单次运行上限 120 s。原始日志存于 `logs/{编号}_{extract|sniff}.log`。

说明：「探针实测」列本轮已扩展到非央视命中流（relay 播放探针 + curl/ffmpeg 交叉验证）。所有条目的 quality 字段均为空（当前实现未回填清晰度标签）。

| 站点 | 测试片 | 链路 | 结果数 | 协议/清晰度 | 受限/DRM | 探针实测 | 结论 |
|---|---|---|---|---|---|---|---|
| 央视网 CCTV | 《人民大街》第1集（纪录片点播） | extract 命中 | 1 | HLS / 未标 | 无 DRM、无受限、有 relay | 回传 3 帧、无错误 → 可播（日志措辞为「实测可播或无结论」） | 可播 ✅ 符合预期 |
| 央视网 CCTV | 《生活万岁》第7集（4K 专区老片） | extract 命中 | 1 | HLS / 未标 | restriction：分片列表 HTTP 404，流地址已失效；无 relay | 未运行（已标受限，跳过探针） | 受限-流地址失效（HTTP 404）✅ 符合预期 |
| 央视频 | 电视直播首页 | extract 0 → sniff 命中 | 1 | HLS（直播）/ 未标 | restriction：WASM 私有加扰，实测解码画面异常，无法播放；无 relay | 实测不可播（WASM 私有加扰，解码画面异常） | 受限-WASM 私有加扰 ✅ 符合预期 |
| 腾讯视频 | 《2026电视剧品质盛典》 | extract 0 → sniff 命中 | 1 | HLS（ltscsy.qq.com）/ 未标 | 未标 DRM、未标受限、有 relay | webview 有声音无画面（HEVC-in-TS，WebKit 不支持 TS 封装 HEVC）；ffmpeg 抽帧为真实画面（864x486） | 流未加密但 HEVC 编码：本地 webview 只出声，投屏给 VLC/电视可播 ⚠️（嗅探到的是 214s 片段级流，正片未验证） |
| 腾讯视频 | 《2025电视剧品质盛典》（VIP 标记） | extract 0 → sniff 未命中 | 0 | — | — | 未运行 | 未解析到：extract 页面标题为「那条视频不见了」，sniff 12 s 超时无命中（疑似下架或需登录，待验证） |
| 爱奇艺 | v_11r4zeip6ck | extract 0 → sniff 命中 | 1 | HLS（liveats-vod.video.iqiyi.com，qd_vip=0）/ 未标 | 未标 DRM、未标受限、有 relay | 分片直连 HTTP 405 `{"code":"D2102"}`（webview 同样 decode 失败） | 受限-分片签名鉴权（405 D2102）❌ |
| 爱奇艺 | v_18tntljwejo | extract 0 → sniff 命中 | 1 | HLS（meta-cdn.video.iqiyi.com，qd_vip=0）/ 未标 | 未标 DRM、未标受限、有 relay | m3u8 可拉取、无 EXT-X-KEY，但分片直连 HTTP 405 `{"code":"D2102"}` | 受限-分片签名鉴权（405 D2102）❌ |
| 优酷 | 《普通一兵》（老电影） | extract 0 → sniff 未命中 | 0 | — | — | 未运行 | 未解析到：sniff 12 s 超时无命中 |
| 优酷 | 《恋恋四季：2015 全球电影混剪》 | extract 0 → sniff 命中 | 2 | MP4 直链（vali-g1.cp31.ott.cibntv.net）/ 未标 | 未标 DRM、未标受限、有 relay | relay 实测：1920x1080、readyState=4、连播 14 s 无错误（moov 前置，Range 转发正常） | 可播 ✅（MP4 直链，全场最佳投屏候选） |
| 西瓜视频 | 朝鲜战争纪录片 | extract 0 → sniff 未命中 | 0 | — | — | 页态诊断：页面为字节 jsvmp 虚拟化反爬页（body 为空、`_ $jsvmprt`），播放器不加载 | 反爬拦截，已知限制 ❌（滑块验证无法无头通过，kazumi 同样不支持） |
| 西瓜视频 | 玲玲的飞行轨迹 | extract 0 → sniff 未命中 | 0 | — | — | 同上 | 同上 ❌ |
| 1905 电影网 | 《别哭！妈妈》 | extract 0 → sniff 命中 | 1 | HLS-fMP4（fmp4hd.vodfile.m1905.com）/ 480P | 无受限、有 relay | 播放器初始化并实际起播（页内进度 00:02/01:35:08） | 可播 ✅（2026-08-04 修复后命中，编码 AV1+AAC · fMP4） |
| 1905 电影网 | 《打过长江去》 | extract 0 → sniff 命中 | 1 | 同上 | 无受限、有 relay | 同上 | 可播 ✅ |

## 观察与结论

### 央视系（CCTV / 央视频）——回归全部符合预期
- 纪录片点播走 extract 静态解析即可拿到 HLS（cntv CDN），带 relay，探针回传 3 帧判定可播，未误标受限。
- 4K 专区老片能取到 HLS 地址但分片列表已 404，extract 阶段即正确标注「流地址已失效」且不挂 relay；已标受限的候选会跳过解码探针（日志中无 `[probe]` 行），行为合理。
- 央视频直播 extract 必然为空，依赖 sniff 兜底抓到直播 HLS；探针能识别 WASM 私有加扰并给出「实测不可播」的明确结论。央视家族的探针机制工作正常。
- 一个小观察：可播判定的日志措辞是「实测可播或无结论」，把两种状态合并在一行里，仅靠日志无法区分；需结合帧数/错误字段人工判断。

### 腾讯视频——流未加密但编码受限
- 2026 品质盛典 sniff 到 1 条 HLS（ltscsy.qq.com），实测分片无加密、ffmpeg 抽帧为真实画面，但编码是 HEVC-in-TS：WebKit webview 不支持 TS 封装的 HEVC，本地只能出声；投屏给 VLC/电视类播放器可正常播放。
- 注意嗅探到的是 214s 片段级流，正片是否同样干净未验证。
- 2025 品质盛典 extract 页面标题显示「那条视频不见了」，疑似已下架或需登录；sniff 超时无命中。VIP 内容未能验证。

### 爱奇艺——extract 无效，sniff 稳定命中但分片鉴权
- 两部影片 extract 均为 0 条（只取到站点首页式标题），sniff 均能抓到 1 条 HLS（`qd_vip=0` 参数），带 relay。
- 实测：m3u8 播放列表本身可拉取、无 EXT-X-KEY，但分片（`data.video.iqiyi.com` / `liveats-vod.video.iqiyi.com`）直连与经 relay 均返回 HTTP 405 `{"code":"D2102"}`——分片级签名鉴权（疑似绑定会话/时效），非播放器问题。判定为受限。

### 优酷——MP4 直链实测可播，全场最佳投屏候选
- 《普通一兵》两条链路均空。
- 2015 混剪 sniff 到 2 条 MP4 直链（cibntv CDN），relay 实测 1920x1080、moov 前置、Range 转发正常，webview 连播 14 s 无错误。是全场唯一拿到非 HLS 协议的站点，也是最成熟的投屏候选。

### 西瓜视频——字节 jsvmp 反爬，已知限制
- 页面是 jsvmp 虚拟化保护的反爬页（curl 抓到的是空 body + `_ $jsvmprt` 混淆脚本），真实内容要等反爬校验（含滑块）通过后才渲染，无头窗口无法通过。extract 与 sniff 均无解，kazumi 等同类工具同样不支持。除非后续接入带真实登录态的浏览器桥，否则列为已知限制。

### 1905 电影网——UA 修复后可播（2026-08-04）
- 之前零命中的根因不是登录墙：页态诊断显示播放器按 UA 判定「浏览器无法播放此视频」直接拒绝初始化（页面 0 个 video 元素）。
- 修复：嗅探窗口统一伪装桌面 Chrome UA（与 extract/relay 一致）后播放器正常初始化并起播，嗅探到 HLS-fMP4 流（`fmp4hd.vodfile.m1905.com`，带 tm/sign 签名），编码识别为 **AV1+AAC · fMP4**（1905 用 AV1 编码，投屏接收端需注意 AV1 支持度）。
- 同时过滤了 `_init.mp4`（DASH init 段非独立可播流）避免噪音命中。

### 嗅探能力增强（2026-08-04，借鉴 kazumi）
- fetch/XHR 响应体检测：克隆响应读首块，以 `#EXTM3U` 开头即按内容判定 HLS 上报（覆盖无 .m3u8 扩展名的清单接口）；上报带 proto 提示，归一化时 URL 无扩展名也能正确识别为 hls。
- iframe 嵌套地址抠取：监视 iframe src，正则抠 query 里的明文/percent-encoded 两级 .m3u8/.mp4/.mpd 地址（聚合站"解析页"模式）。
- 自动播放推进：每 2s 静音 play() 所有 video + 前 3 轮点击常见播放按钮（WKWebView 经 wry 默认已放开 autoplay 限制，但站点自定义播放键需要模拟点击）。
- 页态诊断：嗅探结束回传标题/video 数/媒体资源清单（beacon /diag → stdout），零命中站点可快速定位原因。
- 回归确认：央视频直播 WASM 受限判定不受影响；优酷多抓到 1 条 HLS（2 MP4 + 1 HLS）；腾讯 HEVC、央视纪录片、B 站番剧均不变。

### 总体
- extract 静态解析目前只对央视系真正有效；其余站点全部依赖 sniff 兜底。
- sniff 对腾讯（部分）、爱奇艺、优酷、1905 有效；西瓜受字节 jsvmp 反爬拦截为已知限制。
- 非央视命中流实测结论：「解析到地址」≠「可播」，且受限原因各异——腾讯是 HEVC 编码兼容性（流干净、投屏可播）、爱奇艺是分片签名鉴权（405 D2102，真受限）、优酷 MP4 直链完全可播、1905 是 AV1 编码（投屏需注意接收端 AV1 支持）。
- 受限判定三层机制（HLS 活性 / DRM 结构 / 解码探针）已全量生效：2026-08-04 起解码探针扩展到**所有**可播候选（不再限央视家族），爱奇艺分片 405 这类「清单能拉、分片鉴权」会在结果页直接标「流地址失效或加载失败」并禁点；HEVC/AV1/VP9 等 webview 解不了编码的候选自动跳过探针，避免「没画面 ≠ 流坏」误判（腾讯、1905 实测保持可播）。

### 7sefun（苹果CMS 聚合站）——嗅探全框架注入 + 广告误杀修复（2026-08-05）
- 链路：页面内嵌 `player_aaaa`（encrypt:2，base64+urldecode 双重解码）→ 二级线路页 `lmm85.com`（Cloudflare 拦截直连）→ 实际走第三方播放器 iframe `dp.no3acg.com/player/ec.php`（"超级播放器"，ArtPlayer 内核），正片地址 AES-128-CBC 加密在 ConFig.url（key 派生自 uid，iv 硬编码）。
- 零命中根因有两个，均已修复：
  1. 嗅探脚本此前只注主框架（`.initialization_script`），iframe 内播放器完全没 hook → 改用 `.initialization_script_for_all_frames`，iframe 内经 `postMessage` 向顶层框架代报命中（Tauri IPC 只注主框架、http beacon 在 https 页是混合内容，iframe 内均不可靠）。
  2. 广告 URL 过滤误杀正片：正片托管在快手 CDN `v1.adkwai.com`（338MB / 25 分钟 / H.264，`adVideoLp` 只是桶名），被广告域名正则误判丢弃。修复：URL 层只拦确定无疑的广告网络平台（doubleclick 等），广告判定改为看 DOM 上下文——广告容器（`.action-ad`/`#wyn`/`.pause-ad` 等）内的 video 才是贴片广告，上报跳过、nudge 快进；主播放器视频一律按正片处理。
- 实测：嗅探命中 1 条 MP4 正片（H.264，无 DRM，有 relay），Kazumi 对该站同样靠 webview 嗅探（规则 `useWebview:true`，无特殊提取逻辑）。
- 经验：「广告域名 ≠ 广告内容」，聚合站常把正片上传到有广告联盟 CDN 蹭免费存储/带宽，广告识别必须以播放器 DOM 结构为准。
