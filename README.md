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
- **L2 webview 嗅探**：**本轮未实现**。隐藏 webview 需要 Tauri GUI 环境，后续在 Tauri 壳内实现（design.md §3 L2 / 里程碑 M2）。
- **L3 站点规则包**（`src/extract/rules.rs`）：**最小骨架**——从本地 JSON 文件加载（域名后缀匹配 + 带 `(?<url>...)` 命名分组的正则模板，可附 referer/ua）。**远程热更未实现**，后续里程碑 M3 再做（含签名校验）。

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

- L2 隐藏 webview 嗅探：已在 `demo/` 的 Tauri 2 demo 中验证（见下章）；合入正式 Tauri 壳时可直接搬 `demo/src/main.rs` 的 `do_sniff` 与注入脚本。
- L3 规则包远程热更 + 签名校验 + 首批站点专用解析器（M3）。
- relay_url 在绑定 `0.0.0.0` 时用局域网 IP 生成（当前按请求 Host 头生成，design.md §6 的自动化未做）。

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
│   │   Performance 五路 hook）  │
│   ├─ 双通道回收命中：           │◄── event.emit('sniff-found') + Image beacon
│   │   IPC event + 127.0.0.1:8377│   （beacon 是远程页 IPC 受限时的兜底）
│   ├─ 12s 上限 / 首命中后 3s 收尾│
│   └─ DRM 检测 + 清晰度 +        │──► 复用 get-video crate（Extractor/drm）
│      relay_url，关窗返回 JSON   │
├───────────────────────────────┤
│ get-video relay 127.0.0.1:8321 │  播放地址中转（m3u8 重写/Range/防盗链）
└───────────────────────────────┘
```

## 构建（webkit2gtk 2.38 环境的三个补丁，缺一不可）

本机 openEuler 24.03 的 webkit2gtk 只有 **2.38.2**，而所有正式版 Tauri 2 都按 2.40 编译。demo 通过三层适配跑通：

1. **tauri 钉 `=2.7.0`**（对应 wry 0.52）；2.8+ 的 wry 0.53 增加更多 2.40 符号引用。`tauri-runtime` 需 `cargo update -p tauri-runtime --precise 2.7.1` 对齐（tauri 语义化版本坑：runtime 有独立版本线，错配会编译报 trait 缺方法）。
2. **构建期 pkg-config shim**：`scripts/fake-pkgconfig/*.pc` 把版本号虚报为 2.40.0，只用于通过 gtk-rs 构建期版本检查。构建命令：
   ```bash
   PKG_CONFIG_PATH=$PWD/scripts/fake-pkgconfig cargo build -p get-video-demo
   ```
3. **运行期 IPC body 补丁**：Tauri 的 `invoke` 参数放在自定义协议 POST body 里，读 body 的 `webkit_uri_scheme_request_get_http_body` 是 2.40 新增——2.38 上参数静默丢失（报 `missing required key`）。适配：
   - `demo/src/main.rs` 顶部提供 3 个 2.40 符号桩（满足链接）；
   - `demo/ui/index.html` 包装 `window.fetch`，把 `ipc://` 请求的 body 挪进 `x-tauri-body` 请求头；
   - `[patch.crates-io]` 指向 `demo/vendor/wry`（wry 0.52.1 + 补丁：body 为空时改读该请求头，见文件内 `[get-video demo patch]` 注释）。

系统 webkit2gtk 升到 ≥ 2.40 后，以上全部可删，tauri 也可升回最新。

## 运行

```bash
# 无显示环境（Xvfb）：
Xvfb :99 -screen 0 1280x800x24 &
python3 scripts/test_pages_server.py &     # 本地测试页 :8890（JS 动态加载视频）
PKG_CONFIG_PATH=$PWD/scripts/fake-pkgconfig cargo build -p get-video-demo
DISPLAY=:99 WEBKIT_DISABLE_DMABUF_RENDERER=1 WEBKIT_DISABLE_COMPOSITING_MODE=1 \
  ./target/debug/get-video-demo

# 有显示环境直接 ./target/debug/get-video-demo 即可
```

测试页：`http://127.0.0.1:8890/a.html`（2s 后 JS 动态插入 `<video src=公网mp4>`）、`http://127.0.0.1:8890/b.html`（hls.js 动态加载公网 m3u8）。

## 无头验证入口

| 参数 | 作用 |
|---|---|
| `--sniff-cli <url>` | 不开 UI 直接跑嗅探，打印 `SNIFF_RESULT_JSON` 后退出 |
| `--ui-test <url>` | GUI 起来后自动填地址点「嗅探」，再点第一条结果，全程日志回传 |
| `--probe-eval <js>` | 在主窗口执行任意 JS（可用 `invoke('report_log')` 回传） |

日志关键行：`[relay] 已启动`、`[ui] 前端页面已加载`、`[sniff] IPC 调用`、`[sniff] 命中`、`[ui] 前端已渲染结果`、`[page] 播放器状态`。

## 已知限制（如实）

- **本环境 webview 内播放不可用**：实测 WebKitGTK 的媒体后端在此系统上整个坏掉——`gst-launch-1.0 playbin` 对标准 h264/aac mp4 都无法 preroll（GStreamer 插件版本混杂：core 1.22.5 / plugins-good 1.20.3 / bad-free 1.16.2，且 Xvfb 无 GL/音频服务），`<video>` 报 MEDIA_ERR_SRC_NOT_SUPPORTED，与 demo 代码无关。**播放闭环以 ffmpeg 验证为准**（见下方「已验证结论」）；正常桌面环境（版本一致的 GStreamer + 有显示/音频）不受影响。
- webkit2gtk 2.38 下远程页的 Tauri **event 上报可用**（`event.emit` 走另一通道），demo 仍保留了 beacon 兜底以策万全。

