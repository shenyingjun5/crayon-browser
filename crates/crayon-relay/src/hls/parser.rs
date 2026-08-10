//! HLS AST-preserving parser and opaque-route rewriter (MED-14).
//!
//! Parsing keeps every line byte-identical and in order; rewriting replaces
//! only URI slots (URI lines and the `URI="..."` attribute of
//! EXT-X-MEDIA/EXT-X-MAP/EXT-X-I-FRAME-STREAM-INF) with opaque
//! `/s/{token}/r/{id}/{name}` paths supplied by the allocator (RL-010).
//!
//! Refusal rules (current compliance posture):
//! - any key-bearing playlist (`EXT-X-KEY` with METHOD != NONE, or
//!   EXT-X-SESSION-KEY) is rejected — the relay never rewrites encrypted
//!   streams and never fetches keys (PL-005);
//! - nesting depth, line count and resource count are bounded;
//! - relative, absolute and query-carrying URIs all resolve against the
//!   document base URL without losing the query (RL-010).

/// Maximum playlist lines (bounded input rule).
pub const MAX_LINES: usize = 10_000;
/// Maximum nested playlist depth (master → master → media …).
pub const MAX_DEPTH: u8 = 5;
/// Maximum opaque resources allocated per document.
pub const MAX_RESOURCES_PER_DOCUMENT: usize = 4096;

/// Parse/rewrite failure.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum HlsError {
    /// Body does not start with `#EXTM3U`.
    NotHls,
    TooManyLines,
    /// Playlist requires a key (encryption facts present).
    Encrypted,
    DepthExceeded,
    ResourceLimit,
    /// Allocator declined a URI (e.g. origin scope violation).
    AllocationFailed(String),
}

impl Display for HlsError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::NotHls => "not an HLS playlist",
            Self::TooManyLines => "playlist exceeds the line bound",
            Self::Encrypted => "playlist requires a key",
            Self::DepthExceeded => "nested playlist depth exceeded",
            Self::ResourceLimit => "playlist exceeds the resource bound",
            Self::AllocationFailed(_) => "uri allocation failed",
        };
        f.write_str(message)
    }
}

impl std::error::Error for HlsError {}

use std::fmt::{Display, Formatter};

/// One parsed line. `raw` is byte-preserved (minus the line feed); a URI
/// slot marks the replaceable part.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlaylistLine {
    raw: String,
    uri_slot: Option<UriSlot>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum UriSlot {
    /// The whole line is a URI (variant/segment line).
    WholeLine,
    /// `URI="..."` attribute inside a tag line.
    Attribute,
}

/// Playlist classification.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlaylistKind {
    Master,
    Media,
}

/// AST-preserving parsed playlist.
#[derive(Clone, Debug)]
pub struct ParsedPlaylist {
    lines: Vec<PlaylistLine>,
    kind: PlaylistKind,
    trailing_newline: bool,
}

impl ParsedPlaylist {
    #[must_use]
    pub fn kind(&self) -> PlaylistKind {
        self.kind
    }

    #[must_use]
    pub fn line_count(&self) -> usize {
        self.lines.len()
    }
}

/// Parses a playlist, preserving bytes and order.
pub fn parse(body: &str) -> Result<ParsedPlaylist, HlsError> {
    if !body.starts_with("#EXTM3U") {
        return Err(HlsError::NotHls);
    }
    let trailing_newline = body.ends_with('\n');
    let mut raw_lines: Vec<&str> = body.split('\n').collect();
    if trailing_newline {
        raw_lines.pop(); // split 在尾部换行后留下空串
    }
    if raw_lines.len() > MAX_LINES {
        return Err(HlsError::TooManyLines);
    }
    let mut kind = PlaylistKind::Media;
    let mut lines = Vec::with_capacity(raw_lines.len());
    for raw in raw_lines {
        let trimmed = raw.trim_end_matches('\r');
        let probe = trimmed.trim();
        let mut uri_slot = None;
        if probe.is_empty() {
            // 空行原样保留
        } else if probe.starts_with("#EXT-X-STREAM-INF:")
            || probe.starts_with("#EXT-X-I-FRAME-STREAM-INF:")
        {
            kind = PlaylistKind::Master;
            if probe.starts_with("#EXT-X-I-FRAME-STREAM-INF:") {
                uri_slot = Some(UriSlot::Attribute);
            }
        } else if probe.starts_with("#EXT-X-MEDIA:") {
            kind = PlaylistKind::Master;
            if attr_value(probe, "URI").is_some() {
                uri_slot = Some(UriSlot::Attribute);
            }
        } else if probe.starts_with("#EXT-X-MAP:") {
            if attr_value(probe, "URI").is_some() {
                uri_slot = Some(UriSlot::Attribute);
            }
        } else if probe.starts_with("#EXT-X-KEY:") {
            let method = attr_value(probe, "METHOD").unwrap_or_default();
            if method != "NONE" {
                return Err(HlsError::Encrypted);
            }
        } else if probe.starts_with("#EXT-X-SESSION-KEY:") {
            return Err(HlsError::Encrypted);
        } else if !probe.starts_with('#') {
            uri_slot = Some(UriSlot::WholeLine);
        }
        lines.push(PlaylistLine {
            raw: raw.to_string(),
            uri_slot,
        });
    }
    Ok(ParsedPlaylist {
        lines,
        kind,
        trailing_newline,
    })
}

/// Rewrites the playlist. `allocator` receives each absolute upstream URL
/// and returns its opaque route path; relative and query-carrying URIs are
/// resolved against `base_url` first (query preserved, RL-010).
pub fn rewrite(
    parsed: &ParsedPlaylist,
    base_url: &str,
    depth: u8,
    mut allocator: impl FnMut(&str) -> Result<String, HlsError>,
) -> Result<String, HlsError> {
    if depth > MAX_DEPTH {
        return Err(HlsError::DepthExceeded);
    }
    let base = url::Url::parse(base_url).map_err(|_| HlsError::NotHls)?;
    let mut allocated = 0usize;
    let mut out = String::with_capacity(parsed.lines.len() * 64);
    for (index, line) in parsed.lines.iter().enumerate() {
        let Some(slot) = &line.uri_slot else {
            out.push_str(&line.raw);
            push_newline(&mut out, parsed, index);
            continue;
        };
        allocated += 1;
        if allocated > MAX_RESOURCES_PER_DOCUMENT {
            return Err(HlsError::ResourceLimit);
        }
        match slot {
            UriSlot::WholeLine => {
                let absolute = resolve(&base, line.raw.trim_end_matches('\r').trim())?;
                out.push_str(&allocator(&absolute)?);
            }
            UriSlot::Attribute => {
                let raw = line.raw.trim_end_matches('\r').to_string();
                let uri = attr_value(&raw, "URI").ok_or(HlsError::NotHls)?;
                let absolute = resolve(&base, &uri)?;
                let opaque = allocator(&absolute)?;
                out.push_str(&replace_uri_attr(&raw, &opaque));
            }
        }
        push_newline(&mut out, parsed, index);
    }
    Ok(out)
}

fn push_newline(out: &mut String, parsed: &ParsedPlaylist, index: usize) {
    if index + 1 < parsed.lines.len() || parsed.trailing_newline {
        out.push('\n');
    }
}

fn resolve(base: &url::Url, uri: &str) -> Result<String, HlsError> {
    base.join(uri)
        .map(|u| u.to_string())
        .map_err(|_| HlsError::AllocationFailed(uri.to_string()))
}

/// Reads `NAME="value"` or `NAME=value` from a tag line.
fn attr_value(line: &str, name: &str) -> Option<String> {
    let prefix = format!("{name}=");
    let start = line.find(&prefix)? + prefix.len();
    let rest = &line[start..];
    if let Some(stripped) = rest.strip_prefix('"') {
        let end = stripped.find('"')?;
        Some(stripped[..end].to_string())
    } else {
        let end = rest.find(',').unwrap_or(rest.len());
        Some(rest[..end].to_string())
    }
}

/// Replaces the `URI="..."` value inside a tag line, keeping everything
/// else byte-identical.
fn replace_uri_attr(line: &str, opaque: &str) -> String {
    let prefix = "URI=\"";
    let Some(start) = line.find(prefix) else {
        return line.to_string();
    };
    let value_start = start + prefix.len();
    let Some(rel_end) = line[value_start..].find('"') else {
        return line.to_string();
    };
    format!(
        "{}{}{}",
        &line[..value_start],
        opaque,
        &line[value_start + rel_end..]
    )
}
