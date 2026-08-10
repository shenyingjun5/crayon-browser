//! MediaInspector contract (MED-06): PL-003 bounded MP4 fallback, PL-004 HLS
//! asset relationships, PL-005 key-bearing HLS facts without key fetch,
//! PL-006 DASH ContentProtection. All fixtures local (MockUpstream).

use crayon_media_probe::http::{ProbeHttpClient, ProbeHttpConfig};
use crayon_media_probe::{HlsEncryption, HlsPlaylist, Inspection, MediaInspector};
use test_support::upstream::{MockUpstream, UpstreamScript};

fn inspector() -> MediaInspector {
    MediaInspector::new(ProbeHttpClient::new(ProbeHttpConfig {
        allow_private_addresses: true, // 测试钩子：指向本机 mock
        ..ProbeHttpConfig::default()
    }))
}

fn m3u8(body: &str) -> UpstreamScript {
    UpstreamScript::Full {
        status: 200,
        content_type: Some("application/vnd.apple.mpegurl".to_string()),
        body: body.as_bytes().to_vec(),
    }
}

#[tokio::test]
async fn pl_003_mp4_identified_via_bounded_range_after_head_405() {
    let mut body = vec![0x00, 0x00, 0x00, 0x18];
    body.extend_from_slice(b"ftypmp42");
    body.extend_from_slice(&[0u8; 64]);
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::HeadRejected(Box::new(UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body,
        })),
    )])
    .await
    .unwrap();

    let outcome = inspector()
        .inspect(&upstream.url("/movie.mp4"))
        .await
        .unwrap();
    match outcome {
        Inspection::Mp4(info) => assert_eq!(info.major_brand, "mp42"),
        other => panic!("应识别为 MP4: {other:?}"),
    }
    // 不下载主体：记录到的 Range 请求证明只取头部
    let requests = upstream.requests();
    assert!(requests.iter().any(|r| r.header("range").is_some()));
}

#[tokio::test]
async fn pl_004_hls_master_asset_relationships() {
    let master = "#EXTM3U\n\
        #EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",NAME=\"eng\",URI=\"audio/eng.m3u8\"\n\
        #EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub\",NAME=\"zh\",URI=\"sub/zh.m3u8\"\n\
        #EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"avc1.64001f,mp4a.40.2\"\n\
        low/index.m3u8\n\
        #EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1920x1080,CODECS=\"hvc1.1.6.L120,mp4a.40.2\"\n\
        hi/index.m3u8\n";
    let upstream = MockUpstream::start(vec![("/master.m3u8".to_string(), m3u8(master))])
        .await
        .unwrap();

    let outcome = inspector()
        .inspect(&upstream.url("/master.m3u8"))
        .await
        .unwrap();
    let Inspection::Hls(HlsPlaylist::Master {
        variants,
        renditions,
        session_keys,
    }) = outcome
    else {
        panic!("应识别为 HLS master")
    };
    assert_eq!(variants.len(), 2);
    assert_eq!(variants[0].bandwidth, Some(800000));
    assert_eq!(variants[0].resolution, Some((640, 360)));
    assert_eq!(variants[0].codecs, vec!["avc1.64001f", "mp4a.40.2"]);
    assert!(
        variants[0].uri.ends_with("/low/index.m3u8"),
        "相对 URI 转绝对"
    );
    assert_eq!(variants[1].resolution, Some((1920, 1080)));
    assert_eq!(renditions.len(), 2);
    assert_eq!(renditions[0].media_type, "AUDIO");
    assert!(renditions[0]
        .uri
        .as_ref()
        .unwrap()
        .ends_with("/audio/eng.m3u8"));
    assert_eq!(renditions[1].media_type, "SUBTITLES");
    assert!(session_keys.is_empty());
}

#[tokio::test]
async fn pl_005_key_bearing_hls_reported_without_fetching_key() {
    // AES-128
    let playlist = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n#EXTINF:4.0,\nseg0.ts\n#EXT-X-ENDLIST\n";
    let upstream = MockUpstream::start(vec![
        ("/enc.m3u8".to_string(), m3u8(playlist)),
        (
            "/key.bin".to_string(),
            UpstreamScript::Full {
                status: 200,
                content_type: None,
                body: vec![7u8; 16],
            },
        ),
    ])
    .await
    .unwrap();
    let outcome = inspector()
        .inspect(&upstream.url("/enc.m3u8"))
        .await
        .unwrap();
    let Inspection::Hls(HlsPlaylist::Media { encryption, .. }) = outcome else {
        panic!("应识别为 HLS media")
    };
    assert!(matches!(encryption, HlsEncryption::Aes128 { .. }));
    assert!(encryption.requires_key());
    assert_eq!(upstream.hit_count("/key.bin"), 0, "不得请求 key");

    // SAMPLE-AES
    let sample = "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"key.bin\"\n#EXTINF:4.0,\nseg0.ts\n";
    upstream.set_route("/enc.m3u8", m3u8(sample));
    let outcome = inspector()
        .inspect(&upstream.url("/enc.m3u8"))
        .await
        .unwrap();
    let Inspection::Hls(HlsPlaylist::Media { encryption, .. }) = outcome else {
        panic!()
    };
    assert!(matches!(encryption, HlsEncryption::SampleAes { .. }));
    assert!(encryption.requires_key());

    // SESSION-KEY（master 级声明）
    let master = "#EXTM3U\n#EXT-X-SESSION-KEY:METHOD=AES-128,URI=\"key.bin\"\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n";
    upstream.set_route("/enc.m3u8", m3u8(master));
    let outcome = inspector()
        .inspect(&upstream.url("/enc.m3u8"))
        .await
        .unwrap();
    let Inspection::Hls(HlsPlaylist::Master { session_keys, .. }) = outcome else {
        panic!()
    };
    assert_eq!(session_keys.len(), 1);
    assert!(session_keys[0].requires_key());
    assert_eq!(upstream.hit_count("/key.bin"), 0, "全程不得请求 key");
}

#[tokio::test]
async fn pl_006_dash_content_protection_detected() {
    let mpd = r#"<?xml version="1.0"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011"><Period><AdaptationSet mimeType="video/mp4">
<ContentProtection schemeIdUri="urn:mpeg:dash:mp4protection:2011" value="cenc"/>
<Representation id="v1" bandwidth="1000000"/><Representation id="v2" bandwidth="2000000"/>
</AdaptationSet></Period></MPD>"#;
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
    let outcome = inspector()
        .inspect(&upstream.url("/manifest.mpd"))
        .await
        .unwrap();
    let Inspection::Dash(info) = outcome else {
        panic!("应识别为 DASH")
    };
    assert!(info.has_content_protection);
    assert_eq!(info.representation_count, 2);
    // DRM 事实只作为数据上报，inspection 不产出任何直投资产（policy 拒绝）。
}

#[tokio::test]
async fn live_media_playlist_and_content_sniffing() {
    // 直播列表（无 ENDLIST）+ Content-Type 非 mpegurl 但 body 以 #EXTM3U 开头
    let playlist =
        "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MEDIA-SEQUENCE:9\n#EXTINF:4.0,\nseg9.ts\n";
    let upstream = MockUpstream::start(vec![(
        "/live".to_string(),
        UpstreamScript::Full {
            status: 200,
            content_type: Some("text/plain".to_string()),
            body: playlist.as_bytes().to_vec(),
        },
    )])
    .await
    .unwrap();
    let outcome = inspector().inspect(&upstream.url("/live")).await.unwrap();
    let Inspection::Hls(HlsPlaylist::Media {
        segment_uris,
        has_endlist,
        ..
    }) = outcome
    else {
        panic!("应按内容识别为 HLS media")
    };
    assert!(!has_endlist, "直播无 ENDLIST");
    assert_eq!(segment_uris.len(), 1);
    assert!(segment_uris[0].ends_with("/seg9.ts"));
}

#[tokio::test]
async fn unknown_content_is_unknown_not_error() {
    let upstream = MockUpstream::start(vec![(
        "/page.html".to_string(),
        UpstreamScript::Full {
            status: 200,
            content_type: Some("text/html".to_string()),
            body: b"<html><body>hello</body></html>".to_vec(),
        },
    )])
    .await
    .unwrap();
    let outcome = inspector()
        .inspect(&upstream.url("/page.html"))
        .await
        .unwrap();
    assert_eq!(outcome, Inspection::Unknown);
}
