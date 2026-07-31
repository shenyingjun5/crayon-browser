//! relay 服务生命周期与路由（docs/design.md §5）。

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
use std::sync::Arc;

#[derive(Clone)]
pub struct AppState {
    pub proxy: Arc<proxy::ProxyState>,
    pub rules: crate::extract::RulePack,
}

#[derive(Debug, Clone)]
pub struct RelayConfig {
    pub host: String,
    pub port: u16,
    /// 测试钩子：允许代理内网/本机地址（关闭 SSRF 黑名单）。
    pub allow_private_hosts: bool,
    /// L3 规则包本地 JSON 路径。
    pub rules_path: Option<std::path::PathBuf>,
}

impl Default for RelayConfig {
    fn default() -> Self {
        Self {
            host: "127.0.0.1".into(),
            port: 8321,
            allow_private_hosts: false,
            rules_path: None,
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
        .route("/api/extract", get(api_extract))
        .route("/health", get(health))
        .route("/player", get(player_page))
        .with_state(state)
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
