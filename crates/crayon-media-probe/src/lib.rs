//! Bounded, platform-neutral media inspection primitives.

mod codec;
mod frame;
pub mod http;
mod protection;

pub use codec::{
    codec_name, codecs_from_m3u8, hls_container, mp4_codecs, segment_container, ts_codecs,
    CodecInfo,
};
pub use frame::{frames_degenerate, probe_verdict, FrameStat, ProbeVerdict};
pub use http::{ProbeHttpClient, ProbeHttpConfig, ProbeHttpError, ProbeResponse};
pub use protection::{hls_is_drm, keyformat_is_drm, mpd_is_drm};
