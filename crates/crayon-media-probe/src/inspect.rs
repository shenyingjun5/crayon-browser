//! Bounded media inspection orchestration (MED-06).
//!
//! Combines the probe HTTP client (MED-05) with the format parsers to
//! produce asset/protection facts for the policy engine. Hard rules:
//!
//! - never downloads a body: only HEAD + byte-capped Range reads (PL-003);
//! - never requests a key/license: encryption facts are reported from the
//!   manifest text alone (PL-005/PL-006);
//! - ordinary transport failures stay ordinary errors (PL-014) — an
//!   inspection failure never upgrades trust or implies a relay route.

use crate::hls::{parse_hls, HlsPlaylist};
use crate::http::{ProbeHttpClient, ProbeHttpError};

/// Bytes fetched for playlist/manifest inspection.
const MANIFEST_READ_BYTES: usize = 64 * 1024;
/// Bytes fetched for container sniffing.
const CONTAINER_SNIFF_BYTES: usize = 4 * 1024;

/// DASH manifest inspection.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DashInspection {
    pub has_content_protection: bool,
    pub representation_count: usize,
}

/// MP4/M4S container inspection.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Mp4Inspection {
    pub major_brand: String,
}

/// Inspection outcome. DRM/encryption facts are data; the refusal decision
/// belongs to the policy engine (MED-07/MED-08).
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Inspection {
    Hls(HlsPlaylist),
    Dash(DashInspection),
    Mp4(Mp4Inspection),
    /// Not a recognized media container/manifest.
    Unknown,
}

/// Bounded inspector over a probe HTTP client.
pub struct MediaInspector {
    http: ProbeHttpClient,
}

impl MediaInspector {
    #[must_use]
    pub const fn new(http: ProbeHttpClient) -> Self {
        Self { http }
    }

    /// Inspects a candidate URL. HLS/DASH are recognized by content-type or
    /// `#EXTM3U` body; MP4 by the `ftyp` box. A HEAD 405 falls back to a
    /// bounded Range read (PL-003).
    pub async fn inspect(&self, url: &str) -> Result<Inspection, ProbeHttpError> {
        let head = self.http.head(url).await?;
        let content_type = head.content_type.clone().unwrap_or_default();

        let looks_hls = content_type.contains("mpegurl")
            || url.split('?').next().unwrap_or("").ends_with(".m3u8");
        let looks_dash =
            content_type.contains("dash") || url.split('?').next().unwrap_or("").ends_with(".mpd");

        if looks_hls || looks_dash || head.status == 405 || head.status == 200 {
            let cap = if looks_hls || looks_dash {
                MANIFEST_READ_BYTES
            } else {
                CONTAINER_SNIFF_BYTES
            };
            let range = self.http.range_get(url, 0, cap).await?;
            if !(200..300).contains(&range.status) {
                return Ok(Inspection::Unknown);
            }
            let range_ct = range.content_type.clone().unwrap_or_default();
            let body = &range.body;
            if body.starts_with(b"#EXTM3U") || range_ct.contains("mpegurl") {
                return Ok(match parse_hls(&String::from_utf8_lossy(body), url) {
                    Some(playlist) => Inspection::Hls(playlist),
                    None => Inspection::Unknown,
                });
            }
            if range_ct.contains("dash") || looks_dash {
                let text = String::from_utf8_lossy(body);
                return Ok(Inspection::Dash(DashInspection {
                    has_content_protection: text.contains("ContentProtection"),
                    representation_count: text.matches("<Representation").count(),
                }));
            }
            if let Some(brand) = mp4_major_brand(body) {
                return Ok(Inspection::Mp4(Mp4Inspection { major_brand: brand }));
            }
        }
        Ok(Inspection::Unknown)
    }
}

/// Reads the major brand from the first `ftyp` box without downloading the
/// body. Returns `None` when the bytes are not an MP4-family container.
#[must_use]
pub fn mp4_major_brand(bytes: &[u8]) -> Option<String> {
    if bytes.len() < 12 {
        return None;
    }
    let declared = u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]) as usize;
    if &bytes[4..8] != b"ftyp" || declared < 12 {
        return None;
    }
    let brand = &bytes[8..12];
    if !brand.iter().all(|b| b.is_ascii_alphanumeric()) {
        return None;
    }
    Some(String::from_utf8_lossy(brand).to_string())
}
