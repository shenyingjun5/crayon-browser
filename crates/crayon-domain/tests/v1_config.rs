//! ProductConfig contract: defaults file loads, strict validation failures
//! (missing/illegal/boundary/secret) abort with stable codes (FND-11, RG-004).

use crayon_domain::{ConfigError, LogLevel, ProductConfig, UpdateChannel};
use std::path::PathBuf;

fn defaults_path() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../config/product-defaults.toml")
}

fn minimal_toml() -> &'static str {
    r#"
schema_version = 1
[network]
relay_port_min = 20000
relay_port_max = 29999
control_port_min = 30000
control_port_max = 30999
[timeouts]
connect_seconds = 10
read_idle_seconds = 10
session_ttl_seconds = 7200
[capacity]
max_sessions = 32
max_requests_per_session = 4096
max_message_bytes = 65536
[update]
channel = "stable"
[logging]
level = "info"
redact_urls = true
"#
}

#[test]
fn shipped_defaults_load_and_validate() {
    let config = ProductConfig::load(&defaults_path()).expect("shipped defaults must load");
    assert_eq!(config.schema_version, 1);
    assert!(config.network.relay_port_min < config.network.relay_port_max);
    assert_eq!(config.update.channel, UpdateChannel::Stable);
    assert_eq!(config.logging.level, LogLevel::Info);
    assert!(config.logging.redact_urls, "URL 脱敏默认开启（隐私契约）");
}

#[test]
fn valid_minimal_config_loads() {
    ProductConfig::from_toml(minimal_toml()).expect("minimal valid config");
}

#[test]
fn unsupported_version_is_rejected() {
    let text = minimal_toml().replace("schema_version = 1", "schema_version = 2");
    assert_eq!(
        ProductConfig::from_toml(&text),
        Err(ConfigError::UnsupportedVersion(2))
    );
}

#[test]
fn missing_section_is_rejected() {
    let text = minimal_toml().replace("[logging]\nlevel = \"info\"\nredact_urls = true\n", "");
    let err = ProductConfig::from_toml(&text).unwrap_err();
    assert_eq!(err.code(), "config_malformed");
}

#[test]
fn unknown_field_is_rejected() {
    let text = minimal_toml().replace("redact_urls = true", "redact_urls = true\nextra_key = 1");
    let err = ProductConfig::from_toml(&text).unwrap_err();
    assert_eq!(err.code(), "config_malformed");
}

#[test]
fn port_range_validation() {
    // Inverted range.
    let text = minimal_toml()
        .replace("relay_port_min = 20000", "relay_port_min = 29999")
        .replace("relay_port_max = 29999", "relay_port_max = 20000");
    assert_eq!(
        ProductConfig::from_toml(&text),
        Err(ConfigError::InvalidPortRange("network.relay_port"))
    );
    // Overlapping ranges.
    let text = minimal_toml().replace("control_port_min = 30000", "control_port_min = 29999");
    assert_eq!(
        ProductConfig::from_toml(&text),
        Err(ConfigError::InvalidPortRange("network.ranges_overlap"))
    );
    // Boundary: the narrowest valid range (min + 1 == max) loads fine.
    let text = minimal_toml().replace("relay_port_max = 29999", "relay_port_max = 20001");
    assert!(ProductConfig::from_toml(&text).is_ok());
}

#[test]
fn timeout_and_capacity_bounds() {
    let text = minimal_toml().replace("connect_seconds = 10", "connect_seconds = 0");
    assert_eq!(
        ProductConfig::from_toml(&text),
        Err(ConfigError::InvalidTimeout("timeouts.connect_seconds"))
    );
    let text = minimal_toml().replace("session_ttl_seconds = 7200", "session_ttl_seconds = 90000");
    assert_eq!(
        ProductConfig::from_toml(&text),
        Err(ConfigError::InvalidTimeout("timeouts.session_ttl_seconds"))
    );
    let text = minimal_toml().replace("max_sessions = 32", "max_sessions = 0");
    assert_eq!(
        ProductConfig::from_toml(&text),
        Err(ConfigError::InvalidCapacity("capacity.max_sessions"))
    );
}

#[test]
fn unknown_channel_is_rejected() {
    let text = minimal_toml().replace("channel = \"stable\"", "channel = \"nightly\"");
    let err = ProductConfig::from_toml(&text).unwrap_err();
    assert_eq!(err.code(), "config_malformed");
}

#[test]
fn secret_keys_are_rejected_before_parsing_values() {
    for key in ["api_token", "cookie", "upstream_password", "client_secret"] {
        let text = format!("{}\n[extra]\n{key} = \"x\"\n", minimal_toml());
        let err = ProductConfig::from_toml(&text).unwrap_err();
        assert_eq!(err.code(), "config_secret_not_allowed", "key: {key}");
    }
}

#[test]
fn missing_file_reports_io_error() {
    let err = ProductConfig::load(&defaults_path().with_file_name("nope.toml")).unwrap_err();
    assert_eq!(err.code(), "config_io");
}
