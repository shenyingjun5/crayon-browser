//! 在线测试（docs/test-cases.md 的 [在线] 用例）：依赖公网资源，可能随时间失效。
//!
//! 运行方式（手工兼容测试，双重显式启用）：
//! `GET_VIDEO_ONLINE=1 cargo test --test online -- --ignored --test-threads=1`
//!
//! 未设置环境变量时全部跳过且不作为产品失败；输出不得包含 URL 或账号信息。

use futures_util::StreamExt;
use get_video::extract::{Extractor, RulePack};
use get_video::relay::{self, RelayConfig};
use reqwest::header;
use std::time::Duration;

const MUX_MASTER: &str = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";
const APPLE_MEDIA_MASTER: &str =
    "https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8";
const APPLE_BYTERANGE: &str =
    "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8";
const OCEANS_AES: &str = "https://playertest.longtailvideo.com/adaptive/oceans_aes/oceans_aes.m3u8";
const LIVE_HLS: &str = "https://cph-p2p-msl.akamaized.net/hls/live/2000341/test/master.m3u8";
/// 失效预案替补直播流（docs/test-cases.md §5：test-streams.mux.dev 池）。
const LIVE_HLS_FALLBACK: &str = "https://test-streams.mux.dev/pts_shift/master.m3u8";
const W3S_MP4: &str = "https://www.w3schools.com/html/mov_bbb.mp4";
const W3S_PAGE: &str = "https://www.w3schools.com/html/html5_video.asp";
const DASH_NO_DRM: &str = "https://dash.akamaized.net/akamai/bbb_30fps/bbb_30fps.mpd";
const DASH_DRM: &str =
    "https://media.axprod.net/TestVectors/v7-MultiDRM-SingleKey/Manifest_1080p.mpd";

async fn spawn_relay() -> relay::RelayHandle {
    relay::start(RelayConfig {
        host: "127.0.0.1".into(),
        port: 0,
        allow_private_hosts: false,
        rules_path: None,
        dash_store: None,
    })
    .await
    .unwrap()
}

fn client() -> reqwest::Client {
    reqwest::Client::builder()
        .timeout(Duration::from_secs(30))
        .build()
        .unwrap()
}

/// 手工在线兼容测试门禁：必须显式设置 GET_VIDEO_ONLINE=1（且 --ignored）。
/// 未启用时跳过，不作为产品单测失败。
fn online_enabled() -> bool {
    matches!(std::env::var("GET_VIDEO_ONLINE"), Ok(v) if v == "1")
}

fn proxy_url(relay_base: &str, target: &str) -> String {
    format!(
        "{}/proxy/{}",
        relay_base,
        get_video::encode_url_component(target)
    )
}

/// E1：真实页面内嵌 <source src> MP4，相对地址转绝对。
#[tokio::test]
#[ignore]
async fn e1_w3schools_page_mp4() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let extractor = Extractor::new(&relay.base_url(), RulePack::empty());
    let info = extractor.extract(W3S_PAGE).await.unwrap();
    let f = info
        .formats
        .iter()
        .find(|f| f.url == W3S_MP4)
        .unwrap_or_else(|| {
            panic!(
                "应提取出相对地址对应的 mp4 格式（共 {} 个）",
                info.formats.len()
            )
        });
    assert_eq!(f.protocol, "mp4");
    assert!(!f.drm);
    assert!(f.relay_url.is_some());
    relay.shutdown().await;
}

/// R1：Master 多码率 + 相对子列表，保留多码率结构。
#[tokio::test]
#[ignore]
async fn r1_mux_master_multibitrate() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let resp = client()
        .get(proxy_url(&relay.base_url(), MUX_MASTER))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    let body = resp.text().await.unwrap();
    let variant_count = body.matches("#EXT-X-STREAM-INF").count();
    assert_eq!(
        variant_count, 5,
        "应保留五档码率（实际 {variant_count} 档）"
    );
    // 每个 STREAM-INF 的下一行都被改写为 /proxy/ 绝对地址
    let mut rewritten = 0;
    let mut lines = body.lines().map(|l| l.trim()).peekable();
    while let Some(line) = lines.next() {
        if line.starts_with("#EXT-X-STREAM-INF") {
            let next = lines
                .find(|l| !l.is_empty() && !l.starts_with('#'))
                .unwrap();
            assert!(
                next.starts_with(&format!("{}/proxy/", relay.base_url())),
                "子列表行未改写: {next}"
            );
            assert!(
                next.contains("url_"),
                "改写后应保留相对路径解析结果: {next}"
            );
            rewritten += 1;
        }
    }
    assert_eq!(rewritten, 5);
    relay.shutdown().await;
}

/// R2：媒体列表 + 相对分片 + EXT-X-BYTERANGE 原样保留。
#[tokio::test]
#[ignore]
async fn r2_apple_byterange_playlist() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let resp = client()
        .get(proxy_url(&relay.base_url(), APPLE_BYTERANGE))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    let body = resp.text().await.unwrap();
    assert!(body.contains("#EXT-X-BYTERANGE:"), "BYTERANGE 标签应保留");
    // BYTERANGE 标签行不被改写
    for line in body.lines() {
        if line.starts_with("#EXT-X-BYTERANGE") {
            assert!(!line.contains("/proxy/"), "标签行不应改写");
        }
    }
    // 分片行被改写为代理地址（main.ts 相对路径转绝对）
    let seg_lines: Vec<&str> = body
        .lines()
        .map(|l| l.trim())
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .collect();
    assert!(!seg_lines.is_empty());
    for s in &seg_lines {
        assert!(s.contains("/proxy/"), "分片行未改写");
        assert!(s.contains("main.ts"), "分片相对路径应解析出 main.ts");
    }
    relay.shutdown().await;
}

/// R3：Master 含 EXT-X-MEDIA 的 URI 同样改写。
#[tokio::test]
#[ignore]
async fn r3_apple_master_ext_x_media() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let resp = client()
        .get(proxy_url(&relay.base_url(), APPLE_MEDIA_MASTER))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    let body = resp.text().await.unwrap();
    let media_lines: Vec<&str> = body
        .lines()
        .filter(|l| l.starts_with("#EXT-X-MEDIA:") && l.contains("URI="))
        .collect();
    assert!(!media_lines.is_empty(), "夹具应含带 URI 的 EXT-X-MEDIA");
    for l in &media_lines {
        assert!(l.contains("/proxy/"), "EXT-X-MEDIA URI 未改写");
    }
    // STREAM-INF 子列表同样改写
    assert!(body.contains("/proxy/"));
    relay.shutdown().await;
}

/// R4 / D5：AES-128 加密流，KEY URI 改写，drm=false。
#[tokio::test]
#[ignore]
async fn r4_aes128_key_rewrite_not_drm() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let c = client();
    // master → 子列表
    let body = c
        .get(proxy_url(&relay.base_url(), OCEANS_AES))
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    let sub = body
        .lines()
        .map(|l| l.trim())
        .find(|l| !l.is_empty() && !l.starts_with('#'))
        .expect("master 中应有子列表")
        .to_string();
    assert!(sub.contains("/proxy/"));
    let sub_body = c.get(&sub).send().await.unwrap().text().await.unwrap();
    let key_line = sub_body
        .lines()
        .find(|l| l.starts_with("#EXT-X-KEY:"))
        .expect("子列表应含 EXT-X-KEY");
    assert!(key_line.contains("METHOD=AES-128"));
    assert!(key_line.contains("/proxy/"), "KEY URI 应改写为代理地址");
    assert!(
        key_line.contains("oceans.key"),
        "相对 key 应解析出 oceans.key"
    );
    // key 经代理可拉取
    let uri_start = key_line.find("URI=\"").unwrap() + 5;
    let uri_end = key_line[uri_start..].find('"').unwrap() + uri_start;
    let key_url = &key_line[uri_start..uri_end];
    let key_resp = c.get(key_url).send().await.unwrap();
    assert_eq!(key_resp.status(), 200, "key 经代理应可拉取");
    assert_eq!(
        key_resp.bytes().await.unwrap().len(),
        16,
        "AES-128 key 16 字节"
    );
    // D5：AES-128 + 公开 key ≠ DRM
    assert!(
        !get_video::drm::hls_is_drm(&sub_body),
        "AES-128 公开 key 不应标记 DRM"
    );
    relay.shutdown().await;
}

/// R9：MP4 Range 透传 → 206 + Content-Range + Accept-Ranges。
#[tokio::test]
#[ignore]
async fn r9_mp4_range_passthrough() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let resp = client()
        .get(proxy_url(&relay.base_url(), W3S_MP4))
        .header(header::RANGE, "bytes=0-1023")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 206, "Range 应透传出 206");
    assert!(resp.headers().get(header::CONTENT_RANGE).is_some());
    assert_eq!(resp.headers().get(header::ACCEPT_RANGES).unwrap(), "bytes");
    let body = resp.bytes().await.unwrap();
    assert_eq!(body.len(), 1024);
    relay.shutdown().await;
}

/// R11：直播流播放列表刷新 + 分片流式转发。
/// 主用例失效时按 §5 失效预案换替补流。
#[tokio::test]
#[ignore]
async fn r11_live_hls() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let c = client();
    let mut media = String::new();
    for candidate in [LIVE_HLS, LIVE_HLS_FALLBACK] {
        let Ok(resp) = c.get(proxy_url(&relay.base_url(), candidate)).send().await else {
            continue;
        };
        if resp.status() != 200 {
            continue;
        }
        let body = resp.text().await.unwrap();
        if !body.starts_with("#EXTM3U") {
            continue;
        }
        // master → 第一个子列表
        let Some(sub) = body
            .lines()
            .map(|l| l.trim())
            .find(|l| !l.is_empty() && !l.starts_with('#'))
            .map(|s| s.to_string())
        else {
            continue;
        };
        assert!(sub.contains("/proxy/"), "子列表应改写");
        let Ok(sub_resp) = c.get(&sub).send().await else {
            continue;
        };
        if sub_resp.status() != 200 {
            eprintln!("主用例子列表不可用（{}），换替补", sub_resp.status());
            continue;
        }
        let text = sub_resp.text().await.unwrap();
        if text.starts_with("#EXTM3U") {
            media = text;
            break;
        }
    }
    assert!(!media.is_empty(), "主用例与替补直播源均不可用");
    // 直播列表通常无 ENDLIST（不强制断言，只检查能拿到分片）
    let seg = media
        .lines()
        .map(|l| l.trim())
        .find(|l| !l.is_empty() && !l.starts_with('#'))
        .expect("live 媒体列表应有分片")
        .to_string();
    assert!(seg.contains("/proxy/"));
    // 分片流式转发：读到首块即停，不全量缓冲
    let resp = c.get(&seg).send().await.unwrap();
    assert_eq!(resp.status(), 200);
    let mut stream = resp.bytes_stream();
    let first = stream.next().await.unwrap().unwrap();
    assert!(!first.is_empty());
    relay.shutdown().await;
}

/// D3：DASH 多 DRM（ContentProtection cenc + PlayReady）。
#[tokio::test]
#[ignore]
async fn d3_dash_multi_drm() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let text = client()
        .get(DASH_DRM)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(
        text.contains("ContentProtection"),
        "夹具前提：应含 ContentProtection"
    );
    assert!(get_video::drm::mpd_is_drm(&text), "应标记 drm:true");
}

/// D4 在线对照：DASH VOD 无 DRM。
#[tokio::test]
#[ignore]
async fn d4_online_dash_vod_no_drm() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let text = client()
        .get(DASH_NO_DRM)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(!get_video::drm::mpd_is_drm(&text), "应标记 drm:false");
}

/// 央视纪录片：HTML 无直链，站点解析器经 guid → getHttpVideoInfo 拿到 hls。
#[tokio::test]
#[ignore]
async fn cctv_documentary_site_extractor() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let extractor = Extractor::new(&relay.base_url(), RulePack::empty());
    let info = extractor
        .extract("https://tv.cctv.com/2026/07/30/VIDEVUdpLU5FN93bTJDFAfwM260730.shtml")
        .await
        .unwrap();
    assert_eq!(info.source, "site-api", "应走站点解析器");
    let f = info
        .formats
        .iter()
        .find(|f| f.protocol == "hls")
        .unwrap_or_else(|| panic!("应有 hls 格式（共 {} 个）", info.formats.len()));
    assert!(f.url.contains(".m3u8"), "hls 地址异常");
    assert!(!f.drm, "公开纪录片不应标记 DRM");
    assert!(f.relay_url.is_some(), "应产出 relay 地址");
    relay.shutdown().await;
}

/// B 站番剧：ep 页无直链，站点解析器经 pgc/playurl 拿整段 mp4（未登录 360P）。
#[tokio::test]
#[ignore]
async fn bilibili_bangumi_site_extractor() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let extractor = Extractor::new(&relay.base_url(), RulePack::empty());
    let info = extractor
        .extract("https://www.bilibili.com/bangumi/play/ep733316")
        .await
        .unwrap();
    assert_eq!(info.source, "site-api", "应走站点解析器");
    assert!(!info.formats.is_empty(), "应有可用格式");
    let f = &info.formats[0];
    assert_eq!(
        f.protocol,
        "mp4",
        "durl 整段应为 mp4（共 {} 个格式）",
        info.formats.len()
    );
    assert!(!f.drm, "is_drm=false 的内容不应标记 DRM");
    assert!(f.relay_url.is_some(), "应产出 relay 地址");
    assert_eq!(
        f.headers.get("Referer").map(String::as_str),
        Some("https://www.bilibili.com"),
        "bilivideo 防盗链需要 B 站 Referer"
    );
    relay.shutdown().await;
}

/// B 站普通视频页：BV → view 换 cid → x/player/playurl 出整段 mp4。
#[tokio::test]
#[ignore]
async fn bilibili_ugc_video_site_extractor() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let extractor = Extractor::new(&relay.base_url(), RulePack::empty());
    let info = extractor
        .extract("https://www.bilibili.com/video/BV1xx411c7mD")
        .await
        .unwrap();
    assert_eq!(info.source, "site-api", "应走站点解析器");
    assert!(!info.formats.is_empty(), "应有可用格式");
    let f = &info.formats[0];
    assert_eq!(
        f.protocol,
        "mp4",
        "durl 整段应为 mp4（共 {} 个格式）",
        info.formats.len()
    );
    assert!(!f.drm);
    assert!(f.relay_url.is_some());
    relay.shutdown().await;
}

/// B 站番剧 ss 季页：HTML 默认集 ep_id 转 pgc/playurl。
#[tokio::test]
#[ignore]
async fn bilibili_bangumi_ss_site_extractor() {
    if !online_enabled() {
        eprintln!("skip: GET_VIDEO_ONLINE=1 未设置（手工兼容测试）");
        return;
    }
    let relay = spawn_relay().await;
    let extractor = Extractor::new(&relay.base_url(), RulePack::empty());
    let info = extractor
        .extract("https://www.bilibili.com/bangumi/play/ss28747")
        .await
        .unwrap();
    assert_eq!(info.source, "site-api");
    assert!(!info.formats.is_empty(), "ss 页应经默认集拿到格式");
    relay.shutdown().await;
}
