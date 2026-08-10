//! `BrowserFixtureServer`: deterministic browser test pages (testing-standard
//! §4): video/audio, iframe, MSE, Worker, ad scheduling, DRM signal and
//! user-gesture pages. Pages are small, self-contained and served on a
//! loopback random port; media bytes come from fixture routes, never a
//! public network.

use crate::server::{MiniServer, RawBody, RawResponse, RecordedRequest};
use std::sync::Arc;

struct Page {
    path: &'static str,
    html: &'static str,
}

fn page(path: &'static str, html: &'static str) -> Page {
    Page { path, html }
}

/// One fake MP4 byte string is enough for load-path tests; playback behaviour
/// itself is covered by relay/probe fixtures.
const FAKE_MP4: &[u8] = b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom";

fn pages() -> Vec<Page> {
    vec![
        page(
            "/video.html",
            r#"<!doctype html><html><body>
<video id="v" src="/media/movie.mp4" controls></video>
</body></html>"#,
        ),
        page(
            "/audio.html",
            r#"<!doctype html><html><body>
<audio id="a" src="/media/track.mp4" controls></audio>
</body></html>"#,
        ),
        page(
            "/iframe.html",
            r#"<!doctype html><html><body>
<iframe src="/video.html"></iframe>
</body></html>"#,
        ),
        page(
            "/mse.html",
            r#"<!doctype html><html><body>
<video id="v" controls></video>
<script>
// MSE fixture: attaches a MediaSource and appends one init segment fetched
// from the fixture media route. No autoplay, no ad logic.
const v = document.getElementById('v');
const ms = new MediaSource();
v.src = URL.createObjectURL(ms);
ms.addEventListener('sourceopen', async () => {
  const sb = ms.addSourceBuffer('video/mp4; codecs="avc1.42E01E"');
  const buf = await fetch('/media/init.mp4').then(r => r.arrayBuffer());
  sb.appendBuffer(buf);
});
</script>
</body></html>"#,
        ),
        page(
            "/worker.html",
            r#"<!doctype html><html><body>
<script>
// Worker fixture: the fetch happens inside a dedicated worker, exercising
// worker-scope network observation.
const w = new Worker('/worker.js');
w.onmessage = (e) => { document.title = 'worker:' + e.data; };
</script>
</body></html>"#,
        ),
        page(
            "/ad-schedule.html",
            r#"<!doctype html><html><body>
<video id="v" controls></video>
<script>
// Ad-scheduling fixture: an ad break element precedes the main content and
// both play only after a real user gesture (see /gesture.html). The product
// must observe this timeline, never skip or fast-forward it.
const v = document.getElementById('v');
window.playAdThenContent = () => {
  v.src = '/media/ad.mp4';
  v.onended = () => { v.src = '/media/movie.mp4'; v.play(); };
  v.play();
};
</script>
</body></html>"#,
        ),
        page(
            "/drm-signal.html",
            r#"<!doctype html><html><body>
<video id="v" controls></video>
<script>
// DRM-signal fixture: requests a (never-granted) fake license so tests can
// assert the product detects and refuses DRM without touching keys.
navigator.requestMediaKeySystemAccess('com.example.fake', [{
  initDataTypes: ['cenc'],
  videoCapabilities: [{ contentType: 'video/mp4' }]
}]).then(
  () => { document.title = 'drm:unexpected-grant'; },
  () => { document.title = 'drm:refused'; }
);
</script>
</body></html>"#,
        ),
        page(
            "/gesture.html",
            r#"<!doctype html><html><body>
<button id="play" onclick="document.getElementById('v').play()">play</button>
<video id="v" src="/media/movie.mp4"></video>
</body></html>"#,
        ),
    ]
}

const WORKER_JS: &str = r#"fetch('/media/worker-playlist.m3u8').then(r => r.text()).then(t => postMessage(t.split('\n')[0]));"#;

const FAKE_M3U8: &str = "#EXTM3U\n#EXT-X-VERSION:3\n#EXTINF:4.0,\nseg0.ts\n#EXT-X-ENDLIST\n";

/// Deterministic fixture server for browser-layer tests.
pub struct BrowserFixtureServer {
    server: MiniServer,
}

impl BrowserFixtureServer {
    pub async fn start() -> std::io::Result<Self> {
        let server = MiniServer::start(Arc::new(|request: &RecordedRequest| {
            let (status, content_type, body): (u16, &str, &[u8]) = match request.path.as_str() {
                p if pages().iter().any(|page| page.path == p) => {
                    let page = pages().into_iter().find(|page| page.path == p).unwrap();
                    (200, "text/html; charset=utf-8", page.html.as_bytes())
                }
                "/worker.js" => (200, "text/javascript", WORKER_JS.as_bytes()),
                "/media/worker-playlist.m3u8" => {
                    (200, "application/vnd.apple.mpegurl", FAKE_M3U8.as_bytes())
                }
                "/media/movie.mp4" | "/media/track.mp4" | "/media/ad.mp4" | "/media/init.mp4" => {
                    (200, "video/mp4", FAKE_MP4)
                }
                _ => (404, "text/plain", b"".as_slice()),
            };
            RawResponse {
                status,
                headers: vec![("Content-Type".to_string(), content_type.to_string())],
                body: RawBody::Full(body.to_vec()),
            }
        }))
        .await?;
        Ok(Self { server })
    }

    /// Loopback base URL with the system-assigned random port.
    #[must_use]
    pub fn base_url(&self) -> String {
        self.server.base_url()
    }

    /// Absolute URL of a fixture page (e.g. `page_url("/video.html")`).
    #[must_use]
    pub fn page_url(&self, path: &str) -> String {
        format!("{}{}", self.base_url(), path)
    }

    /// Number of times a path was requested.
    #[must_use]
    pub fn hit_count(&self, path: &str) -> usize {
        self.server
            .requests()
            .iter()
            .filter(|r| r.path == path)
            .count()
    }
}
