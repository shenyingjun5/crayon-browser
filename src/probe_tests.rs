use super::*;

#[path = "../crates/crayon-media-probe/src/frame_tests.rs"]
mod frame_compatibility_tests;

#[test]
fn webview_can_judge_by_codec() {
    assert!(webview_can_judge(Some("H.264+AAC · TS")));
    assert!(webview_can_judge(Some("H.264+AAC · MP4")));
    assert!(webview_can_judge(Some("· fMP4")));
    assert!(webview_can_judge(None));
    assert!(!webview_can_judge(Some("HEVC+AAC · TS")));
    assert!(!webview_can_judge(Some("HEVC+AAC · DASH(fMP4)")));
    assert!(!webview_can_judge(Some("AV1+AAC · fMP4")));
    assert!(!webview_can_judge(Some("VP9+AAC · fMP4")));
}
