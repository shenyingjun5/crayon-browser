//! HLS parser/rewrite contract (MED-14): RL-010 相对/绝对/query URI 全部
//! 改写为 opaque 路径且非 URI 行字节不变；加密拒绝；深度/行数/资源有界。

use crayon_relay::hls::parser::{parse, rewrite, HlsError, PlaylistKind, MAX_DEPTH};
use std::collections::HashMap;

/// 记录型 allocator：upstream URL → opaque 路径。
type Seen = std::sync::Arc<std::sync::Mutex<Vec<String>>>;
fn allocator() -> (impl FnMut(&str) -> Result<String, HlsError>, Seen) {
    let seen = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
    let record = seen.clone();
    let mut counter = 0u32;
    let mut map: HashMap<String, String> = HashMap::new();
    (
        move |url: &str| {
            record.lock().unwrap().push(url.to_string());
            if let Some(path) = map.get(url) {
                return Ok(path.clone());
            }
            counter += 1;
            let path = format!("/s/token123/r/res-{counter:03}/asset");
            map.insert(url.to_string(), path.clone());
            Ok(path)
        },
        seen,
    )
}

const MASTER: &str = "#EXTM3U\n\
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",NAME=\"eng\",URI=\"audio/eng.m3u8?tok=1\"\n\
#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\n\
low/index.m3u8\n\
#EXT-X-STREAM-INF:BANDWIDTH=5000000\n\
https://cdn.example.com/hi/index.m3u8?sign=abc&x=1\n";

const MEDIA: &str = "#EXTM3U\n\
#EXT-X-TARGETDURATION:4\n\
#EXT-X-MAP:URI=\"init.mp4\"\n\
#EXTINF:4.0,\n\
seg0.ts\n\
#EXT-X-BYTERANGE:1000@0\n\
seg1.ts?t=9\n\
#EXT-X-ENDLIST\n";

#[test]
fn rl_010_master_uris_become_opaque_and_structure_is_preserved() {
    let parsed = parse(MASTER).unwrap();
    assert_eq!(parsed.kind(), PlaylistKind::Master);
    let (mut alloc, seen) = allocator();
    let out = rewrite(
        &parsed,
        "https://cdn.example.com/master.m3u8",
        0,
        &mut alloc,
    )
    .unwrap();

    // 两个 variant 行与 MEDIA URI 都改写为 opaque 路径
    assert_eq!(out.matches("/s/token123/r/res-").count(), 3);
    assert!(!out.contains("index.m3u8"), "原始 URI 不出现在输出");
    assert!(!out.contains("eng.m3u8"));
    // 结构行字节不变
    assert!(out.contains("#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\n"));
    assert!(out.contains(
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud\",NAME=\"eng\",URI=\"/s/token123/r/res-001/asset\""
    ));
    // query 保留（相对与绝对 URI）
    let seen = seen.lock().unwrap();
    assert!(seen
        .iter()
        .any(|u| u == "https://cdn.example.com/audio/eng.m3u8?tok=1"));
    assert!(seen
        .iter()
        .any(|u| u == "https://cdn.example.com/low/index.m3u8"));
    assert!(seen
        .iter()
        .any(|u| u == "https://cdn.example.com/hi/index.m3u8?sign=abc&x=1"));
}

#[test]
fn rl_010_media_playlist_map_and_query_segments() {
    let parsed = parse(MEDIA).unwrap();
    assert_eq!(parsed.kind(), PlaylistKind::Media);
    let (mut alloc, seen) = allocator();
    let out = rewrite(
        &parsed,
        "https://cdn.example.com/v/index.m3u8",
        0,
        &mut alloc,
    )
    .unwrap();

    assert_eq!(out.matches("/s/token123/r/res-").count(), 3);
    // BYTERANGE 等非 URI 行字节保留
    assert!(out.contains("#EXT-X-BYTERANGE:1000@0\n"));
    assert!(out.contains("#EXT-X-TARGETDURATION:4\n"));
    assert!(out.ends_with("#EXT-X-ENDLIST\n"));
    let seen = seen.lock().unwrap();
    assert!(seen.contains(&"https://cdn.example.com/v/init.mp4".to_string()));
    assert!(seen.contains(&"https://cdn.example.com/v/seg0.ts".to_string()));
    assert!(seen.contains(&"https://cdn.example.com/v/seg1.ts?t=9".to_string()));
}

#[test]
fn non_uri_lines_are_byte_identical() {
    let body = "#EXTM3U\r\n#EXT-X-VERSION:3\r\n#EXT-X-TARGETDURATION:4\r\nseg0.ts\r\n";
    let parsed = parse(body).unwrap();
    let (mut alloc, _) = allocator();
    let out = rewrite(&parsed, "https://cdn.example.com/x.m3u8", 0, &mut alloc).unwrap();
    assert!(out.contains("#EXT-X-VERSION:3\r\n"), "CRLF 标签行原样保留");
    assert!(out.contains("#EXT-X-TARGETDURATION:4\r\n"));
}

#[test]
fn encrypted_playlists_are_rejected() {
    for body in [
        "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k.bin\"\nseg0.ts\n",
        "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"k\"\nseg0.ts\n",
        "#EXTM3U\n#EXT-X-SESSION-KEY:METHOD=AES-128,URI=\"k\"\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n",
    ] {
        assert_eq!(parse(body).unwrap_err(), HlsError::Encrypted, "{body}");
    }
    // METHOD=NONE 不是加密
    assert!(parse("#EXTM3U\n#EXT-X-KEY:METHOD=NONE\nseg0.ts\n").is_ok());
}

#[test]
fn bounds_are_enforced() {
    assert_eq!(parse("not a playlist").unwrap_err(), HlsError::NotHls);

    // 深度超限
    let parsed = parse(MEDIA).unwrap();
    let (mut alloc, _) = allocator();
    assert_eq!(
        rewrite(
            &parsed,
            "https://cdn.example.com/x.m3u8",
            MAX_DEPTH + 1,
            &mut alloc
        )
        .unwrap_err(),
        HlsError::DepthExceeded
    );

    // 行数超限
    let big = format!("#EXTM3U\n{}", "#EXT-X-COMMENT:x\n".repeat(10_001));
    assert_eq!(parse(&big).unwrap_err(), HlsError::TooManyLines);

    // 资源数超限（allocator 单调分配新 id）
    let many = format!(
        "#EXTM3U\n{}#EXT-X-ENDLIST\n",
        (0..4097)
            .map(|i| format!("seg{i}.ts\n"))
            .collect::<String>()
    );
    let parsed = parse(&many).unwrap();
    let (mut alloc, _) = allocator();
    assert_eq!(
        rewrite(&parsed, "https://cdn.example.com/x.m3u8", 0, &mut alloc).unwrap_err(),
        HlsError::ResourceLimit
    );
}
