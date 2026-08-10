use super::*;

#[test]
fn ssrf_blocklist() {
    for u in [
        "http://127.0.0.1/x",
        "http://localhost/x",
        "http://10.0.0.1/x",
        "http://192.168.1.1/x",
        "http://172.16.0.1/x",
        "http://172.31.255.1/x",
        "http://169.254.169.254/latest/meta-data",
        "http://[::1]/x",
    ] {
        assert!(is_blocked_host(&url::Url::parse(u).unwrap()), "{u}");
    }
    for u in [
        "http://172.32.0.1/x",
        "https://example.com/x",
        "http://8.8.8.8/x",
    ] {
        assert!(!is_blocked_host(&url::Url::parse(u).unwrap()), "{u}");
    }
}

#[test]
fn percent_encoding_validation() {
    assert!(valid_percent_encoding("https%3A%2F%2Fa.com%2Fx.m3u8"));
    assert!(valid_percent_encoding("plain"));
    assert!(!valid_percent_encoding("bad%2"));
    assert!(!valid_percent_encoding("bad%zz"));
    assert!(!valid_percent_encoding("100%"));
}

#[test]
fn rewrite_basic() {
    let text = "#EXTM3U\n#EXT-X-VERSION:3\n#EXTINF:5.0,\nseg1.ts\n#EXT-X-ENDLIST\n";
    let out = rewrite_m3u8(
        text,
        "http://up.example.com/live/index.m3u8",
        "127.0.0.1:8321",
        Some("http://up.example.com/"),
        None,
        0,
    );
    assert!(out.contains("#EXTINF:5.0,"));
    assert!(out.contains(
        "http://127.0.0.1:8321/proxy/http%3A%2F%2Fup.example.com%2Flive%2Fseg1.ts/seg1.ts?referer=http%3A%2F%2Fup.example.com%2F"
    ));
}
