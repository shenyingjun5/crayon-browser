//! DRM 特征检测：只做检测标记，不做解密、不接 CDM。

/// 已知全 DRM 站点名单（前置标记，命中即 drm:true）。
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

/// 已知 DRM 系统的 KEYFORMAT / UUID 关键词（小写匹配）。
/// - com.apple.*  → FairPlay
/// - edef8ba9-... → Widevine
/// - 9a04f079-... → PlayReady
/// - com.microsoft.playready / com.widevine → 其它常见标识
const DRM_KEYFORMAT_MARKERS: &[&str] = &[
    "com.apple",
    "edef8ba9",
    "9a04f079",
    "com.microsoft.playready",
    "com.widevine",
    "widevine",
    "playready",
    "fairplay",
];

/// 按站点名单做前置 DRM 判断。
pub fn is_known_drm_site(url: &str) -> bool {
    let host = url::Url::parse(url)
        .ok()
        .and_then(|u| u.host_str().map(|h| h.to_ascii_lowercase()))
        .unwrap_or_default();
    DRM_SITES
        .iter()
        .any(|s| host == *s || host.ends_with(&format!(".{s}")))
}

/// 检测 HLS（m3u8）文本是否为 DRM 加密。
///
/// 规则（docs/design.md §4）：
/// - `#EXT-X-KEY` 的 METHOD 为 NONE（或无 KEY 行）→ 非 DRM；
/// - METHOD=SAMPLE-AES → DRM；
/// - KEYFORMAT 命中已知 DRM 标识（FairPlay/Widevine/PlayReady…）→ DRM；
/// - METHOD=AES-128 且 KEYFORMAT 缺省或为 identity（key 公开可拉）→ 非 DRM（可播）。
pub fn hls_is_drm(m3u8_text: &str) -> bool {
    for line in m3u8_text.lines() {
        let line = line.trim();
        if !line.starts_with("#EXT-X-KEY:") {
            continue;
        }
        let attrs = &line["#EXT-X-KEY:".len()..];
        let method = attr_value(attrs, "METHOD").unwrap_or_default();
        if method.eq_ignore_ascii_case("NONE") {
            continue;
        }
        let keyformat = attr_value(attrs, "KEYFORMAT").unwrap_or_default();
        let kf = keyformat.to_ascii_lowercase();
        if !kf.is_empty() && kf != "identity" {
            // 显式 KEYFORMAT 且非 identity：命中已知标识或非公开 key 体系，一律按 DRM 处理
            return true;
        }
        if method.eq_ignore_ascii_case("SAMPLE-AES")
            || method.to_ascii_uppercase().starts_with("SAMPLE-AES")
        {
            return true;
        }
        // METHOD=AES-128、无 KEYFORMAT：公开 key，非 DRM
    }
    false
}

/// 检测 DASH（mpd）文本是否含 ContentProtection。
pub fn mpd_is_drm(mpd_text: &str) -> bool {
    let lower = mpd_text.to_ascii_lowercase();
    if !lower.contains("<contentprotection") {
        return false;
    }
    // 存在 ContentProtection 元素即按 DRM 标记（含 cenc/widevine/playready 等）
    true
}

/// 供测试与内部使用：KEYFORMAT 标识是否命中已知 DRM 关键词。
pub fn keyformat_is_drm(keyformat: &str) -> bool {
    let kf = keyformat.to_ascii_lowercase();
    DRM_KEYFORMAT_MARKERS.iter().any(|m| kf.contains(m))
}

/// 从 `#EXT-X-KEY:...` 属性串里取某个属性的值（支持带引号与不带引号）。
fn attr_value(attrs: &str, name: &str) -> Option<String> {
    // 简单扫描：NAME="value" 或 NAME=value
    let pat = format!("{name}=");
    let start = attrs.find(&pat)? + pat.len();
    let rest = &attrs[start..];
    if let Some(stripped) = rest.strip_prefix('"') {
        let end = stripped.find('"')?;
        Some(stripped[..end].to_string())
    } else {
        let end = rest.find(',').unwrap_or(rest.len());
        Some(rest[..end].trim().to_string())
    }
}

#[cfg(test)]
mod tests {
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
        let mpd =
            r#"<?xml version="1.0"?><MPD><AdaptationSet><Representation/></AdaptationSet></MPD>"#;
        assert!(!mpd_is_drm(mpd));
    }

    #[test]
    fn known_drm_sites() {
        assert!(is_known_drm_site("https://www.netflix.com/watch/123"));
        assert!(!is_known_drm_site("https://example.com/watch"));
    }
}
