use super::*;

#[test]
fn rule_pack_match() {
    let json = r#"[{
        "name": "demo",
        "domains": ["example.com"],
        "pattern": "videoUrl\\s*=\\s*\"(?<url>[^\"]+\\.m3u8)\"",
        "referer": "https://example.com/"
    }]"#;
    let pack = RulePack::from_json(json).unwrap();
    let html = r#"<script>videoUrl = "https://cdn.example.com/a/b.m3u8";</script>"#;
    let hits = pack.apply("https://www.example.com/watch/1", html);
    assert_eq!(hits.len(), 1);
    assert_eq!(hits[0].url, "https://cdn.example.com/a/b.m3u8");
    // 域名不匹配则不命中
    assert!(pack.apply("https://other.com/", html).is_empty());
}

#[test]
fn rule_pack_rejects_bad_regex() {
    let json = r#"[{"name":"bad","domains":["x.com"],"pattern":"(.*)"}]"#;
    assert!(RulePack::from_json(json).is_err());
}
