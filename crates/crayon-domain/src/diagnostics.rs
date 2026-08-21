//! Diagnostics data classification, deterministic redaction, the versioned
//! event schema and a bounded non-blocking producer.
//!
//! Rules:
//! - Only `Operational` and `Diagnostic` class data may enter a diagnostic
//!   event; user content and secrets fail closed at construction.
//! - Attribute values are redacted deterministically at insertion time
//!   (RL-014): URL queries and userinfo, credential headers, bearer/basic
//!   tokens and well-known token parameters are scrubbed.
//! - The producer is bounded and non-blocking; a full queue drops the
//!   incoming event and counts the drop.  Diagnostics never participates
//!   in business correctness.
//! - The module performs no I/O and reads no clock; timestamps are
//!   caller-injected.

use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, VecDeque};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Wire schema version carried by every diagnostic event.
pub const DIAGNOSTICS_SCHEMA_VERSION: u32 = 1;

/// Maximum length of an event name, in bytes.
pub const MAX_EVENT_NAME_LEN: usize = 64;

/// Maximum length of an attribute key, in bytes.
pub const MAX_ATTRIBUTE_KEY_LEN: usize = 32;

/// Maximum length of an attribute value, in bytes.
pub const MAX_ATTRIBUTE_VALUE_LEN: usize = 256;

/// Maximum number of attributes on one event.
pub const MAX_ATTRIBUTES_PER_EVENT: usize = 8;

/// Default bounded producer queue capacity.
pub const DEFAULT_QUEUE_CAPACITY: usize = 256;

/// Placeholder substituted for scrubbed content.
const REDACTED: &str = "[redacted]";

/// Closed data classification for anything that could leave the process.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DataClass {
    /// Operational facts (feature flags, versions, counts).
    Operational,
    /// Diagnostic facts (error codes, latency buckets).
    Diagnostic,
    /// Browsing content: URLs with queries, titles, page text.
    UserContent,
    /// Credentials: cookies, authorization headers, tokens, passwords.
    Secret,
}

impl DataClass {
    /// Reports whether data of this class may enter a diagnostic event.
    #[must_use]
    pub const fn permits_diagnostics(self) -> bool {
        matches!(self, Self::Operational | Self::Diagnostic)
    }
}

/// Event construction or validation failure.  Variants are stable and
/// carry no payload data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DiagnosticError {
    /// The class does not permit diagnostics (user content or secret).
    ForbiddenClass,
    /// Event name is empty, too long or uses characters outside the
    /// closed set.
    InvalidName,
    /// Attribute key is empty, too long or uses characters outside the
    /// closed set.
    InvalidAttributeKey,
    /// Attribute value exceeds the length bound.
    InvalidAttributeValue,
    /// The event already carries the maximum number of attributes.
    AttributeCapacity,
    /// A decoded event violates the schema (wrong version, invalid name
    /// or key, forbidden class).
    InvalidEvent,
}

impl Display for DiagnosticError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::ForbiddenClass => "data class may not enter diagnostics",
            Self::InvalidName => "event name violates shape or bounds",
            Self::InvalidAttributeKey => "attribute key violates shape or bounds",
            Self::InvalidAttributeValue => "attribute value exceeds the length bound",
            Self::AttributeCapacity => "event attribute capacity reached",
            Self::InvalidEvent => "decoded event violates the diagnostics schema",
        };
        formatter.write_str(message)
    }
}

impl Error for DiagnosticError {}

/// One versioned diagnostics event.
///
/// Construct through [`DiagnosticEvent::new`] and [`Self::with_attribute`]
/// so every value is validated and redacted.  A deserialized event must be
/// re-checked with [`Self::validate`] before use.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DiagnosticEvent {
    schema: u32,
    class: DataClass,
    name: String,
    timestamp_ms: u64,
    attributes: BTreeMap<String, String>,
}

impl DiagnosticEvent {
    /// Creates an event.  `timestamp_ms` is caller-injected; this module
    /// never reads a clock.
    pub fn new(class: DataClass, name: &str, timestamp_ms: u64) -> Result<Self, DiagnosticError> {
        if !class.permits_diagnostics() {
            return Err(DiagnosticError::ForbiddenClass);
        }
        if !is_valid_token(name, MAX_EVENT_NAME_LEN) {
            return Err(DiagnosticError::InvalidName);
        }
        Ok(Self {
            schema: DIAGNOSTICS_SCHEMA_VERSION,
            class,
            name: name.to_owned(),
            timestamp_ms,
            attributes: BTreeMap::new(),
        })
    }

    /// Adds an attribute.  The value is redacted deterministically before
    /// storage; a value that stays overlong after redaction is rejected.
    pub fn with_attribute(mut self, key: &str, value: &str) -> Result<Self, DiagnosticError> {
        if !is_valid_token(key, MAX_ATTRIBUTE_KEY_LEN) {
            return Err(DiagnosticError::InvalidAttributeKey);
        }
        let redacted = redact_sensitive(value);
        if redacted.len() > MAX_ATTRIBUTE_VALUE_LEN {
            return Err(DiagnosticError::InvalidAttributeValue);
        }
        if self.attributes.len() >= MAX_ATTRIBUTES_PER_EVENT {
            return Err(DiagnosticError::AttributeCapacity);
        }
        self.attributes.insert(key.to_owned(), redacted);
        Ok(self)
    }

    /// Re-checks a decoded event against the schema invariants.
    pub fn validate(&self) -> Result<(), DiagnosticError> {
        if self.schema != DIAGNOSTICS_SCHEMA_VERSION
            || !self.class.permits_diagnostics()
            || !is_valid_token(&self.name, MAX_EVENT_NAME_LEN)
            || self.attributes.len() > MAX_ATTRIBUTES_PER_EVENT
        {
            return Err(DiagnosticError::InvalidEvent);
        }
        for (key, value) in &self.attributes {
            if !is_valid_token(key, MAX_ATTRIBUTE_KEY_LEN) || value.len() > MAX_ATTRIBUTE_VALUE_LEN
            {
                return Err(DiagnosticError::InvalidEvent);
            }
        }
        Ok(())
    }

    #[must_use]
    pub const fn class(&self) -> DataClass {
        self.class
    }

    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }

    #[must_use]
    pub const fn timestamp_ms(&self) -> u64 {
        self.timestamp_ms
    }

    #[must_use]
    pub fn attributes(&self) -> &BTreeMap<String, String> {
        &self.attributes
    }
}

/// Bounded, non-blocking producer queue for diagnostic events.
///
/// When the queue is full the incoming event is dropped and counted; the
/// caller's business flow is never blocked or failed by diagnostics.
pub struct DiagnosticProducer {
    queue: VecDeque<DiagnosticEvent>,
    capacity: usize,
    dropped: u64,
}

impl DiagnosticProducer {
    /// Creates a producer with the given capacity.  Zero converges to one
    /// so the queue is always usable.
    #[must_use]
    pub fn new(capacity: usize) -> Self {
        Self {
            queue: VecDeque::new(),
            capacity: capacity.max(1),
            dropped: 0,
        }
    }

    /// Enqueues an event.  Returns false (and counts a drop) when full.
    pub fn enqueue(&mut self, event: DiagnosticEvent) -> bool {
        if self.queue.len() >= self.capacity {
            self.dropped = self.dropped.saturating_add(1);
            return false;
        }
        self.queue.push_back(event);
        true
    }

    /// Total number of events dropped because the queue was full.
    #[must_use]
    pub const fn dropped(&self) -> u64 {
        self.dropped
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.queue.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.queue.is_empty()
    }

    /// Drains the queue in FIFO order, leaving it empty.
    pub fn drain(&mut self) -> impl Iterator<Item = DiagnosticEvent> + '_ {
        self.queue.drain(..)
    }
}

/// Reports whether `token` is non-empty, within `max_len` and uses only
/// the closed character set `[a-z0-9_.:-]`.
fn is_valid_token(token: &str, max_len: usize) -> bool {
    !token.is_empty()
        && token.len() <= max_len
        && token.bytes().all(|byte| {
            byte.is_ascii_lowercase()
                || byte.is_ascii_digit()
                || matches!(byte, b'_' | b'.' | b':' | b'-')
        })
}

/// Deterministically scrubs credential material from `text` (RL-014):
/// URL queries and userinfo, credential headers, bearer/basic tokens and
/// well-known token parameters.  Benign text passes through unchanged.
#[must_use]
pub fn redact_sensitive(text: &str) -> String {
    let mut output = String::with_capacity(text.len());
    for (index, line) in text.split('\n').enumerate() {
        if index > 0 {
            output.push('\n');
        }
        redact_line(line, &mut output);
    }
    output
}

/// Redacts one line: header rules first, then inline token patterns.
fn redact_line(line: &str, output: &mut String) {
    const HEADER_KEYS: [&str; 4] = [
        "cookie",
        "set-cookie",
        "authorization",
        "proxy-authorization",
    ];
    let lower = line.to_ascii_lowercase();
    for key in HEADER_KEYS {
        if let Some(position) = lower.find(&format!("{key}:")) {
            let colon = position + key.len();
            output.push_str(&line[..=colon]);
            output.push_str(REDACTED);
            return;
        }
    }
    let mut rest = line;
    while !rest.is_empty() {
        match find_sensitive_run(rest) {
            Some((start, end)) => {
                output.push_str(&rest[..start]);
                output.push_str(REDACTED);
                rest = &rest[end..];
            }
            None => {
                output.push_str(rest);
                break;
            }
        }
    }
}

/// Finds the next sensitive run `[start, end)` in `text`: a bearer/basic
/// token, a `token=`/`sign=`/`sessdata=` parameter value, a URL query or
/// URL userinfo.
fn find_sensitive_run(text: &str) -> Option<(usize, usize)> {
    let lower = text.to_ascii_lowercase();
    let mut best: Option<(usize, usize)> = None;
    let mut consider = |run: Option<(usize, usize)>| {
        if let Some(candidate) = run {
            if best.is_none_or(|(start, _)| candidate.0 < start) {
                best = Some(candidate);
            }
        }
    };
    for marker in ["bearer ", "basic "] {
        consider(find_after_marker(&lower, text.len(), marker, |c| {
            c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_' | '~' | '+' | '/' | '=')
        }));
    }
    for marker in ["token=", "sign=", "sessdata="] {
        consider(find_after_marker(&lower, text.len(), marker, |c| {
            c != '&' && !c.is_whitespace()
        }));
    }
    consider(find_url_run(text));
    best
}

/// Finds the value run after the first case-insensitive `marker`; the
/// returned span covers only the value characters matching `is_value`.
fn find_after_marker(
    lower: &str,
    text_len: usize,
    marker: &str,
    is_value: impl Fn(char) -> bool,
) -> Option<(usize, usize)> {
    let start = lower.find(marker)? + marker.len();
    let tail = &lower[start..text_len];
    let value_len = tail.find(|c| !is_value(c)).unwrap_or(tail.len());
    if value_len == 0 {
        return None;
    }
    Some((start, start + value_len))
}

/// Finds a URL query or userinfo run.  Scans every `scheme://` token and
/// returns the earliest sensitive span (query preferred when earlier).
fn find_url_run(text: &str) -> Option<(usize, usize)> {
    let mut search_from = 0_usize;
    while let Some(scheme_end) = text[search_from..].find("://") {
        let token_start = search_from + scheme_end + "://".len();
        let token_tail = &text[token_start..];
        let token_len = token_tail
            .find(|c: char| c.is_whitespace() || matches!(c, '"' | '\'' | '<' | '>' | ')'))
            .unwrap_or(token_tail.len());
        let token_end = token_start + token_len;
        let token = &text[token_start..token_end];
        // Userinfo: `user:pass@` before the first path slash.
        let path_start = token.find('/').unwrap_or(token.len());
        if let Some(at) = token[..path_start].find('@') {
            return Some((token_start, token_start + at));
        }
        if let Some(question) = token.find('?') {
            return Some((token_start + question, token_end));
        }
        search_from = token_end.max(search_from + 1);
    }
    None
}
