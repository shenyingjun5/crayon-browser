//! `legacy-dev` only relay lifecycle and general-purpose routes.

pub mod proxy;

use axum::{
    extract::{Query, State},
    http::StatusCode,
    response::{Html, IntoResponse, Json, Response},
    routing::{get, options},
    Router,
};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::{Arc, Mutex};

/// DASH MPD 清单内存仓库：提取器生成 MPD（B 站音画分轨合成）按键存入，
/// relay 经 `/dashmpd/{id}` 提供给播放器/投屏设备。
pub type DashStore = Arc<Mutex<HashMap<String, String>>>;

#[derive(Clone)]
pub struct AppState {
    pub proxy: Arc<proxy::ProxyState>,
    pub rules: crate::extract::RulePack,
    pub dash_docs: DashStore,
}

#[derive(Debug, Clone)]
pub struct RelayConfig {
    pub host: String,
    pub port: u16,
    /// 测试钩子：允许代理内网/本机地址（关闭 SSRF 黑名单）。
    pub allow_private_hosts: bool,
    /// L3 规则包本地 JSON 路径。
    pub rules_path: Option<std::path::PathBuf>,
    /// 应用侧注入的共享 MPD 仓库（提取器写入）；缺省独立空仓库。
    pub dash_store: Option<DashStore>,
}

impl Default for RelayConfig {
    fn default() -> Self {
        Self {
            host: "127.0.0.1".into(),
            port: 8321,
            allow_private_hosts: false,
            rules_path: None,
            dash_store: None,
        }
    }
}

pub struct RelayHandle {
    pub addr: SocketAddr,
    shutdown_tx: tokio::sync::oneshot::Sender<()>,
    join: tokio::task::JoinHandle<()>,
}

impl RelayHandle {
    /// 服务基地址，如 `http://127.0.0.1:8321`。
    pub fn base_url(&self) -> String {
        format!("http://{}", self.addr)
    }

    pub async fn shutdown(self) {
        let _ = self.shutdown_tx.send(());
        let _ = self.join.await;
    }
}

pub fn build_router(state: AppState) -> Router {
    Router::new()
        .route("/proxy/{*rest}", get(proxy::proxy_handler))
        .route("/proxy/{*rest}", options(proxy::options_handler))
        .route("/dashmpd/{id}", get(dashmpd_handler))
        .route("/api/extract", get(api_extract))
        .route("/health", get(health))
        .route("/player", get(player_page))
        .route("/probeplayer", get(probeplayer_page))
        .with_state(state)
}

/// 提供提取器生成的 DASH MPD（B 站音画分轨合成清单）。
/// 与 /proxy 一样带 CORS——页面（tauri:// 源）和投屏设备都要跨源读取。
async fn dashmpd_handler(
    State(state): State<AppState>,
    axum::extract::Path(id): axum::extract::Path<String>,
) -> Response {
    let doc = state.dash_docs.lock().unwrap().get(&id).cloned();
    let mut resp = match doc {
        Some(xml) => (
            [(axum::http::header::CONTENT_TYPE, "application/dash+xml")],
            xml,
        )
            .into_response(),
        None => (StatusCode::NOT_FOUND, "dash doc not found").into_response(),
    };
    resp.headers_mut().insert(
        axum::http::header::ACCESS_CONTROL_ALLOW_ORIGIN,
        axum::http::HeaderValue::from_static("*"),
    );
    resp
}

/// 启动 relay 服务；port=0 时绑定随机端口（测试用）。
pub async fn start(config: RelayConfig) -> std::io::Result<RelayHandle> {
    let rules = match &config.rules_path {
        Some(p) => crate::extract::RulePack::load(p)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))?,
        None => crate::extract::RulePack::empty(),
    };
    let state = AppState {
        proxy: proxy::ProxyState::new(config.allow_private_hosts),
        rules,
        dash_docs: config.dash_store.unwrap_or_default(),
    };
    let listener =
        tokio::net::TcpListener::bind(format!("{}:{}", config.host, config.port)).await?;
    let addr = listener.local_addr()?;
    let (tx, rx) = tokio::sync::oneshot::channel::<()>();
    let join = tokio::spawn(async move {
        axum::serve(listener, build_router(state))
            .with_graceful_shutdown(async move {
                let _ = rx.await;
            })
            .await
            .expect("relay serve");
    });
    Ok(RelayHandle {
        addr,
        shutdown_tx: tx,
        join,
    })
}

async fn health() -> &'static str {
    "ok"
}

async fn api_extract(
    State(state): State<AppState>,
    Query(params): Query<HashMap<String, String>>,
    req: axum::extract::Request,
) -> Response {
    let Some(url) = params.get("url").cloned() else {
        return (StatusCode::BAD_REQUEST, "missing url param").into_response();
    };
    let host = req
        .headers()
        .get(axum::http::header::HOST)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("127.0.0.1:8321")
        .to_string();
    let extractor = crate::extract::Extractor::new(&format!("http://{host}"), state.rules.clone());
    match extractor.extract(&url).await {
        Ok(info) => Json(info).into_response(),
        Err(e) => (StatusCode::BAD_GATEWAY, format!("extract failed: {e}")).into_response(),
    }
}

/// 解码探针播放页：同源加载 `/proxy` 流（canvas 无跨域污染），抽帧统计
/// 灰度均值/方差后经 Image beacon 回传给 app 侧上报服务。
/// 供 app 层隐藏 webview 探测「播放列表正常但解码画面异常」的私有加扰流
/// （`src/probe.rs` 有判定逻辑与实证背景）。参数：src=同源流地址（必填）、
/// id=探针编号、report=上报服务基地址。
async fn probeplayer_page(Query(params): Query<HashMap<String, String>>) -> Html<String> {
    let src = serde_json::to_string(params.get("src").map(|s| s.as_str()).unwrap_or(""))
        .unwrap_or_else(|_| "\"\"".into());
    let id = serde_json::to_string(params.get("id").map(|s| s.as_str()).unwrap_or(""))
        .unwrap_or_else(|_| "\"\"".into());
    let report = serde_json::to_string(params.get("report").map(|s| s.as_str()).unwrap_or(""))
        .unwrap_or_else(|_| "\"\"".into());
    Html(format!(
        r#"<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>probe</title></head>
<body style="margin:0;background:#000">
<video id="v" muted autoplay style="width:2px;height:2px"></video>
<script>
const SRC = {src};
const ID = {id};
const REPORT = {report};
const v = document.getElementById('v');
const stats = [];
let done = false;
function sample() {{
  try {{
    if (!v.videoWidth) return;
    const c = document.createElement('canvas');
    c.width = v.videoWidth; c.height = v.videoHeight;
    const x = c.getContext('2d');
    x.drawImage(v, 0, 0);
    const d = x.getImageData(0, 0, c.width, c.height).data;
    let s = 0, sq = 0;
    const n = d.length / 4;
    for (let i = 0; i < d.length; i += 4) {{
      const l = (d[i] + d[i+1] + d[i+2]) / 3;
      s += l; sq += l * l;
    }}
    const mean = s / n;
    stats.push(mean.toFixed(2) + ',' + Math.sqrt(Math.max(0, sq / n - mean * mean)).toFixed(2));
  }} catch (e) {{}}
}}
function report(err) {{
  if (done) return; done = true;
  try {{
    new Image().src = REPORT + '/probe-report?id=' + encodeURIComponent(ID)
      + '&f=' + encodeURIComponent(stats.join(';'))
      + '&ct=' + v.currentTime.toFixed(1)
      + (err ? '&err=' + encodeURIComponent(String(err)) : '');
  }} catch (e) {{}}
  document.title = 'probe-done';
}}
if (SRC && REPORT) {{
  v.src = SRC;
  v.play().catch(() => {{}});
  v.addEventListener('canplay', () => {{
    setTimeout(sample, 1200);
    setTimeout(sample, 2500);
    setTimeout(sample, 4000);
    setTimeout(() => report(), 5000);
  }});
  v.addEventListener('error', () => report(v.error && v.error.message || 'video error'));
  setTimeout(() => report('timeout'), 12000);
}} else {{
  report('missing params');
}}
</script>
</body></html>"#
    ))
}

/// 极简 hls.js 测试页（仅联调验收用，不是产品 UI）。
async fn player_page(Query(params): Query<HashMap<String, String>>) -> Html<String> {
    let url = params.get("url").cloned().unwrap_or_default();
    let url_js = serde_json::to_string(&url).unwrap_or_else(|_| "\"\"".into());
    Html(format!(
        r#"<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>get-video 联调播放页</title>
<script src="https://cdn.jsdelivr.net/npm/hls.js@1"></script></head>
<body>
<video id="v" controls autoplay style="width:100%;max-width:960px"></video>
<p id="err" style="color:red"></p>
<script>
const url = {url_js};
const v = document.getElementById('v');
if (!url) {{ document.getElementById('err').textContent = '用法: /player?url=<relay或直接地址>'; }}
else if (/\.mp4(\?|$)/.test(url)) {{ v.src = url; }}
else if (Hls.isSupported()) {{ const h = new Hls(); h.loadSource(url); h.attachMedia(v); }}
else if (v.canPlayType('application/vnd.apple.mpegurl')) {{ v.src = url; }}
else {{ document.getElementById('err').textContent = '当前浏览器不支持 HLS'; }}
</script>
</body></html>"#
    ))
}
