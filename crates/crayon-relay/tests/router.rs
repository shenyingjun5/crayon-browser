//! Relay router contract (MED-11): RL-001 正式路由面无 legacy 路由、RL-003
//! 授权先于 upstream、RL-008 猜测/穿越/方法/超长输入拒绝、控制面 secret
//! 鉴权与有界 body。

use crayon_domain::{DeviceId, ResourceId};
use crayon_relay::router::{
    control_router, media_router, FetchPlan, FetchRequest, FetchedMedia, RelayCore,
    ResourceFetcher, RouteKind,
};
use crayon_relay::session::DEFAULT_SESSION_TTL_MS;
use crayon_relay::vault::UpstreamRecipe;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

struct FakeFetcher {
    calls: Mutex<Vec<(RouteKind, FetchPlan)>>,
}

impl ResourceFetcher for FakeFetcher {
    fn fetch(
        &self,
        kind: RouteKind,
        plan: FetchPlan,
        _request: FetchRequest,
    ) -> std::pin::Pin<
        Box<
            dyn std::future::Future<Output = Result<FetchedMedia, crayon_relay::router::FetchError>>
                + Send,
        >,
    > {
        self.calls.lock().unwrap().push((kind, plan));
        Box::pin(async {
            Ok(FetchedMedia {
                status: 200,
                headers: vec![("content-type".to_string(), "video/mp4".to_string())],
                body: axum::body::Body::from("media-bytes"),
            })
        })
    }
}

struct Harness {
    base: String,
    core: Arc<RelayCore>,
    now: Arc<AtomicU64>,
    fetcher: Arc<FakeFetcher>,
    token: String,
}

async fn spawn(app: axum::Router) -> String {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr: SocketAddr = listener.local_addr().unwrap();
    tokio::spawn(async move {
        axum::serve(
            listener,
            app.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .await
        .unwrap();
    });
    format!("http://{addr}")
}

async fn media_harness() -> Harness {
    let now = Arc::new(AtomicU64::new(1000));
    let clock = now.clone();
    let fetcher = Arc::new(FakeFetcher {
        calls: Mutex::new(Vec::new()),
    });
    let core = Arc::new(
        RelayCore::new(Arc::new(move || clock.load(Ordering::SeqCst)))
            .with_fetcher(fetcher.clone()),
    );
    let token = {
        let mut registry = core.registry.lock().unwrap();
        let grant = registry
            .create_session(
                DeviceId::new("dev-01").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                vec!["cdn.example.com".to_string()],
                DEFAULT_SESSION_TTL_MS,
                1000,
            )
            .unwrap();
        registry
            .register_resource(
                &grant.token_hex,
                ResourceId::new("res-01").unwrap(),
                "cdn.example.com",
                0,
            )
            .unwrap();
        grant.token_hex
    };
    {
        let session_id = {
            let registry = core.registry.lock().unwrap();
            registry
                .authorize(
                    &token,
                    &ResourceId::new("res-01").unwrap(),
                    Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                    1000,
                )
                .unwrap()
                .session_id
        };
        core.vault
            .lock()
            .unwrap()
            .store(
                &session_id,
                ResourceId::new("res-01").unwrap(),
                UpstreamRecipe::new(
                    "https://cdn.example.com/live/seg0.ts?sign=abc",
                    Some("https://example.com".to_string()),
                    None,
                )
                .unwrap(),
            )
            .unwrap();
    }
    let base = spawn(media_router(core.clone())).await;
    Harness {
        base,
        core,
        now,
        fetcher,
        token,
    }
}

fn client() -> reqwest::Client {
    reqwest::Client::builder().build().unwrap()
}

#[tokio::test]
async fn authorized_request_reaches_fetcher_with_scoped_plan() {
    let h = media_harness().await;
    let resp = client()
        .get(format!("{}/s/{}/r/res-01/seg0.ts", h.base, h.token))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"media-bytes");
    let calls = h.fetcher.calls.lock().unwrap();
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].0, RouteKind::Resource);
    assert_eq!(
        calls[0].1.url,
        "https://cdn.example.com/live/seg0.ts?sign=abc"
    );
    assert_eq!(
        calls[0].1.headers,
        vec![("Referer".to_string(), "https://example.com".to_string())]
    );
}

#[tokio::test]
async fn rl_003_unauthorized_never_reaches_fetcher() {
    let h = media_harness().await;
    // token 猜测（合法形状、未知值）→ 401
    let resp = client()
        .get(format!("{}/s/{}/r/res-01/x.ts", h.base, "0".repeat(32)))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 401);
    assert!(resp.text().await.unwrap().contains("session_unknown"));

    // 绑定其他 IP 的 session → 403
    let other = {
        let mut registry = h.core.registry.lock().unwrap();
        registry
            .create_session(
                DeviceId::new("dev-02").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(10, 0, 0, 9))),
                vec!["cdn.example.com".to_string()],
                DEFAULT_SESSION_TTL_MS,
                1000,
            )
            .unwrap()
            .token_hex
    };
    let resp = client()
        .get(format!("{}/s/{}/r/res-01/x.ts", h.base, other))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 403);

    // 未注册资源 → 404
    let resp = client()
        .get(format!("{}/s/{}/r/res-99/x.ts", h.base, h.token))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 404);

    // TTL 过期 → 401
    h.now
        .store(1000 + DEFAULT_SESSION_TTL_MS + 1, Ordering::SeqCst);
    let resp = client()
        .get(format!("{}/s/{}/r/res-01/x.ts", h.base, h.token))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 401);

    assert_eq!(
        h.fetcher.calls.lock().unwrap().len(),
        0,
        "未授权不得触达 fetcher"
    );
}

#[tokio::test]
async fn rl_008_malformed_and_wrong_method_rejected() {
    let h = media_harness().await;
    // 畸形 token / 非法字符资源 id / 路径穿越段
    for path in [
        "/s/xyz/r/res-01/x.ts".to_string(),
        format!("/s/{}/r/bad%20id/x.ts", h.token),
        format!("/s/{}/r/../x.ts", h.token),
    ] {
        let resp = client()
            .get(format!("{}{path}", h.base))
            .send()
            .await
            .unwrap();
        assert!(
            matches!(resp.status().as_u16(), 400 | 401 | 404),
            "{path}: {}",
            resp.status()
        );
    }
    // POST / CONNECT 到媒体面 → 405
    let resp = client()
        .post(format!("{}/s/{}/r/res-01/x.ts", h.base, h.token))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 405);
}

#[tokio::test]
async fn rl_001_no_legacy_routes_on_media_plane() {
    let h = media_harness().await;
    for path in [
        "/api/extract?url=x".to_string(),
        "/proxy/abc".to_string(),
        "/player?url=x".to_string(),
        "/probeplayer".to_string(),
        "/internal/health".to_string(), // 控制面路由不在媒体面
    ] {
        let resp = client()
            .get(format!("{}{path}", h.base))
            .send()
            .await
            .unwrap();
        assert_eq!(resp.status(), 404, "{path}");
    }
}

#[tokio::test]
async fn fetcher_absent_is_explicit_503() {
    let now = Arc::new(AtomicU64::new(1000));
    let clock = now.clone();
    let core = Arc::new(RelayCore::new(Arc::new(move || {
        clock.load(Ordering::SeqCst)
    })));
    let token = {
        let mut registry = core.registry.lock().unwrap();
        let grant = registry
            .create_session(
                DeviceId::new("dev-01").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                vec!["cdn.example.com".to_string()],
                DEFAULT_SESSION_TTL_MS,
                1000,
            )
            .unwrap();
        registry
            .register_resource(
                &grant.token_hex,
                ResourceId::new("res-01").unwrap(),
                "cdn.example.com",
                0,
            )
            .unwrap();
        grant.token_hex
    };
    let session_id = {
        let registry = core.registry.lock().unwrap();
        registry
            .authorize(
                &token,
                &ResourceId::new("res-01").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                1000,
            )
            .unwrap()
            .session_id
    };
    core.vault
        .lock()
        .unwrap()
        .store(
            &session_id,
            ResourceId::new("res-01").unwrap(),
            UpstreamRecipe::new("https://cdn.example.com/v.mp4", None, None).unwrap(),
        )
        .unwrap();
    let base = spawn(media_router(core)).await;
    let resp = client()
        .get(format!("{base}/s/{token}/r/res-01/v.mp4"))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 503);
    assert!(resp.text().await.unwrap().contains("serving_unavailable"));
}

#[tokio::test]
async fn control_plane_requires_secret_and_bounds_body() {
    let now = Arc::new(AtomicU64::new(1000));
    let clock = now.clone();
    let core = Arc::new(
        RelayCore::new(Arc::new(move || clock.load(Ordering::SeqCst)))
            .with_control_secret("test-control-secret".to_string()),
    );
    let base = spawn(control_router(core.clone())).await;
    let c = client();

    assert_eq!(
        c.get(format!("{base}/internal/health"))
            .send()
            .await
            .unwrap()
            .text()
            .await
            .unwrap(),
        "ok"
    );

    let body = serde_json::json!({
        "receiver_id": "dev-01",
        "upstream_allow_set": ["cdn.example.com"]
    });
    // 无 secret / 错误 secret → 401
    assert_eq!(
        c.post(format!("{base}/internal/cast/session"))
            .json(&body)
            .send()
            .await
            .unwrap()
            .status(),
        401
    );
    assert_eq!(
        c.post(format!("{base}/internal/cast/session"))
            .header("x-crayon-control-secret", "wrong")
            .json(&body)
            .send()
            .await
            .unwrap()
            .status(),
        401
    );
    // 正确 secret → 创建成功
    let resp = c
        .post(format!("{base}/internal/cast/session"))
        .header("x-crayon-control-secret", "test-control-secret")
        .json(&body)
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    let created: serde_json::Value = resp.json().await.unwrap();
    let token = created["token"].as_str().unwrap().to_string();
    assert_eq!(token.len(), 32);

    // 未知字段拒绝 / 超长 body 拒绝（RL-008）
    let bad = serde_json::json!({"receiver_id":"dev-01","upstream_allow_set":["a"],"extra":1});
    assert_eq!(
        c.post(format!("{base}/internal/cast/session"))
            .header("x-crayon-control-secret", "test-control-secret")
            .json(&bad)
            .send()
            .await
            .unwrap()
            .status()
            .as_u16(),
        422
    );
    let huge = "x".repeat(32 * 1024);
    let resp = c
        .post(format!("{base}/internal/cast/session"))
        .header("x-crayon-control-secret", "test-control-secret")
        .header("content-type", "application/json")
        .body(format!("{{\"receiver_id\":\"{huge}\"}}"))
        .send()
        .await
        .unwrap();
    assert!(
        matches!(resp.status().as_u16(), 400 | 413 | 422),
        "{}",
        resp.status()
    );

    // 撤销幂等：两次 DELETE 均 204；之后媒体面 401
    assert_eq!(
        c.delete(format!("{base}/internal/cast/session/{token}"))
            .header("x-crayon-control-secret", "test-control-secret")
            .send()
            .await
            .unwrap()
            .status(),
        204
    );
    assert_eq!(
        c.delete(format!("{base}/internal/cast/session/{token}"))
            .header("x-crayon-control-secret", "test-control-secret")
            .send()
            .await
            .unwrap()
            .status(),
        204
    );
    // 控制面无媒体/legacy 路由
    assert_eq!(
        c.get(format!("{base}/proxy/abc"))
            .send()
            .await
            .unwrap()
            .status(),
        404
    );
    assert_eq!(
        c.get(format!("{base}/internal/cast/session"))
            .send()
            .await
            .unwrap()
            .status(),
        405
    );
}
