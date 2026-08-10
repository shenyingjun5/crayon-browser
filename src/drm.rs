//! Legacy site policy built on the formal DRM/protection detector.

pub use crayon_media_probe::{hls_is_drm, keyformat_is_drm, mpd_is_drm};

const DRM_SITES: &[&str] = &[
    "netflix.com",
    "disneyplus.com",
    "hulu.com",
    "hbomax.com",
    "max.com",
    "primevideo.com",
    "tv.apple.com",
    "spotify.com",
    "open.spotify.com",
];

const CCTV_FAMILY: &[&str] = &[
    "cctv.com",
    "cctv.cn",
    "cntv.cn",
    "cntv.com",
    "yangshipin.cn",
    "cntv.lxdns.com",
    "newcntv.qcloudcdn.com",
];

pub fn is_known_drm_site(url: &str) -> bool {
    host_matches(url, DRM_SITES)
}

/// 命中不代表受限；只触发 legacy app 的实际画面探测。
pub fn is_cctv_family(url: &str) -> bool {
    host_matches(url, CCTV_FAMILY)
}

fn host_matches(url: &str, sites: &[&str]) -> bool {
    let host = url::Url::parse(url)
        .ok()
        .and_then(|url| url.host_str().map(str::to_ascii_lowercase))
        .unwrap_or_default();
    sites
        .iter()
        .any(|site| host == *site || host.ends_with(&format!(".{site}")))
}

/// Legacy synchronous restriction reason. Formal policy uses typed decisions.
pub fn restricted_reason(page_url: &str, stream_url: &str) -> Option<&'static str> {
    if is_known_drm_site(page_url) || is_known_drm_site(stream_url) {
        return Some("全站 DRM 加密");
    }
    None
}

#[cfg(test)]
#[path = "drm_tests.rs"]
mod tests;
