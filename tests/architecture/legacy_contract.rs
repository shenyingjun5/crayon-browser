//! FND-01 baseline and FND-04 formal-product prohibition contracts.
//!
//! BR-009, BR-010, RL-001, and RG-006 assert the formal-product target. The
//! remaining RG-004 assertion records fixed legacy endpoints that later typed
//! configuration work must remove.

const LEGACY_APP_MAIN: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/app/src/main.rs"));
const LEGACY_SNIFFER_LOADER: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/app/src/legacy_sniffer.rs"
));
const LEGACY_SNIFFER_JS: &str = concat!(
    include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/app/src/scripts/legacy_sniffer.js"
    )),
    "\n"
);
const LEGACY_RELAY: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/src/relay/mod.rs"));
const LEGACY_BEACON: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/app/src/legacy_beacon.rs"
));
const LEGACY_COMMANDS: &str =
    include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/app/src/commands.rs"));
const LEGACY_SNIFF: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/app/src/legacy_sniff.rs"
));
const LEGACY_PROBE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/app/src/legacy_probe.rs"
));
const LEGACY_LOGIN: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/app/src/login.rs"));
const LEGACY_RELAY_BOOT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/app/src/legacy_relay.rs"
));
const LEGACY_CLI: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/app/src/cli.rs"));
const LEGACY_APP_SETUP: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/app/src/app.rs"));
const ROOT_CARGO: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/Cargo.toml"));
const ROOT_LIB: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/src/lib.rs"));
const APP_CARGO: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/app/Cargo.toml"));
const DEMO_CARGO: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/demo/Cargo.toml"));

fn assert_markers_present(source: &str, contract: &str, markers: &[&str]) {
    for marker in markers {
        assert!(
            source.contains(marker),
            "contract {contract} changed: expected marker `{marker}`"
        );
    }
}

fn assert_forbidden_markers_absent(source: &str, contract: &str, markers: &[&str]) {
    for marker in markers {
        assert!(
            !source.contains(marker),
            "formal-product contract {contract} violated by marker `{marker}`"
        );
    }
}

fn fnv1a64(source: &str) -> u64 {
    source
        .as_bytes()
        .iter()
        .fold(0xcbf2_9ce4_8422_2325, |hash, byte| {
            (hash ^ u64::from(*byte)).wrapping_mul(0x0000_0100_0000_01b3)
        })
}

#[test]
fn fnd_07a_sniffer_script_is_external_and_integrity_locked() {
    assert_markers_present(
        LEGACY_APP_MAIN,
        "FND-07A resource loading",
        &["mod legacy_sniffer;", "mod legacy_sniff;"],
    );
    // FND-07D 起注入脚本由 legacy_sniff 编排引用。
    assert_markers_present(
        LEGACY_SNIFF,
        "FND-07A resource usage",
        &["use crate::legacy_sniffer::SNIFF_JS;"],
    );
    assert_markers_present(
        LEGACY_SNIFFER_LOADER,
        "FND-07A byte-preserving resource loading",
        &["concat!(include_str!", "\"\\n\""],
    );
    assert_forbidden_markers_absent(
        LEGACY_APP_MAIN,
        "FND-07A Rust/JavaScript isolation",
        &[
            "const SNIFF_JS: &str = r#\"",
            "window.__getVideoSniff = true",
        ],
    );
    assert_eq!(
        fnv1a64(LEGACY_SNIFFER_JS),
        0x63ca_75fa_1195_0408,
        "legacy sniffer changed; review behavior and update the integrity vector explicitly"
    );
}

#[test]
fn br_009_formal_observer_does_not_filter_or_identify_ads() {
    for source in [
        LEGACY_APP_MAIN,
        LEGACY_SNIFFER_JS,
        LEGACY_COMMANDS,
        LEGACY_SNIFF,
        LEGACY_PROBE,
        LEGACY_LOGIN,
    ] {
        assert_forbidden_markers_absent(
            source,
            "BR-009",
            &[
                "const AD_RE =",
                "const AD_BOX_SEL =",
                "if (AD_RE.test(u)) return;",
                "inAdBox(",
                "[class*=\"adskip\"]",
                "[class*=\"skip-ad\"]",
            ],
        );
    }
}

#[test]
fn br_010_formal_observer_does_not_play_click_or_seek() {
    for source in [
        LEGACY_APP_MAIN,
        LEGACY_SNIFFER_JS,
        LEGACY_COMMANDS,
        LEGACY_SNIFF,
        LEGACY_PROBE,
        LEGACY_LOGIN,
    ] {
        assert_forbidden_markers_absent(
            source,
            "BR-010",
            &[
                "if (v.paused) v.play()",
                "v.currentTime = v.duration - 0.5;",
                "try { b.click(); }",
                "querySelector('#list li').click()",
                "const PLAY_BTN_SEL =",
                "setInterval(nudge, 2000);",
            ],
        );
    }
}

#[test]
fn fnd_07d_command_surface_and_module_wiring_unchanged() {
    // 命令面契约：六个 handler 名称与注册顺序不变（前端 invoke 依赖）。
    assert_markers_present(
        LEGACY_APP_MAIN,
        "FND-07D command surface",
        &[
            "tauri::generate_handler![",
            "sniff,",
            "extract,",
            "report_log,",
            "open_login,",
            "close_login,",
            "lan_addr",
        ],
    );
    // 模块接线：编排代码只存在于 FND-07D 指定模块。
    assert_markers_present(
        LEGACY_APP_MAIN,
        "FND-07D module wiring",
        &[
            "mod commands;",
            "mod legacy_probe;",
            "mod legacy_sniff;",
            "mod login;",
        ],
    );
    // 命令必须从实际所有者模块导入；仅锁 handler 名称不足以证明 app 可编译。
    assert_markers_present(
        LEGACY_APP_MAIN,
        "FND-07D command owner wiring",
        &[
            "use commands::{extract, lan_addr, report_log, sniff};",
            "use login::{close_login, open_login};",
        ],
    );
    assert_forbidden_markers_absent(
        LEGACY_APP_MAIN,
        "FND-07D stale command owner wiring",
        &["use commands::{close_login"],
    );
    // 登录窗口行为标记：复用同一窗口、可见、标题不变。
    assert_markers_present(
        LEGACY_LOGIN,
        "FND-07D login window build",
        &[
            "get_webview_window(\"login\")",
            ".visible(true)",
            "站点登录（登录完成后直接关闭本窗口）",
        ],
    );
}

#[test]
fn rg_004_legacy_baseline_detects_fixed_beacon_and_lan_bind() {
    // FND-07C 起 beacon 服务在独立模块；固定端口/route 仍为待消除的 legacy 基线。
    assert_markers_present(
        LEGACY_APP_MAIN,
        "RG-004 legacy beacon module wiring",
        &["mod legacy_beacon;", "mod app;"],
    );
    // FND-07E 起 beacon 由 app 装配启动。
    assert_markers_present(
        LEGACY_APP_SETUP,
        "RG-004 legacy beacon startup wiring",
        &["use crate::legacy_beacon::start_beacon_server;"],
    );
    assert_markers_present(
        LEGACY_BEACON,
        "RG-004 legacy beacon baseline",
        &[
            "http://127.0.0.1:8377/sniff",
            "TcpListener::bind(\"127.0.0.1:8377\")",
        ],
    );
    assert_markers_present(
        LEGACY_RELAY_BOOT,
        "RG-004 legacy relay baseline",
        &["host: \"0.0.0.0\".into()", "port: 8321"],
    );
}

#[test]
fn fnd_07e_assembly_entry_and_cli_markers() {
    // main.rs 只保留命令注册与装配入口（验收：< 300 行）。
    let lines = LEGACY_APP_MAIN.lines().count();
    assert!(lines < 300, "app/src/main.rs 应小于 300 行，实际 {lines}");
    assert_markers_present(
        LEGACY_APP_MAIN,
        "FND-07E assembly wiring",
        &[
            "mod app;",
            "mod cli;",
            "mod legacy_relay;",
            ".setup(app::setup)",
            "tauri::generate_handler![",
        ],
    );
    // CLI/UI smoke 契约：无头验证模式的标志与结果 marker 逐字锁定。
    assert_markers_present(
        LEGACY_CLI,
        "FND-07E CLI markers",
        &[
            "\"--sniff-cli\"",
            "\"--extract-cli\"",
            "\"--ui-test\"",
            "\"--probe-eval\"",
            "SNIFF_RESULT_JSON: {}",
            "EXTRACT_RESULT_JSON: {}",
        ],
    );
}

#[test]
fn fnd_12_legacy_sniff_rejects_invalid_url_without_panicking() {
    assert_markers_present(
        LEGACY_SNIFF,
        "FND-12-R2 typed sniff URL validation",
        &[
            "fn parse_sniff_url",
            "let external_url = parse_sniff_url(url)?;",
        ],
    );
    assert_forbidden_markers_absent(
        LEGACY_SNIFF,
        "FND-12-R2 panic-free sniff URL validation",
        &["url_owned.parse().unwrap()"],
    );
}

#[test]
fn rl_001_formal_build_excludes_general_purpose_lan_router() {
    assert_markers_present(
        ROOT_CARGO,
        "RL-001 feature boundary",
        &[
            "default = [\"formal-product\"]",
            "legacy-dev = [",
            "\"dep:axum\"",
            "\"dep:reqwest\"",
            "\"dep:tokio\"",
        ],
    );
    assert_markers_present(
        ROOT_LIB,
        "RL-001 module boundary",
        &["#[cfg(feature = \"legacy-dev\")]\npub mod relay;"],
    );
    assert_markers_present(
        LEGACY_RELAY,
        "RL-001 legacy routes remain explicit",
        &[
            ".route(\"/proxy/{*rest}\"",
            ".route(\"/api/extract\"",
            ".route(\"/player\"",
            ".route(\"/probeplayer\"",
        ],
    );
}

#[test]
fn rg_006_legacy_targets_require_explicit_non_default_feature() {
    assert_markers_present(ROOT_LIB, "RG-006 root targets", &["compile_error!"]);
    assert_markers_present(
        ROOT_CARGO,
        "RG-006 explicit legacy targets",
        &["required-features = [\"legacy-dev\"]"],
    );
    for manifest in [APP_CARGO, DEMO_CARGO] {
        assert_markers_present(
            manifest,
            "RG-006 legacy workspace member",
            &["default-features = false", "features = [\"legacy-dev\"]"],
        );
    }
}
