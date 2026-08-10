//! The single Direct/Relay/ExternalClientHandoff/Reject decision function
//! (MED-08, Mirror semantics migrated by MED-19, design §9.2). Pure: same
//! input always yields the same decision on every platform (PL-013) — the
//! declared handoff capability only changes whether the fallback is a
//! suggestion or a stable rejection, never the safety conclusions.
//!
//! Decision order (design §9.2):
//! 1. playback gate (untrusted/no activation/not advanced) → Reject;
//! 2. DRM/EME → Reject; key-required → no direct/relay; blob/stream → no
//!    direct URL (BR-012); inconclusive probe → safe fallback only;
//! 3. credential-bound media never leaves the browser (PL-008);
//! 4. receiver-incompatible protocol/codec → handoff or stable reject
//!    (PL-007);
//! 5. ad continuity unknown + from-the-start playback → handoff (PL-009);
//! 6. otherwise: credential-free candidates go Direct, referer/UA-bound go
//!    through the session Relay.
//!
//! The fallback is `ExternalClientHandoff` (MED-19): a suggestion only. It
//! creates no capturer, encoder, WebRTC transport, receiver handle or relay
//! session, and requires explicit user confirmation (PL-015).

use crayon_domain::CoreError;
use crayon_ipc_schema::{
    AdContinuity, CastPolicyDecision, CastPolicyInput, ExternalClientHandoff, HandoffReason,
    HeadersClass, ProtocolKind, VideoCodecKind,
};
use crayon_media_observer::PlaybackObservation;
use crayon_media_probe::Protection;

/// Position below this (and not live) counts as from-the-start playback
/// (PL-009, design §9.2 step 5).
const FROM_START_THRESHOLD_SECONDS: f64 = 1.0;

/// Whether the platform offers the external Crayon cast client handoff
/// (download + launch surface declared by the platform adapter).
/// Capability-driven and fail closed (PL-011).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffAvailability {
    Available,
    Unavailable,
}

/// Decision context supplied by the runtime: gate facts, protection
/// assessment and the declared external-client handoff capability.
#[derive(Clone, Copy, Debug)]
pub struct PolicyContext {
    pub observation: PlaybackObservation,
    pub protection: Protection,
    pub external_client_handoff: HandoffAvailability,
}

/// The one and only cast policy function.
#[must_use]
pub fn decide(input: &CastPolicyInput, context: &PolicyContext) -> CastPolicyDecision {
    // 1. 用户主动播放门禁（PL-010）：页面自报不可信，必须由 Browser 验证。
    if let Some(error) = gate_error(context.observation) {
        return CastPolicyDecision::Reject { reason: error };
    }

    // 2. 保护与来源事实。
    match context.protection {
        Protection::DrmProtected => {
            return CastPolicyDecision::Reject {
                reason: CoreError::DrmProtected,
            };
        }
        // 需要密钥的加密流：当前合规姿态只允许外部客户端交接兜底。
        Protection::KeyRequired => {
            return handoff_or_reject(HandoffReason::KeyRequired, context);
        }
        // blob:/MediaStream 无底层 URL（BR-012）。
        Protection::NoDirectUrl => {
            return handoff_or_reject(HandoffReason::NoDirectUrl, context);
        }
        // 预检不确定：不直投，安全兜底（PL-014）。
        Protection::Unknown => {
            return handoff_or_reject(HandoffReason::ProbeInconclusive, context);
        }
        Protection::Clear => {}
    }

    let candidate = input.candidate();

    // 3. 需要 Cookie/Authorization 的媒体不离开浏览器（PL-008）。
    if candidate.headers_class() == HeadersClass::CredentialBound {
        return handoff_or_reject(HandoffReason::CredentialBound, context);
    }

    // 4. 接收端兼容性（PL-007）。
    if !receiver_supports_protocol(input) || !receiver_supports_codec(input) {
        return handoff_or_reject(HandoffReason::ReceiverIncompatible, context);
    }

    // 5. 广告连续性未知且从头播放（PL-009）。
    if candidate.ad_continuity() == AdContinuity::Unknown && is_from_start(input) {
        return handoff_or_reject(HandoffReason::AdContinuityUnknown, context);
    }

    // 6. 直投/relay 分流：无需特殊请求头 → Direct；需要 Referer/UA → Relay
    //    （由 session relay 代持请求头，不进入接收端命令）。
    match candidate.headers_class() {
        HeadersClass::None => CastPolicyDecision::Direct,
        HeadersClass::RefererOnly | HeadersClass::RefererAndUa => CastPolicyDecision::Relay,
        HeadersClass::CredentialBound => unreachable!("credential-bound handled above"),
    }
}

/// Maps playback-gate facts to a stable rejection, or `None` when the gate
/// passes (mirrors `assess_planning`'s fail-closed rules).
fn gate_error(observation: PlaybackObservation) -> Option<CoreError> {
    use crayon_media_observer::{ObservationOrigin, PlaybackProgress, UserActivation};
    if !matches!(observation.origin(), ObservationOrigin::BrowserVerified) {
        return Some(CoreError::UntrustedObservation);
    }
    if !matches!(
        observation.user_activation(),
        UserActivation::BrowserVerified
    ) {
        return Some(CoreError::MissingUserActivation);
    }
    if !matches!(observation.playback_progress(), PlaybackProgress::Advanced) {
        return Some(CoreError::PlaybackNotAdvanced);
    }
    None
}

/// External-client handoff suggestion when the platform declares the
/// capability; otherwise a stable capability rejection (PL-011). The
/// suggestion is pure data — it starts nothing and holds no session
/// material (PL-015).
fn handoff_or_reject(reason: HandoffReason, context: &PolicyContext) -> CastPolicyDecision {
    match context.external_client_handoff {
        HandoffAvailability::Available => {
            CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(reason))
        }
        HandoffAvailability::Unavailable => CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable,
        },
    }
}

fn receiver_supports_protocol(input: &CastPolicyInput) -> bool {
    let receiver = input.receiver();
    match input.candidate().protocol() {
        ProtocolKind::Hls => receiver.hls(),
        ProtocolKind::Dash => receiver.dash(),
        ProtocolKind::Mp4 => receiver.mp4(),
    }
}

/// Unknown codec is not penalized (HLS without CODECS is common); a known
/// codec the receiver lacks makes the candidate incompatible.
fn receiver_supports_codec(input: &CastPolicyInput) -> bool {
    let receiver = input.receiver();
    match input.candidate().video_codec() {
        None => true,
        Some(VideoCodecKind::H264) => receiver.h264(),
        Some(VideoCodecKind::Hevc) => receiver.hevc(),
        Some(VideoCodecKind::Av1) => receiver.av1(),
        // 接收端能力模型未覆盖的编码族：保守视为不支持，走兜底。
        Some(VideoCodecKind::Vp9 | VideoCodecKind::Vp8 | VideoCodecKind::Other) => false,
    }
}

fn is_from_start(input: &CastPolicyInput) -> bool {
    let playback = input.playback();
    !playback.is_live() && playback.position_seconds() < FROM_START_THRESHOLD_SECONDS
}
