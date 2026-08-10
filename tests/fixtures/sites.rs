use super::*;

// ---------------------------------------------------------------------------
// E8：站点专用解析器（央视 cntv）夹具
// ---------------------------------------------------------------------------

/// E8：央视纪录片页面 HTML 只有 guid，播放地址经「guid → 站点 API → JSON」
/// 两步拿到——L1 正则扫不到，站点解析器补齐。
#[tokio::test]
async fn e8_cntv_site_extractor() {
    let upstream = spawn_upstream().await;
    let html = r#"<html><head><script>var guid = "4646c21e429d43a08eac19d18704c4e9";</script></head></html>"#;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_cntv(
        &client,
        html,
        &format!("{upstream}/cntv/getHttpVideoInfo.do"),
    )
    .await
    .expect("应命中央视站点解析器");
    assert_eq!(r.title.as_deref(), Some("夹具纪录片"));
    assert_eq!(r.candidates.len(), 1, "空 chapter 地址应被过滤");
    assert_eq!(r.candidates[0].url, format!("{upstream}/m3u8/plain.m3u8"));
    assert_eq!(r.candidates[0].protocol, Protocol::Hls);
    // guid 缺失时不命中
    assert!(get_video::extract::sites::extract_cntv(
        &client,
        "<html>no guid here</html>",
        &format!("{upstream}/cntv/getHttpVideoInfo.do"),
    )
    .await
    .is_none());
}

// ---------------------------------------------------------------------------
// E9：站点专用解析器（B 站：番剧 ep/ss + 普通 BV 页）夹具
// ---------------------------------------------------------------------------

/// E9a：番剧 ep 页——DASH 480P 高于整段 360P 时 dash 合成候选在前，整段保留为低清备选。
#[tokio::test]
async fn e9a_bilibili_durl_preferred() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ep733316?spm_id_from=333.337.0.0",
        "",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .expect("应命中 B 站解析器");
    assert_eq!(r.candidates.len(), 2, "dash 高清合成候选 + 整段低清备选");
    assert_eq!(r.candidates[0].protocol, Protocol::Dash);
    assert_eq!(r.candidates[0].quality, Some(480));
    assert_eq!(
        r.candidates[0].url,
        format!("{upstream}/v_da2-1-30032.m4s?upsig=x")
    );
    assert_eq!(
        r.candidates[0].audio_url.as_deref(),
        Some(format!("{upstream}/a_da2-1-30216.m4s?upsig=y").as_str())
    );
    assert_eq!(r.candidates[1].url, format!("{upstream}/video.mp4?upsig=z"));
    assert_eq!(r.candidates[1].protocol, Protocol::Mp4);
    assert_eq!(r.candidates[1].quality, Some(360));
    assert_eq!(r.referer.as_deref(), Some("https://www.bilibili.com"));
    assert!(r.note.is_none());
    // 既非番剧也非视频页的 URL 不命中
    assert!(get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/",
        "",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .is_none());
}

/// E9b：番剧 ep 页——durl 为空时输出 DASH 合成候选（视频轨+音频轨一体，经 relay 出 MPD）。
#[tokio::test]
async fn e9b_bilibili_dash_fallback() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ep733316",
        "",
        &bili_endpoints(&upstream, "/bili_dash/pgc/playurl"),
        None,
    )
    .await
    .expect("应命中 B 站解析器");
    assert_eq!(r.candidates.len(), 1, "无整段时仅 dash 合成候选");
    let c = &r.candidates[0];
    assert_eq!(c.protocol, Protocol::Dash);
    assert_eq!(c.quality, Some(480));
    assert!(c.audio_url.is_some(), "合成候选携带音频轨地址");
    assert!(r.note.is_none());
}

/// E9c：普通 BV 视频页——view 换 cid，?p=2 选第二集，ugc playurl 出整段。
#[tokio::test]
async fn e9c_bilibili_bv_multi_page() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/video/BV1xx411c7mD?p=2",
        "",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .expect("应命中 BV 页解析");
    assert_eq!(r.candidates.len(), 1);
    assert!(
        r.candidates[0].url.contains("ugc_222"),
        "?p=2 应选 cid=222 的分 P: {}",
        r.candidates[0].url
    );
    assert_eq!(r.candidates[0].quality, Some(720));
    assert_eq!(r.title.as_deref(), Some("夹具视频 P2 下"));
}

/// E9d：番剧 ss 季页——HTML 里的默认集 ep_id 转 pgc/playurl。
#[tokio::test]
async fn e9d_bilibili_ss_season_page() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let html = r#"<script>window.__INITIAL_STATE__={"epInfo":{"ep_id":733316}};</script>"#;
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ss28747",
        html,
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .expect("ss 页应经默认集 ep_id 命中");
    assert_eq!(r.candidates.len(), 2, "dash 合成候选 + 整段备选");
    assert_eq!(r.candidates[1].url, format!("{upstream}/video.mp4?upsig=z"));
    // ss 页 HTML 无 ep_id 时不命中
    assert!(get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ss28747",
        "<html>nothing</html>",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .is_none());
}
