use super::*;

// ---------------------------------------------------------------------------
// R5-R8：m3u8 重写夹具
// ---------------------------------------------------------------------------

#[tokio::test]
async fn r5_ext_x_map_rewrite() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/m3u8/map.m3u8"), "");
    let body = client.get(&url).send().await.unwrap().text().await.unwrap();
    assert!(
        body.contains(&format!(
            "#EXT-X-MAP:URI=\"{}/proxy/{}/init.mp4\"",
            relay.base_url(),
            get_video::encode_url_component(&format!("{upstream}/m3u8/init.mp4"))
        )),
        "MAP URI 未改写: {body}"
    );
    assert!(body.contains("/proxy/"), "分片行未改写: {body}");
    relay.shutdown().await;
}

#[tokio::test]
async fn r6_recursive_master_depth_limit() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    // chain0 → chain1 → ... → chain5 正常（depth 0..=5），chain6 请求 depth=6 → 400
    let mut current = format!("{upstream}/m3u8/chain0.m3u8");
    let mut ok_count = 0;
    loop {
        let url = if ok_count == 0 {
            proxy_url(&relay.base_url(), &current, "")
        } else {
            // current 已经是 relay 重写后的地址
            current.clone()
        };
        let resp = client.get(&url).send().await.unwrap();
        if resp.status() == StatusCode::BAD_REQUEST {
            break;
        }
        assert_eq!(resp.status(), StatusCode::OK, "中间层失败: {url}");
        let body = resp.text().await.unwrap();
        ok_count += 1;
        // 找下一层地址
        let next = body
            .lines()
            .map(|l| l.trim())
            .find(|l| !l.is_empty() && !l.starts_with('#'))
            .expect("chain 里没有下一层");
        current = next.to_string();
    }
    assert_eq!(ok_count, 6, "应允许 depth 0..=5 共 6 层，实际 {ok_count}");
    relay.shutdown().await;
}

#[tokio::test]
async fn r7_content_type_plain_but_m3u8_body() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(
        &relay.base_url(),
        &format!("{upstream}/m3u8/plain.m3u8"),
        "",
    );
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let ct = resp
        .headers()
        .get(header::CONTENT_TYPE)
        .unwrap()
        .to_str()
        .unwrap()
        .to_string();
    let body = resp.text().await.unwrap();
    assert!(
        ct.contains("mpegurl"),
        "按内容判定后应输出 m3u8 Content-Type，实际 {ct}"
    );
    assert!(body.contains("/proxy/"), "按内容判定应走重写: {body}");
    relay.shutdown().await;
}

#[tokio::test]
async fn r8_segment_with_query() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(
        &relay.base_url(),
        &format!("{upstream}/m3u8/query.m3u8"),
        "",
    );
    let body = client.get(&url).send().await.unwrap().text().await.unwrap();
    let target = first_proxied_target(&body).expect("未找到改写后的分片地址");
    assert_eq!(
        target,
        format!("{upstream}/seg.ts?token=abc&x=1"),
        "query 丢失或错乱: {target}"
    );
    // 顺着改写地址拉分片，mock 上游回显 query，字节级断言
    let seg_line = body
        .lines()
        .map(|l| l.trim())
        .find(|l| !l.is_empty() && !l.starts_with('#'))
        .unwrap()
        .to_string();
    let seg_body = client
        .get(&seg_line)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert_eq!(seg_body, "seg-bytes:token=abc&x=1");
    relay.shutdown().await;
}

// ---------------------------------------------------------------------------
// D1 / D2 / D4-夹具：DRM 检测（extract 层，验证 drm 标记与不产出 relay 地址）
// ---------------------------------------------------------------------------

#[tokio::test]
async fn d1_fairplay_marked_drm() {
    let f = extract_format_for("drm_fps.html").await;
    assert!(f.drm, "FairPlay 应标记 drm:true");
    assert!(f.relay_url.is_none(), "DRM 内容不产出 relay 地址");
}

#[tokio::test]
async fn d2_widevine_keyformat_marked_drm() {
    let f = extract_format_for("drm_wv.html").await;
    assert!(f.drm, "Widevine KEYFORMAT 应标记 drm:true");
    assert!(f.relay_url.is_none());
}

/// D5：主列表 200 但变体 404 → 标受限「流地址已失效」，不产出 relay 地址。
/// （央视 4K 专区老片实测场景：CDN 清档只剩主列表）
#[tokio::test]
async fn d5_dead_variant_marked_restricted() {
    let f = extract_format_for("dead_hls.html").await;
    assert!(!f.drm, "失效不是 DRM");
    let reason = f.restriction.expect("变体 404 应标受限");
    assert!(reason.contains("HTTP 404"), "原因应含状态码: {reason}");
    assert!(f.relay_url.is_none(), "失效流不产出 relay 地址");
}
