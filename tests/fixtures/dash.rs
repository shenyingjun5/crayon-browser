use super::*;

#[tokio::test]
async fn d4_dash_vod_no_drm() {
    let f = extract_format_for("clean_dash.html").await;
    assert!(!f.drm, "无 ContentProtection 的 DASH 应为 drm:false");
    assert!(f.relay_url.is_some());
    assert_eq!(f.protocol, "dash");
}
