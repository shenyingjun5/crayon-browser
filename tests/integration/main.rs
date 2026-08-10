//! L3 集成测试（FND-10）：原 [在线] 用例的本地 fixture 版本。
//!
//! 公网依赖全部替换为 test-support 的 `MockUpstream`（loopback 随机端口、
//! 脚本化响应、请求录制），断网环境可跑；relay 走 `--allow-private-hosts`
//! 等价的 `allow_private_hosts: true` 测试钩子（仅指向本机 mock）。

use futures_util::StreamExt;
use get_video::extract::{Extractor, RulePack};
use get_video::relay::{self, RelayConfig};
use reqwest::header;
use std::time::Duration;
use test_support::upstream::{MockUpstream, UpstreamScript};

/// relay 测试钩子：允许代理本机 mock（公网 SSRF 规则由 fixtures/security 覆盖）。
async fn spawn_relay() -> relay::RelayHandle {
    relay::start(RelayConfig {
        host: "127.0.0.1".into(),
        port: 0,
        allow_private_hosts: true,
        rules_path: None,
        dash_store: None,
    })
    .await
    .unwrap()
}

fn client() -> reqwest::Client {
    reqwest::Client::builder()
        .timeout(Duration::from_secs(10))
        .build()
        .unwrap()
}

fn proxy_url(relay_base: &str, target: &str) -> String {
    format!(
        "{}/proxy/{}",
        relay_base,
        get_video::encode_url_component(target)
    )
}

fn m3u8(body: &str) -> UpstreamScript {
    UpstreamScript::Full {
        status: 200,
        content_type: Some("application/vnd.apple.mpegurl".to_string()),
        body: body.as_bytes().to_vec(),
    }
}

fn octets(body: &[u8]) -> UpstreamScript {
    UpstreamScript::Full {
        status: 200,
        content_type: Some("application/octet-stream".to_string()),
        body: body.to_vec(),
    }
}

/// 第一个非注释行（改写后应为 /proxy/ 地址）。
fn first_uri_line(body: &str) -> &str {
    body.lines()
        .map(str::trim)
        .find(|l| !l.is_empty() && !l.starts_with('#'))
        .expect("播放列表应有 URI 行")
}

/// R1 本地版：Master 多码率 + 相对子列表，保留多码率结构不自动选档。
#[tokio::test]
async fn r1_local_master_multibitrate() {
    let upstream = MockUpstream::start(vec![
        (
            "/master.m3u8".to_string(),
            m3u8(
                "#EXTM3U\n\
                 #EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\n\
                 low/index.m3u8\n\
                 #EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1920x1080\n\
                 hi/index.m3u8\n",
            ),
        ),
        (
            "/low/index.m3u8".to_string(),
            m3u8("#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg0.ts\n#EXT-X-ENDLIST\n"),
        ),
        (
            "/hi/index.m3u8".to_string(),
            m3u8("#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg0.ts\n#EXT-X-ENDLIST\n"),
        ),
        ("/low/seg0.ts".to_string(), octets(b"low-segment")),
        ("/hi/seg0.ts".to_string(), octets(b"hi-segment")),
    ])
    .await
    .unwrap();
    let relay = spawn_relay().await;

    let body = client()
        .get(proxy_url(&relay.base_url(), &upstream.url("/master.m3u8")))
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    // 多码率结构保留：两档 STREAM-INF 原样，子列表行改写为 /proxy/
    assert_eq!(body.matches("#EXT-X-STREAM-INF").count(), 2);
    let subs: Vec<&str> = body
        .lines()
        .map(str::trim)
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .collect();
    assert_eq!(subs.len(), 2);
    assert!(subs.iter().all(|s| s.contains("/proxy/")), "{subs:?}");

    // 跟进第一档子列表：分片同样改写且可经 relay 拉取
    let sub_body = client()
        .get(subs[0])
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    let seg = first_uri_line(&sub_body);
    assert!(seg.contains("/proxy/"), "分片应改写: {seg}");
    let seg_resp = client().get(seg).send().await.unwrap();
    assert_eq!(seg_resp.status(), 200);
    assert_eq!(seg_resp.bytes().await.unwrap().len(), 11);
    relay.shutdown().await;
}

/// R2 本地版：EXT-X-BYTERANGE 标签原样保留，分片行改写。
#[tokio::test]
async fn r2_local_byterange_playlist() {
    let playlist = "#EXTM3U\n\
                    #EXT-X-TARGETDURATION:10\n\
                    #EXT-X-BYTERANGE:1000@0\n\
                    main.ts\n\
                    #EXT-X-BYTERANGE:1000@1000\n\
                    main.ts\n\
                    #EXT-X-ENDLIST\n";
    let upstream = MockUpstream::start(vec![
        ("/prog.m3u8".to_string(), m3u8(playlist)),
        ("/main.ts".to_string(), octets(&[0u8; 2000])),
    ])
    .await
    .unwrap();
    let relay = spawn_relay().await;

    let body = client()
        .get(proxy_url(&relay.base_url(), &upstream.url("/prog.m3u8")))
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(body.contains("#EXT-X-BYTERANGE:1000@0"));
    assert!(body.contains("#EXT-X-BYTERANGE:1000@1000"));
    let uri_lines: Vec<&str> = body
        .lines()
        .map(str::trim)
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .collect();
    assert_eq!(uri_lines.len(), 2);
    assert!(uri_lines.iter().all(|l| l.contains("/proxy/")));
    relay.shutdown().await;
}

/// R3 本地版：Master 的 EXT-X-MEDIA（音频/字幕组 URI）同样改写。
#[tokio::test]
async fn r3_local_master_ext_x_media() {
    let master = "#EXTM3U\n\
                  #EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",NAME=\"eng\",URI=\"audio/eng.m3u8\"\n\
                  #EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub\",NAME=\"zh\",URI=\"sub/zh.m3u8\"\n\
                  #EXT-X-STREAM-INF:BANDWIDTH=800000\n\
                  v/index.m3u8\n";
    let upstream = MockUpstream::start(vec![
        ("/master.m3u8".to_string(), m3u8(master)),
        (
            "/audio/eng.m3u8".to_string(),
            m3u8("#EXTM3U\n#EXT-X-ENDLIST\n"),
        ),
        (
            "/sub/zh.m3u8".to_string(),
            m3u8("#EXTM3U\n#EXT-X-ENDLIST\n"),
        ),
        (
            "/v/index.m3u8".to_string(),
            m3u8("#EXTM3U\n#EXT-X-ENDLIST\n"),
        ),
    ])
    .await
    .unwrap();
    let relay = spawn_relay().await;

    let body = client()
        .get(proxy_url(&relay.base_url(), &upstream.url("/master.m3u8")))
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    let media_uris: Vec<&str> = body
        .lines()
        .filter(|l| l.starts_with("#EXT-X-MEDIA:"))
        .collect();
    assert_eq!(media_uris.len(), 2);
    for line in media_uris {
        let uri_start = line.find("URI=\"").unwrap() + 5;
        let uri_end = line[uri_start..].find('"').unwrap() + uri_start;
        let uri = &line[uri_start..uri_end];
        assert!(uri.contains("/proxy/"), "EXT-X-MEDIA URI 应改写: {line}");
        // 改写后的音频/字幕列表可经 relay 拉取（相对路径解析正确）
        let resp = client().get(uri).send().await.unwrap();
        assert_eq!(resp.status(), 200);
    }
    relay.shutdown().await;
}

/// R4/D5 本地版：AES-128 key URI 改写且可拉取，公开 key ≠ DRM。
#[tokio::test]
async fn r4_local_aes128_key_rewrite_not_drm() {
    let playlist = "#EXTM3U\n\
                    #EXT-X-TARGETDURATION:10\n\
                    #EXT-X-KEY:METHOD=AES-128,URI=\"oceans.key\"\n\
                    #EXTINF:10.0,\n\
                    seg0.ts\n\
                    #EXT-X-ENDLIST\n";
    let upstream = MockUpstream::start(vec![
        ("/oceans.m3u8".to_string(), m3u8(playlist)),
        ("/oceans.key".to_string(), octets(&[7u8; 16])),
        ("/seg0.ts".to_string(), octets(b"segment")),
    ])
    .await
    .unwrap();
    let relay = spawn_relay().await;

    let body = client()
        .get(proxy_url(&relay.base_url(), &upstream.url("/oceans.m3u8")))
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    let key_line = body
        .lines()
        .find(|l| l.starts_with("#EXT-X-KEY:"))
        .expect("子列表应含 EXT-X-KEY");
    assert!(key_line.contains("METHOD=AES-128"));
    assert!(key_line.contains("/proxy/"), "KEY URI 应改写为代理地址");
    assert!(key_line.contains("oceans.key"), "相对 key 应解析出文件名");

    let uri_start = key_line.find("URI=\"").unwrap() + 5;
    let uri_end = key_line[uri_start..].find('"').unwrap() + uri_start;
    let key_url = &key_line[uri_start..uri_end];
    let key_resp = client().get(key_url).send().await.unwrap();
    assert_eq!(key_resp.status(), 200, "key 经代理应可拉取");
    assert_eq!(
        key_resp.bytes().await.unwrap().len(),
        16,
        "AES-128 key 16 字节"
    );

    // D5：AES-128 + 公开 key ≠ DRM
    assert!(
        !get_video::drm::hls_is_drm(&body),
        "AES-128 公开 key 不应标记 DRM"
    );
    relay.shutdown().await;
}

/// R9 本地版：MP4 Range 透传 → 206 + Content-Range + Accept-Ranges。
#[tokio::test]
async fn r9_local_mp4_range_passthrough() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: b"0123456789".to_vec(),
        },
    )])
    .await
    .unwrap();
    let relay = spawn_relay().await;

    let resp = client()
        .get(proxy_url(&relay.base_url(), &upstream.url("/movie.mp4")))
        .header(header::RANGE, "bytes=0-3")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 206, "Range 应透传出 206");
    assert_eq!(
        resp.headers().get(header::CONTENT_RANGE).unwrap(),
        "bytes 0-3/10"
    );
    assert_eq!(resp.headers().get(header::ACCEPT_RANGES).unwrap(), "bytes");
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"0123");

    // relay 确实把 Range 转发给了上游
    let requests = upstream.requests();
    assert_eq!(requests[0].header("range"), Some("bytes=0-3"));
    relay.shutdown().await;
}

/// R11 本地版：直播列表（无 ENDLIST）刷新与分片流式转发。
#[tokio::test]
async fn r11_local_live_hls_refresh() {
    let playlist_v1 =
        "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MEDIA-SEQUENCE:100\n#EXTINF:4.0,\nseg100.ts\n";
    let upstream = MockUpstream::start(vec![
        ("/live.m3u8".to_string(), m3u8(playlist_v1)),
        ("/seg100.ts".to_string(), octets(b"segment-100")),
        ("/seg101.ts".to_string(), octets(b"segment-101")),
    ])
    .await
    .unwrap();
    let relay = spawn_relay().await;
    let live_url = proxy_url(&relay.base_url(), &upstream.url("/live.m3u8"));

    let body_v1 = client()
        .get(&live_url)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(!body_v1.contains("#EXT-X-ENDLIST"), "直播列表无 ENDLIST");
    let seg_v1 = first_uri_line(&body_v1).to_string();
    assert!(seg_v1.contains("/proxy/"));
    assert!(seg_v1.contains("seg100.ts"), "分片名保留: {seg_v1}");

    // 列表刷新：上游换到下一个媒体序列，relay 不做缓存透出新内容
    upstream.set_route(
        "/live.m3u8",
        m3u8("#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MEDIA-SEQUENCE:101\n#EXTINF:4.0,\nseg101.ts\n"),
    );
    let body_v2 = client()
        .get(&live_url)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(body_v2.contains("seg101"), "刷新后应出新分片: {body_v2}");

    // 分片流式转发：读到首块即停，不全量缓冲
    let resp = client().get(first_uri_line(&body_v2)).send().await.unwrap();
    assert_eq!(resp.status(), 200);
    let mut stream = resp.bytes_stream();
    let first = stream.next().await.unwrap().unwrap();
    assert!(!first.is_empty());
    relay.shutdown().await;
}

/// E1 本地版：页面内嵌 <source src> 相对地址转绝对。
#[tokio::test]
async fn e1_local_page_relative_source_mp4() {
    let page = "<!doctype html><html><body>\
                <video controls><source src=\"mov_bbb.mp4\" type=\"video/mp4\"></video>\
                </body></html>";
    let upstream = MockUpstream::start(vec![
        (
            "/html5_video.html".to_string(),
            UpstreamScript::Full {
                status: 200,
                content_type: Some("text/html; charset=utf-8".to_string()),
                body: page.as_bytes().to_vec(),
            },
        ),
        ("/mov_bbb.mp4".to_string(), octets(b"fake-mp4")),
    ])
    .await
    .unwrap();
    let relay = spawn_relay().await;

    let extractor = Extractor::new(&relay.base_url(), RulePack::empty());
    let info = extractor
        .extract(&upstream.url("/html5_video.html"))
        .await
        .unwrap();
    let expected = upstream.url("/mov_bbb.mp4");
    let f = info
        .formats
        .iter()
        .find(|f| f.url == expected)
        .expect("应提取出相对地址转换后的 mp4");
    assert_eq!(f.protocol, "mp4");
    assert!(!f.drm);
    assert!(f.relay_url.is_some());
    relay.shutdown().await;
}

/// D3 本地版：DASH 多 DRM（ContentProtection cenc + PlayReady）。
#[tokio::test]
async fn d3_local_dash_multi_drm() {
    let mpd = r#"<?xml version="1.0"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011">
  <Period>
    <AdaptationSet mimeType="video/mp4">
      <ContentProtection schemeIdUri="urn:mpeg:dash:mp4protection:2011" value="cenc"/>
      <ContentProtection schemeIdUri="urn:uuid:9a04f079-9840-4286-ab92-e65be0885f95"/>
      <Representation id="v1" bandwidth="1000000"/>
    </AdaptationSet>
  </Period>
</MPD>"#;
    let upstream = MockUpstream::start(vec![(
        "/manifest.mpd".to_string(),
        UpstreamScript::Full {
            status: 200,
            content_type: Some("application/dash+xml".to_string()),
            body: mpd.as_bytes().to_vec(),
        },
    )])
    .await
    .unwrap();

    let text = client()
        .get(upstream.url("/manifest.mpd"))
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
