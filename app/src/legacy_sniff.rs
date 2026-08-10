//! legacy L2 隐藏 webview 嗅探编排（FND-07D 从 `main.rs` 逐字移出）。
//!
//! 收集窗口、双通道命中合并（IPC + beacon）、页态诊断与结果归一化的
//! 行为、日志文案与超时常量与迁移前完全一致。

use crate::legacy_sniffer::SNIFF_JS;
use crate::models::{SniffHit, SniffResponse, SniffResultItem};
use crate::runtime::{push_hit, AppState};
use get_video::extract::{guess_quality, origin_of, Candidate, Extractor, Protocol, RulePack};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Listener, Manager, WebviewUrl, WebviewWindowBuilder};

/// 嗅探收集窗口：最长 12s；首个命中后再等 3s 收尾。
const SNIFF_MAX_WAIT: Duration = Duration::from_secs(12);
/// 零命中宽限上限：播放器 iframe 加载慢的站点多等一轮。
const SNIFF_MAX_WAIT_EXTENDED: Duration = Duration::from_secs(25);
const SNIFF_TAIL: Duration = Duration::from_secs(3);

/// 核心嗅探流程：创建隐藏 webview 加载目标页，收集命中，关闭窗口，归一化结果。
pub(crate) async fn do_sniff(app: &AppHandle, url: &str) -> Result<SniffResponse, String> {
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
