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
/// `cookie` 为可选的站点登录态（来自应用 webview Cookie 存储），
/// 目前仅 B 站解析器使用（解锁更高清晰度）。
pub async fn extract(
    client: &reqwest::Client,
    page_url: &str,
    html: &str,
    cookie: Option<&str>,
) -> Option<SiteResult> {
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
        return extract_bilibili(client, page_url, html, &BiliEndpoints::production(), cookie)
            .await;
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
        out.candidates
            .push(Candidate::single(url.to_string(), protocol, None));
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
// B 站（bilibili）：番剧 ep/ss 页 + 普通视频 BV 页
// ---------------------------------------------------------------------------

/// B 站播放/稿件 API。
pub const BILI_PGC_API: &str = "https://api.bilibili.com/pgc/player/web/playurl";
pub const BILI_UGC_API: &str = "https://api.bilibili.com/x/player/playurl";
pub const BILI_VIEW_API: &str = "https://api.bilibili.com/x/web-interface/view";

/// B 站 API 端点（生产用 `BiliEndpoints::production()`；夹具测试注入 mock 地址）。
#[derive(Debug, Clone)]
pub struct BiliEndpoints {
    pub pgc: String,
    pub ugc: String,
    pub view: String,
}

impl BiliEndpoints {
    pub fn production() -> Self {
        Self {
            pgc: BILI_PGC_API.into(),
            ugc: BILI_UGC_API.into(),
            view: BILI_VIEW_API.into(),
        }
    }
}

/// 番剧页 URL：`/bangumi/play/ep733316`（单集）/ `ss28747`（整季）。
static EP_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"/bangumi/play/ep(\d+)").unwrap());
static SS_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"/bangumi/play/ss(\d+)").unwrap());
/// ss 季页 HTML 内含默认集的 `"ep_id":733316`（playurl 不接受 season_id，需转 ep）。
static EPID_JSON_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r#""ep_id":(\d+)"#).unwrap());
/// 普通视频页：`/video/BV1xx411c7mD`。
static BV_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"/video/(BV[0-9A-Za-z]+)").unwrap());

#[derive(Debug, Deserialize)]
struct BiliPlayurl {
    #[serde(default)]
    code: i64,
    /// pgc（番剧）包裹字段。
    result: Option<BiliResult>,
    /// ugc（普通视频）包裹字段。
    data: Option<BiliResult>,
}

impl BiliPlayurl {
    fn into_result(self) -> Option<BiliResult> {
        if self.code != 0 {
            return None;
        }
        self.result.or(self.data)
    }
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
    /// 时长（毫秒），生成 MPD 用。
    #[serde(default)]
    timelength: Option<u64>,
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
    /// B 站响应里 base_url 与 baseUrl 会同时出现，alias 会被 serde 判重复，
    /// 因此拆成两个可选字段取值（见 base()）。
    #[serde(default)]
    base_url: Option<String>,
    #[serde(default, rename = "baseUrl")]
    base_url_camel: Option<String>,
    #[serde(default)]
    height: Option<u32>,
    #[serde(default)]
    width: Option<u32>,
    #[serde(default)]
    bandwidth: Option<u64>,
    #[serde(default)]
    codecs: Option<String>,
}

impl BiliStream {
    fn base(&self) -> String {
        self.base_url
            .clone()
            .or_else(|| self.base_url_camel.clone())
            .unwrap_or_default()
    }
}

#[derive(Debug, Deserialize)]
struct BiliView {
    #[serde(default)]
    code: i64,
    data: Option<BiliViewData>,
}

#[derive(Debug, Deserialize)]
struct BiliViewData {
    #[serde(default)]
    title: String,
    #[serde(default)]
    pages: Vec<BiliPage>,
}

#[derive(Debug, Deserialize)]
struct BiliPage {
    #[serde(default)]
    cid: i64,
    #[serde(default)]
    page: i64,
    #[serde(default)]
    part: String,
}

/// B 站：番剧 ep/ss 页走 pgc/playurl(ep_id)，普通 BV 页走 view(bvid) →
/// x/player/playurl(bvid+cid)。整段 mp4（fnval=1）优先，DASH 分轨（fnval=16）兜底。
/// `html` 仅 ss 季页用于提取默认集 ep_id 时需要，其余入口可传空串。
/// 登录 Cookie 解锁更高清晰度（会员内容仍按其权限返回）：
/// 优先用调用方传入的 `cookie`（应用 webview 登录态），
/// 否则回退环境变量 `GET_VIDEO_BILI_COOKIE`（无头/CLI 调试用）。
pub async fn extract_bilibili(
    client: &reqwest::Client,
    page_url: &str,
    html: &str,
    api: &BiliEndpoints,
    cookie: Option<&str>,
) -> Option<SiteResult> {
    let cookie = cookie
        .map(|c| c.to_string())
        .or_else(|| std::env::var("GET_VIDEO_BILI_COOKIE").ok())
        .filter(|c| !c.is_empty());
    // 测试钩子：fnval=16 拿不到高清时，用网页端同款位掩码（如 4048）重试
    let fnval16: u32 = std::env::var("GET_VIDEO_BILI_FNVAL")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(16);
    let mut out = SiteResult {
        referer: Some("https://www.bilibili.com".into()),
        ..Default::default()
    };
    // 定位入口 → 构造 fnval=1 / fnval=16 两个 playurl 请求
    let (req1, req16) = if let Some(cap) = EP_RE.captures(page_url) {
        let ep = cap[1].to_string();
        (
            pgc_req(client, api, &ep, 1, &cookie),
            pgc_req(client, api, &ep, fnval16, &cookie),
        )
    } else if SS_RE.is_match(page_url) {
        // ss 季页：playurl 不认 season_id，从 HTML 取默认集 ep_id
        let cap = EPID_JSON_RE
            .captures(html)
            .or_else(|| EP_RE.captures(html))?;
        let ep = cap[1].to_string();
        (
            pgc_req(client, api, &ep, 1, &cookie),
            pgc_req(client, api, &ep, fnval16, &cookie),
        )
    } else {
        let cap = BV_RE.captures(page_url)?;
        let bvid = cap[1].to_string();
        let (cid, title) = fetch_view(client, api, &bvid, query_p(page_url), &cookie).await?;
        out.title = title;
        (
            ugc_req(client, api, &bvid, cid, 1, &cookie),
            ugc_req(client, api, &bvid, cid, fnval16, &cookie),
        )
    };
    fill_from_playurls(&mut out, req1, req16).await;
    Some(out)
}

fn bili_req(
    client: &reqwest::Client,
    base: &str,
    params: &[(&str, String)],
    cookie: &Option<String>,
) -> reqwest::RequestBuilder {
    let mut req = client.get(base).query(params);
    if let Some(c) = cookie {
        req = req.header(reqwest::header::COOKIE, c.as_str());
    }
    req
}

fn pgc_req(
    client: &reqwest::Client,
    api: &BiliEndpoints,
    ep_id: &str,
    fnval: u32,
    cookie: &Option<String>,
) -> reqwest::RequestBuilder {
    bili_req(
        client,
        &api.pgc,
        &[
            ("ep_id", ep_id.to_string()),
            ("qn", "116".into()),
            ("fourk", "1".into()),
            ("fnval", fnval.to_string()),
            ("fnver", "0".into()),
        ],
        cookie,
    )
}

fn ugc_req(
    client: &reqwest::Client,
    api: &BiliEndpoints,
    bvid: &str,
    cid: i64,
    fnval: u32,
    cookie: &Option<String>,
) -> reqwest::RequestBuilder {
    bili_req(
        client,
        &api.ugc,
        &[
            ("bvid", bvid.to_string()),
            ("cid", cid.to_string()),
            ("qn", "116".into()),
            ("fourk", "1".into()),
            ("fnval", fnval.to_string()),
        ],
        cookie,
    )
}

/// view API → (cid, title)：BV 号换 cid，多分 P 按 URL 的 ?p=N 选集。
async fn fetch_view(
    client: &reqwest::Client,
    api: &BiliEndpoints,
    bvid: &str,
    p: i64,
    cookie: &Option<String>,
) -> Option<(i64, Option<String>)> {
    let req = bili_req(client, &api.view, &[("bvid", bvid.to_string())], cookie);
    let text = req.send().await.ok()?.text().await.ok()?;
    let v: BiliView = serde_json::from_str(&text).ok()?;
    if v.code != 0 {
        return None;
    }
    let data = v.data?;
    if data.pages.is_empty() {
        return None;
    }
    let idx = usize::try_from(p.max(1) - 1)
        .ok()?
        .min(data.pages.len() - 1);
    let page = &data.pages[idx];
    let title = if data.title.is_empty() {
        None
    } else if idx > 0 && !page.part.is_empty() {
        Some(format!("{} P{} {}", data.title, page.page, page.part))
    } else {
        Some(data.title.clone())
    };
    Some((page.cid, title))
}

/// 播放地址两段式：fnval=1 整段 + fnval=16 DASH 都取回，择优输出。
/// DASH 最高视频轨清晰度超过整段（番剧高清只有 DASH）时，
/// 把「视频轨+音频轨」合成一条 dash 候选放最前（应用侧经 relay 出 MPD 播放）。
async fn fill_from_playurls(
    out: &mut SiteResult,
    req1: reqwest::RequestBuilder,
    req16: reqwest::RequestBuilder,
) {
    let r1 = fetch_bili(req1).await;
    let r16 = fetch_bili(req16).await;
    merge_playurls(out, r1.as_ref(), r16.as_ref());
}

/// 两段响应合并决策（纯函数，可单测）：DRM 拦截 → 高清 DASH 合成 → 整段补入 → 试看提示。
fn merge_playurls(out: &mut SiteResult, r1: Option<&BiliResult>, r16: Option<&BiliResult>) {
    for r in [&r1, &r16].into_iter().flatten() {
        if r.is_drm {
            out.note = Some("B站：该内容标记为 DRM，按红线不出地址".into());
            return;
        }
    }
    let durl_h = r1
        .as_ref()
        .filter(|r| !r.durl.is_empty())
        .and_then(|r| qn_to_height(r.quality));
    // DASH 最佳视频轨（按高度）与最佳音频轨（按码率）
    let best_pair = r16.as_ref().and_then(|r| {
        let dash = r.dash.as_ref()?;
        let v = dash
            .video
            .iter()
            .filter(|v| !v.base().trim().is_empty())
            .max_by_key(|v| v.height.or_else(|| qn_to_height(v.id)).unwrap_or(0))?;
        let a = dash
            .audio
            .iter()
            .filter(|a| !a.base().trim().is_empty())
            .max_by_key(|a| a.bandwidth.unwrap_or(0));
        let vh = v.height.or_else(|| qn_to_height(v.id));
        Some((v, a, vh, r.timelength))
    });
    if let Some((v, a, Some(vh), tl)) = best_pair {
        if durl_h.is_none_or(|dh| vh > dh) {
            // 高清只在 DASH：合成双轨候选（url=视频轨，audio_url=音频轨）
            out.candidates.push(Candidate {
                url: v.base(),
                protocol: Protocol::Dash,
                quality: Some(vh),
                audio_url: a.map(|a| a.base()),
                duration_ms: tl,
                codecs: v.codecs.clone(),
                width: v.width,
                height: Some(vh),
                bandwidth: v.bandwidth,
            });
        }
    }
    if let Some(r) = &r1 {
        push_durl(out, r);
    }
    let preview = [&r1, &r16].into_iter().flatten().any(|r| r.is_preview == 1);
    if preview {
        out.note = Some("B站：仅试看片段，完整内容需登录/会员".into());
    }
}

async fn fetch_bili(req: reqwest::RequestBuilder) -> Option<BiliResult> {
    let text = req.send().await.ok()?.text().await.ok()?;
    let data: BiliPlayurl = serde_json::from_str(&text).ok()?;
    data.into_result()
}

/// URL query 的 ?p=N（多分 P 选集，缺省 1）。
fn query_p(page_url: &str) -> i64 {
    url::Url::parse(page_url)
        .ok()
        .and_then(|u| {
            u.query_pairs()
                .find(|(k, _)| k == "p")
                .and_then(|(_, v)| v.parse::<i64>().ok())
        })
        .filter(|&p| p >= 1)
        .unwrap_or(1)
}

fn push_durl(out: &mut SiteResult, r: &BiliResult) {
    for d in &r.durl {
        push_stream(out, &d.url, qn_to_height(r.quality));
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
    out.candidates
        .push(Candidate::single(url.to_string(), protocol, quality));
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
    fn bili_ss_bv_regex() {
        assert!(SS_RE.is_match("https://www.bilibili.com/bangumi/play/ss28747"));
        assert!(!SS_RE.is_match("https://www.bilibili.com/bangumi/play/ep733316"));
        let cap = EPID_JSON_RE
            .captures(r#"{"epInfo":{"ep_id":733316}}"#)
            .unwrap();
        assert_eq!(&cap[1], "733316");
        let bv = BV_RE
            .captures("https://www.bilibili.com/video/BV1xx411c7mD?p=2")
            .unwrap();
        assert_eq!(&bv[1], "BV1xx411c7mD");
        assert!(BV_RE
            .captures("https://www.bilibili.com/bangumi/play/ep1")
            .is_none());
    }

    #[test]
    fn bili_query_p() {
        assert_eq!(query_p("https://www.bilibili.com/video/BV1xx?p=2"), 2);
        assert_eq!(query_p("https://www.bilibili.com/video/BV1xx"), 1);
        assert_eq!(query_p("https://www.bilibili.com/video/BV1xx?p=0"), 1);
        assert_eq!(query_p("https://www.bilibili.com/video/BV1xx?p=abc"), 1);
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
        merge_playurls(&mut out, None, Some(&r));
        // 无整段时：最佳视频轨+最佳音频轨合成一条 dash 候选
        assert_eq!(out.candidates.len(), 1);
        let c = &out.candidates[0];
        assert_eq!(c.protocol, Protocol::Dash);
        assert_eq!(c.quality, Some(480));
        assert_eq!(c.url, "https://upos.bilivideo.com/v.m4s?upsig=a");
        assert_eq!(
            c.audio_url.as_deref(),
            Some("https://upos.bilivideo.com/a.m4s?upsig=c")
        );
    }

    #[test]
    fn bili_dash_preferred_when_higher() {
        // 整段 360P + DASH 480P：dash 合成候选在前，整段保留为低清选项
        let j1 = r#"{"code":0,"result":{"is_preview":0,"is_drm":false,"quality":16,
            "durl":[{"url":"https://upos.bilivideo.com/full.mp4?upsig=d"}]}}"#;
        let j16 = r#"{"code":0,"result":{"is_preview":0,"is_drm":false,"quality":32,"durl":[],
            "timelength":596000,
            "dash":{
                "video":[{"id":32,"baseUrl":"https://upos.bilivideo.com/v.m4s?upsig=a","height":480,"codecs":"avc1.64001F","width":852,"bandwidth":900000}],
                "audio":[{"id":30216,"baseUrl":"https://upos.bilivideo.com/a.m4s?upsig=c","bandwidth":128000}]
            }}}"#;
        let r1: BiliPlayurl = serde_json::from_str(j1).unwrap();
        let r16: BiliPlayurl = serde_json::from_str(j16).unwrap();
        let r1 = r1.result.unwrap();
        let r16 = r16.result.unwrap();
        let mut out = SiteResult::default();
        merge_playurls(&mut out, Some(&r1), Some(&r16));
        assert_eq!(out.candidates.len(), 2);
        assert_eq!(out.candidates[0].protocol, Protocol::Dash);
        assert_eq!(out.candidates[0].quality, Some(480));
        assert_eq!(out.candidates[0].duration_ms, Some(596000));
        assert_eq!(out.candidates[0].codecs.as_deref(), Some("avc1.64001F"));
        assert_eq!(out.candidates[1].protocol, Protocol::Mp4);
        assert_eq!(out.candidates[1].quality, Some(360));
        // 整段清晰度不低于 DASH 时不出 dash 候选
        let j1_hd = r#"{"code":0,"result":{"is_preview":0,"is_drm":false,"quality":80,
            "durl":[{"url":"https://upos.bilivideo.com/full.mp4?upsig=d"}]}}"#;
        let r1_hd: BiliPlayurl = serde_json::from_str(j1_hd).unwrap();
        let r1_hd = r1_hd.result.unwrap();
        let mut out2 = SiteResult::default();
        merge_playurls(&mut out2, Some(&r1_hd), Some(&r16));
        assert_eq!(out2.candidates.len(), 1);
        assert_eq!(out2.candidates[0].protocol, Protocol::Mp4);
        assert_eq!(out2.candidates[0].quality, Some(1080));
    }

    #[test]
    fn bili_drm_skipped_by_caller() {
        // is_drm=true 时调用方直接返回空 candidates + note，这里只验证字段解析
        let json = r#"{"code":0,"result":{"is_drm":true,"durl":[]}}"#;
        let data: BiliPlayurl = serde_json::from_str(json).unwrap();
        assert!(data.result.unwrap().is_drm);
    }
}
