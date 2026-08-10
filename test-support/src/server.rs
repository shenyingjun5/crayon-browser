//! Minimal HTTP/1.1 server core shared by the test-double servers
//! (`MockUpstream`, `BrowserFixtureServer`). Loopback only, random port,
//! one request per connection, bounded header read, no sleeps.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::Notify;

/// Maximum accepted request-head size; larger requests are rejected.
const MAX_HEAD_BYTES: usize = 16 * 1024;

/// A recorded inbound request (method, path, headers as received).
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct RecordedRequest {
    pub(crate) method: String,
    pub(crate) path: String,
    pub(crate) headers: Vec<(String, String)>,
}

impl RecordedRequest {
    /// First header value matching `name` (case-insensitive).
    pub(crate) fn header(&self, name: &str) -> Option<&str> {
        self.headers
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(name))
            .map(|(_, v)| v.as_str())
    }
}

/// Response body delivery strategy.
pub(crate) enum RawBody {
    /// Whole body written at once.
    Full(Vec<u8>),
    /// Chunks released explicitly by the test via a shared [`DripGate`]; the
    /// connection stalls between chunks without any timer (slow/stall cases).
    Drip(Vec<Vec<u8>>, Arc<DripGate>),
}

/// A scripted raw response.
pub(crate) struct RawResponse {
    pub(crate) status: u16,
    pub(crate) headers: Vec<(String, String)>,
    pub(crate) body: RawBody,
}

/// Explicit release gate for drip-fed bodies.
#[derive(Default)]
pub(crate) struct DripGate {
    permits: AtomicUsize,
    notify: Notify,
}

impl DripGate {
    /// Allows `n` more chunks to flow.
    pub(crate) fn release(&self, n: usize) {
        self.permits.fetch_add(n, Ordering::SeqCst);
        self.notify.notify_waiters();
    }

    async fn wait_permit(&self) {
        loop {
            if self.permits.load(Ordering::SeqCst) > 0 {
                self.permits.fetch_sub(1, Ordering::SeqCst);
                return;
            }
            self.notify.notified().await;
        }
    }
}

type Handler = Arc<dyn Fn(&RecordedRequest) -> RawResponse + Send + Sync>;

/// Running server handle; dropping it stops accepting (tasks end with the
/// test runtime). Requests are recorded in arrival order, bounded by
/// `MAX_RECORDED_REQUESTS`.
pub(crate) struct MiniServer {
    addr: std::net::SocketAddr,
    requests: Arc<Mutex<Vec<RecordedRequest>>>,
}

/// Recorded-request bound (bounded buffer rule).
const MAX_RECORDED_REQUESTS: usize = 256;

impl MiniServer {
    pub(crate) async fn start(handler: Handler) -> std::io::Result<Self> {
        let listener = TcpListener::bind("127.0.0.1:0").await?;
        let addr = listener.local_addr()?;
        let requests: Arc<Mutex<Vec<RecordedRequest>>> = Arc::new(Mutex::new(Vec::new()));
        let recorded = requests.clone();
        tokio::spawn(async move {
            while let Ok((stream, _)) = listener.accept().await {
                let handler = handler.clone();
                let recorded = recorded.clone();
                tokio::spawn(async move {
                    let _ = serve_conn(stream, handler, recorded).await;
                });
            }
        });
        Ok(Self { addr, requests })
    }

    /// Loopback base URL with the system-assigned port.
    pub(crate) fn base_url(&self) -> String {
        format!("http://{}", self.addr)
    }

    /// Recorded requests in arrival order.
    pub(crate) fn requests(&self) -> Vec<RecordedRequest> {
        self.requests.lock().unwrap().clone()
    }
}

async fn serve_conn(
    mut stream: TcpStream,
    handler: Handler,
    recorded: Arc<Mutex<Vec<RecordedRequest>>>,
) -> std::io::Result<()> {
    let mut buf = Vec::with_capacity(1024);
    let head_end = loop {
        let mut chunk = [0u8; 1024];
        let n = stream.read(&mut chunk).await?;
        if n == 0 {
            return Ok(()); // client went away before finishing the head
        }
        buf.extend_from_slice(&chunk[..n]);
        if let Some(pos) = find_subslice(&buf, b"\r\n\r\n") {
            break pos;
        }
        if buf.len() > MAX_HEAD_BYTES {
            stream
                .write_all(
                    b"HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\n\r\n",
                )
                .await?;
            return Ok(());
        }
    };

    let head = String::from_utf8_lossy(&buf[..head_end]).to_string();
    let Some(request) = parse_head(&head) else {
        stream
            .write_all(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
            .await?;
        return Ok(());
    };
    {
        let mut log = recorded.lock().unwrap();
        if log.len() < MAX_RECORDED_REQUESTS {
            log.push(request.clone());
        }
    }

    let response = handler(&request);
    write_response(&mut stream, response).await
}

fn find_subslice(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack.windows(needle.len()).position(|w| w == needle)
}

fn parse_head(head: &str) -> Option<RecordedRequest> {
    let mut lines = head.split("\r\n");
    let mut request_line = lines.next()?.split_whitespace();
    let method = request_line.next()?.to_string();
    let path = request_line.next()?.to_string();
    let mut headers = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let (name, value) = line.split_once(':')?;
        headers.push((name.trim().to_string(), value.trim().to_string()));
    }
    Some(RecordedRequest {
        method,
        path,
        headers,
    })
}

async fn write_response(stream: &mut TcpStream, response: RawResponse) -> std::io::Result<()> {
    let reason = match response.status {
        200 => "OK",
        206 => "Partial Content",
        301 => "Moved Permanently",
        302 => "Found",
        400 => "Bad Request",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        500 => "Internal Server Error",
        _ => "Status",
    };
    let body_len: usize = match &response.body {
        RawBody::Full(b) => b.len(),
        RawBody::Drip(chunks, _) => chunks.iter().map(Vec::len).sum(),
    };
    let mut head = format!(
        "HTTP/1.1 {} {reason}\r\nContent-Length: {body_len}\r\n",
        response.status
    );
    for (name, value) in &response.headers {
        head.push_str(&format!("{name}: {value}\r\n"));
    }
    head.push_str("Connection: close\r\n\r\n");
    stream.write_all(head.as_bytes()).await?;

    match response.body {
        RawBody::Full(body) => {
            stream.write_all(&body).await?;
        }
        RawBody::Drip(chunks, gate) => {
            for chunk in chunks {
                gate.wait_permit().await;
                stream.write_all(&chunk).await?;
                stream.flush().await?;
            }
        }
    }
    Ok(())
}
