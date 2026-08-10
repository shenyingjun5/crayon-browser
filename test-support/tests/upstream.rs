//! MockUpstream self-tests: full responses, request recording, redirects and
//! deterministic drip gating. Raw TCP client keeps the test dependency-free.

use std::time::Duration;

use test_support::upstream::{drip, MockUpstream, UpstreamScript};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

/// Minimal HTTP/1.1 GET over raw TCP (server closes the connection at end).
async fn http_get(url: &str, headers: &[(&str, &str)]) -> (u16, Vec<(String, String)>, Vec<u8>) {
    let without_scheme = url.strip_prefix("http://").expect("loopback http url");
    let (host, path) = without_scheme
        .split_once('/')
        .map(|(h, p)| (h, format!("/{p}")))
        .unwrap_or((without_scheme, "/".to_string()));
    let mut stream = TcpStream::connect(host).await.unwrap();
    let mut request = format!("GET {path} HTTP/1.1\r\nHost: {host}\r\n");
    for (name, value) in headers {
        request.push_str(&format!("{name}: {value}\r\n"));
    }
    request.push_str("Connection: close\r\n\r\n");
    stream.write_all(request.as_bytes()).await.unwrap();

    let mut raw = Vec::new();
    stream.read_to_end(&mut raw).await.unwrap();
    let head_end = raw
        .windows(4)
        .position(|w| w == b"\r\n\r\n")
        .expect("response head terminator");
    let head = String::from_utf8_lossy(&raw[..head_end]).to_string();
    let mut lines = head.split("\r\n");
    let status: u16 = lines
        .next()
        .unwrap()
        .split_whitespace()
        .nth(1)
        .unwrap()
        .parse()
        .unwrap();
    let headers_out = lines
        .filter(|l| !l.is_empty())
        .filter_map(|l| l.split_once(':'))
        .map(|(k, v)| (k.trim().to_string(), v.trim().to_string()))
        .collect();
    (status, headers_out, raw[head_end + 4..].to_vec())
}

#[tokio::test]
async fn full_response_and_request_recording() {
    let upstream = MockUpstream::start(vec![(
        "/v.mp4".to_string(),
        UpstreamScript::Full {
            status: 200,
            content_type: Some("video/mp4".to_string()),
            body: b"0123456789".to_vec(),
        },
    )])
    .await
    .unwrap();

    let (status, response_headers, body) =
        http_get(&upstream.url("/v.mp4"), &[("Range", "bytes=0-3")]).await;
    assert_eq!(status, 200);
    assert_eq!(body, b"0123456789");
    assert!(response_headers
        .iter()
        .any(|(k, v)| k == "Content-Type" && v == "video/mp4"));

    let requests = upstream.requests();
    assert_eq!(requests.len(), 1);
    assert_eq!(requests[0].method, "GET");
    assert_eq!(requests[0].path, "/v.mp4");
    assert_eq!(requests[0].header("range"), Some("bytes=0-3"));

    // Unregistered path: 404, never a real network fallthrough.
    let (status, _, _) = http_get(&upstream.url("/missing"), &[]).await;
    assert_eq!(status, 404);
}

#[tokio::test]
async fn redirect_route_reports_location() {
    let upstream = MockUpstream::start(vec![(
        "/old".to_string(),
        UpstreamScript::Redirect {
            location: "http://127.0.0.1:1/new".to_string(),
        },
    )])
    .await
    .unwrap();
    let (status, headers, _) = http_get(&upstream.url("/old"), &[]).await;
    assert_eq!(status, 302);
    assert!(headers
        .iter()
        .any(|(k, v)| k == "Location" && v == "http://127.0.0.1:1/new"));
}

#[tokio::test]
async fn drip_stalls_until_released() {
    let (script, control) = drip(
        200,
        Some("application/octet-stream".to_string()),
        vec![b"aa".to_vec(), b"bb".to_vec(), b"cc".to_vec()],
    );
    let upstream = MockUpstream::start(vec![("/live".to_string(), script)])
        .await
        .unwrap();

    let url = upstream.url("/live");
    let fetch = tokio::spawn(async move { http_get(&url, &[]).await });

    // Bounded deadline (not a fixed sleep): without release the body stalls.
    let stalled = tokio::time::timeout(Duration::from_millis(100), async {
        loop {
            if upstream.hit_count("/live") == 1 {
                break;
            }
            tokio::task::yield_now().await;
        }
    })
    .await;
    assert!(stalled.is_ok(), "request must arrive without any release");

    control.release(3);
    let (_, _, body) = tokio::time::timeout(Duration::from_secs(5), fetch)
        .await
        .expect("released chunks must flow")
        .unwrap();
    assert_eq!(body, b"aabbcc");
}
