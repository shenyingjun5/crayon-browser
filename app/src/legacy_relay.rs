//! legacy relay 启动装配（FND-07E 从 `main.rs` 逐字移出）。
//!
//! 绑定 `0.0.0.0:8321`（被占退回随机端口）让局域网设备可访问投屏地址；
//! 本机播放仍走 127.0.0.1。端口、回退行为与日志文案与迁移前完全一致。

use crate::legacy_network::lan_ip;
use crayon_browser_core::relay::{self, DashStore, RelayConfig, RelayHandle};

/// legacy relay 启动结果：句柄 + 本机/局域网基地址 + 共享 DASH MPD 仓库。
pub(crate) struct LegacyRelay {
    pub(crate) handle: RelayHandle,
    pub(crate) base: String,
    pub(crate) lan_base: String,
    pub(crate) dash_store: DashStore,
}

/// 启动 Crayon legacy relay（在 Tauri setup 的同步上下文中 block_on）。
pub(crate) fn start_legacy_relay() -> LegacyRelay {
    let dash_store: DashStore = Default::default();
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
    LegacyRelay {
        handle,
        base,
        lan_base,
        dash_store,
    }
}
