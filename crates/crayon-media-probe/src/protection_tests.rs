use super::*;

#[test]
fn hls_aes128_is_not_drm() {
    let text = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"oceans.key\"\n#EXTINF:5,\nseg.ts\n";
    assert!(!hls_is_drm(text));
}

#[test]
fn hls_fairplay_is_drm() {
    let text = "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://foo\",KEYFORMAT=\"com.apple.streamingkeydelivery\"\n";
    assert!(hls_is_drm(text));
}

#[test]
fn hls_widevine_keyformat_is_drm() {
    let text = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\",KEYFORMAT=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\"\n";
    assert!(hls_is_drm(text));
}

#[test]
fn hls_method_none_not_drm() {
    let text = "#EXTM3U\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:5,\nseg.ts\n";
    assert!(!hls_is_drm(text));
}

#[test]
fn mpd_with_content_protection_is_drm() {
    let mpd = r#"<?xml version="1.0"?><MPD><AdaptationSet><ContentProtection schemeIdUri="urn:uuid:9a04f079-9840-4286-ab92-e65be0885f95"/></AdaptationSet></MPD>"#;
    assert!(mpd_is_drm(mpd));
}

#[test]
fn mpd_without_content_protection_not_drm() {
    let mpd = r#"<?xml version="1.0"?><MPD><AdaptationSet><Representation/></AdaptationSet></MPD>"#;
    assert!(!mpd_is_drm(mpd));
}

#[test]
fn keyformat_markers_are_case_insensitive_and_bounded() {
    assert!(keyformat_is_drm("URN:UUID:EDEF8BA9-79D6-4ACE-A3C8"));
    assert!(!keyformat_is_drm(""));
    assert!(!keyformat_is_drm("identity"));
}

#[test]
fn unknown_non_identity_keyformat_fails_closed() {
    let text = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"key\",KEYFORMAT=\"vendor.private\"\n";

    assert!(hls_is_drm(text));
}
