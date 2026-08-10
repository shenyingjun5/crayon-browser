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
    let json =
        r#"{"ack":"yes","title":"x","hls_url":"https://a.com/x.m3u8","is_invalid_copyright":"1"}"#;
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
