use super::*;

#[path = "../crates/crayon-media-probe/src/protection_tests.rs"]
mod protection_compatibility_tests;

#[test]
fn known_drm_sites() {
    assert!(is_known_drm_site("https://www.netflix.com/watch/123"));
    assert!(!is_known_drm_site("https://example.com/watch"));
}

#[test]
fn cctv_family_detection() {
    assert!(is_cctv_family("https://tv.cctv.com/2026/07/30/VIDE.shtml"));
    assert!(is_cctv_family("https://www.yangshipin.cn/tv/home"));
    assert!(is_cctv_family(
        "https://hls.cntv.lxdns.com/asp/hls/main.m3u8"
    ));
    assert!(is_cctv_family(
        "https://newcntv.qcloudcdn.com/asp/hls/main.m3u8"
    ));
    assert!(!is_cctv_family("https://www.bilibili.com/bangumi/play/ep1"));
    assert!(!is_cctv_family("https://example.com/"));
}

#[test]
fn restricted_reason_only_covers_drm_sites() {
    assert!(restricted_reason(
        "https://tv.cctv.com/2026/07/30/VIDE.shtml",
        "https://newcntv.qcloudcdn.com/x/main.m3u8"
    )
    .is_none());
    assert_eq!(
        restricted_reason("https://www.netflix.com/watch/1", ""),
        Some("全站 DRM 加密")
    );
}
