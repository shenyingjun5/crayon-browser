//! ProbeHttpClient contract (MED-05): bounded HEAD/Range reads, no redirect
//! following, DNS/IP classification (literal, loopback hostname, mixed
//! answers), timeout and cancellation. PL-003 fallback and PL-014 ordinary
//! failure semantics included. Local fixtures only (MockUpstream, loopback
//! random ports; `allow_private_addresses` test hook).

use crayon_media_probe::http::{
    is_publicly_routable, validate_resolved, ProbeHttpClient, ProbeHttpConfig, ProbeHttpError,
};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
use std::time::Duration;
use test_support::upstream::{drip, MockUpstream, UpstreamScript};

fn client(max_body_bytes: usize) -> ProbeHttpClient {
    ProbeHttpClient::new(ProbeHttpConfig {
        max_body_bytes,
        allow_private_addresses: true, // 测试钩子：指向本机 mock
        ..ProbeHttpConfig::default()
    })
}

#[tokio::test]
async fn empty_and_overflowing_ranges_are_rejected_before_connect() {
    let upstream = MockUpstream::start(vec![]).await.unwrap();
    let target = upstream.url("/never");
    assert!(client(1024).range_get(&target, 0, 0).await.is_err());
    assert!(client(0).range_get(&target, 0, 16).await.is_err());
    assert!(client(1024).range_get(&target, u64::MAX, 16).await.is_err());
    assert_eq!(upstream.hit_count("/never"), 0);
}

#[tokio::test]
async fn url_credentials_are_rejected_before_connect() {
    let upstream = MockUpstream::start(vec![]).await.unwrap();
    let target = upstream
        .url("/never")
        .replacen("://", "://user:example@", 1);
    assert!(client(1024).head(&target).await.is_err());
    assert_eq!(upstream.hit_count("/never"), 0);
}

#[tokio::test]
async fn head_returns_status_and_headers() {
    let upstream = MockUpstream::start(vec![(
        "/v.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: b"0123456789".to_vec(),
        },
    )])
    .await
    .unwrap();
    let resp = client(1024).head(&upstream.url("/v.mp4")).await.unwrap();
    assert_eq!(resp.status, 200);
    assert_eq!(resp.content_type.as_deref(), Some("video/mp4"));
    assert_eq!(resp.content_length, Some(10));
    assert!(resp.accept_ranges);
    assert!(resp.body.is_empty(), "HEAD 不收集 body");
}

#[tokio::test]
async fn range_get_returns_capped_bytes() {
    let upstream = MockUpstream::start(vec![(
        "/v.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: b"0123456789".to_vec(),
        },
    )])
    .await
    .unwrap();
    let resp = client(1024)
        .range_get(&upstream.url("/v.mp4"), 0, 4)
        .await
        .unwrap();
    assert_eq!(resp.status, 206);
    assert_eq!(resp.body, b"0123");

    // 配置上限优先于请求上限
    let resp = ProbeHttpClient::new(ProbeHttpConfig {
        max_body_bytes: 3,
        allow_private_addresses: true,
        ..ProbeHttpConfig::default()
    })
    .range_get(&upstream.url("/v.mp4"), 0, 1024)
    .await
    .unwrap();
    assert!(resp.body.len() <= 3);
}

#[tokio::test]
async fn pl_003_head_405_falls_back_to_range_get() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::HeadRejected(Box::new(UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: b"\x00\x00\x00\x18ftypmp42....rest".to_vec(),
        })),
    )])
    .await
    .unwrap();
    let client = client(64 * 1024);
    let head = client.head(&upstream.url("/movie.mp4")).await.unwrap();
    assert_eq!(head.status, 405, "HEAD 被拒（不视为失败）");
    let range = client
        .range_get(&upstream.url("/movie.mp4"), 0, 64 * 1024)
        .await
        .unwrap();
    assert!(
        range.status == 200 || range.status == 206,
        "Range 响应: {}",
        range.status
    );
    assert!(
        range.body.windows(4).any(|w| w == b"ftyp"),
        "Range 首块含 ftyp，不下载主体"
    );
}

#[tokio::test]
async fn redirects_are_surfaced_not_followed() {
    let upstream = MockUpstream::start(vec![(
        "/old".to_string(),
        UpstreamScript::Redirect {
            location: "http://127.0.0.1:1/new".to_string(),
        },
    )])
    .await
    .unwrap();
    let resp = client(1024).head(&upstream.url("/old")).await.unwrap();
    assert_eq!(resp.status, 302, "3xx 交给调用方，不自动跟随");
    assert_eq!(upstream.hit_count("/new"), 0, "不得跟随重定向");
}

#[tokio::test]
async fn private_and_loopback_targets_are_refused_by_default() {
    let strict = ProbeHttpClient::new(ProbeHttpConfig::default());
    let upstream = MockUpstream::start(vec![(
        "/v.mp4".to_string(),
        UpstreamScript::Full {
            status: 200,
            content_type: None,
            body: b"x".to_vec(),
        },
    )])
    .await
    .unwrap();
    // 字面量 loopback IP
    assert_eq!(
        strict.range_get(&upstream.url("/v.mp4"), 0, 16).await,
        Err(ProbeHttpError::NonPublicAddress)
    );
    // 主机名解析到 loopback
    assert_eq!(
        strict.range_get("http://localhost:1/v.mp4", 0, 16).await,
        Err(ProbeHttpError::NonPublicAddress)
    );
    assert_eq!(upstream.hit_count("/v.mp4"), 0, "拒绝发生在连接之前");
    assert_eq!(
        strict.head("http://[::ffff:127.0.0.1]:1/v.mp4").await,
        Err(ProbeHttpError::NonPublicAddress)
    );
}

#[tokio::test]
async fn unsupported_schemes_are_refused() {
    let client = client(1024);
    assert_eq!(
        client.head("ftp://example.com/v.mp4").await,
        Err(ProbeHttpError::UnsupportedScheme)
    );
    assert_eq!(
        client.head("file:///etc/passwd").await,
        Err(ProbeHttpError::UnsupportedScheme)
    );
}

#[tokio::test]
async fn pl_014_timeout_is_an_ordinary_failure() {
    let (script, _control) = drip(200, None, vec![b"never".to_vec()]);
    let upstream = MockUpstream::start(vec![("/slow".to_string(), script)])
        .await
        .unwrap();
    let client = ProbeHttpClient::new(ProbeHttpConfig {
        total_timeout: Duration::from_millis(300),
        allow_private_addresses: true,
        ..ProbeHttpConfig::default()
    });
    let err = client
        .range_get(&upstream.url("/slow"), 0, 16)
        .await
        .unwrap_err();
    assert_eq!(err, ProbeHttpError::Timeout);
}

#[tokio::test]
async fn pl_014_cancellation_is_clean() {
    let (script, _control) = drip(200, None, vec![b"never".to_vec()]);
    let upstream = MockUpstream::start(vec![("/slow".to_string(), script)])
        .await
        .unwrap();
    let client = ProbeHttpClient::new(ProbeHttpConfig {
        total_timeout: Duration::from_secs(60),
        allow_private_addresses: true,
        ..ProbeHttpConfig::default()
    });
    let url = upstream.url("/slow");
    let task = tokio::spawn(async move { client.range_get(&url, 0, 16).await });
    tokio::task::yield_now().await;
    task.abort();
    assert!(task.await.unwrap_err().is_cancelled(), "取消即终止，不悬挂");
}

#[test]
fn mixed_dns_answers_are_rejected() {
    let public: IpAddr = IpAddr::V4(Ipv4Addr::new(8, 8, 8, 8));
    let private: IpAddr = IpAddr::V4(Ipv4Addr::new(192, 168, 1, 1));
    assert_eq!(validate_resolved(&[]), Err(ProbeHttpError::Dns));
    assert!(validate_resolved(&[public]).is_ok());
    assert_eq!(
        validate_resolved(&[public, private]),
        Err(ProbeHttpError::NonPublicAddress),
        "公私混合答案整体拒绝（rebinding 姿态）"
    );
}

#[test]
fn ip_classification_matrix() {
    let blocked_v4 = [
        "0.0.0.0",
        "10.0.0.1",
        "127.0.0.1",
        "169.254.1.1",
        "172.16.0.1",
        "172.31.0.1",
        "192.168.0.1",
        "100.64.0.1",
        "198.18.0.1",
        "192.0.2.1",
        "224.0.0.1",
        "240.0.0.1",
        "255.255.255.255",
    ];
    for ip in blocked_v4 {
        let addr: IpAddr = ip.parse().unwrap();
        assert!(!is_publicly_routable(&addr), "{ip} 应被拦截");
    }
    assert!(is_publicly_routable(&IpAddr::V4(Ipv4Addr::new(8, 8, 8, 8))));

    for ip in [
        "::1",
        "::",
        "fe80::1",
        "fc00::1",
        "ff02::1",
        "::ffff:192.168.0.1",
        "::ffff:127.0.0.1",
        "::ffff:169.254.169.254",
        "::ffff:100.64.0.1",
    ] {
        let addr: IpAddr = ip.parse().unwrap();
        assert!(!is_publicly_routable(&addr), "{ip} 应被拦截");
    }
    assert!(is_publicly_routable(&IpAddr::V6(
        "2606:4700:4700::1111".parse::<Ipv6Addr>().unwrap()
    )));
}
