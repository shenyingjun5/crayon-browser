use super::*;

#[tokio::test]
async fn cctv_candidate_not_statically_restricted() {
    // 央视频家族不做静态受限判定（加扰与否需解码探针实测，见 probe 模块）。
    // 用 .invalid 域名保证离线确定性：拉取失败 → 活性检测不下结论。
    let ex = Extractor::new("http://127.0.0.1:8321", RulePack::empty());
    let cand = Candidate::single(
        "https://nonexistent.invalid/asp/hls/main.m3u8".into(),
        Protocol::Hls,
        None,
    );
    let f = ex
        .build_format(
            "https://tv.cctv.com/2026/07/30/VIDE.shtml",
            &cand,
            &HashMap::new(),
        )
        .await;
    assert!(f.restriction.is_none(), "央视候选不应静态标受限");
}

#[tokio::test]
async fn drm_site_candidate_restricted() {
    let ex = Extractor::new("http://127.0.0.1:8321", RulePack::empty());
    let cand = Candidate::single(
        "https://www.netflix.com/watch/123.m3u8".into(),
        Protocol::Hls,
        None,
    );
    let f = ex
        .build_format("https://www.netflix.com/watch/123", &cand, &HashMap::new())
        .await;
    assert!(f.restriction.is_some(), "全 DRM 站点应静态标受限");
    assert!(f.relay_url.is_none(), "受限内容不产出 relay 地址");
}

#[tokio::test]
async fn normal_candidate_not_restricted() {
    let ex = Extractor::new("http://127.0.0.1:8321", RulePack::empty());
    let cand = Candidate::single("https://example.com/v.mp4".into(), Protocol::Mp4, None);
    let f = ex
        .build_format("https://example.com/p", &cand, &HashMap::new())
        .await;
    assert!(f.restriction.is_none());
    assert!(f.relay_url.is_some());
}
