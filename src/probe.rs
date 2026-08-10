//! Legacy WebView probe compatibility built on platform-neutral frame verdicts.

pub use crayon_media_probe::{frames_degenerate, probe_verdict, FrameStat, ProbeVerdict};

pub const SCRAMBLED_REASON: &str = "WASM 私有加扰，实测解码画面异常，无法播放";
pub const LOAD_FAILED_REASON: &str = "流地址失效或加载失败，无法播放";

/// Legacy WebView codec gate. Formal shells must use platform capabilities.
pub fn webview_can_judge(codec: Option<&str>) -> bool {
    let Some(label) = codec else { return true };
    let video = label.split(['+', '·']).next().unwrap_or("").trim();
    !matches!(video, "HEVC" | "H.265" | "AV1" | "VP9" | "VP8" | "AV01")
}

#[cfg(test)]
#[path = "probe_tests.rs"]
mod tests;
