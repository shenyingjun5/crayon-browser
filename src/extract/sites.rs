//! 站点专用解析器（首批：央视网 tv.cctv.com / cntv 点播）。
//!
//! 与 L3 正则规则包的差异：这类站点的播放地址不在 HTML 文本里，
//! 需要「从页面提取视频 ID → 调站点公开 API → 解析 JSON」两步才能拿到，
//! 规则包的正则无法表达这种流程，用 Rust 代码实现。

use super::static_parse::{Candidate, Protocol};
use regex::Regex;
use serde::Deserialize;
use std::sync::LazyLock;

/// 站点解析结果。
#[derive(Debug, Default)]
pub struct SiteResult {
    pub title: Option<String>,
    pub candidates: Vec<Candidate>,
    /// 该站播放所需 Referer（缺省由调用方用页面 origin）。
    pub referer: Option<String>,
}

/// 央视播放信息 API（design.md §3 L3 的「站点公开 API」路径）。
pub const CNTV_API: &str = "https://vdn.apps.cntv.cn/api/getHttpVideoInfo.do";

/// 入口：按页面域名分发到具体站点解析器。不匹配任何站点返回 None。
pub async fn extract(client: &reqwest::Client, page_url: &str, html: &str) -> Option<SiteResult> {
    let host = url::Url::parse(page_url)
        .ok()
        .and_then(|u| u.host_str().map(|h| h.to_ascii_lowercase()))?;
    if host == "cctv.com"
        || host.ends_with(".cctv.com")
        || host == "cntv.cn"
        || host.ends_with(".cntv.cn")
    {
        return extract_cntv(client, html, CNTV_API).await;
    }
    None
}

/// 页面内 `var guid = "4646c21e...";`（32 位 hex 视频 ID）。
static GUID_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r#"guid\s*=\s*"([0-9a-fA-F]{32})""#).unwrap());

#[derive(Debug, Deserialize)]
struct CntvResponse {
    #[serde(default)]
    ack: String,
    #[serde(default)]
    title: String,
    #[serde(default)]
    hls_url: String,
    /// "1" 表示版权受限不可播。
    #[serde(default)]
    is_invalid_copyright: String,
    #[serde(default)]
    video: Option<CntvVideo>,
}

#[derive(Debug, Deserialize)]
struct CntvVideo {
    /// 分段 mp4 直链（可能为空字符串，需过滤）。
    #[serde(default)]
    chapters: Vec<CntvChapter>,
}

#[derive(Debug, Deserialize)]
struct CntvChapter {
    #[serde(default)]
    url: String,
}

/// 央视网点播：HTML 提 guid → getHttpVideoInfo.do → hls_url / 分段 mp4。
/// `api_base` 参数化以便夹具测试注入 mock 地址。
pub async fn extract_cntv(
    client: &reqwest::Client,
    html: &str,
    api_base: &str,
) -> Option<SiteResult> {
    let guid = GUID_RE.captures(html)?.get(1)?.as_str().to_string();
    let resp = client
        .get(api_base)
        .query(&[("pid", guid.as_str())])
        .send()
        .await
        .ok()?;
    let text = resp.text().await.ok()?;
    let data: CntvResponse = serde_json::from_str(&text).ok()?;
    Some(result_from_cntv(&data))
}

fn result_from_cntv(data: &CntvResponse) -> SiteResult {
    let mut out = SiteResult {
        title: if data.title.is_empty() {
            None
        } else {
            Some(data.title.clone())
        },
        ..Default::default()
    };
    if data.ack != "yes" || data.is_invalid_copyright == "1" {
        return out;
    }
    let push = |url: &str, out: &mut SiteResult| {
        let url = url.trim();
        let protocol = Protocol::from_url(url);
        if url.is_empty() || protocol == Protocol::Other {
            return;
        }
        if out.candidates.iter().any(|c| c.url == url) {
            return;
        }
        out.candidates.push(Candidate {
            url: url.to_string(),
            protocol,
            quality: None,
        });
    };
    push(&data.hls_url, &mut out);
    if let Some(video) = &data.video {
        for ch in &video.chapters {
            push(&ch.url, &mut out);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn guid_regex() {
        let html =
            r#"<script>var guid = "4646c21e429d43a08eac19d18704c4e9"; var guid1 = guid;</script>"#;
        let cap = GUID_RE.captures(html).unwrap();
        assert_eq!(&cap[1], "4646c21e429d43a08eac19d18704c4e9");
        // 非 hex / 无引号不命中
        assert!(GUID_RE.captures("var guid1 = guid;").is_none());
    }

    #[test]
    fn parse_real_response() {
        // 真实 API 响应裁剪版（2026-08-02 tv.cctv.com 纪录片）
        let json = r#"{
            "ack": "yes",
            "title": "《人民大街》 第1集 起点",
            "hls_url": "https://hls.cntv.lxdns.com/asp/hls/main/0303000a/3/default/4646c21e429d43a08eac19d18704c4e9/main.m3u8?maxbr=2048",
            "is_protected": "0",
            "is_invalid_copyright": "0",
            "video": {
                "validChapterNum": 4,
                "chapters": [
                    {"duration": "300.00", "url": ""},
                    {"duration": "300.00", "url": ""}
                ]
            }
        }"#;
        let data: CntvResponse = serde_json::from_str(json).unwrap();
        let r = result_from_cntv(&data);
        assert_eq!(r.title.as_deref(), Some("《人民大街》 第1集 起点"));
        assert_eq!(r.candidates.len(), 1, "空 chapter 地址应被过滤");
        assert_eq!(r.candidates[0].protocol, Protocol::Hls);
        assert!(r.candidates[0].url.contains("main.m3u8"));
    }

    #[test]
    fn copyright_invalid_yields_no_candidates() {
        let json = r#"{"ack":"yes","title":"x","hls_url":"https://a.com/x.m3u8","is_invalid_copyright":"1"}"#;
        let data: CntvResponse = serde_json::from_str(json).unwrap();
        let r = result_from_cntv(&data);
        assert!(r.candidates.is_empty());
    }

    #[test]
    fn chapters_mp4_collected() {
        let json = r#"{"ack":"yes","title":"x","hls_url":"","video":{"chapters":[{"url":"https://a.com/1.mp4"},{"url":"https://a.com/2.mp4"}]}}"#;
        let data: CntvResponse = serde_json::from_str(json).unwrap();
        let r = result_from_cntv(&data);
        assert_eq!(r.candidates.len(), 2);
        assert_eq!(r.candidates[0].protocol, Protocol::Mp4);
    }
}
