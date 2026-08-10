//! MP4 serving contract (MED-13): RL-009 200/206/416/HEAD/suffix range,
//! 白名单响应头映射、流式背压与断流保护（RL-012）。端到端：media_router
//! + Mp4Fetcher + NetworkGuard + MockUpstream，全部 loopback。

use crayon_domain::{DeviceId, ResourceId};
use crayon_relay::mp4::Mp4Fetcher;
use crayon_relay::network_guard::{NetworkGuard, NetworkGuardConfig};
use crayon_relay::router::{media_router, RelayCore};
use crayon_relay::session::DEFAULT_SESSION_TTL_MS;
use crayon_relay::vault::UpstreamRecipe;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;
use test_support::upstream::{drip, MockUpstream, UpstreamScript};

struct Harness {
    base: String,
    token: String,
    upstream: MockUpstream,
    core: Arc<RelayCore>,
}

async fn harness(routes: Vec<(String, UpstreamScript)>, read_idle: Duration) -> Harness {
    let upstream = MockUpstream::start(routes).await.unwrap();
    let guard = NetworkGuard::new(NetworkGuardConfig {
        allow_private_addresses: true, // 测试钩子：指向本机 mock
        ..NetworkGuardConfig::default()
    });
    let fetcher = Mp4Fetcher::new(guard).with_read_idle_timeout(read_idle);
    let now = Arc::new(AtomicU64::new(1000));
    let clock = now.clone();
    let core = Arc::new(
        RelayCore::new(Arc::new(move || clock.load(Ordering::SeqCst)))
            .with_fetcher(Arc::new(fetcher)),
    );
    let token = {
        let mut registry = core.registry.lock().unwrap();
        let grant = registry
            .create_session(
                DeviceId::new("dev-01").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                vec!["127.0.0.1".to_string()],
                DEFAULT_SESSION_TTL_MS,
                1000,
            )
            .unwrap();
        registry
            .register_resource(
                &grant.token_hex,
                ResourceId::new("res-01").unwrap(),
                "127.0.0.1",
            )
            .unwrap();
        grant.token_hex
    };
    // allow_set 与资源 host 校验：register_resource 需要 "127.0.0.1" 在 set 中
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
            UpstreamRecipe::new(&upstream.url("/movie.mp4"), None, None).unwrap(),
        )
        .unwrap();

    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr: SocketAddr = listener.local_addr().unwrap();
    let router = media_router(core.clone());
    tokio::spawn(async move {
        axum::serve(
            listener,
            router.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .await
        .unwrap();
    });
    Harness {
        base: format!("http://{addr}"),
        token,
        upstream,
        core,
    }
}

fn client() -> reqwest::Client {
    reqwest::Client::builder().build().unwrap()
}

fn movie_route() -> Vec<(String, UpstreamScript)> {
    vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: b"0123456789".to_vec(),
        },
    )]
}

#[tokio::test]
async fn rl_009_full_get_maps_headers_and_filters_unlisted() {
    let mut routes = movie_route();
    routes.push((
        "/movie.mp4".to_string(),
        UpstreamScript::Custom {
            status: 200,
            headers: vec![
                ("Content-Type".to_string(), "video/mp4".to_string()),
                ("Accept-Ranges".to_string(), "bytes".to_string()),
                ("Set-Cookie".to_string(), "session=secret".to_string()),
                ("X-Internal".to_string(), "debug".to_string()),
            ],
            body: b"0123456789".to_vec(),
        },
    ));
    let h = harness(routes, Duration::from_secs(30)).await;
    let resp = client()
        .get(format!("{}/s/{}/r/res-01/movie.mp4", h.base, h.token))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    assert_eq!(resp.headers().get("content-type").unwrap(), "video/mp4");
    assert!(
        resp.headers().get("set-cookie").is_none(),
        "set-cookie 不透传"
    );
    assert!(resp.headers().get("x-internal").is_none(), "未列名头不透传");
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"0123456789");
}

#[tokio::test]
async fn rl_009_range_suffix_and_out_of_range() {
    let h = harness(movie_route(), Duration::from_secs(30)).await;
    let url = format!("{}/s/{}/r/res-01/movie.mp4", h.base, h.token);

    // bytes=0-3 → 206 + Content-Range + 4 字节
    let resp = client()
        .get(&url)
        .header("Range", "bytes=0-3")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 206);
    assert_eq!(resp.headers().get("content-range").unwrap(), "bytes 0-3/10");
    assert_eq!(resp.headers().get("accept-ranges").unwrap(), "bytes");
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"0123");

    // 前缀开放 bytes=8- → 末两字节
    let resp = client()
        .get(&url)
        .header("Range", "bytes=8-")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 206);
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"89");

    // suffix bytes=-4 → 末 4 字节
    let resp = client()
        .get(&url)
        .header("Range", "bytes=-4")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 206);
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"6789");

    // 越界 → 416
    let resp = client()
        .get(&url)
        .header("Range", "bytes=99-")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 416);
    assert_eq!(resp.headers().get("content-range").unwrap(), "bytes */10");

    // 上游确实收到了 Range
    assert!(h
        .upstream
        .requests()
        .iter()
        .any(|r| r.header("range") == Some("bytes=0-3")));
}

#[tokio::test]
async fn rl_009_head_returns_headers_without_body() {
    let h = harness(movie_route(), Duration::from_secs(30)).await;
    let resp = client()
        .head(format!("{}/s/{}/r/res-01/movie.mp4", h.base, h.token))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    assert_eq!(resp.headers().get("content-length").unwrap(), "10");
    assert!(resp.bytes().await.unwrap().is_empty());
    assert_eq!(
        h.upstream.requests()[0].method,
        "HEAD",
        "HEAD 不拉主体（上游记录为 HEAD）"
    );
}

#[tokio::test]
async fn rl_012_stalled_upstream_is_cut_by_idle_timeout() {
    let (script, control) = drip(
        200,
        Some("video/mp4".to_string()),
        vec![b"first".to_vec(), b"second".to_vec()],
    );
    let h = harness(
        vec![("/movie.mp4".to_string(), script)],
        Duration::from_millis(300),
    )
    .await;
    control.release(1); // 只放行第一块
    let resp = client()
        .get(format!("{}/s/{}/r/res-01/movie.mp4", h.base, h.token))
        .send()
        .await
        .unwrap();
    let mut stream = resp.bytes_stream();
    use futures_util::StreamExt;
    let first = stream.next().await.unwrap().unwrap();
    assert_eq!(first.as_ref(), b"first");
    // 第二块永不到达：读空闲超时后流以错误/结束收尾，不悬挂
    let outcome = tokio::time::timeout(Duration::from_secs(5), stream.next()).await;
    match outcome {
        Ok(Some(Err(_))) | Ok(None) => {}
        Ok(Some(Ok(_))) => panic!("不应收到未放行的第二块"),
        Err(_) => panic!("读空闲超时未生效（流悬挂）"),
    }
}

#[tokio::test]
async fn rl_012_concurrent_requests_are_not_serialized() {
    let (slow_script, _control) = drip(200, Some("video/mp4".to_string()), vec![b"slow".to_vec()]);
    let mut routes = movie_route();
    routes.push(("/slow.mp4".to_string(), slow_script));
    let h = harness(routes, Duration::from_secs(30)).await;

    // 第二个资源指向永不放行的 drip 路由
    {
        let mut registry = h.core.registry.lock().unwrap();
        registry
            .register_resource(&h.token, ResourceId::new("res-02").unwrap(), "127.0.0.1")
            .unwrap();
    }
    let session_id = {
        let registry = h.core.registry.lock().unwrap();
        registry
            .authorize(
                &h.token,
                &ResourceId::new("res-02").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                1000,
            )
            .unwrap()
            .session_id
    };
    h.core
        .vault
        .lock()
        .unwrap()
        .store(
            &session_id,
            ResourceId::new("res-02").unwrap(),
            UpstreamRecipe::new(&h.upstream.url("/slow.mp4"), None, None).unwrap(),
        )
        .unwrap();

    // 卡死请求在飞（永不放行），另一个请求必须立刻完成——无全局串行。
    let slow = tokio::spawn({
        let url = format!("{}/s/{}/r/res-02/slow.mp4", h.base, h.token);
        let client = client();
        async move { client.get(&url).send().await }
    });
    for _ in 0..1000 {
        if h.upstream.hit_count("/slow.mp4") == 1 {
            break;
        }
        tokio::task::yield_now().await;
    }
    assert_eq!(h.upstream.hit_count("/slow.mp4"), 1);

    let fast = tokio::time::timeout(
        Duration::from_secs(5),
        client()
            .get(format!("{}/s/{}/r/res-01/movie.mp4", h.base, h.token))
            .header("Range", "bytes=0-1")
            .send(),
    )
    .await
    .expect("并发请求不得被卡死请求阻塞")
    .unwrap();
    assert_eq!(fast.status(), 206);
    slow.abort();
}
