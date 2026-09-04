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
use crate::lan::{SameOriginLanTarget, SELECTED_LAN_PROBE_TIMEOUT};

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

/// Local inspection facts only; neither recognition nor HTTP success proves
/// transferable protection or receiver reachability. Safe for diagnostics.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InspectionStatus {
    Recognized,
    Unrecognized,
    RedirectRefused,
    UpstreamRejected,
}

/// Detailed result kept in trusted memory: parsed manifests can contain URLs.
/// Deliberately no Debug; only the closed status may leave this boundary.
pub struct InspectionReport {
    inspection: Inspection,
    status: InspectionStatus,
}

impl InspectionReport {
    #[must_use]
    pub const fn status(&self) -> InspectionStatus {
        self.status
    }

    #[must_use]
    pub fn into_inspection(self) -> Inspection {
        self.inspection
    }

    fn parsed(inspection: Inspection) -> Self {
        let status = if matches!(inspection, Inspection::Unknown) {
            InspectionStatus::Unrecognized
        } else {
            InspectionStatus::Recognized
        };
        Self { inspection, status }
    }

    fn inconclusive_response(status: u16) -> Self {
        Self {
            inspection: Inspection::Unknown,
            status: match status {
                200..=299 => InspectionStatus::Unrecognized,
                300..=399 => InspectionStatus::RedirectRefused,
                _ => InspectionStatus::UpstreamRejected,
            },
        }
    }
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

    /// Consumes one exact same-origin LAN target after the caller has checked
    /// current playback and selected-device authority. The batch is bounded,
    /// does not follow redirects or fetch manifest children, and dropping the
    /// future cancels outstanding HTTP work without a detached worker.
    pub async fn inspect_selected_lan(
        &self,
        target: SameOriginLanTarget,
    ) -> Result<Inspection, ProbeHttpError> {
        self.inspect_selected_lan_report(target)
            .await
            .map(InspectionReport::into_inspection)
    }

    /// Same exact-target authority and deadline as `inspect_selected_lan`,
    /// retaining local HTTP outcomes independently of protection evidence.
    pub async fn inspect_selected_lan_report(
        &self,
        target: SameOriginLanTarget,
    ) -> Result<InspectionReport, ProbeHttpError> {
        let url = target.url().to_owned();
        let scoped = Self::new(self.http.for_selected_lan(target));
        tokio::time::timeout(SELECTED_LAN_PROBE_TIMEOUT, scoped.inspect_report(&url))
            .await
            .map_err(|_| ProbeHttpError::Timeout)?
    }

    /// Inspects a candidate URL. HLS/DASH are recognized by content-type or
    /// `#EXTM3U` body; MP4 by the `ftyp` box. A HEAD 405 falls back to a
    /// bounded Range read (PL-003).
    pub async fn inspect(&self, url: &str) -> Result<Inspection, ProbeHttpError> {
        self.inspect_report(url)
            .await
            .map(InspectionReport::into_inspection)
    }

    /// Preserves refusal vs unrecognized content without changing the legacy
    /// HEAD/Range budget, fallback conditions, parsers or error semantics.
    pub async fn inspect_report(&self, url: &str) -> Result<InspectionReport, ProbeHttpError> {
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
                return Ok(InspectionReport::inconclusive_response(range.status));
            }
            let range_ct = range.content_type.clone().unwrap_or_default();
            let body = &range.body;
            if body.starts_with(b"#EXTM3U") || range_ct.contains("mpegurl") {
                return Ok(InspectionReport::parsed(
                    match parse_hls(&String::from_utf8_lossy(body), url) {
                        Some(playlist) => Inspection::Hls(playlist),
                        None => Inspection::Unknown,
                    },
                ));
            }
            if range_ct.contains("dash") || looks_dash {
                let text = String::from_utf8_lossy(body);
                return Ok(InspectionReport::parsed(Inspection::Dash(DashInspection {
                    has_content_protection: text.contains("ContentProtection"),
                    representation_count: text.matches("<Representation").count(),
                })));
            }
            if let Some(brand) = mp4_major_brand(body) {
                return Ok(InspectionReport::parsed(Inspection::Mp4(Mp4Inspection {
                    major_brand: brand,
                })));
            }
            return Ok(InspectionReport::parsed(Inspection::Unknown));
        }
        Ok(InspectionReport::inconclusive_response(head.status))
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
