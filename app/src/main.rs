//! `legacy-dev` Tauri 迁移壳（Tauri 2），不是正式产品构建目标。
//!
//! 架构：主窗口 UI（输入网址/展示结果/播放），叠加 L1/L3 提取（get-video Extractor）、
//! 隐藏 WebviewWindow（加载目标页、注入嗅探 JS，L2）、get-video relay
//! （0.0.0.0:8321，播放地址中转；局域网设备可访问投屏地址）。
//!
//! 嗅探结果上报双通道（去重合并）：
//!
//! 1. `window.__TAURI__.event.emit('sniff-found', ...)`（IPC，需 capabilities 放行 remote）；
//! 2. Image beacon → 本应用自建的 127.0.0.1:8377 上报服务（兜底，跨域无预检）。
//!
//! CLI 无头验证模式：
//! - `get-video-app --sniff-cli <url>`：跑 L2 嗅探，打印 SNIFF_RESULT_JSON 后退出；
//! - `get-video-app --extract-cli <url>`：跑 L1/L3 提取，打印 EXTRACT_RESULT_JSON 后退出。

use get_video::extract::{
    guess_quality, origin_of, Candidate, Extractor, Protocol, RulePack, VideoInfo,
};
use get_video::relay::{self, RelayConfig};
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, Listener, Manager, WebviewUrl, WebviewWindowBuilder};

/// 嗅探收集窗口：最长 12s；首个命中后再等 3s 收尾。
const SNIFF_MAX_WAIT: Duration = Duration::from_secs(12);
/// 零命中宽限上限：播放器 iframe 加载慢的站点多等一轮。
const SNIFF_MAX_WAIT_EXTENDED: Duration = Duration::from_secs(25);
const SNIFF_TAIL: Duration = Duration::from_secs(3);

mod legacy_beacon;
mod legacy_network;
mod legacy_sniffer;
mod models;
mod runtime;

use legacy_beacon::start_beacon_server;
use legacy_network::lan_ip;
use legacy_sniffer::SNIFF_JS;
use models::{ProbeTarget, SniffHit, SniffResponse, SniffResultItem};
use runtime::{push_hit, AppState};

/// 核心嗅探流程：创建隐藏 webview 加载目标页，收集命中，关闭窗口，归一化结果。
async fn do_sniff(app: &AppHandle, url: &str) -> Result<SniffResponse, String> {
    let state = app.state::<Arc<AppState>>();
    let relay_base = state.relay_base.clone();
    let hits_arc: Arc<Mutex<Vec<SniffHit>>> = Arc::new(Mutex::new(Vec::new()));

    // IPC 通道：监听注入脚本 emit 的 sniff-found
    let hits_l = hits_arc.clone();
    let listener_id = app.listen_any("sniff-found", move |ev| {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(ev.payload()) {
            let u = v.get("url").and_then(|x| x.as_str()).unwrap_or("");
            let p = v.get("page").and_then(|x| x.as_str()).unwrap_or("");
            let proto = v.get("proto").and_then(|x| x.as_str()).map(str::to_string);
            push_hit(&hits_l, u.to_string(), p.to_string(), proto);
        }
    });

    // 把 hits_arc 挂到 state 上，beacon 服务从那里取（同时只跑一个嗅探）
    {
        let mut g = state.hits.lock().unwrap();
        *g = Vec::new();
    }

    // 创建隐藏 webview（GTK 要求主线程，走 run_on_main_thread）
    let label = format!("sniff-{}", std::process::id());
    let app2 = app.clone();
    let url_owned = url.to_string();
    let label2 = label.clone();
    let (tx, rx) = tokio::sync::oneshot::channel();
    app.run_on_main_thread(move || {
        let r = WebviewWindowBuilder::new(
            &app2,
            &label2,
            WebviewUrl::External(url_owned.parse().unwrap()),
        )
        .title("sniff")
        .visible(false)
        // 部分站点（1905 等）按 UA 判定「浏览器不支持」而拒绝初始化播放器，
        // 统一伪装成桌面 Chrome（与 extract/relay 的 DEFAULT_UA 一致，
        // 也保证 UA 绑定的签名 URL 全链路一致）
        .user_agent(get_video::DEFAULT_UA)
        // 注入所有框架：苹果CMS 类站点把播放器放在 iframe（甚至多级 iframe
        // 跳转线路站），只注主框架会漏掉 iframe 内的拉流请求（7sefun 实测）
        .initialization_script_for_all_frames(SNIFF_JS)
        .build();
        let _ = tx.send(r);
    })
    .map_err(|e| format!("dispatch main thread: {e}"))?;
    let win = rx
        .await
        .map_err(|_| "main thread dropped".to_string())?
        .map_err(|e| format!("创建嗅探窗口失败: {e}"))?;
    println!("[sniff] 隐藏窗口已创建: {label} -> {url}");

    // 收集：基础 12s；首个命中后再等 3s 收尾。
    // 12s 仍零命中时宽限到 25s——第三方播放器 iframe 加载慢（7sefun 实测
    // ~13s 才注入播放器框架，且前置广告已过滤不占命中），快速命中的站点不受影响。
    let start = Instant::now();
    let mut first_hit_at: Option<Instant> = None;
    let mut extended = false;
    loop {
        tokio::time::sleep(Duration::from_millis(200)).await;
        // 合并 beacon 通道（state.hits）到 hits_arc
        {
            let mut shared = state.hits.lock().unwrap();
            let mut local = hits_arc.lock().unwrap();
            for h in shared.drain(..) {
                if !local.iter().any(|x| x.url == h.url) {
                    println!("[sniff] 命中(beacon): {}", h.url);
                    local.push(h);
                }
            }
            let n = local.len();
            drop(local);
            drop(shared);
            if n > 0 {
                match first_hit_at {
                    None => {
                        first_hit_at = Some(Instant::now());
                        println!("[sniff] 首个命中，进入 3s 收尾");
                    }
                    Some(first) if first.elapsed() >= SNIFF_TAIL => break,
                    Some(_) => {}
                }
            }
        }
        let limit = if extended {
            SNIFF_MAX_WAIT_EXTENDED
        } else {
            SNIFF_MAX_WAIT
        };
        if start.elapsed() >= limit {
            if !extended && first_hit_at.is_none() {
                extended = true;
                println!("[sniff] 12s 零命中，宽限等待至 25s");
            } else {
                break;
            }
        }
    }

    // 收集页态诊断（关窗前）：标题/video/iframe 数 + 最近的媒体类资源名，
    // 经 beacon /diag 打到 stdout，便于排查「零命中」站点（西瓜/1905 这类）。
    {
        let app4 = app.clone();
        let win2 = win.clone();
        let _ = app4.run_on_main_thread(move || {
            let _ = win2.eval(
                r#"try{
var rs=performance.getEntriesByType('resource').map(e=>e.name);
var media=rs.filter(u=>/\.(m3u8|mp4|mpd|ts|m4s|flv)(\?|#|$)/i.test(u)).slice(-10);
var msg=JSON.stringify({t:document.title,v:document.querySelectorAll('video').length,f:document.querySelectorAll('iframe').length,fs:Array.from(document.querySelectorAll('iframe')).map(x=>x.src).slice(0,5),r:rs.length,m:media,all:rs.slice(-30),url:location.href,txt:(document.body?document.body.innerText.slice(0,150):'')});
new Image().src='http://127.0.0.1:8377/diag?msg='+encodeURIComponent(msg);
}catch(e){}"#,
            );
        });
        // 等 beacon 送达
        tokio::time::sleep(Duration::from_millis(600)).await;
    }

    // 关闭隐藏窗口、注销监听
    let app3 = app.clone();
    let _ = app.run_on_main_thread(move || {
        let _ = win.close();
        app3.unlisten(listener_id);
    });

    let found: Vec<SniffHit> = hits_arc.lock().unwrap().clone();
    println!("[sniff] 收集结束，共 {} 条命中", found.len());

    // 归一化：协议/清晰度/DRM/relay_url（复用 get-video crate 逻辑）
    let extractor = Extractor::new(&relay_base, RulePack::empty());
    let fallback_page_origin = origin_of(url);
    let mut results = Vec::new();
    for (i, hit) in found.iter().enumerate() {
        // 协议：URL 扩展名优先；内容判定提示（响应体为 m3u8）兜底
        let protocol = match Protocol::from_url(&hit.url) {
            Protocol::Other if hit.proto.as_deref() == Some("hls") => Protocol::Hls,
            p => p,
        };
        let cand = Candidate::single(hit.url.clone(), protocol, guess_quality(&hit.url));
        let mut headers = HashMap::new();
        let hit_page_origin = origin_of(&hit.page);
        headers.insert(
            "Referer".to_string(),
            if hit_page_origin.is_empty() {
                fallback_page_origin.clone()
            } else {
                hit_page_origin
            },
        );
        headers.insert("User-Agent".to_string(), get_video::DEFAULT_UA.to_string());
        // 受限站点（全站 DRM 名单）：页面与流地址任一命中即打标，
        // 不再拉流做 DRM 检测，也不产出 relay 地址
        let page_ctx = if hit.page.is_empty() { url } else { &hit.page };
        let mut restriction =
            get_video::drm::restricted_reason(page_ctx, &hit.url).map(str::to_string);
        let mut drm = false;
        if restriction.is_none() {
            match protocol {
                // HLS：一次拉取同时判 DRM 与活性（变体 404 即失效）
                Protocol::Hls => match extractor.inspect_hls(&cand, &headers).await {
                    get_video::extract::HlsVerdict::Dead(reason) => restriction = Some(reason),
                    get_video::extract::HlsVerdict::Drm => drm = true,
                    get_video::extract::HlsVerdict::Unknown => {}
                },
                _ => drm = extractor.detect_drm(&cand, &headers).await,
            }
        }
        let relay_url = if drm || restriction.is_some() {
            None
        } else {
            Some(extractor.relay_url(&hit.url, &headers))
        };
        // 编码/封装识别：仅对可播候选做，失败不影响结果
        let codec = if relay_url.is_some() {
            extractor.probe_codec(&hit.url, protocol, &headers).await
        } else {
            None
        };
        results.push(SniffResultItem {
            index: i,
            url: hit.url.clone(),
            protocol: protocol.as_str().to_string(),
            quality: cand.quality.map(|q| format!("{q}p")),
            drm,
            restriction,
            relay_url,
            codec,
        });
    }

    let note = if results.is_empty() {
        Some("未嗅探到视频：页面加载失败、无公开视频或需登录（12s 超时）".to_string())
    } else {
        None
    };
    Ok(SniffResponse {
        page: url.to_string(),
        count: results.len(),
        results,
        note,
    })
}

/// 单流解码探针：隐藏 webview 加载 relay 的 /probeplayer 页（与 /proxy 同源，
/// canvas 可读），抽帧统计经 beacon 回传后综合判定。
/// 返回 Some(受限原因)=实测不可播（加扰黑屏 / 加载失败）；None=可播或无结论。
async fn probe_one(app: &AppHandle, relay_url: &str) -> Option<&'static str> {
    let state = app.state::<Arc<AppState>>();
    let id = format!(
        "p{}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .ok()?
            .as_millis()
    );
    let page = format!(
        "{}/probeplayer?src={}&id={}&report={}",
        state.relay_base,
        get_video::encode_url_component(relay_url),
        id,
        get_video::encode_url_component("http://127.0.0.1:8377")
    );
    // 隐藏窗口须主线程创建（GTK 约束，macOS 同样保险）
    let label = format!("probe-{id}");
    let app2 = app.clone();
    let (tx, rx) = tokio::sync::oneshot::channel();
    app.run_on_main_thread(move || {
        let r =
            WebviewWindowBuilder::new(&app2, &label, WebviewUrl::External(page.parse().unwrap()))
                .title("probe")
                .visible(false)
                .build();
        let _ = tx.send(r);
    })
    .map_err(|e| format!("dispatch main thread: {e}"))
    .ok()?;
    let win = rx.await.ok()?.ok()?;

    // 轮询回传（probeplayer 正常 ~7s 上报，12s 自超时；这里 16s 兜底）
    let mut report = None;
    for _ in 0..80 {
        tokio::time::sleep(Duration::from_millis(200)).await;
        let r = state.probe_reports.lock().unwrap().remove(&id);
        if r.is_some() {
            report = r;
            break;
        }
    }
    let app3 = app.clone();
    let _ = app.run_on_main_thread(move || {
        let _ = win.close();
        let _ = app3;
    });

    let rep = report?;
    let stats: Vec<get_video::probe::FrameStat> = rep
        .frames
        .iter()
        .map(|&(mean, std)| get_video::probe::FrameStat { mean, std })
        .collect();
    // 超时不算加载失败（可能是网络慢），播放器 error 事件才算确定性失败
    let load_error = matches!(rep.err.as_deref(), Some(e) if e != "timeout");
    match get_video::probe::probe_verdict(&stats, load_error) {
        get_video::probe::ProbeVerdict::Scrambled => Some(get_video::probe::SCRAMBLED_REASON),
        get_video::probe::ProbeVerdict::LoadFailed => Some(get_video::probe::LOAD_FAILED_REASON),
        get_video::probe::ProbeVerdict::Playable => None,
        get_video::probe::ProbeVerdict::Inconclusive => {
            println!("[probe] {relay_url} 无结论（err={:?}）", rep.err);
            None
        }
    }
}

/// 对全部可播候选逐个跑解码探针（顺序执行，避免并发拉流）。
/// 实测不可播的流：向前端发 `probe-restriction` 事件（UI 异步打标），
/// 并返回 (流地址, 受限原因) 集合（CLI 模式据此在打印前直接改结果）。
async fn probe_scrambled(app: &AppHandle, targets: &[ProbeTarget]) -> Vec<(String, &'static str)> {
    let mut bad = Vec::new();
    for t in targets {
        match probe_one(app, &t.relay_url).await {
            Some(reason) => {
                println!("[probe] 实测不可播（{reason}）: {}", t.url);
                bad.push((t.url.clone(), reason));
                let _ = app.emit(
                    "probe-restriction",
                    serde_json::json!({
                        "url": t.url,
                        "reason": reason,
                    }),
                );
            }
            None => println!("[probe] 实测可播或无结论: {}", t.url),
        }
    }
    bad
}

/// 加载 L3 规则包：环境变量 GET_VIDEO_RULES 指向本地 JSON；未设置/加载失败用空包。
fn load_rule_pack() -> RulePack {
    match std::env::var("GET_VIDEO_RULES") {
        Ok(p) if !p.is_empty() => match RulePack::load(std::path::Path::new(&p)) {
            Ok(rp) => {
                println!("[extract] 规则包已加载: {p}");
                rp
            }
            Err(e) => {
                eprintln!("[extract] 规则包加载失败（{e}），使用空规则包");
                RulePack::empty()
            }
        },
        _ => RulePack::empty(),
    }
}

/// L1/L3 提取：静态解析 + 站点专用解析器 + 规则包 + DRM 检测（秒级，无 webview）。
async fn do_extract(app: &AppHandle, relay_base: &str, url: &str) -> Result<VideoInfo, String> {
    let mut extractor = Extractor::new(relay_base, load_rule_pack());
    extractor.set_dash_store(app.state::<Arc<AppState>>().dash_store.clone());
    // webview 登录态（B 站等）→ 传给 L3 站点解析器解锁高清晰度
    let cookie = site_cookie_header(app, url);
    if cookie.is_some() {
        println!("[extract] 携带站点登录 Cookie");
    }
    extractor.extract_with_cookie(url, cookie.as_deref()).await
}

#[tauri::command]
fn report_log(msg: String) {
    println!("[page] {msg}");
}

#[tauri::command]
fn lan_addr(app: AppHandle) -> String {
    app.state::<Arc<AppState>>().lan_base.clone()
}

/// 从 webview Cookie 存储（含 HttpOnly）取目标站点的登录 Cookie，
/// 按域名后缀匹配拼成 `name=value; ...` 形式的 Cookie 头。
/// 应用内所有窗口共享同一 Cookie 存储，登录窗口种的会话这里能拿到。
fn site_cookie_header(app: &AppHandle, url: &str) -> Option<String> {
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

#[tauri::command]
async fn sniff(app: AppHandle, url: String) -> Result<SniffResponse, String> {
    let state = app.state::<Arc<AppState>>();
    if state.busy.swap(true, Ordering::SeqCst) {
        return Err("已有任务进行中".to_string());
    }
    println!("[sniff] IPC 调用: {url}");
    let r = do_sniff(&app, &url).await;
    state.busy.store(false, Ordering::SeqCst);
    if let Ok(resp) = &r {
        println!("[sniff] 返回 {} 条结果给前端", resp.count);
        // 后台解码探针：全部可播候选逐个实测画面，异常者经事件异步打标
        let targets = sniff_probe_targets(resp);
        if !targets.is_empty() {
            let ah = app.clone();
            tauri::async_runtime::spawn(async move {
                probe_scrambled(&ah, &targets).await;
            });
        }
    }
    if let Err(e) = &r {
        println!("[sniff] 失败: {e}");
    }
    r
}

/// 嗅探结果中需要跑解码探针的候选（未受限、非 DRM、原生可播协议，
/// 且编码是 webview 能解码的——HEVC/AV1 等跳过，避免「没画面 ≠ 流坏」误判）。
fn sniff_probe_targets(resp: &SniffResponse) -> Vec<ProbeTarget> {
    resp.results
        .iter()
        .filter(|r| {
            r.restriction.is_none()
                && !r.drm
                && (r.protocol == "hls" || r.protocol == "mp4")
                && get_video::probe::webview_can_judge(r.codec.as_deref())
        })
        .filter_map(|r| {
            r.relay_url.clone().map(|relay_url| ProbeTarget {
                url: r.url.clone(),
                relay_url,
            })
        })
        .collect()
}

#[tauri::command]
async fn extract(app: AppHandle, url: String) -> Result<VideoInfo, String> {
    let state = app.state::<Arc<AppState>>();
    if state.busy.swap(true, Ordering::SeqCst) {
        return Err("已有任务进行中".to_string());
    }
    println!("[extract] IPC 调用: {url}");
    let relay_base = state.relay_base.clone();
    let r = do_extract(&app, &relay_base, &url).await;
    state.busy.store(false, Ordering::SeqCst);
    if let Ok(info) = &r {
        println!(
            "[extract] 返回 {} 个格式给前端（source={}）",
            info.formats.len(),
            info.source
        );
        // 后台解码探针：同嗅探链路
        let targets = extract_probe_targets(info);
        if !targets.is_empty() {
            let ah = app.clone();
            tauri::async_runtime::spawn(async move {
                probe_scrambled(&ah, &targets).await;
            });
        }
    }
    if let Err(e) = &r {
        println!("[extract] 失败: {e}");
    }
    r
}

/// 提取结果中需要跑解码探针的候选（同嗅探链路的筛选口径）。
fn extract_probe_targets(info: &VideoInfo) -> Vec<ProbeTarget> {
    info.formats
        .iter()
        .filter(|f| {
            f.restriction.is_none()
                && !f.drm
                && (f.protocol == "hls" || f.protocol == "mp4")
                && get_video::probe::webview_can_judge(f.codec.as_deref())
        })
        .filter_map(|f| {
            f.relay_url.clone().map(|relay_url| ProbeTarget {
                url: f.url.clone(),
                relay_url,
            })
        })
        .collect()
}

/// 打开站点登录窗口（可见 webview）。
///
/// 应用内所有 webview 共享同一 Cookie 存储：用户在此窗口登录后，
/// 后续隐藏嗅探窗口自动携带登录会话；Cookie 持久化在应用数据目录，
/// 重启应用后仍有效。已打开时复用并导航到新地址。
#[tauri::command]
async fn open_login(app: AppHandle, url: String) -> Result<(), String> {
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
async fn close_login(app: AppHandle) -> Result<(), String> {
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

fn main() {
    // CLI 验证模式：--sniff-cli <url>（无头直接跑 L2 嗅探）
    // CLI 验证模式：--extract-cli <url>（无头直接跑 L1/L3 提取）
    // UI 验证模式：--ui-test <url>（GUI 起来后在主窗口里真实点击「嗅探」走完整 IPC 链路）
    let args: Vec<String> = std::env::args().collect();
    let cli_url = args
        .windows(2)
        .find(|w| w[0] == "--sniff-cli")
        .map(|w| w[1].clone());
    let extract_cli_url = args
        .windows(2)
        .find(|w| w[0] == "--extract-cli")
        .map(|w| w[1].clone());
    let ui_test_url = args
        .windows(2)
        .find(|w| w[0] == "--ui-test")
        .map(|w| w[1].clone());

    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            sniff,
            extract,
            report_log,
            open_login,
            close_login,
            lan_addr
        ])
        .setup(move |app| {
            // 前端渲染回执（无头验证 UI 链路用）
            app.listen_any("ui-results", |ev| {
                println!("[ui] 前端已渲染结果: {}", ev.payload());
            });
            app.listen_any("ui-ready", |_| {
                println!("[ui] 前端页面已加载");
            });
            app.listen_any("ui-probe", |ev| {
                println!("[probe] {}", ev.payload());
            });
            // 启动 get-video relay：绑定 0.0.0.0 让局域网设备（手机/电视投屏）可访问；
            // 本机播放仍走 127.0.0.1（8321 被占则退回随机端口）
            let dash_store: get_video::relay::DashStore = Default::default();
            let ds1 = dash_store.clone();
            let ds2 = dash_store.clone();
            let handle = tauri::async_runtime::block_on(async {
                match relay::start(RelayConfig {
                    host: "0.0.0.0".into(),
                    port: 8321,
                    allow_private_hosts: false,
                    rules_path: None,
                    dash_store: Some(ds1),
                })
                .await
                {
                    Ok(h) => h,
                    Err(e) => {
                        eprintln!("[relay] 8321 绑定失败（{e}），退回随机端口");
                        relay::start(RelayConfig {
                            host: "0.0.0.0".into(),
                            port: 0,
                            allow_private_hosts: false,
                            rules_path: None,
                            dash_store: Some(ds2),
                        })
                        .await
                        .expect("relay 启动失败")
                    }
                }
            });
            let port = handle.addr.port();
            let base = format!("http://127.0.0.1:{port}");
            let lan_base = match lan_ip() {
                Some(ip) => format!("http://{ip}:{port}"),
                None => base.clone(),
            };
            println!("[relay] 已启动: {base}（局域网: {lan_base}）");
            let state = Arc::new(AppState {
                hits: Mutex::new(Vec::new()),
                relay_base: base,
                lan_base,
                dash_store,
                probe_reports: Mutex::new(HashMap::new()),
                busy: AtomicBool::new(false),
                _relay: Mutex::new(Some(handle)),
            });
            app.manage(state.clone());
            tauri::async_runtime::spawn(start_beacon_server(state.clone()));

            // CLI 模式：隐藏主窗口，直接跑嗅探并打印结果
            if let Some(u) = cli_url {
                if let Some(w) = app.get_webview_window("main") {
                    let _ = w.hide();
                }
                let ah = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    let r = do_sniff(&ah, &u).await;
                    match r {
                        Ok(mut resp) => {
                            // CLI 同步跑解码探针，打印前直接把异常流标受限
                            let targets = sniff_probe_targets(&resp);
                            let bad = probe_scrambled(&ah, &targets).await;
                            for item in resp.results.iter_mut() {
                                if let Some((_, reason)) =
                                    bad.iter().find(|(url, _)| url == &item.url)
                                {
                                    item.restriction = Some(reason.to_string());
                                    item.relay_url = None;
                                }
                            }
                            println!(
                                "SNIFF_RESULT_JSON: {}",
                                serde_json::to_string(&resp).unwrap()
                            );
                        }
                        Err(e) => println!("SNIFF_RESULT_JSON: {{\"error\": \"{e}\"}}"),
                    }
                    ah.exit(0);
                });
            }

            // CLI 模式：隐藏主窗口，直接跑 L1/L3 提取并打印结果
            if let Some(u) = extract_cli_url {
                if let Some(w) = app.get_webview_window("main") {
                    let _ = w.hide();
                }
                let relay_base = state.relay_base.clone();
                let ah = app.handle().clone();
                let ah2 = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    let r = do_extract(&ah2, &relay_base, &u).await;
                    match r {
                        Ok(mut info) => {
                            // CLI 同步跑解码探针（同 sniff CLI）
                            let targets = extract_probe_targets(&info);
                            let bad = probe_scrambled(&ah, &targets).await;
                            for f in info.formats.iter_mut() {
                                if let Some((_, reason)) = bad.iter().find(|(url, _)| url == &f.url)
                                {
                                    f.restriction = Some(reason.to_string());
                                    f.relay_url = None;
                                }
                            }
                            println!(
                                "EXTRACT_RESULT_JSON: {}",
                                serde_json::to_string(&info).unwrap()
                            );
                        }
                        Err(e) => println!("EXTRACT_RESULT_JSON: {{\"error\": \"{e}\"}}"),
                    }
                    ah.exit(0);
                });
            }

            // legacy UI 验证模式：填地址并触发解析，只读取结果/播放器状态；
            // 禁止替用户点击媒体或启动播放。
            if let Some(u) = ui_test_url {
                let ah = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    tokio::time::sleep(Duration::from_secs(3)).await;
                    if let Some(w) = ah.get_webview_window("main") {
                        let js = format!(
                            "document.getElementById('url').value = {}; go();",
                            serde_json::to_string(&u).unwrap()
                        );
                        println!("[ui-test] 主窗口注入点击嗅探: {u}");
                        match w.eval(&js) {
                            Ok(()) => println!("[ui-test] eval ok"),
                            Err(e) => println!("[ui-test] eval 失败: {e}"),
                        }
                        // 统一解析链路：快速提取 + 深度嗅探最长约 20s，只等待结果渲染。
                        tokio::time::sleep(Duration::from_secs(19)).await;
                        let _ = w.eval(
                            "try{const v=document.getElementById('player');window.__TAURI__.core.invoke('report_log',{msg:'播放器状态: currentTime='+v.currentTime.toFixed(2)+' paused='+v.paused+' error='+(v.error&&v.error.message||'none')+' readyState='+v.readyState+' networkState='+v.networkState});}catch(e){window.__TAURI__.core.invoke('report_log',{msg:'状态读取失败: '+e});}",
                        );
                    }
                    tokio::time::sleep(Duration::from_secs(4)).await;
                    ah.exit(0);
                });
            }

            // 诊断模式：--probe-eval <js>，在主窗口执行任意 JS（可用 report_log 回传）
            if let Some(pos) = std::env::args().position(|a| a == "--probe-eval") {
                let custom = std::env::args().nth(pos + 1);
                let ah = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    tokio::time::sleep(Duration::from_secs(3)).await;
                    if let (Some(w), Some(js)) = (ah.get_webview_window("main"), custom) {
                        let _ = w.eval(&js);
                    }
                    tokio::time::sleep(Duration::from_secs(45)).await;
                    ah.exit(0);
                });
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
