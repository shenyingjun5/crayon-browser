//! Locale resource contract (FND-11): zh-CN/en-US key parity, non-empty
//! values, and a secret scan over shipped config + locale resources.

use serde_json::Value;
use std::collections::BTreeSet;
use std::path::PathBuf;
use test_support::leak_scanner::LeakScanner;

fn repo_path(rel: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../")
        .join(rel)
}

fn locale_keys(name: &str) -> BTreeSet<String> {
    let text =
        std::fs::read_to_string(repo_path(&format!("browser/shared-ui/locales/{name}.json")))
            .expect("locale resource must exist");
    let value: Value = serde_json::from_str(&text).expect("locale resource must be valid JSON");
    let object = value.as_object().expect("locale root must be an object");
    for (key, val) in object {
        assert!(
            val.as_str().is_some_and(|s| !s.trim().is_empty()),
            "{name}:{key} must be a non-empty string"
        );
    }
    object.keys().cloned().collect()
}

#[test]
fn locale_key_parity_between_zh_cn_and_en_us() {
    let zh = locale_keys("zh-CN");
    let en = locale_keys("en-US");
    assert!(!zh.is_empty());
    assert_eq!(zh, en, "locale key sets must be identical");
}

#[test]
fn shipped_config_and_locales_carry_no_secrets() {
    // example.com 是文档化示例域名，非泄漏。
    let allowed = ["https://example.com"];
    for rel in [
        "config/product-defaults.toml",
        "config/feature-schema.json",
        "browser/shared-ui/locales/zh-CN.json",
        "browser/shared-ui/locales/en-US.json",
    ] {
        let text = std::fs::read_to_string(repo_path(rel)).expect("resource must exist");
        let findings = LeakScanner::scan_text(&text, &allowed);
        assert!(findings.is_empty(), "{rel}: {findings:?}");
    }
}
