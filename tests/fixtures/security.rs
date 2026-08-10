use super::*;

// ---------------------------------------------------------------------------
// R13-R16：防盗链与头部夹具
// ---------------------------------------------------------------------------

#[tokio::test]
async fn r13_referer_spoofing() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let target = format!("{upstream}/guard/referer");
    // 带 referer 参数 → 200
    let ok = client
        .get(proxy_url(
            &relay.base_url(),
            &target,
            "referer=http%3A%2F%2Fallowed.example%2F",
        ))
        .send()
        .await
        .unwrap();
    assert_eq!(ok.status(), StatusCode::OK);
    assert_eq!(ok.text().await.unwrap(), "guard-pass");
    // 不带 referer → relay 默认用目标 origin，mock 源拒绝 → 403 透传
    let blocked = client
        .get(proxy_url(&relay.base_url(), &target, ""))
        .send()
        .await
        .unwrap();
    assert_eq!(blocked.status(), StatusCode::FORBIDDEN);
    relay.shutdown().await;
}

#[tokio::test]
async fn r14_user_agent() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let target = format!("{upstream}/guard/ua");
    // 默认桌面浏览器 UA → 200
    let ok = client
        .get(proxy_url(&relay.base_url(), &target, ""))
        .send()
        .await
        .unwrap();
    assert_eq!(ok.status(), StatusCode::OK);
    // 客户端自带 UA 不应透传给上游（客户端用 BadBot，relay 仍用默认浏览器 UA）
    let ok2 = client
        .get(proxy_url(&relay.base_url(), &target, ""))
        .header(header::USER_AGENT, "BadBot/1.0")
        .send()
        .await
        .unwrap();
    assert_eq!(ok2.status(), StatusCode::OK);
    // 显式 ua 参数覆盖
    let blocked = client
        .get(proxy_url(&relay.base_url(), &target, "ua=BadBot%2F1.0"))
        .send()
        .await
        .unwrap();
    assert_eq!(blocked.status(), StatusCode::FORBIDDEN);
    relay.shutdown().await;
}

#[tokio::test]
async fn r15_sensitive_headers_stripped() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/sensitive"), "");
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    assert!(
        resp.headers().get(header::SET_COOKIE).is_none(),
        "set-cookie 未净化"
    );
    assert!(resp.headers().get(header::X_FRAME_OPTIONS).is_none());
    assert!(resp.headers().get("content-security-policy").is_none());
    assert_eq!(resp.text().await.unwrap(), "sensitive-body");
    relay.shutdown().await;
}

#[tokio::test]
async fn r16_cors() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/sensitive"), "");
    // GET 带 ACAO:*
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(
        resp.headers()
            .get(header::ACCESS_CONTROL_ALLOW_ORIGIN)
            .unwrap(),
        "*"
    );
    // OPTIONS 预检 → 204
    let preflight = client
        .request(reqwest::Method::OPTIONS, &url)
        .header(header::ORIGIN, "http://player.example")
        .header(header::ACCESS_CONTROL_REQUEST_METHOD, "GET")
        .send()
        .await
        .unwrap();
    assert_eq!(preflight.status(), StatusCode::NO_CONTENT);
    assert_eq!(
        preflight
            .headers()
            .get(header::ACCESS_CONTROL_ALLOW_ORIGIN)
            .unwrap(),
        "*"
    );
    relay.shutdown().await;
}

// ---------------------------------------------------------------------------
// S1-S3：安全夹具（relay 不开 allow_private_hosts）
// ---------------------------------------------------------------------------

#[tokio::test]
async fn s1_ssrf_blocked() {
    let relay = spawn_relay(false).await;
    let client = reqwest::Client::new();
    for target in [
        "http://127.0.0.1:8321/x",
        "http://localhost/secret",
        "http://192.168.1.1/admin",
        "http://10.0.0.1/internal",
        "http://169.254.169.254/latest/meta-data",
        "http://172.16.0.1/x",
    ] {
        let url = proxy_url(&relay.base_url(), target, "");
        let resp = client.get(&url).send().await.unwrap();
        assert_eq!(
            resp.status(),
            StatusCode::BAD_REQUEST,
            "{target} 应被 400 拒绝"
        );
    }
    relay.shutdown().await;
}

#[tokio::test]
async fn s2_non_http_scheme() {
    let relay = spawn_relay(false).await;
    let client = reqwest::Client::new();
    for target in ["file:///etc/passwd", "ftp://example.com/x"] {
        let url = proxy_url(&relay.base_url(), target, "");
        let resp = client.get(&url).send().await.unwrap();
        assert_eq!(
            resp.status(),
            StatusCode::BAD_REQUEST,
            "{target} 应被 400 拒绝"
        );
    }
    relay.shutdown().await;
}

#[tokio::test]
async fn s3_malformed_encoding() {
    let relay = spawn_relay(false).await;
    let client = reqwest::Client::new();
    // 非法 percent 编码：直接拼原始请求路径（不经 Url 规范化）
    for path in ["/proxy/bad%zz", "/proxy/%2", "/proxy/100%"] {
        let url = format!("{}{}", relay.base_url(), path);
        let resp = client.get(&url).send().await.unwrap();
        assert_eq!(
            resp.status(),
            StatusCode::BAD_REQUEST,
            "{path} 应被 400 拒绝"
        );
    }
    relay.shutdown().await;
}
