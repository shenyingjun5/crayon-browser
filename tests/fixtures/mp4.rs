use super::*;

// ---------------------------------------------------------------------------
// R10 / R12：Range 与传输夹具
// ---------------------------------------------------------------------------

#[tokio::test]
async fn r10_upstream_without_range_support() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/no-range"), "");
    let resp = client
        .get(&url)
        .header(header::RANGE, "bytes=0-3")
        .send()
        .await
        .unwrap();
    // relay 透传 200，不伪造 206
    assert_eq!(resp.status(), StatusCode::OK);
    assert!(resp.headers().get(header::CONTENT_RANGE).is_none());
    assert_eq!(resp.text().await.unwrap(), "0123456789abcdef");
    relay.shutdown().await;
}

#[tokio::test]
async fn r12_large_file_streaming() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/big"), "");
    let start = Instant::now();
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let mut stream = resp.bytes_stream();
    // 首字节延迟必须远小于全量耗时（上游每块 5ms，50MB 全量约 1s+）
    let first = stream.next().await.unwrap().unwrap();
    let ttfb = start.elapsed();
    assert!(!first.is_empty());
    assert!(
        ttfb < Duration::from_millis(800),
        "首字节延迟 {ttfb:?}，疑似非流式转发"
    );
    // 边下边读，收满 50MB
    let mut total = first.len();
    while let Some(chunk) = stream.next().await {
        total += chunk.unwrap().len();
    }
    assert_eq!(total, 50 * 1024 * 1024);
    relay.shutdown().await;
}
