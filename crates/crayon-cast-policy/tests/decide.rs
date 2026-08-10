//! Policy decision contract (MED-08): decision order, stable rejections and
//! the cross-platform golden (PL-007..PL-014).

use crayon_cast_policy::{decide, Degradation, PolicyContext};
use crayon_domain::{
    BrowserEngineKind, CoreError, LocalDiscoveryKind, PlatformCapabilities, ProtectedSurfaceKind,
    ReceiverCapabilities, SecureStoreKind, TabId,
};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyDecision, CastPolicyInput, HeadersClass, MediaCandidate, PageContext,
    PlaybackState, ProtocolKind, VideoCodecKind,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};
use crayon_media_probe::Protection;
use test_support::platform::PlatformFake;

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

fn ctx(protection: Protection, platform: PlatformCapabilities) -> PolicyContext {
    PolicyContext {
        observation: verified(),
        protection,
        platform,
    }
}

fn cef() -> PlatformCapabilities {
    PlatformFake::cef_desktop()
}

fn direct_input() -> CastPolicyInput {
    input(HeadersClass::None, AdContinuity::Preserved, 120.0)
}

#[test]
fn happy_path_direct_and_relay() {
    // 无特殊请求头 → Direct
    let outcome = decide(&direct_input(), &ctx(Protection::Clear, cef()));
    assert_eq!(outcome.decision, CastPolicyDecision::Direct);
    assert_eq!(outcome.degradation, None);
    // 需要 Referer/UA → Relay（请求头由 session relay 代持）
    let outcome = decide(
        &input(HeadersClass::RefererOnly, AdContinuity::Preserved, 120.0),
        &ctx(Protection::Clear, cef()),
    );
    assert_eq!(outcome.decision, CastPolicyDecision::Relay);
    let outcome = decide(
        &input(HeadersClass::RefererAndUa, AdContinuity::Preserved, 120.0),
        &ctx(Protection::Clear, cef()),
    );
    assert_eq!(outcome.decision, CastPolicyDecision::Relay);
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
            platform: cef(),
        };
        assert_eq!(
            decide(&direct_input(), &context).decision,
            CastPolicyDecision::Reject { reason: expected }
        );
    }
}

#[test]
fn drm_rejects_everywhere() {
    for platform in [cef(), PlatformFake::arkweb_reduced()] {
        assert_eq!(
            decide(&direct_input(), &ctx(Protection::DrmProtected, platform)).decision,
            CastPolicyDecision::Reject {
                reason: CoreError::DrmProtected
            }
        );
    }
}

#[test]
fn key_required_and_unknown_fall_back_to_mirror() {
    for protection in [
        Protection::KeyRequired,
        Protection::Unknown,
        Protection::NoDirectUrl,
    ] {
        let outcome = decide(&direct_input(), &ctx(protection, cef()));
        assert_eq!(
            outcome.decision,
            CastPolicyDecision::Mirror,
            "{protection:?}"
        );
    }
}

#[test]
fn pl_008_credential_bound_media_never_leaves_the_browser() {
    let outcome = decide(
        &input(
            HeadersClass::CredentialBound,
            AdContinuity::Preserved,
            120.0,
        ),
        &ctx(Protection::Clear, cef()),
    );
    assert_eq!(outcome.decision, CastPolicyDecision::Mirror);
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
        decide(&no_hls, &ctx(Protection::Clear, cef())).decision,
        CastPolicyDecision::Mirror
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
        decide(&hevc_input, &ctx(Protection::Clear, cef())).decision,
        CastPolicyDecision::Mirror
    );
    // 平台也不能 mirror（无 tab 采集）→ 稳定拒绝
    assert_eq!(
        decide(
            &no_hls,
            &ctx(Protection::Clear, PlatformFake::arkweb_reduced())
        )
        .decision,
        CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable
        }
    );
}

#[test]
fn pl_009_unknown_ad_continuity_from_start_mirrors() {
    let from_start = input(HeadersClass::None, AdContinuity::Unknown, 0.0);
    assert_eq!(
        decide(&from_start, &ctx(Protection::Clear, cef())).decision,
        CastPolicyDecision::Mirror
    );
    // 续播位置不受影响
    let resumed = input(HeadersClass::None, AdContinuity::Unknown, 120.0);
    assert_eq!(
        decide(&resumed, &ctx(Protection::Clear, cef())).decision,
        CastPolicyDecision::Direct
    );
    // 广告连续性已确认保留：从头也可直投
    let preserved = input(HeadersClass::None, AdContinuity::Preserved, 0.0);
    assert_eq!(
        decide(&preserved, &ctx(Protection::Clear, cef())).decision,
        CastPolicyDecision::Direct
    );
}

#[test]
fn pl_011_missing_system_audio_degrades_with_explicit_reason() {
    let no_audio = PlatformCapabilities::new(
        BrowserEngineKind::Cef,
        true,
        false,
        true,
        LocalDiscoveryKind::MdnsUdp,
        SecureStoreKind::OsNative,
        ProtectedSurfaceKind::Blocked,
    );
    let outcome = decide(
        &input(
            HeadersClass::CredentialBound,
            AdContinuity::Preserved,
            120.0,
        ),
        &ctx(Protection::Clear, no_audio),
    );
    assert_eq!(outcome.decision, CastPolicyDecision::Mirror);
    assert_eq!(outcome.degradation, Some(Degradation::NoSystemAudio));
}

#[test]
fn pl_013_safety_conclusions_are_platform_independent() {
    // 同一输入在桌面 CEF 与 ArkWeb 受限能力下：安全/隐私结论完全一致，
    // 只有可用模式不同。
    let arkweb = PlatformFake::arkweb_reduced();
    for platform in [cef(), arkweb] {
        // DRM 拒绝一致
        assert_eq!(
            decide(&direct_input(), &ctx(Protection::DrmProtected, platform)).decision,
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
            platform,
        };
        assert!(matches!(
            decide(&direct_input(), &context).decision,
            CastPolicyDecision::Reject { .. }
        ));
    }
    // 模式差异：credential-bound 在桌面 Mirror，在 ArkWeb（无 tab 采集）稳定拒绝。
    let credential = input(
        HeadersClass::CredentialBound,
        AdContinuity::Preserved,
        120.0,
    );
    assert_eq!(
        decide(&credential, &ctx(Protection::Clear, cef())).decision,
        CastPolicyDecision::Mirror
    );
    assert_eq!(
        decide(&credential, &ctx(Protection::Clear, arkweb)).decision,
        CastPolicyDecision::Reject {
            reason: CoreError::CapabilitiesUnavailable
        }
    );
}
