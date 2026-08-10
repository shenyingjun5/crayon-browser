//! HLS playlist inspection parser (MED-06).
//!
//! Read-only, bounded, line-based: extracts the asset relationship facts the
//! policy engine needs (variants, renditions, encryption, init map, endlist)
//! without fetching anything. The AST-preserving rewrite parser for the
//! relay is a separate component (MED-14).

/// Maximum playlist lines inspected (bounded input rule; the HTTP client
/// already caps bytes).
const MAX_LINES: usize = 10_000;

/// Encryption facts found in a playlist. Presence of any non-`None` variant
/// means a key is required; the current compliance posture refuses such
/// streams for direct cast/relay and the key URI is never fetched.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum HlsEncryption {
    None,
    /// `#EXT-X-KEY:METHOD=AES-128` with its (unfetched) key URI.
    Aes128 {
        key_uri: Option<String>,
    },
    /// `#EXT-X-KEY:METHOD=SAMPLE-AES`.
    SampleAes {
        key_uri: Option<String>,
    },
    /// `#EXT-X-KEY` with a DRM `KEYFORMAT` (FairPlay/Widevine/PlayReady…).
    DrmKeyFormat {
        keyformat: String,
    },
    /// `#EXT-X-SESSION-KEY` (master-level key declaration).
    SessionKey {
        keyformat: Option<String>,
    },
}

impl HlsEncryption {
    #[must_use]
    pub const fn requires_key(&self) -> bool {
        !matches!(self, Self::None)
    }
}

/// One `#EXT-X-STREAM-INF` variant.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct VariantInfo {
    pub uri: String,
    pub bandwidth: Option<u64>,
    pub resolution: Option<(u32, u32)>,
    pub codecs: Vec<String>,
}

/// One `#EXT-X-MEDIA` rendition.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RenditionInfo {
    pub media_type: String,
    pub group_id: String,
    pub name: String,
    pub uri: Option<String>,
}

/// Structured inspection result of an HLS playlist.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum HlsPlaylist {
    Master {
        variants: Vec<VariantInfo>,
        renditions: Vec<RenditionInfo>,
        session_keys: Vec<HlsEncryption>,
    },
    Media {
        segment_uris: Vec<String>,
        has_endlist: bool,
        init_map_uri: Option<String>,
        encryption: HlsEncryption,
    },
}

/// Parses playlist text into the inspection structure. Relative URIs are
/// resolved against `base_url` so the asset table is directly usable.
#[must_use]
pub fn parse_hls(body: &str, base_url: &str) -> Option<HlsPlaylist> {
    if !body.starts_with("#EXTM3U") {
        return None;
    }
    let resolve = |uri: &str| -> String {
        url::Url::parse(base_url)
            .and_then(|base| base.join(uri))
            .map(|u| u.to_string())
            .unwrap_or_else(|_| uri.to_string())
    };

    let mut variants = Vec::new();
    let mut renditions = Vec::new();
    let mut session_keys = Vec::new();
    let mut segment_uris = Vec::new();
    let mut has_endlist = false;
    let mut init_map_uri = None;
    let mut encryption = HlsEncryption::None;
    let mut pending_variant: Option<VariantInfo> = None;
    let mut is_master = false;

    for line in body.lines().take(MAX_LINES) {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        if let Some(attrs) = line.strip_prefix("#EXT-X-STREAM-INF:") {
            is_master = true;
            pending_variant = Some(VariantInfo {
                uri: String::new(),
                bandwidth: attr_u64(attrs, "BANDWIDTH"),
                resolution: attr_resolution(attrs),
                codecs: attr_str(attrs, "CODECS")
                    .map(|c| c.split(',').map(|s| s.trim().to_string()).collect())
                    .unwrap_or_default(),
            });
        } else if let Some(attrs) = line.strip_prefix("#EXT-X-MEDIA:") {
            is_master = true;
            renditions.push(RenditionInfo {
                media_type: attr_str(attrs, "TYPE").unwrap_or_default(),
                group_id: attr_str(attrs, "GROUP-ID").unwrap_or_default(),
                name: attr_str(attrs, "NAME").unwrap_or_default(),
                uri: attr_str(attrs, "URI").map(|u| resolve(&u)),
            });
        } else if let Some(attrs) = line.strip_prefix("#EXT-X-SESSION-KEY:") {
            session_keys.push(HlsEncryption::SessionKey {
                keyformat: attr_str(attrs, "KEYFORMAT"),
            });
        } else if let Some(attrs) = line.strip_prefix("#EXT-X-KEY:") {
            encryption = parse_key(attrs);
        } else if let Some(attrs) = line.strip_prefix("#EXT-X-MAP:") {
            init_map_uri = attr_str(attrs, "URI").map(|u| resolve(&u));
        } else if line == "#EXT-X-ENDLIST" {
            has_endlist = true;
        } else if !line.starts_with('#') {
            if let Some(mut variant) = pending_variant.take() {
                variant.uri = resolve(line);
                variants.push(variant);
            } else {
                segment_uris.push(resolve(line));
            }
        }
    }

    if is_master {
        Some(HlsPlaylist::Master {
            variants,
            renditions,
            session_keys,
        })
    } else {
        Some(HlsPlaylist::Media {
            segment_uris,
            has_endlist,
            init_map_uri,
            encryption,
        })
    }
}

fn parse_key(attrs: &str) -> HlsEncryption {
    let method = attr_str(attrs, "METHOD").unwrap_or_default();
    let key_uri = attr_str(attrs, "URI");
    let keyformat = attr_str(attrs, "KEYFORMAT");
    match method.as_str() {
        "NONE" => HlsEncryption::None,
        "AES-128" => match keyformat {
            Some(kf) if crate::protection::keyformat_is_drm(&kf) => {
                HlsEncryption::DrmKeyFormat { keyformat: kf }
            }
            _ => HlsEncryption::Aes128 { key_uri },
        },
        "SAMPLE-AES" => HlsEncryption::SampleAes { key_uri },
        _ => match keyformat {
            Some(kf) if crate::protection::keyformat_is_drm(&kf) => {
                HlsEncryption::DrmKeyFormat { keyformat: kf }
            }
            _ => HlsEncryption::SampleAes { key_uri },
        },
    }
}

/// Reads an unquoted `NAME=value` attribute.
fn attr_u64(attrs: &str, name: &str) -> Option<u64> {
    attr_raw(attrs, name).and_then(|v| v.parse().ok())
}

/// Reads a quoted `NAME="value"` attribute.
fn attr_str(attrs: &str, name: &str) -> Option<String> {
    let raw = attr_raw(attrs, name)?;
    Some(raw.trim_matches('"').to_string())
}

fn attr_raw(attrs: &str, name: &str) -> Option<String> {
    let prefix = format!("{name}=");
    let start = attrs.find(&prefix)? + prefix.len();
    let rest = &attrs[start..];
    if let Some(stripped) = rest.strip_prefix('"') {
        let end = stripped.find('"')?;
        Some(stripped[..end].to_string())
    } else {
        let end = rest.find(',').unwrap_or(rest.len());
        Some(rest[..end].to_string())
    }
}

fn attr_resolution(attrs: &str) -> Option<(u32, u32)> {
    let raw = attr_raw(attrs, "RESOLUTION")?;
    let (w, h) = raw.split_once('x')?;
    Some((w.parse().ok()?, h.parse().ok()?))
}
