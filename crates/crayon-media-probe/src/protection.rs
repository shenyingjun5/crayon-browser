//! DRM/protection 特征检测：只做检测标记，不做解密、不接 CDM。

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
#[path = "protection_tests.rs"]
mod tests;
