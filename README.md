# get-video

视频地址解析 + 本地 relay 流服务模块（投屏应用内部组件，设计见 `docs/design.md` v0.3）。

- **输入**：网页 URL → **输出一**：结构化视频流列表（协议/清晰度/所需请求头/DRM 标记/relay 地址）
- **输出二**：本地 HTTP relay，把有防盗链/跨域限制或需改写 m3u8 的流转成 `http://127.0.0.1:<port>/proxy/...` 本地可播地址
- **DRM 原则：不碰**。检测到 DRM（FairPlay/Widevine/PlayReady/DASH ContentProtection）只标记 `drm: true`，不解密、不产出 relay 地址

## 构建与运行

```bash
cargo build --release
./target/release/get-video                     # 默认 127.0.0.1:8321
./target/release/get-video --host 0.0.0.0 --port 8321   # 局域网设备可拉流
```

参数（均有对应环境变量）：

| 参数 | 环境变量 | 默认 | 说明 |
|---|---|---|---|
| `--host` | `GET_VIDEO_HOST` | `127.0.0.1` | 监听地址 |
| `--port` | `GET_VIDEO_PORT` | `8321` | 监听端口 |
| `--rules` | `GET_VIDEO_RULES` | 无 | L3 站点规则包本地 JSON 路径 |
| `--allow-private-hosts` | `GET_VIDEO_ALLOW_PRIVATE` | 关 | **测试钩子**：关闭 SSRF 黑名单，仅本地调试用 |
| 无（仅环境变量） | `GET_VIDEO_BILI_COOKIE` | 无 | B 站登录态 Cookie 整串，解锁登录/会员清晰度（见下「站点登录态」） |

## HTTP API

### `GET /api/extract?url=<页面URL>`

L1 静态提取 + L3 规则包 + DRM 检测，返回统一 JSON：

```json
{
  "title": "HTML Video",
  "webpage": "https://www.w3schools.com/html/html5_video.asp",
  "source": "static",
  "formats": [{
    "url": "https://www.w3schools.com/html/mov_bbb.mp4",
    "protocol": "mp4",
    "drm": false,
    "headers": {"Referer": "https://www.w3schools.com", "User-Agent": "..."},
    "relay_url": "http://127.0.0.1:8321/proxy/<编码URL>/mov_bbb.mp4?referer=..."
  }]
}
```

### 站点登录态（Cookie）

部分站点的清晰度/内容权限依赖登录态。当前支持 **B 站**：设置环境变量
`GET_VIDEO_BILI_COOKIE` 后，B 站解析器在调 `api.bilibili.com` 播放 API 时
携带该 Cookie，可解锁登录清晰度（1080P）与你账号的会员内容（按你自己账号的权限）：

```bash
# 浏览器登录 bilibili.com → 开发者工具 → Network → 任一 api 请求 →
# 复制请求头 Cookie 整串（至少含 SESSDATA）
export GET_VIDEO_BILI_COOKIE='SESSDATA=xxxx; bili_jct=yyyy; DedeUserID=zzzz'
./target/release/get-video   # 重新启动生效
```

安全口径：Cookie 是敏感凭据——它**只随提取请求发往 `api.bilibili.com`**，
不会写入返回结果，不会经 relay 转发到任何媒体 CDN，也不会发往其他站点；
relay 代理媒体流量时一律不携带任何 Cookie。请勿把含 Cookie 的环境共享给他人。

### `GET /proxy/<urlencoded目标URL>[/<文件名>]?referer=...&ua=...`

- **m3u8 重写**：分片行、`EXT-X-KEY`/`EXT-X-MAP`/`EXT-X-MEDIA` 的 `URI` 转绝对地址后改写回 `/proxy/` 并透传 referer/ua；master 保留多码率结构不自动选档；递归限 5 层（`depth` 参数内部传递）；Content-Type 含 mpegurl **或**内容以 `#EXTM3U` 开头都按 m3u8 处理。
- **文件名后缀**：重写后的地址带装饰性文件名后缀（`/proxy/<编码>/seg.ts`），兼容对 URL 扩展名严格校验的播放器（如打了 CVE-2023-6604 补丁的 ffmpeg）；服务端解析时忽略该后缀。
- **Range 透传**：仅对非 m3u8 内容转发客户端 `Range`；206/`Content-Range`/`Accept-Ranges` 原样回传（mp4 拖动依赖）。m3u8 因为要重写、字节必然变化，一律全量拉取，不回传 Range 相关头。
- **防盗链伪造**：UA 默认桌面浏览器（可用 `ua=` 覆盖），Referer 用 `referer=` 参数、缺省取目标 origin；不转发客户端 Cookie。
- **响应头净化**：删 `content-security-policy`/`set-cookie`/`x-frame-options`；统一 `Access-Control-Allow-Origin: *` + OPTIONS 预检（204）。
- **真流式**：非 m3u8 内容 stream 转发，不全量入内存；连接/读空闲超时 10s，建连失败重试 2 次。
- **SSRF 黑名单**：仅 http/https，拒 `localhost`、`127.*`、`10.*`、`192.168.*`、`172.16-31.*`、`169.254.*`、`[::1]` 等。

### `GET /health` / `GET /player?url=...`

存活检查 / 极简 hls.js 联调播放页（CDN 引 hls.js，非产品 UI）。

## 提取层

- **L1 静态快路径**（`src/extract/static_parse.rs`）：正则扫 `.m3u8/.mp4/.mpd`（含 `https:\/\/` 转义还原、`%3A%2F` 解码还原三种文本形态）→ `<video src>/<source src>/data-src` → JSON-LD `VideoObject` → maccms 风格 `player_aaaa` 配置（encrypt 0/1/2）。去重、协议分类、清晰度推测并降序排列，相对地址转绝对。
- **L2 webview 嗅探**：已在 `app/`（正式 Tauri 壳）与 `demo/`（验证 demo）实现。隐藏 webview 加载目标页，注入六路 hook（fetch/XHR/media src/MutationObserver/PerformanceObserver/Worker）脚本抓媒体请求（design.md §3 L2）。
- **L3 站点规则包**（`src/extract/rules.rs`）：**最小骨架**——从本地 JSON 文件加载（域名后缀匹配 + 带 `(?<url>...)` 命名分组的正则模板，可附 referer/ua）。**远程热更未实现**，后续里程碑 M3 再做（含签名校验）。
- **L3 站点专用解析器**（`src/extract/sites.rs`）：地址不在 HTML 文本里、需「提取视频 ID → 调站点公开 API → 解析 JSON」的站点，用 Rust 代码实现。已支持：
  - **央视网（tv.cctv.com / cntv.cn）点播**：页面提 `guid` → `vdn.apps.cntv.cn/api/getHttpVideoInfo.do?pid=<guid>` → 取 `hls_url` 与分段 mp4（`is_invalid_copyright=1` 版权受限不出结果）；夹具测试 E8 + 在线测试覆盖。
  - **B 站（bilibili.com）**：番剧 **ep 单集页 / ss 季页**（ss 从 HTML 取默认集 `ep_id`，playurl 不认 season_id）与**普通视频 BV 页**（`x/web-interface/view` 换 cid，多分 P 按 `?p=N` 选集，标题含分 P 名）。播放地址统一走 **fnval=1 整段 mp4 优先**（音画合一）；durl 为空时**兜底 fnval=16 DASH 分轨**（视频轨无声/音频轨无画面，note 提示需双轨合并）。`is_drm=true` 按红线不出地址；Referer 固定 `https://www.bilibili.com` 过 bilivideo 防盗链；可选环境变量 `GET_VIDEO_BILI_COOKIE` 携带用户自己的登录 Cookie 解锁更高清晰度（会员内容仍按其权限返回）。夹具测试 E9a-E9d + 在线测试覆盖。

## DRM 检测（`src/drm.rs`）

- HLS：`EXT-X-KEY` METHOD 非 NONE 时——`KEYFORMAT` 含 `com.apple`/`edef8ba9`/`9a04f079` 等标识或为非 identity → `drm: true`；`METHOD=SAMPLE-AES` → true；`METHOD=AES-128` 且无 KEYFORMAT（key 公开）→ **false（可播）**。
- DASH：含 `<ContentProtection>` → true。
- 已知 DRM 站点名单（netflix 等）前置标记。
- 提取 HLS 时若是 master 列表，会再下一层取第一个子列表检测。

## 测试

```bash
# 夹具测试（本地 mock 上游，确定性，CI 硬门槛）：E2-E7、R5-R8、R10、R12-R16、S1-S3、D1/D2/D4
cargo test

# 在线测试（依赖公网资源，可能失效）：E1、R1-R4、R9、R11、D3、D4-在线
cargo test --test online -- --ignored --test-threads=1

cargo clippy --all-targets   # 无 error
cargo fmt --check
```

### 播放闭环（P1-P5）

```bash
./target/debug/get-video --port 8321 --allow-private-hosts &   # P5 需要 allow-private
# P1 HLS 点播
ffmpeg -i "http://127.0.0.1:8321/proxy/$(python3 -c "import urllib.parse;print(urllib.parse.quote('https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8',safe=''))")" -t 5 -f null -
# P2 MP4 拖动（ffmpeg 会发 Range 请求）
ffmpeg -ss 3 -i "http://127.0.0.1:8321/proxy/$(python3 -c "import urllib.parse;print(urllib.parse.quote('https://www.w3schools.com/html/mov_bbb.mp4',safe=''))")/mov_bbb.mp4" -t 3 -f null -
# P3 AES-128（key 经代理拉取解密）
ffmpeg -i "http://127.0.0.1:8321/proxy/$(python3 -c "import urllib.parse;print(urllib.parse.quote('https://playertest.longtailvideo.com/adaptive/oceans_aes/oceans_aes.m3u8',safe=''))")/oceans_aes.m3u8" -t 5 -f null -
# P4 直播（主源失效时用替补 pts_shift）
ffmpeg -i "http://127.0.0.1:8321/proxy/$(python3 -c "import urllib.parse;print(urllib.parse.quote('https://test-streams.mux.dev/pts_shift/master.m3u8',safe=''))")" -t 10 -f null -
# P5 防盗链 mock 源（scripts/referer_server.py）
ffmpeg -f lavfi -i testsrc=duration=5:size=320x240:rate=15 -pix_fmt yuv420p /tmp/p5.mp4
python3 scripts/referer_server.py 8899 /tmp/p5.mp4 http://allowed.example/ &
ffmpeg -i "http://127.0.0.1:8321/proxy/$(python3 -c "import urllib.parse;print(urllib.parse.quote('http://127.0.0.1:8899/p5.mp4',safe=''))")/p5.mp4?referer=http%3A%2F%2Fallowed.example%2F" -t 3 -f null -   # 通
# 去掉 ?referer= 则 403
```

## 已验证结论（2026-07-30）

- 全部夹具测试（23）+ 单元测试（15）+ 在线测试（9）通过；P1-P5 ffmpeg 实测通过。
- 在线直播主源 `cph-p2p-msl.akamaized.net` master 可达但其子列表 `master_1.m3u8` 返回 404（源站问题，直连同样 404），R11/P4 按失效预案用 `test-streams.mux.dev/pts_shift/master.m3u8` 替补验证通过。
- 开发中发现并修复两个 relay 真实 bug（详见提交/报告）：m3u8 转发客户端 Range 导致上游 206 的 Content-Range 与重写后正文长度不符（本环境打了 CVE-2023-6604 补丁的 ffmpeg 会直接拒播）；reqwest 总超时掐断慢速分片流。

## 待办

- L2 隐藏 webview 嗅探：已完成，合入 `app/` 正式 Tauri 壳（`demo/` 保留为验证 demo）。
- L3 规则包远程热更 + 签名校验（M3）。首批站点专用解析器已起步：央视网 cntv/cctv 点播（`src/extract/sites.rs`）。
- relay_url 在绑定 `0.0.0.0` 时用局域网 IP 生成（当前按请求 Host 头生成，design.md §6 的自动化未做）。

---

# 正式 Tauri 壳（`app/`）

`get-video-app` 是把三条能力合入正式产品的 Tauri 2 壳：

- **L1/L3 提取**：前端「提取」按钮 → `invoke('extract')` → `get_video::extract::Extractor`（规则包从环境变量 `GET_VIDEO_RULES` 指向的本地 JSON 加载，未设置用空包），秒级返回 `VideoInfo`；
- **L2 webview 嗅探**：前端「嗅探」按钮 → `invoke('sniff')` → 隐藏 WebviewWindow + 六路 hook 注入脚本（与 `demo/` 同款，含 Worker hook），最长约 15 秒；
- **本地 relay**：启动时拉起 `127.0.0.1:8321`（被占退回随机端口），结果经 `relay_url` 在页面内播放（hls.js / `<video>`）。

```bash
cargo build -p get-video-app
./target/debug/get-video-app                          # GUI 主窗口

# 无头验证（需 macOS 窗口环境）
./target/debug/get-video-app --extract-cli <url>      # L1/L3 提取，打印 EXTRACT_RESULT_JSON 后退出
./target/debug/get-video-app --sniff-cli <url>        # L2 嗅探，打印 SNIFF_RESULT_JSON 后退出
./target/debug/get-video-app --ui-test <url>          # GUI 自动点击「嗅探」走完整 IPC 链路
```

## 站点登录窗口（2026-08-02）

前端「登录」按钮 → `invoke('open_login', {url})` → 弹出**可见**的登录 WebviewWindow 加载目标站点，用户在其中完成登录后直接关窗即可；之后再点「嗅探」，隐藏嗅探窗会自动携带登录态。重复点「登录」复用同一窗口并导航到新地址。

已实证（本地 cookie 测试服务器两轮验证）：应用内所有 webview（主窗 / 登录窗 / 隐藏嗅探窗）共享同一 Cookie 存储，且持久 Cookie（带 `Max-Age`/`Expires`）落盘于应用数据目录，**重启应用后登录态仍有效**——A 进程登录窗种下 Cookie，B 进程 `--sniff-cli` 的页面请求与媒体请求均自动携带该 Cookie。

注意：仅 Cookie 形态的登录态有效；站点若把会话只放在 `localStorage`/内存 token，隐藏嗅探窗同样共享存储（同一数据目录），但纯内存 token 不在此列。

---

# Tauri 2 demo：L2 webview 嗅探闭环（`demo/`）

演示「输入网址 → 点击嗅探 → 列出可播放结果 → 点击本地播放」的完整闭环。

## 架构

```
┌───────────────────────────────┐
│ 主窗口（demo/ui/index.html）    │  输入 URL、展示结果、hls.js/<video> 播放
│   └─ invoke('sniff', url) ────┼──┐
├───────────────────────────────┤  │
│ Rust（demo/src/main.rs）       │  ▼
│   do_sniff：                   │ 创建隐藏 WebviewWindow 加载目标页
│   ├─ 注入嗅探 JS（fetch/XHR/   │   （initialization_script，document start）
│   │   media src/Mutation/      │
│   │   Performance/Worker 六路  │
│   │   hook）                   │
│   ├─ 双通道回收命中：           │◄── event.emit('sniff-found') + Image beacon
│   │   IPC event + 127.0.0.1:8377│   （beacon 是远程页 IPC 受限时的兜底）
│   ├─ 12s 上限 / 首命中后 3s 收尾│
│   └─ DRM 检测 + 清晰度 +        │──► 复用 get-video crate（Extractor/drm）
│      relay_url，关窗返回 JSON   │
├───────────────────────────────┤
│ get-video relay 127.0.0.1:8321 │  播放地址中转（m3u8 重写/Range/防盗链）
└───────────────────────────────┘
```

## 跨平台构建

demo 的业务代码在 macOS、Windows 和 Linux 共用，WebView 后端由 Tauri 按目标平台选择：

| 目标平台 | WebView 后端 | 构建方式 |
|---|---|---|
| macOS | WKWebView | `cargo build --workspace` |
| Windows | WebView2 | `cargo build --workspace` |
| Linux | WebKitGTK 2.40+ | `cargo build --workspace` |

Linux 的最低基线是 WebKitGTK **2.40**（`webkit2gtk-4.1`）。项目直接使用 Tauri/wry 官方依赖链，不提供、也不会绕过系统库版本检查。WebKitGTK 2.38 及更旧系统需先升级系统库。macOS 与 Windows 不引入 GTK 依赖。

## 运行

```bash
# 无显示环境（Xvfb）：
Xvfb :99 -screen 0 1280x800x24 &
python3 scripts/test_pages_server.py &     # 本地测试页 :8890（JS 动态加载视频）
cargo build -p get-video-demo
DISPLAY=:99 WEBKIT_DISABLE_DMABUF_RENDERER=1 WEBKIT_DISABLE_COMPOSITING_MODE=1 \
  ./target/debug/get-video-demo

# 有显示环境直接 ./target/debug/get-video-demo 即可
```

测试页：`http://127.0.0.1:8890/a.html`（2s 后 JS 动态插入 `<video src=公网mp4>`）、`http://127.0.0.1:8890/b.html`（hls.js 动态加载公网 m3u8）、`http://127.0.0.1:8890/c.html`（**Web Worker 内 fetch m3u8**，验证 Worker hook——模拟央视频 `cmg.worker.js` 这类 worker 内拉流的站点）。

## Worker hook（2026-08-02）

主线程的 fetch/XHR hook 与 PerformanceObserver 都看不到 dedicated Worker 内的请求，央视频这类「WASM + Worker 拉流」的站点因此是嗅探盲区。解法：包装 `window.Worker` 构造器——classic worker（非 module、http(s) 脚本）改为「嗅探 shim + `importScripts(原脚本)`」的 blob worker，shim 在 worker 作用域 hook `fetch`/`XHR.open`，命中经 `postMessage` 回主线程复用双通道上报；module worker、blob: 脚本与构造异常一律回退原构造器（不破坏页面）。已验证：

- 测试页 `/c.html`（worker 内 fetch m3u8）：成功嗅出；
- **央视频直播 `yangshipin.cn/tv/home`**：成功嗅出 CCTV 频道签名 HLS 地址（`hlslive-tx-cdn.ysp.cctv.cn/...m3u8?ysign=...`），ffmpeg 经 relay 拉流 8s 通过（中途插入直播流的 h264 解码告警属正常现象）。

## 无头验证入口

| 参数 | 作用 |
|---|---|
| `--sniff-cli <url>` | 不开 UI 直接跑嗅探，打印 `SNIFF_RESULT_JSON` 后退出 |
| `--ui-test <url>` | GUI 起来后自动填地址点「嗅探」，再点第一条结果，全程日志回传 |
| `--probe-eval <js>` | 在主窗口执行任意 JS（可用 `invoke('report_log')` 回传） |

日志关键行：`[relay] 已启动`、`[ui] 前端页面已加载`、`[sniff] IPC 调用`、`[sniff] 命中`、`[ui] 前端已渲染结果`、`[page] 播放器状态`。

## 已知限制

- Linux 内嵌播放依赖系统 GStreamer 的编解码器与图形/音频环境；无头 Xvfb 环境仍建议以 ffmpeg 验证播放闭环。
