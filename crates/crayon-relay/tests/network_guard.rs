//! NetworkGuard contract (MED-12): RL-006 逐跳私网/未授权拒绝、RL-007
//! 校验后固定地址、RL-015 逐跳 header scope、allow-set 与跳数有界。
//! 本地 fixture 经 resolver seam + allow_private 钩子完成，不访问公网。

use crayon_relay::network_guard::{GuardError, NetworkGuard, NetworkGuardConfig};
use std::net::{IpAddr, Ipv4Addr};
use std::sync::Arc;
use test_support::upstream::{MockUpstream, UpstreamScript};

fn guard(allow_private: bool) -> NetworkGuard {
    NetworkGuard::new(NetworkGuardConfig {
        allow_private_addresses: allow_private,
        ..NetworkGuardConfig::default()
    })
}

fn body_script(text: &str) -> UpstreamScript {
    UpstreamScript::Full {
        status: 200,
        content_type: Some("text/plain".to_string()),
        body: text.as_bytes().to_vec(),
    }
}

#[tokio::test]
async fn happy_path_same_origin_redirect_carries_headers() {
    let upstream = MockUpstream::start(vec![
        (
            "/a".to_string(),
            UpstreamScript::Redirect {
                location: "/b".to_string(),
            },
        ),
        ("/b".to_string(), body_script("final")),
    ])
    .await
    .unwrap();
    let allow = vec![format!("127.0.0.1:{}", port_of(&upstream))];
    let headers = vec![("Referer".to_string(), "https://example.com".to_string())];
    let fetch = guard(true)
        .fetch(&upstream.url("/a"), &headers, &allow)
        .await
        .unwrap();
    assert_eq!(fetch.response.status(), 200);
    assert_eq!(fetch.hops, 1);
    assert!(fetch.final_url.ends_with("/b"));
    // 同 origin 跳转保留 Referer
    let last = upstream.requests().pop().unwrap();
    assert_eq!(last.header("referer"), Some("https://example.com"));
}

#[tokio::test]
async fn rl_006_redirect_outside_allow_set_is_rejected_without_internal_response() {
    let first = MockUpstream::start(vec![(
        "/a".to_string(),
        UpstreamScript::Redirect {
            location: "http://127.0.0.1:1/internal".to_string(),
        },
    )])
    .await
    .unwrap();
    let allow = vec![format!("127.0.0.1:{}", port_of(&first))];
    let err = guard(true)
        .fetch(&first.url("/a"), &[], &allow)
        .await
        .unwrap_err();
    assert_eq!(
        err,
        GuardError::NotAllowedHost,
        "重定向到 allow-set 外即拒绝"
    );
}

#[tokio::test]
async fn rl_006_private_literal_targets_rejected_before_connect() {
    let strict = guard(false);
    for url in [
        "http://127.0.0.1:8321/x",
        "http://169.254.169.254/latest/meta-data",
        "http://10.0.0.1/",
        "http://192.168.1.1/",
        "http://[::1]/",
    ] {
        assert_eq!(
            strict.fetch(url, &[], &["*".to_string()]).await.map(|_| ()),
            Err(GuardError::NotAllowedHost),
            "{url} 应先被 allow-set 拦截"
        );
    }
    // 字面量私网地址即使在 allow-set 内也被分类拒绝
    assert_eq!(
        strict
            .fetch(
                "http://169.254.169.254/latest/meta-data",
                &[],
                &["169.254.169.254".to_string()]
            )
            .await
            .map(|_| ()),
        Err(GuardError::NonPublicAddress)
    );
    // localhost 解析到 loopback：解析后分类拒绝
    assert_eq!(
        strict
            .fetch("http://localhost:8321/x", &[], &["localhost".to_string()])
            .await
            .map(|_| ()),
        Err(GuardError::NonPublicAddress)
    );
}

#[tokio::test]
async fn rl_007_connection_pins_the_validated_address() {
    // resolver seam：主机名映射到本机 mock；guard 固定解析结果建连
    let upstream = MockUpstream::start(vec![("/v".to_string(), body_script("pinned"))])
        .await
        .unwrap();
    let port = port_of(&upstream);
    let resolver: crayon_relay::network_guard::Resolver = Arc::new(move |host, _port| {
        if host == "fixture.example.com" {
            Ok(vec![IpAddr::V4(Ipv4Addr::LOCALHOST)])
        } else {
            Err(GuardError::Dns)
        }
    });
    let guard = NetworkGuard::new(NetworkGuardConfig {
        allow_private_addresses: true,
        resolver: Some(resolver),
        ..NetworkGuardConfig::default()
    });
    let allow = vec![format!("fixture.example.com:{port}")];
    let fetch = guard
        .fetch(&format!("http://fixture.example.com:{port}/v"), &[], &allow)
        .await
        .unwrap();
    assert_eq!(fetch.response.status(), 200);
    assert_eq!(upstream.hit_count("/v"), 1);
}

#[tokio::test]
async fn rl_015_cross_origin_hop_strips_scoped_headers() {
    let second = MockUpstream::start(vec![("/final".to_string(), body_script("ok"))])
        .await
        .unwrap();
    let first = MockUpstream::start(vec![(
        "/a".to_string(),
        UpstreamScript::Redirect {
            location: second.url("/final"),
        },
    )])
    .await
    .unwrap();
    let allow = vec![
        format!("127.0.0.1:{}", port_of(&first)),
        format!("127.0.0.1:{}", port_of(&second)),
    ];
    let headers = vec![
        ("Referer".to_string(), "https://example.com".to_string()),
        ("User-Agent".to_string(), "TestUA/1.0".to_string()),
    ];
    let fetch = guard(true)
        .fetch(&first.url("/a"), &headers, &allow)
        .await
        .unwrap();
    assert_eq!(fetch.response.status(), 200);
    // 首跳带头，跨 origin 跳转后剥离
    let first_requests = first.requests();
    assert_eq!(
        first_requests[0].header("referer"),
        Some("https://example.com")
    );
    let second_requests = second.requests();
    assert_eq!(
        second_requests[0].header("referer"),
        None,
        "跨 origin 不得携带 Referer"
    );
    assert_eq!(second_requests[0].header("user-agent"), None);
}

#[tokio::test]
async fn redirect_hops_are_bounded() {
    let upstream = MockUpstream::start(vec![(
        "/loop".to_string(),
        UpstreamScript::Redirect {
            location: "/loop".to_string(),
        },
    )])
    .await
    .unwrap();
    let allow = vec![format!("127.0.0.1:{}", port_of(&upstream))];
    let err = guard(true)
        .fetch(&upstream.url("/loop"), &[], &allow)
        .await
        .unwrap_err();
    assert_eq!(err, GuardError::TooManyHops);
    assert!(upstream.hit_count("/loop") <= 5, "跳数有界");
}

#[tokio::test]
async fn non_http_schemes_rejected() {
    assert_eq!(
        guard(true)
            .fetch("ftp://example.com/x", &[], &["example.com".to_string()])
            .await
            .map(|_| ()),
        Err(GuardError::UnsupportedScheme)
    );
}

fn port_of(upstream: &MockUpstream) -> u16 {
    upstream
        .base_url()
        .rsplit(':')
        .next()
        .unwrap()
        .parse()
        .unwrap()
}
