//! Policy decision contract (MED-08, Mirror semantics migrated by MED-19):
//! decision order, stable rejections, external-client handoff fallback and
//! the cross-platform golden (PL-007..PL-015).

use crayon_cast_policy::{decide, HandoffAvailability, PolicyContext};
use crayon_domain::{CoreError, ReceiverCapabilities, TabId};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyDecision, CastPolicyInput, ExternalClientHandoff, HandoffConfirmation,
    HandoffReason, HeadersClass, MediaCandidate, PageContext, PlaybackState, ProtocolKind,
    VideoCodecKind,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};
use crayon_media_probe::Protection;

fn full_receiver() -> ReceiverCapabilities {
    ReceiverCapabilities::new(true, true, true, true, true, true, 2160)
}

fn input(headers: HeadersClass, ad: AdContinuity, position: f64) -> CastPolicyInput {
    CastPolicyInput::new(
        PageContext::new(
            TabId::new("tab-01").unwrap(),
            "https://example.com/watch".to_string(),
        ),
        PlaybackState::new(position, Some(3600.0), false),
        MediaCandidate::new(
            "https://cdn.example.com/master.m3u8".to_string(),
            ProtocolKind::Hls,
            false,
            headers,
            Some(VideoCodecKind::H264),
            None,
            ad,
        ),
        full_receiver(),
    )
}

fn verified() -> PlaybackObservation {
    PlaybackObservation::new(
        ObservationOrigin::BrowserVerified,
        UserActivation::BrowserVerified,
        PlaybackProgress::Advanced,
    )
}

fn ctx(protection: Protection, handoff: HandoffAvailability) -> PolicyContext {
    PolicyContext {
        observation: verified(),
        protection,
        external_client_handoff: handoff,
    }
}

fn available(protection: Protection) -> PolicyContext {
    ctx(protection, HandoffAvailability::Available)
}

fn direct_input() -> CastPolicyInput {
    input(HeadersClass::None, AdContinuity::Preserved, 120.0)
}

fn handoff(reason: HandoffReason) -> CastPolicyDecision {
    CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(reason))
}

#[test]
fn happy_path_direct_and_relay() {
    // 无特殊请求头 → Direct
    assert_eq!(
        decide(&direct_input(), &available(Protection::Clear)),
        CastPolicyDecision::Direct
    );
    // 需要 Referer/UA → Relay（请求头由 session relay 代持）
    assert_eq!(
        decide(
            &input(HeadersClass::RefererOnly, AdContinuity::Preserved, 120.0),
            &available(Protection::Clear),
        ),
        CastPolicyDecision::Relay
    );
    assert_eq!(
        decide(
            &input(HeadersClass::RefererAndUa, AdContinuity::Preserved, 120.0),
            &available(Protection::Clear),
        ),
        CastPolicyDecision::Relay
    );
}

#[test]
fn pl_010_gate_failure_rejects_even_when_all_else_is_fine() {
    for (origin, activation, progress, expected) in [
        (
            ObservationOrigin::PageReported,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
            CoreError::UntrustedObservation,
        ),
        (
            ObservationOrigin::BrowserVerified,
            UserActivation::Missing,
            PlaybackProgress::Advanced,
            CoreError::MissingUserActivation,
        ),
        (
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::NotAdvanced,
            CoreError::PlaybackNotAdvanced,
        ),
    ] {
        let context = PolicyContext {
            observation: PlaybackObservation::new(origin, activation, progress),
            protection: Protection::Clear,
            external_client_handoff: HandoffAvailability::Available,
        };
        assert_eq!(
            decide(&direct_input(), &context),
            CastPolicyDecision::Reject { reason: expected }
        );
    }
}

#[test]
fn drm_rejects_everywhere() {
    for availability in [
        HandoffAvailability::Available,
        HandoffAvailability::Unavailable,
    ] {
        assert_eq!(
            decide(
                &direct_input(),
                &ctx(Protection::DrmProtected, availability)
            ),
            CastPolicyDecision::Reject {
                reason: CoreError::DrmProtected
            }
        );
    }
}

#[test]
fn key_required_no_direct_url_and_unknown_fall_back_to_handoff() {
    for (protection, reason) in [
        (Protection::KeyRequired, HandoffReason::KeyRequired),
        (Protection::NoDirectUrl, HandoffReason::NoDirectUrl),
        (Protection::Unknown, HandoffReason::ProbeInconclusive),
    ] {
        assert_eq!(
            decide(&direct_input(), &available(protection)),
            handoff(reason),
            "{protection:?}"
        );
    }
}

#[test]
fn pl_008_credential_bound_media_never_leaves_the_browser() {
    assert_eq!(
        decide(
            &input(
                HeadersClass::CredentialBound,
                AdContinuity::Preserved,
                120.0,
            ),
            &available(Protection::Clear),
        ),
        handoff(HandoffReason::CredentialBound)
    );
}

#[test]
fn pl_007_receiver_incompatible_falls_back_or_stable_reject() {
    // 协议不支持
    let base = direct_input();
    let no_hls = CastPolicyInput::new(
        base.page().clone(),
        base.playback(),
        base.candidate().clone(),
        ReceiverCapabilities::new(true, false, true, true, true, true, 2160),
    );
    assert_eq!(
        decide(&no_hls, &available(Protection::Clear)),
        handoff(HandoffReason::ReceiverIncompatible)
    );
    // 编码不支持
    let hevc_input = CastPolicyInput::new(
        direct_input().page().clone(),
        direct_input().playback(),
        MediaCandidate::new(
            "https://cdn.example.com/v.m3u8".to_string(),
            ProtocolKind::Hls,
            false,
            HeadersClass::None,
            Some(VideoCodecKind::Hevc),
            None,
            AdContinuity::Preserved,
        ),
        ReceiverCapabilities::new(true, true, true, true, false, false, 2160),
    );
    assert_eq!(
        decide(&hevc_input, &available(Protection::Clear)),
        handoff(HandoffReason::ReceiverIncompatible)
    );
    // 平台无外部交接能力 → 稳定拒绝（PL-011）
    assert_eq!(
        decide(
            &no_hls,
            &ctx(Protection::Clear, HandoffAvailability::Unavailable)
        ),
        CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable
        }
    );
}

#[test]
fn pl_009_unknown_ad_continuity_from_start_hands_off() {
    let from_start = input(HeadersClass::None, AdContinuity::Unknown, 0.0);
    assert_eq!(
        decide(&from_start, &available(Protection::Clear)),
        handoff(HandoffReason::AdContinuityUnknown)
    );
    // 续播位置不受影响
    let resumed = input(HeadersClass::None, AdContinuity::Unknown, 120.0);
    assert_eq!(
        decide(&resumed, &available(Protection::Clear)),
        CastPolicyDecision::Direct
    );
    // 广告连续性已确认保留：从头也可直投
    let preserved = input(HeadersClass::None, AdContinuity::Preserved, 0.0);
    assert_eq!(
        decide(&preserved, &available(Protection::Clear)),
        CastPolicyDecision::Direct
    );
}

#[test]
fn pl_011_missing_handoff_capability_is_a_stable_rejection() {
    // 任何兜底分支在无交接能力时都以 capabilities_unavailable 稳定拒绝。
    let credential = input(
        HeadersClass::CredentialBound,
        AdContinuity::Preserved,
        120.0,
    );
    assert_eq!(
        decide(
            &credential,
            &ctx(Protection::Clear, HandoffAvailability::Unavailable)
        ),
        CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable
        }
    );
    assert_eq!(
        decide(
            &direct_input(),
            &ctx(Protection::KeyRequired, HandoffAvailability::Unavailable)
        ),
        CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable
        }
    );
}

#[test]
fn pl_015_handoff_is_pure_advice_requiring_confirmation() {
    let decision = decide(
        &input(
            HeadersClass::CredentialBound,
            AdContinuity::Preserved,
            120.0,
        ),
        &available(Protection::Clear),
    );
    let CastPolicyDecision::ExternalClientHandoff(advice) = decision else {
        panic!("应为外部交接建议: {decision:?}")
    };
    assert_eq!(advice.confirmation(), HandoffConfirmation::Required);
    // DTO 不持有媒体 URL、Relay token、receiver session 或传输面。
    let wire = serde_json::to_value(advice).unwrap();
    let mut keys: Vec<&str> = wire
        .as_object()
        .unwrap()
        .keys()
        .map(String::as_str)
        .collect();
    keys.sort_unstable();
    assert_eq!(keys, ["confirmation", "reason"]);
    // 决策是纯函数：重复调用结果一致，不产生任何会话状态。
    assert_eq!(
        decide(
            &input(
                HeadersClass::CredentialBound,
                AdContinuity::Preserved,
                120.0,
            ),
            &available(Protection::Clear),
        ),
        decision
    );
}

#[test]
fn pl_013_safety_conclusions_are_capability_independent() {
    // 同一输入在交接能力有无两种声明下：安全/隐私结论完全一致，
    // 只有兜底形态不同（建议 vs 稳定拒绝）。
    for availability in [
        HandoffAvailability::Available,
        HandoffAvailability::Unavailable,
    ] {
        // DRM 拒绝一致
        assert_eq!(
            decide(
                &direct_input(),
                &ctx(Protection::DrmProtected, availability)
            ),
            CastPolicyDecision::Reject {
                reason: CoreError::DrmProtected
            }
        );
        // 门禁拒绝一致
        let context = PolicyContext {
            observation: PlaybackObservation::new(
                ObservationOrigin::PageReported,
                UserActivation::Missing,
                PlaybackProgress::NotAdvanced,
            ),
            protection: Protection::Clear,
            external_client_handoff: availability,
        };
        assert!(matches!(
            decide(&direct_input(), &context),
            CastPolicyDecision::Reject { .. }
        ));
    }
    // 模式差异：credential-bound 在有交接能力时是建议，无能力时稳定拒绝。
    let credential = input(
        HeadersClass::CredentialBound,
        AdContinuity::Preserved,
        120.0,
    );
    assert_eq!(
        decide(&credential, &available(Protection::Clear)),
        handoff(HandoffReason::CredentialBound)
    );
    assert_eq!(
        decide(
            &credential,
            &ctx(Protection::Clear, HandoffAvailability::Unavailable)
        ),
        CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable
        }
    );
}
