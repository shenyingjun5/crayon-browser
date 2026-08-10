use super::*;

#[path = "../crates/crayon-media-probe/src/codec_tests.rs"]
mod parser_compatibility_tests;

#[test]
fn master_first_variant() {
    let text = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nsub/v1.m3u8\n";
    assert_eq!(
        first_media_line(text, "http://a.com/live/master.m3u8").as_deref(),
        Some("http://a.com/live/sub/v1.m3u8")
    );
}

#[test]
fn ext_x_map_uri_parse() {
    let text =
        "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4?sign=abc\",BYTERANGE=\"720@0\"\n#EXTINF:5,\ns1.m4s\n";
    assert_eq!(ext_x_map_uri(text).as_deref(), Some("init.mp4?sign=abc"));
    assert_eq!(ext_x_map_uri("#EXTM3U\n#EXTINF:5,\ns1.ts\n"), None);
}
