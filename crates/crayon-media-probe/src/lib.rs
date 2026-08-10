//! Bounded, platform-neutral media inspection primitives.

mod assess;
mod codec;
mod frame;
mod hls;
pub mod http;
pub mod inspect;
mod protection;

pub use assess::{assess_protection, Protection, ProtectionAssessment, ProtectionEvidence};

pub use codec::{
    codec_name, codecs_from_m3u8, hls_container, mp4_codecs, segment_container, ts_codecs,
    CodecInfo,
};
pub use frame::{frames_degenerate, probe_verdict, FrameStat, ProbeVerdict};
pub use hls::{HlsEncryption, HlsPlaylist, RenditionInfo, VariantInfo};
pub use http::{ProbeHttpClient, ProbeHttpConfig, ProbeHttpError, ProbeResponse};
pub use inspect::{mp4_major_brand, DashInspection, Inspection, MediaInspector, Mp4Inspection};
pub use protection::{hls_is_drm, keyformat_is_drm, mpd_is_drm};
