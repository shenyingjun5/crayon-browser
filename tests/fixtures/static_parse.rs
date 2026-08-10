use super::*;

// ---------------------------------------------------------------------------
// E2-E7：L1 静态解析夹具
// ---------------------------------------------------------------------------

#[test]
fn e2_escaped_m3u8_link() {
    let html = r#"<html><body><script>var url = "https:\/\/cdn.example.com\/a\/b.m3u8";</script></body></html>"#;
    let r = parse_html(html, "https://site.example/watch");
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/a/b.m3u8" && c.protocol == Protocol::Hls),
        "转义还原失败: {:?}",
        r.candidates
    );
}

#[test]
fn e3_percent_encoded_m3u8_link() {
    let html = r#"<html><body><script>var cfg="https%3A%2F%2Fcdn.example.com%2Fv%2Findex.m3u8%3Ftoken%3Dabc";</script></body></html>"#;
    let r = parse_html(html, "https://site.example/watch");
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/v/index.m3u8?token=abc"),
        "percent 解码失败: {:?}",
        r.candidates
    );
}

#[test]
fn e4_jsonld_video_object() {
    let html = r##"<html><head>
<script type="application/ld+json">{"@context":"https://schema.org","@type":"VideoObject","name":"测试视频标题","contentUrl":"https://cdn.example.com/media/movie.mp4"}</script>
</head><body></body></html>"##;
    let r = parse_html(html, "https://site.example/watch");
    assert_eq!(r.title.as_deref(), Some("测试视频标题"));
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/media/movie.mp4"
                && c.protocol == Protocol::Mp4)
    );
}

#[test]
fn e5_maccms_player_config() {
    let html = r#"<html><body><script>
var player_aaaa={"flag":"play","encrypt":0,"trysee":0,"points":0,"link":"/vod/1.html","link_next":"","link_pre":"","url":"https:\/\/cdn.example.com\/2026\/07\/index.m3u8","url_next":"","from":"m3u8","server":"no","note":"","id":"1"}
</script></body></html>"#;
    let r = parse_html(html, "https://maccms.example/vod/1.html");
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/2026/07/index.m3u8"),
        "maccms 提取失败: {:?}",
        r.candidates
    );
}

#[test]
fn e5_maccms_encrypt_urlencoded() {
    let html = r#"<html><body><script>
var player_aaaa={"flag":"play","encrypt":1,"url":"https%3A%2F%2Fcdn.example.com%2Fenc%2Findex.m3u8","from":"m3u8"}
</script></body></html>"#;
    let r = parse_html(html, "https://maccms.example/vod/2.html");
    assert!(r
        .candidates
        .iter()
        .any(|c| c.url == "https://cdn.example.com/enc/index.m3u8"));
}

#[test]
fn e6_no_video_page() {
    let html = "<html><head><title>纯文本页</title></head><body><p>没有任何视频</p></body></html>";
    let r = parse_html(html, "https://site.example/text");
    assert!(r.candidates.is_empty());
}

#[test]
fn e7_dedup_and_quality_sort() {
    let html = r#"<html><body>
<p>1080p: https://cdn.example.com/v/master.m3u8</p>
<p>重复1: https://cdn.example.com/v/master.m3u8</p>
<p>重复2: "https:\/\/cdn.example.com\/v\/master.m3u8"</p>
<p>720p: https://cdn.example.com/v/720p/index.m3u8</p>
<p>备份: https://cdn.example.com/v/movie.mp4</p>
</body></html>"#;
    let r = parse_html(html, "https://site.example/watch");
    let urls: Vec<&str> = r.candidates.iter().map(|c| c.url.as_str()).collect();
    // 去重：master.m3u8 只出现一次
    assert_eq!(
        urls.iter()
            .filter(|u| **u == "https://cdn.example.com/v/master.m3u8")
            .count(),
        1,
        "去重失败: {urls:?}"
    );
    assert_eq!(urls.len(), 3);
    // 清晰度降序：1080p（带 1080p 上下文）在最前
    assert_eq!(
        urls[0], "https://cdn.example.com/v/master.m3u8",
        "清晰度排序失败: {urls:?}"
    );
    assert_eq!(r.candidates[0].quality, Some(1080));
    assert_eq!(r.candidates[1].quality, Some(720));
}
