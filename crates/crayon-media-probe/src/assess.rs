//! Protection/codec evidence merge with conservative semantics (MED-07).
//!
//! Merges inspection facts (MED-06), browser-layer signals (EME `encrypted`,
//! blob/MediaStream sources) and codec evidence into one assessment. The
//! merge is deliberately conservative: any protection signal wins over a
//! clean-looking URL, and an inconclusive inspection is `Unknown` — never
//! silently `Clear`. The current compliance posture refuses direct cast/relay
//! for anything that needs a key; this module only reports facts, the
//! Direct/Relay/ExternalClientHandoff/Reject decision belongs to
//! `crayon-cast-policy`.

use crate::hls::{HlsEncryption, HlsPlaylist};
use crate::inspect::Inspection;
use crate::CodecInfo;

/// One piece of protection evidence.
#[derive(Clone, Debug, PartialEq)]
pub enum ProtectionEvidence {
    /// Bounded inspection outcome from `MediaInspector`.
    Inspection(Inspection),
    /// Browser observed an EME `encrypted` event for the associated media
    /// element — upgrades protection even when the URL looks clear (BR-011).
    EmeEncryptedSignal,
    /// The media source is a `blob:` URL or MediaStream with no underlying
    /// fetchable URL (BR-012) — direct cast must never fabricate one.
    BlobOrStreamSource,
}

/// Merged protection verdict (strongest wins).
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum Protection {
    /// No protection evidence found.
    Clear,
    /// Inspection was inconclusive — conservatively not direct-castable.
    Unknown,
    /// AES-128 / SAMPLE-AES / SESSION-KEY: a key would be required.
    KeyRequired,
    /// No underlying URL exists (blob:/MediaStream).
    NoDirectUrl,
    /// DRM KEYFORMAT, DASH ContentProtection, or an EME encrypted signal.
    DrmProtected,
}

/// Assessment result: protection verdict + optional codec evidence.
#[derive(Clone, Debug, PartialEq)]
pub struct ProtectionAssessment {
    pub protection: Protection,
    /// Codec/container evidence when parsers produced it (m3u8 CODECS,
    /// MP4 box tree); `None` means "not determined", never a guess.
    pub codecs: Option<CodecInfo>,
}

/// Merges evidence into one assessment. Precedence (strongest first):
/// `DrmProtected` > `NoDirectUrl` > `KeyRequired` > `Unknown` > `Clear`.
#[must_use]
pub fn assess_protection(
    evidence: &[ProtectionEvidence],
    codecs: Option<CodecInfo>,
) -> ProtectionAssessment {
    let mut protection = Protection::Clear;
    let mut upgrade = |p: Protection| {
        if p > protection {
            protection = p;
        }
    };
    for item in evidence {
        match item {
            ProtectionEvidence::EmeEncryptedSignal => upgrade(Protection::DrmProtected),
            ProtectionEvidence::BlobOrStreamSource => upgrade(Protection::NoDirectUrl),
            ProtectionEvidence::Inspection(Inspection::Unknown) => upgrade(Protection::Unknown),
            ProtectionEvidence::Inspection(Inspection::Mp4(_)) => {}
            ProtectionEvidence::Inspection(Inspection::Dash(dash)) => {
                if dash.has_content_protection {
                    upgrade(Protection::DrmProtected);
                }
            }
            ProtectionEvidence::Inspection(Inspection::Hls(HlsPlaylist::Media {
                encryption,
                ..
            })) => match encryption {
                HlsEncryption::None => {}
                HlsEncryption::DrmKeyFormat { .. } => upgrade(Protection::DrmProtected),
                HlsEncryption::Aes128 { .. }
                | HlsEncryption::SampleAes { .. }
                | HlsEncryption::SessionKey { .. } => upgrade(Protection::KeyRequired),
            },
            ProtectionEvidence::Inspection(Inspection::Hls(HlsPlaylist::Master {
                session_keys,
                ..
            })) => {
                if !session_keys.is_empty() {
                    upgrade(Protection::KeyRequired);
                }
            }
        }
    }
    ProtectionAssessment { protection, codecs }
}
