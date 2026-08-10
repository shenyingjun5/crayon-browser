//! legacy 站点登录窗口与 webview 登录态读取（FND-07D 从 `main.rs` 逐字移出）。
//!
//! Cookie 只用于拼目标站点的登录 Cookie 头传给 L3 站点解析器；
//! 不进入日志、诊断输出或任何可序列化结果。

use tauri::{AppHandle, Manager, WebviewUrl, WebviewWindowBuilder};

/// 打开站点登录窗口（可见 webview）。
///
/// 应用内所有 webview 共享同一 Cookie 存储：用户在此窗口登录后，
/// 后续隐藏嗅探窗口自动携带登录会话；Cookie 持久化在应用数据目录，
/// 重启应用后仍有效。已打开时复用并导航到新地址。
#[tauri::command]
pub(crate) async fn open_login(app: AppHandle, url: String) -> Result<(), String> {
    let parsed: tauri::Url = url.parse().map_err(|e| format!("URL 无效: {e}"))?;
    let app2 = app.clone();
    let (tx, rx) = tokio::sync::oneshot::channel();
    app.run_on_main_thread(move || {
        let r = if let Some(w) = app2.get_webview_window("login") {
            let js = format!(
                "location.href = {};",
                serde_json::to_string(parsed.as_str()).unwrap()
            );
            w.eval(&js)
                .and_then(|_| w.set_focus())
                .map_err(|e| e.to_string())
        } else {
            WebviewWindowBuilder::new(&app2, "login", WebviewUrl::External(parsed))
                .title("站点登录（登录完成后直接关闭本窗口）")
                .inner_size(1100.0, 760.0)
                .visible(true)
                .build()
                .map(|_| ())
                .map_err(|e| e.to_string())
        };
        let _ = tx.send(r);
    })
    .map_err(|e| format!("dispatch main thread: {e}"))?;
    let r = rx.await.map_err(|_| "main thread dropped".to_string())?;
    println!(
        "[login] 登录窗口就绪: {url}（结果: {}）",
        if r.is_ok() { "ok" } else { "复用/失败" }
    );
    r
}

/// 关闭站点登录窗口（未打开时静默成功）。
#[tauri::command]
pub(crate) async fn close_login(app: AppHandle) -> Result<(), String> {
    let app2 = app.clone();
    let (tx, rx) = tokio::sync::oneshot::channel();
    app.run_on_main_thread(move || {
        let r = match app2.get_webview_window("login") {
            Some(w) => w.close().map_err(|e| e.to_string()),
            None => Ok(()),
        };
        let _ = tx.send(r);
    })
    .map_err(|e| format!("dispatch main thread: {e}"))?;
    rx.await.map_err(|_| "main thread dropped".to_string())?
}

/// 从 webview Cookie 存储（含 HttpOnly）取目标站点的登录 Cookie，
/// 按域名后缀匹配拼成 `name=value; ...` 形式的 Cookie 头。
/// 应用内所有窗口共享同一 Cookie 存储，登录窗口种的会话这里能拿到。
pub(crate) fn site_cookie_header(app: &AppHandle, url: &str) -> Option<String> {
    let host = tauri::Url::parse(url)
        .ok()?
        .host_str()?
        .to_ascii_lowercase();
    let w = app.get_webview_window("main")?;
    let cookies = w.cookies().ok()?;
    let mut pairs = Vec::new();
    for c in cookies {
        let dom = c
            .domain()
            .unwrap_or("")
            .trim_start_matches('.')
            .to_ascii_lowercase();
        if !dom.is_empty() && (host == dom || host.ends_with(&format!(".{dom}"))) {
            pairs.push(format!("{}={}", c.name(), c.value()));
        }
    }
    if pairs.is_empty() {
        None
    } else {
        Some(pairs.join("; "))
    }
}
