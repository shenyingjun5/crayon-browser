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
        &["mod legacy_sniffer;", "use legacy_sniffer::SNIFF_JS;"],
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
    for source in [LEGACY_APP_MAIN, LEGACY_SNIFFER_JS] {
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
    for source in [LEGACY_APP_MAIN, LEGACY_SNIFFER_JS] {
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
fn rg_004_legacy_baseline_detects_fixed_beacon_and_lan_bind() {
    // FND-07C 起 beacon 服务在独立模块；固定端口/route 仍为待消除的 legacy 基线。
    assert_markers_present(
        LEGACY_APP_MAIN,
        "RG-004 legacy beacon module wiring",
        &[
            "mod legacy_beacon;",
            "use legacy_beacon::start_beacon_server;",
        ],
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
        LEGACY_APP_MAIN,
        "RG-004 legacy relay baseline",
        &["host: \"0.0.0.0\".into()", "port: 8321"],
    );
}

#[test]
fn rl_001_formal_build_excludes_general_purpose_lan_router() {
    assert_markers_present(
        ROOT_CARGO,
        "RL-001 feature boundary",
        &["default = [\"formal-product\"]", "legacy-dev = []"],
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
