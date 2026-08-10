//! Strongly typed product configuration (FND-11).
//!
//! `ProductConfig` centralises every independently variable value: port
//! ranges, timeouts, capacities, update channel and log policy. Loading is
//! strict — unknown fields, missing sections, out-of-range values and any
//! secret-bearing key abort startup with a stable `ConfigError` code.
//! Secrets are forbidden in configuration by rule (AGENTS.md §5).

use serde::Deserialize;
use std::fmt::{Display, Formatter};
use std::path::Path;

/// Supported configuration schema version.
pub const CONFIG_SCHEMA_VERSION: u32 = 1;

/// Stable configuration failure with a machine-readable code.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ConfigError {
    /// File could not be read.
    Io(String),
    /// TOML parse failure.
    Malformed(String),
    /// `schema_version` is outside the supported window.
    UnsupportedVersion(u32),
    /// Port range is empty, inverted, or out of bounds.
    InvalidPortRange(&'static str),
    /// Timeout value is zero or exceeds its bound.
    InvalidTimeout(&'static str),
    /// Capacity value is zero or exceeds its bound.
    InvalidCapacity(&'static str),
    /// A secret-bearing key was found; secrets are forbidden in config.
    SecretNotAllowed(String),
}

impl ConfigError {
    /// Stable machine-readable code.
    #[must_use]
    pub const fn code(&self) -> &'static str {
        match self {
            Self::Io(_) => "config_io",
            Self::Malformed(_) => "config_malformed",
            Self::UnsupportedVersion(_) => "config_unsupported_version",
            Self::InvalidPortRange(_) => "config_invalid_port_range",
            Self::InvalidTimeout(_) => "config_invalid_timeout",
            Self::InvalidCapacity(_) => "config_invalid_capacity",
            Self::SecretNotAllowed(_) => "config_secret_not_allowed",
        }
    }
}

impl Display for ConfigError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(detail) => write!(f, "{}: {detail}", self.code()),
            Self::Malformed(detail) => write!(f, "{}: {detail}", self.code()),
            Self::UnsupportedVersion(v) => write!(f, "{}: {v}", self.code()),
            Self::InvalidPortRange(field) => write!(f, "{}: {field}", self.code()),
            Self::InvalidTimeout(field) => write!(f, "{}: {field}", self.code()),
            Self::InvalidCapacity(field) => write!(f, "{}: {field}", self.code()),
            Self::SecretNotAllowed(key) => write!(f, "{}: {key}", self.code()),
        }
    }
}

impl std::error::Error for ConfigError {}

/// Key fragments that mark a config entry as secret-bearing (never allowed).
const SECRET_KEY_FRAGMENTS: &[&str] = &[
    "password",
    "secret",
    "token",
    "cookie",
    "authorization",
    "api_key",
    "apikey",
    "private_key",
];

/// Network port ranges (inclusive). Sessions bind inside these ranges only.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NetworkConfig {
    pub relay_port_min: u16,
    pub relay_port_max: u16,
    pub control_port_min: u16,
    pub control_port_max: u16,
}

/// Timeout policy in seconds.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TimeoutConfig {
    pub connect_seconds: u64,
    pub read_idle_seconds: u64,
    /// Cast/relay session time-to-live.
    pub session_ttl_seconds: u64,
}

/// Capacity bounds (queues, sessions, message sizes).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CapacityConfig {
    pub max_sessions: u32,
    pub max_requests_per_session: u32,
    pub max_message_bytes: u32,
}

/// Update channel (design §17: three isolated channels).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UpdateChannel {
    Stable,
    Beta,
    Dev,
}

/// Log verbosity.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum LogLevel {
    Error,
    Warn,
    Info,
    Debug,
}

/// Logging policy. URL redaction defaults to on and is part of the privacy
/// contract (design §15).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LoggingConfig {
    pub level: LogLevel,
    pub redact_urls: bool,
}

/// Validated product configuration.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ProductConfig {
    pub schema_version: u32,
    pub network: NetworkConfig,
    pub timeouts: TimeoutConfig,
    pub capacity: CapacityConfig,
    pub update: UpdateSection,
    pub logging: LoggingConfig,
}

/// Update section wrapper (TOML `[update]`).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UpdateSection {
    pub channel: UpdateChannel,
}

/// Longest plausible session: 24h.
const MAX_SESSION_TTL_SECONDS: u64 = 24 * 3600;
/// Single timeout bound: 10 minutes.
const MAX_TIMEOUT_SECONDS: u64 = 600;
/// Capacity ceilings (bounded queues rule).
const MAX_SESSIONS: u32 = 1024;
const MAX_REQUESTS_PER_SESSION: u32 = 1_000_000;
const MAX_MESSAGE_BYTES: u32 = 16 * 1024 * 1024;

impl ProductConfig {
    /// Loads and validates from a TOML file.
    pub fn load(path: &Path) -> Result<Self, ConfigError> {
        let text = std::fs::read_to_string(path)
            .map_err(|e| ConfigError::Io(format!("{}: {e}", path.display())))?;
        Self::from_toml(&text)
    }

    /// Parses and validates TOML text. Any failure aborts startup with a
    /// stable error — there is no best-effort partial configuration.
    pub fn from_toml(text: &str) -> Result<Self, ConfigError> {
        reject_secret_keys(text)?;
        let config: Self =
            toml::from_str(text).map_err(|e| ConfigError::Malformed(e.to_string()))?;
        config.validate()?;
        Ok(config)
    }

    fn validate(&self) -> Result<(), ConfigError> {
        if self.schema_version != CONFIG_SCHEMA_VERSION {
            return Err(ConfigError::UnsupportedVersion(self.schema_version));
        }
        validate_port_range(
            "network.relay_port",
            self.network.relay_port_min,
            self.network.relay_port_max,
        )?;
        validate_port_range(
            "network.control_port",
            self.network.control_port_min,
            self.network.control_port_max,
        )?;
        if self.network.relay_port_min <= self.network.control_port_max
            && self.network.control_port_min <= self.network.relay_port_max
        {
            return Err(ConfigError::InvalidPortRange("network.ranges_overlap"));
        }
        validate_timeout("timeouts.connect_seconds", self.timeouts.connect_seconds)?;
        validate_timeout(
            "timeouts.read_idle_seconds",
            self.timeouts.read_idle_seconds,
        )?;
        if self.timeouts.session_ttl_seconds == 0
            || self.timeouts.session_ttl_seconds > MAX_SESSION_TTL_SECONDS
        {
            return Err(ConfigError::InvalidTimeout("timeouts.session_ttl_seconds"));
        }
        validate_capacity(
            "capacity.max_sessions",
            self.capacity.max_sessions,
            MAX_SESSIONS,
        )?;
        validate_capacity(
            "capacity.max_requests_per_session",
            self.capacity.max_requests_per_session,
            MAX_REQUESTS_PER_SESSION,
        )?;
        validate_capacity(
            "capacity.max_message_bytes",
            self.capacity.max_message_bytes,
            MAX_MESSAGE_BYTES,
        )?;
        Ok(())
    }
}

fn validate_port_range(field: &'static str, min: u16, max: u16) -> Result<(), ConfigError> {
    if min == 0 || min >= max {
        return Err(ConfigError::InvalidPortRange(field));
    }
    Ok(())
}

fn validate_timeout(field: &'static str, seconds: u64) -> Result<(), ConfigError> {
    if seconds == 0 || seconds > MAX_TIMEOUT_SECONDS {
        return Err(ConfigError::InvalidTimeout(field));
    }
    Ok(())
}

fn validate_capacity(field: &'static str, value: u32, max: u32) -> Result<(), ConfigError> {
    if value == 0 || value > max {
        return Err(ConfigError::InvalidCapacity(field));
    }
    Ok(())
}

/// Rejects any TOML key whose name contains a secret-bearing fragment
/// (case-insensitive), before values are ever looked at.
fn reject_secret_keys(text: &str) -> Result<(), ConfigError> {
    let value: toml::Value =
        toml::from_str(text).map_err(|e| ConfigError::Malformed(e.to_string()))?;
    let mut stack = vec![&value];
    while let Some(current) = stack.pop() {
        if let toml::Value::Table(table) = current {
            for (key, child) in table {
                let lower = key.to_ascii_lowercase();
                if SECRET_KEY_FRAGMENTS
                    .iter()
                    .any(|fragment| lower.contains(fragment))
                {
                    return Err(ConfigError::SecretNotAllowed(key.clone()));
                }
                stack.push(child);
            }
        }
    }
    Ok(())
}
