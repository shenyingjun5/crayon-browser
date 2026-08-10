//! BrowserFixtureServer self-tests: every documented page route serves
//! deterministic HTML on a random loopback port; unknown paths 404.

use test_support::browser_fixture::BrowserFixtureServer;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

async fn http_get(url: &str) -> (u16, String) {
    let without_scheme = url.strip_prefix("http://").unwrap();
    let (host, path) = without_scheme
        .split_once('/')
        .map(|(h, p)| (h, format!("/{p}")))
        .unwrap();
    let mut stream = TcpStream::connect(host).await.unwrap();
    stream
        .write_all(
            format!("GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n").as_bytes(),
        )
        .await
        .unwrap();
    let mut raw = Vec::new();
    stream.read_to_end(&mut raw).await.unwrap();
    let text = String::from_utf8_lossy(&raw).to_string();
    let status: u16 = text.split_whitespace().nth(1).unwrap().parse().unwrap();
    (status, text)
}

#[tokio::test]
async fn all_documented_pages_are_served() {
    let server = BrowserFixtureServer::start().await.unwrap();
    for (path, marker) in [
        ("/video.html", "<video id=\"v\""),
        ("/audio.html", "<audio id=\"a\""),
        ("/iframe.html", "<iframe"),
        ("/mse.html", "MediaSource"),
        ("/worker.html", "new Worker('/worker.js')"),
        ("/ad-schedule.html", "playAdThenContent"),
        ("/drm-signal.html", "requestMediaKeySystemAccess"),
        ("/gesture.html", "onclick"),
    ] {
        let (status, body) = http_get(&server.page_url(path)).await;
        assert_eq!(status, 200, "{path}");
        assert!(body.contains(marker), "{path} must contain {marker}");
        assert_eq!(server.hit_count(path), 1);
    }

    let (status, body) = http_get(&server.page_url("/worker.js")).await;
    assert_eq!(status, 200);
    assert!(body.contains("worker-playlist.m3u8"));

    let (status, body) = http_get(&server.page_url("/media/worker-playlist.m3u8")).await;
    assert_eq!(status, 200);
    assert!(body.contains("#EXTM3U"));

    let (status, _) = http_get(&server.page_url("/nope.html")).await;
    assert_eq!(status, 404);
}
