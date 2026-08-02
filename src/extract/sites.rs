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
    /// 附加说明（如「DASH 音画分轨需合并」「仅试看」），透传到 VideoInfo.note。
    pub note: Option<String>,
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
    if host == "bilibili.com" || host.ends_with(".bilibili.com") {
        return extract_bilibili(client, page_url, BILI_PGC_API).await;
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

// ---------------------------------------------------------------------------
// B 站（bilibili）番剧
// ---------------------------------------------------------------------------

/// B 站番剧播放 API（pgc = professional generated content）。
pub const BILI_PGC_API: &str = "https://api.bilibili.com/pgc/player/web/playurl";

/// 番剧页 URL 提 ep_id：`/bangumi/play/ep733316`。
static EP_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"/bangumi/play/ep(\d+)").unwrap());

#[derive(Debug, Deserialize)]
struct BiliPlayurl {
    #[serde(default)]
    code: i64,
    result: Option<BiliResult>,
}

#[derive(Debug, Deserialize)]
struct BiliResult {
    /// 1 = 仅试看片段。
    #[serde(default)]
    is_preview: i64,
    /// true = DRM 内容（红线：跳过不出地址）。
    #[serde(default)]
    is_drm: bool,
    /// 实际清晰度 qn 编号（经 qn_to_height 换算）。
    #[serde(default)]
    quality: i64,
    /// fnval=1：渐进式整段（音画合一，单 URL 可播）。
    #[serde(default)]
    durl: Vec<BiliDurl>,
    /// fnval=16：DASH 音画分轨（m4s）。
    #[serde(default)]
    dash: Option<BiliDash>,
}

#[derive(Debug, Deserialize)]
struct BiliDurl {
    #[serde(default)]
    url: String,
}

#[derive(Debug, Deserialize)]
struct BiliDash {
    #[serde(default)]
    video: Vec<BiliStream>,
    #[serde(default)]
    audio: Vec<BiliStream>,
}

#[derive(Debug, Deserialize)]
struct BiliStream {
    /// 清晰度 qn 编号。
    #[serde(default)]
    id: i64,
    #[serde(default, alias = "baseUrl")]
    base_url: String,
    #[serde(default)]
    height: Option<u32>,
}

/// B 站番剧：URL 提 ep_id → pgc/playurl → 整段 mp4（优先）或 DASH 分轨（兜底）。
/// `api_base` 参数化以便夹具测试注入 mock 地址。
/// 可选环境变量 `GET_VIDEO_BILI_COOKIE`：携带用户自己的登录 Cookie 解锁
/// 更高清晰度（未登录仅 360P 整段 / 480P 分轨；会员内容仍按其权限返回）。
pub async fn extract_bilibili(
    client: &reqwest::Client,
    page_url: &str,
    api_base: &str,
) -> Option<SiteResult> {
    let ep_id = EP_RE.captures(page_url)?.get(1)?.as_str().to_string();
    let cookie = std::env::var("GET_VIDEO_BILI_COOKIE")
        .ok()
        .filter(|c| !c.is_empty());
    let get = |fnval: u32| {
        let fv = fnval.to_string();
        let mut req = client.get(api_base).query(&[
            ("ep_id", ep_id.as_str()),
            ("qn", "116"),
            ("fourk", "1"),
            ("fnval", fv.as_str()),
        ]);
        if let Some(c) = &cookie {
            req = req.header(reqwest::header::COOKIE, c.as_str());
        }
        req
    };
    let mut out = SiteResult {
        referer: Some("https://www.bilibili.com".into()),
        ..Default::default()
    };
    // 优先 fnval=1：durl 渐进式整段（音画合一）
    if let Some(r) = fetch_bili(get(1)).await {
        if r.is_drm {
            out.note = Some("B站：该内容标记为 DRM，按红线不出地址".into());
            return Some(out);
        }
        push_durl(&mut out, &r);
        if !out.candidates.is_empty() {
            if r.is_preview == 1 {
                out.note = Some("B站：仅试看片段，完整内容需登录/会员".into());
            }
            return Some(out);
        }
    }
    // 兜底 fnval=16：DASH 音画分轨（视频轨无声、音频轨无画面）
    if let Some(r) = fetch_bili(get(16)).await {
        if r.is_drm {
            out.note = Some("B站：该内容标记为 DRM，按红线不出地址".into());
            return Some(out);
        }
        push_dash(&mut out, &r);
        if !out.candidates.is_empty() {
            out.note =
                Some("B站 DASH 音画分离：视频轨无声、音频轨无画面，需应用侧双轨合并后播放".into());
        } else if r.is_preview == 1 {
            out.note = Some("B站：仅试看片段，完整内容需登录/会员".into());
        }
    }
    Some(out)
}

async fn fetch_bili(req: reqwest::RequestBuilder) -> Option<BiliResult> {
    let text = req.send().await.ok()?.text().await.ok()?;
    let data: BiliPlayurl = serde_json::from_str(&text).ok()?;
    if data.code != 0 {
        return None;
    }
    data.result
}

fn push_durl(out: &mut SiteResult, r: &BiliResult) {
    for d in &r.durl {
        push_stream(out, &d.url, qn_to_height(r.quality));
    }
}

fn push_dash(out: &mut SiteResult, r: &BiliResult) {
    let Some(dash) = &r.dash else { return };
    for v in &dash.video {
        push_stream(out, &v.base_url, v.height.or_else(|| qn_to_height(v.id)));
    }
    for a in &dash.audio {
        push_stream(out, &a.base_url, None);
    }
}

fn push_stream(out: &mut SiteResult, url: &str, quality: Option<u32>) {
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
        quality,
    });
}

/// B 站 qn 清晰度编号 → 高度像素（用于清晰度展示与排序）。
fn qn_to_height(qn: i64) -> Option<u32> {
    Some(match qn {
        120 => 2160,            // 4K
        116 | 112 | 80 => 1080, // 1080P60 / 高码率 / 1080P
        74 | 64 => 720,
        32 => 480,
        16 => 360,
        _ => return None,
    })
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

    // ------------------------------------------------------------------
    // B 站
    // ------------------------------------------------------------------

    #[test]
    fn bili_ep_regex() {
        let cap = EP_RE
            .captures("https://www.bilibili.com/bangumi/play/ep733316?spm_id_from=333.337.0.0")
            .unwrap();
        assert_eq!(&cap[1], "733316");
        assert!(EP_RE
            .captures("https://www.bilibili.com/video/BV1xx")
            .is_none());
    }

    #[test]
    fn bili_qn_mapping() {
        assert_eq!(qn_to_height(120), Some(2160));
        assert_eq!(qn_to_height(80), Some(1080));
        assert_eq!(qn_to_height(16), Some(360));
        assert_eq!(qn_to_height(0), None);
    }

    #[test]
    fn bili_durl_parse() {
        // 真实 API 响应裁剪版（2026-08-02 fnval=1 未登录，360P 整段）
        let json = r#"{"code":0,"result":{"is_preview":0,"is_drm":false,"quality":16,
            "durl":[{"url":"https://cn-gddg-ct-01-10.bilivideo.com/upgcxcode/93/06/x.mp4?upsig=abc"}]}}"#;
        let data: BiliPlayurl = serde_json::from_str(json).unwrap();
        let r = data.result.unwrap();
        let mut out = SiteResult::default();
        push_durl(&mut out, &r);
        assert_eq!(out.candidates.len(), 1);
        assert_eq!(out.candidates[0].protocol, Protocol::Mp4);
        assert_eq!(out.candidates[0].quality, Some(360));
    }

    #[test]
    fn bili_dash_parse() {
        // 真实 API 响应裁剪版（2026-08-02 fnval=16 未登录，DASH 分轨）
        let json = r#"{"code":0,"result":{"is_preview":0,"is_drm":false,"quality":32,"durl":[],
            "dash":{
                "video":[{"id":32,"baseUrl":"https://upos.bilivideo.com/v.m4s?upsig=a","height":480},
                         {"id":16,"base_url":"https://upos.bilivideo.com/v2.m4s?upsig=b","height":360}],
                "audio":[{"id":30216,"baseUrl":"https://upos.bilivideo.com/a.m4s?upsig=c"}]
            }}}"#;
        let data: BiliPlayurl = serde_json::from_str(json).unwrap();
        let r = data.result.unwrap();
        let mut out = SiteResult::default();
        push_dash(&mut out, &r);
        assert_eq!(out.candidates.len(), 3);
        assert_eq!(out.candidates[0].quality, Some(480));
        assert_eq!(out.candidates[2].quality, None, "音频轨无清晰度");
        assert!(out.candidates.iter().all(|c| c.protocol == Protocol::Mp4));
    }

    #[test]
    fn bili_drm_skipped_by_caller() {
        // is_drm=true 时调用方直接返回空 candidates + note，这里只验证字段解析
        let json = r#"{"code":0,"result":{"is_drm":true,"durl":[]}}"#;
        let data: BiliPlayurl = serde_json::from_str(json).unwrap();
        assert!(data.result.unwrap().is_drm);
    }
}
