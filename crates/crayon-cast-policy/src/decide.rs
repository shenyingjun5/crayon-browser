//! The single Mirror/Direct/Relay/Reject decision function (MED-08,
//! design §9.2). Pure: same input always yields the same decision on every
//! platform (PL-013) — platform capabilities only change which modes are
//! available, never the safety conclusions.
//!
//! Decision order (design §9.2):
//! 1. playback gate (untrusted/no activation/not advanced) → Reject;
//! 2. DRM/EME → Reject; key-required → no direct/relay; blob/stream → no
//!    direct URL (BR-012); inconclusive probe → safe fallback only;
//! 3. credential-bound media never leaves the browser (PL-008);
//! 4. receiver-incompatible protocol/codec → fallback or stable reject
//!    (PL-007);
//! 5. ad continuity unknown + from-the-start playback → Mirror (PL-009);
//! 6. otherwise: credential-free candidates go Direct, referer/UA-bound go
//!    through the session Relay.

use crayon_domain::{CoreError, PlatformCapabilities};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyDecision, CastPolicyInput, HeadersClass, ProtocolKind, VideoCodecKind,
};
use crayon_media_observer::PlaybackObservation;
use crayon_media_probe::Protection;

/// Position below this (and not live) counts as from-the-start playback
/// (PL-009, design §9.2 step 5).
const FROM_START_THRESHOLD_SECONDS: f64 = 1.0;

/// Capability-driven degradation attached to a Mirror decision (PL-011).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Degradation {
    /// Platform cannot capture system audio; mirror carries video only.
    NoSystemAudio,
}

/// Policy outcome: the wire decision plus optional degradation note.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyOutcome {
    pub decision: CastPolicyDecision,
    pub degradation: Option<Degradation>,
}

impl PolicyOutcome {
    const fn plain(decision: CastPolicyDecision) -> Self {
        Self {
            decision,
            degradation: None,
        }
    }
}

/// Decision context supplied by the runtime: gate facts, protection
/// assessment and platform capabilities.
#[derive(Clone, Copy, Debug)]
pub struct PolicyContext {
    pub observation: PlaybackObservation,
    pub protection: Protection,
    pub platform: PlatformCapabilities,
}

/// The one and only cast policy function.
#[must_use]
pub fn decide(input: &CastPolicyInput, context: &PolicyContext) -> PolicyOutcome {
    // 1. 用户主动播放门禁（PL-010）：页面自报不可信，必须由 Browser 验证。
    if let Some(error) = gate_error(context.observation) {
        return PolicyOutcome::plain(CastPolicyDecision::Reject { reason: error });
    }

    // 2. 保护与来源事实。
    match context.protection {
        Protection::DrmProtected => {
            return PolicyOutcome::plain(CastPolicyDecision::Reject {
                reason: CoreError::DrmProtected,
            });
        }
        // 需要密钥的加密流：当前合规姿态只允许标签页兜底。
        Protection::KeyRequired => return mirror_or_reject(context.platform),
        // blob:/MediaStream 无底层 URL（BR-012）。
        Protection::NoDirectUrl => return mirror_or_reject(context.platform),
        // 预检不确定：不直投，安全兜底（PL-014）。
        Protection::Unknown => return mirror_or_reject(context.platform),
        Protection::Clear => {}
    }

    let candidate = input.candidate();

    // 3. 需要 Cookie/Authorization 的媒体不离开浏览器（PL-008）。
    if candidate.headers_class() == HeadersClass::CredentialBound {
        return mirror_or_reject(context.platform);
    }

    // 4. 接收端兼容性（PL-007）。
    if !receiver_supports_protocol(input) || !receiver_supports_codec(input) {
        return mirror_or_reject(context.platform);
    }

    // 5. 广告连续性未知且从头播放（PL-009）。
    if candidate.ad_continuity() == AdContinuity::Unknown && is_from_start(input) {
        return mirror_or_reject(context.platform);
    }

    // 6. 直投/relay 分流：无需特殊请求头 → Direct；需要 Referer/UA → Relay
    //    （由 session relay 代持请求头，不进入接收端命令）。
    match candidate.headers_class() {
        HeadersClass::None => PolicyOutcome::plain(CastPolicyDecision::Direct),
        HeadersClass::RefererOnly | HeadersClass::RefererAndUa => PolicyOutcome::plain(
            // Relay 决策在 v1 wire 上归入 Direct 家族之外的独立模式，
            // 见 ipc-schema 的 CastPolicyDecision::Relay。
            CastPolicyDecision::Relay,
        ),
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

/// Mirror fallback when the platform can capture the tab; otherwise a stable
/// capability rejection (PL-011).
fn mirror_or_reject(platform: PlatformCapabilities) -> PolicyOutcome {
    if platform.tab_video() {
        PolicyOutcome {
            decision: CastPolicyDecision::Mirror,
            degradation: if platform.system_audio() {
                None
            } else {
                Some(Degradation::NoSystemAudio)
            },
        }
    } else {
        PolicyOutcome::plain(CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable,
        })
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
