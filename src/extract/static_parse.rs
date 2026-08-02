//! L1 静态快路径：正则直链扫描、video/source 标签、JSON-LD、maccms 播放器配置。

use base64::Engine as _;
use regex::Regex;
use std::sync::LazyLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Protocol {
    Hls,
    Dash,
    Mp4,
    Flv,
    Other,
}

impl Protocol {
    pub fn as_str(&self) -> &'static str {
        match self {
            Protocol::Hls => "hls",
            Protocol::Dash => "dash",
            Protocol::Mp4 => "mp4",
            Protocol::Flv => "flv",
            Protocol::Other => "other",
        }
    }

    /// 按 URL 路径扩展名分类（忽略 query）。
    pub fn from_url(url: &str) -> Self {
        let path = url::Url::parse(url)
            .map(|u| u.path().to_ascii_lowercase())
            .unwrap_or_else(|_| {
                url.split(['?', '#'])
                    .next()
                    .unwrap_or("")
                    .to_ascii_lowercase()
            });
        if path.ends_with(".m3u8") {
            Protocol::Hls
        } else if path.ends_with(".mpd") {
            Protocol::Dash
        } else if path.ends_with(".mp4") || path.ends_with(".m4s") {
            // .m4s：B 站等 DASH 分轨文件自带 init 段，单体即完整可播 fMP4
            Protocol::Mp4
        } else if path.ends_with(".flv") {
            Protocol::Flv
        } else {
            Protocol::Other
        }
    }
}

/// 一个候选视频地址。
#[derive(Debug, Clone)]
pub struct Candidate {
    pub url: String,
    pub protocol: Protocol,
    /// 数值化清晰度（如 1080），未知为 None。
    pub quality: Option<u32>,
}

/// L1 解析结果。
#[derive(Debug, Default)]
pub struct StaticParseResult {
    pub title: Option<String>,
    pub candidates: Vec<Candidate>,
}

static MEDIA_URL_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"(?i)https?://[^\s"'<>\\]+?\.(?:m3u8|mp4|mpd)(?:\?[^\s"'<>\\]*)?"#).unwrap()
});
static TITLE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?is)<title[^>]*>(.*?)</title>").unwrap());
static VIDEO_TAG_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"(?is)<(?:video|source)\b[^>]*?\s(?:src|data-src)\s*=\s*["']([^"']+)["']"#)
        .unwrap()
});
static JSONLD_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"(?is)<script[^>]*type\s*=\s*["']application/ld\+json["'][^>]*>(.*?)</script>"#)
        .unwrap()
});
static QUALITY_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?i)(\d{3,4})\s*p|\b(4k|fhd|uhd|hd|sd)\b").unwrap());

/// 对 HTML 做 L1 静态解析。`page_url` 用于相对地址转绝对。
pub fn parse_html(html: &str, page_url: &str) -> StaticParseResult {
    let mut result = StaticParseResult {
        title: extract_title(html),
        candidates: Vec::new(),
    };

    // 1) 直链正则扫描：原始文本 + `\/` 转义还原 + percent 解码三种形态
    let unescaped = html.replace("\\/", "/");
    let decoded = percent_encoding::percent_decode_str(html)
        .decode_utf8_lossy()
        .into_owned();
    for text in [html, &unescaped, &decoded] {
        for m in MEDIA_URL_RE.find_iter(text) {
            let raw = m.as_str();
            let ctx_start = m.start().saturating_sub(80);
            let ctx_end = (m.end() + 40).min(text.len());
            let context = &text[ctx_start..ctx_end];
            push_candidate(&mut result.candidates, raw, page_url, Some(context));
        }
    }

    // 2) <video src> / <source src> / data-src
    for cap in VIDEO_TAG_RE.captures_iter(html) {
        let src = cap[1].replace("\\/", "/");
        if looks_like_media(&src) {
            push_candidate(&mut result.candidates, &src, page_url, Some(&cap[0]));
        }
    }

    // 3) JSON-LD VideoObject
    for cap in JSONLD_RE.captures_iter(html) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(cap[1].trim()) {
            walk_jsonld(&v, &mut result, page_url);
        }
    }

    // 4) maccms 风格 player_aaaa 配置
    if let Some(cfg) = extract_player_config(html) {
        if let Some(url) = maccms_config_url(&cfg) {
            let url = url.replace("\\/", "/");
            push_candidate(&mut result.candidates, &url, page_url, Some(html));
        }
    }

    // 去重（保留先出现的）+ 按清晰度降序
    let mut seen = std::collections::HashSet::new();
    result.candidates.retain(|c| seen.insert(c.url.clone()));
    result
        .candidates
        .sort_by_key(|c| std::cmp::Reverse(c.quality.unwrap_or(0)));
    result
}

fn push_candidate(out: &mut Vec<Candidate>, raw: &str, page_url: &str, context: Option<&str>) {
    let Some(abs) = resolve_url(page_url, raw) else {
        return;
    };
    if !abs.starts_with("http://") && !abs.starts_with("https://") {
        return;
    }
    let protocol = Protocol::from_url(&abs);
    if protocol == Protocol::Other {
        return;
    }
    let quality = guess_quality(&abs).or_else(|| context.and_then(guess_quality));
    out.push(Candidate {
        url: abs,
        protocol,
        quality,
    });
}

fn looks_like_media(src: &str) -> bool {
    let lower = src
        .split(['?', '#'])
        .next()
        .unwrap_or("")
        .to_ascii_lowercase();
    lower.ends_with(".m3u8") || lower.ends_with(".mp4") || lower.ends_with(".mpd")
}

/// 相对地址转绝对；已是绝对地址则原样返回。
pub fn resolve_url(base: &str, raw: &str) -> Option<String> {
    let raw = raw.trim();
    if raw.starts_with("http://") || raw.starts_with("https://") {
        return Some(raw.to_string());
    }
    let base = url::Url::parse(base).ok()?;
    base.join(raw).ok().map(|u| u.to_string())
}

fn extract_title(html: &str) -> Option<String> {
    TITLE_RE
        .captures(html)
        .map(|c| c[1].trim().to_string())
        .filter(|t| !t.is_empty())
}

/// 从文本里推测清晰度，返回数值（1080p→1080，4k→2160，hd→720，sd→480）。
pub fn guess_quality(text: &str) -> Option<u32> {
    let cap = QUALITY_RE.captures(text)?;
    if let Some(n) = cap.get(1) {
        return n.as_str().parse().ok();
    }
    match cap.get(2)?.as_str().to_ascii_lowercase().as_str() {
        "4k" | "uhd" => Some(2160),
        "fhd" => Some(1080),
        "hd" => Some(720),
        "sd" => Some(480),
        _ => None,
    }
}

fn walk_jsonld(v: &serde_json::Value, result: &mut StaticParseResult, page_url: &str) {
    match v {
        serde_json::Value::Array(arr) => {
            for item in arr {
                walk_jsonld(item, result, page_url);
            }
        }
        serde_json::Value::Object(map) => {
            let is_video = match map.get("@type") {
                Some(serde_json::Value::String(t)) => t == "VideoObject",
                Some(serde_json::Value::Array(ts)) => ts.iter().any(|t| t == "VideoObject"),
                _ => false,
            };
            if is_video {
                if let Some(name) = map.get("name").and_then(|n| n.as_str()) {
                    if result.title.is_none() {
                        result.title = Some(name.to_string());
                    }
                }
                for key in ["contentUrl", "embedUrl", "url"] {
                    if let Some(u) = map.get(key) {
                        collect_jsonld_urls(u, result, page_url);
                    }
                }
            }
            if let Some(graph) = map.get("@graph") {
                walk_jsonld(graph, result, page_url);
            }
        }
        _ => {}
    }
}

fn collect_jsonld_urls(v: &serde_json::Value, result: &mut StaticParseResult, page_url: &str) {
    match v {
        serde_json::Value::String(s) => {
            if looks_like_media(s) {
                push_candidate(&mut result.candidates, s, page_url, None);
            }
        }
        serde_json::Value::Array(arr) => {
            for item in arr {
                collect_jsonld_urls(item, result, page_url);
            }
        }
        _ => {}
    }
}

/// 提取 `var player_aaaa = {...}` 形式的配置 JSON（括号平衡扫描）。
fn extract_player_config(html: &str) -> Option<serde_json::Value> {
    let idx = html.find("player_aaaa")?;
    let rest = &html[idx..];
    let start = rest.find('{')?;
    let bytes = rest.as_bytes();
    let mut depth = 0i32;
    let mut in_str = false;
    let mut escape = false;
    let mut end = None;
    for (i, &b) in bytes.iter().enumerate().skip(start) {
        if in_str {
            if escape {
                escape = false;
            } else if b == b'\\' {
                escape = true;
            } else if b == b'"' {
                in_str = false;
            }
            continue;
        }
        match b {
            b'"' => in_str = true,
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    end = Some(i + 1);
                    break;
                }
            }
            _ => {}
        }
    }
    let end = end?;
    serde_json::from_str(&rest[start..end]).ok()
}

/// 从 maccms 配置中取播放地址：encrypt 0 明文 / 1 urlencode / 2 base64。
fn maccms_config_url(cfg: &serde_json::Value) -> Option<String> {
    let url = cfg.get("url")?.as_str()?;
    let encrypt = cfg
        .get("encrypt")
        .and_then(|e| e.as_u64().or_else(|| e.as_str()?.parse().ok()))
        .unwrap_or(0);
    match encrypt {
        1 => Some(
            percent_encoding::percent_decode_str(url)
                .decode_utf8_lossy()
                .into_owned(),
        ),
        2 => base64::engine::general_purpose::STANDARD
            .decode(url)
            .ok()
            .and_then(|b| String::from_utf8(b).ok()),
        _ => Some(url.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn protocol_classification() {
        assert_eq!(
            Protocol::from_url("https://a.com/x/index.m3u8?token=1"),
            Protocol::Hls
        );
        assert_eq!(Protocol::from_url("https://a.com/x.mp4"), Protocol::Mp4);
        assert_eq!(
            Protocol::from_url("https://a.com/x_da2-1-30032.m4s?upsig=a"),
            Protocol::Mp4
        );
        assert_eq!(Protocol::from_url("https://a.com/x.flv"), Protocol::Flv);
        assert_eq!(Protocol::from_url("https://a.com/x.mpd"), Protocol::Dash);
        assert_eq!(Protocol::from_url("https://a.com/x.html"), Protocol::Other);
    }

    #[test]
    fn quality_guess() {
        assert_eq!(guess_quality("https://a.com/1080p/index.m3u8"), Some(1080));
        assert_eq!(guess_quality("4K 超清"), Some(2160));
        assert_eq!(guess_quality("nothing here"), None);
    }
}
