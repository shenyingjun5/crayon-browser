//! Crate-internal cast usecase tests (SDK-12): phase gating, bounded event
//! queue policy, fencing and idempotent stop — driven by the SDK-04 fake
//! facade and recording backend/revocation doubles. Full-chain Fake E2E V2
//! lives in `tests/cast_usecase.rs`.

use super::{
    CastPhase, CastStartOutcome, CastUsecase, EventQueue, RelayRevocation,
    MAX_PENDING_SESSION_EVENTS,
};
use crate::delivery::{DeliveryRequest, SessionBackend};
use crayon_cast_adapter::{
    AssessmentStatus, CastError, CastFacade, CastMediaKind, CastPlaybackState, CastSessionPhase,
    CastSessionRef, CastSessionSnapshot, CastTerminalReason, DeviceState, DiscoveredDevice,
    ReceiverCapabilityCache,
};
use crayon_cast_policy::HandoffAvailability;
use crayon_domain::{DeviceId, ReceiverCapabilities, SessionGeneration, SessionId, TabId};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyInput, HeadersClass, MediaCandidate, PageContext, PlaybackState,
    ProtocolKind,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};
use crayon_media_probe::Protection;
use crayon_relay::session::RevokeReason;
use std::sync::{Arc, Mutex};
use test_support::cast_facade::FakeCastFacade;

/// Recording session backend: every `open` succeeds with a fixed loopback
/// relay URL. Clone shares the record.
#[derive(Clone, Default)]
struct RecordingBackend {
    opened: Arc<Mutex<Vec<String>>>,
}

impl RecordingBackend {
    fn opened(&self) -> Vec<String> {
        self.opened.lock().unwrap().clone()
    }
}

impl SessionBackend for RecordingBackend {
    fn open(
        &mut self,
        _receiver: &DeviceId,
        _receiver_ip: Option<std::net::IpAddr>,
        candidate_url: &str,
        _protocol: ProtocolKind,
        _headers_class: HeadersClass,
        _page_url: &str,
    ) -> Result<String, crayon_domain::CoreError> {
        self.opened.lock().unwrap().push(candidate_url.to_string());
        Ok("http://127.0.0.1:20001/s/0123456789abcdef0123456789abcdef/master.m3u8".to_string())
    }
}

/// Recording revocation double.
#[derive(Default)]
struct RecordingRevocation {
    calls: Mutex<Vec<(RevokeReason, Option<DeviceId>)>>,
}

impl RecordingRevocation {
    fn calls(&self) -> Vec<(RevokeReason, Option<DeviceId>)> {
        self.calls.lock().unwrap().clone()
    }
}

impl RelayRevocation for RecordingRevocation {
    fn revoke(&self, reason: RevokeReason, receiver: Option<&DeviceId>) -> usize {
        self.calls.lock().unwrap().push((reason, receiver.cloned()));
        0
    }
}

struct Harness {
    facade: Arc<FakeCastFacade>,
    backend: RecordingBackend,
    revocation: Arc<RecordingRevocation>,
    usecase: CastUsecase,
    device: DeviceId,
}

fn device(id: &str) -> DeviceId {
    DeviceId::new(id).unwrap()
}

fn harness() -> Harness {
    let facade = Arc::new(FakeCastFacade::new());
    let device = device("dev-01");
    facade.upsert_device(DiscoveredDevice::new(
        device.clone(),
        "Living Room".to_string(),
        DeviceState::Ready,
        false,
    ));
    facade.set_assessment(&device, CastMediaKind::Video, AssessmentStatus::Supported);
    facade.set_assessment(&device, CastMediaKind::Hls, AssessmentStatus::Supported);
    let facade_trait: Arc<dyn CastFacade> = facade.clone();
    let cache = Arc::new(ReceiverCapabilityCache::new(
        facade_trait,
        Default::default(),
    ));
    let backend = RecordingBackend::default();
    let revocation = Arc::new(RecordingRevocation::default());
    let usecase = CastUsecase::new(
        facade.clone(),
        cache,
        Box::new(backend.clone()),
        revocation.clone(),
    );
    Harness {
        facade,
        backend,
        revocation,
        usecase,
        device,
    }
}

fn request(device: &DeviceId, headers: HeadersClass) -> DeliveryRequest {
    DeliveryRequest {
        input: CastPolicyInput::new(
            PageContext::new(
                TabId::new("tab-01").unwrap(),
                "https://example.com/watch".to_string(),
            ),
            PlaybackState::new(120.0, Some(3600.0), false),
            MediaCandidate::new(
                "https://cdn.example.com/media/movie.mp4?sign=abc".to_string(),
                ProtocolKind::Mp4,
                false,
                headers,
                None,
                None,
                AdContinuity::Preserved,
            ),
            ReceiverCapabilities::new(true, true, true, true, true, true, 2160),
        ),
        observation: PlaybackObservation::new(
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
        ),
        protection: Protection::Clear,
        external_client_handoff: HandoffAvailability::Available,
        receiver: device.clone(),
        receiver_ip: None,
    }
}

fn snapshot(
    session: &CastSessionRef,
    phase: CastSessionPhase,
    revision: u64,
    reason: Option<CastTerminalReason>,
) -> CastSessionSnapshot {
    CastSessionSnapshot::new(
        session.clone(),
        phase,
        CastPlaybackState::Playing,
        revision,
        reason,
    )
}

#[test]
fn phase_gating_follows_the_core_state_machine() {
    let h = harness();
    assert_eq!(h.usecase.phase(), CastPhase::Idle);
    // Not eligible yet: start and picker are fail-closed.
    assert_eq!(
        h.usecase
            .start_cast(&request(&h.device, HeadersClass::None)),
        CastStartOutcome::Failed(CastError::InvalidState)
    );
    assert_eq!(
        h.usecase.open_receiver_picker(),
        Err(CastError::InvalidState)
    );
    h.usecase.on_page_browsing();
    assert_eq!(h.usecase.phase(), CastPhase::Browsing);
    assert_eq!(
        h.usecase
            .start_cast(&request(&h.device, HeadersClass::None)),
        CastStartOutcome::Failed(CastError::InvalidState),
        "browsing without verified playback may not cast"
    );
    h.usecase.on_playback_eligible();
    assert_eq!(h.usecase.phase(), CastPhase::PlaybackEligible);
    h.usecase.open_receiver_picker().unwrap();
    assert_eq!(h.usecase.phase(), CastPhase::SelectingReceiver);
}

#[test]
fn stop_without_session_is_idempotent_noop() {
    let h = harness();
    assert_eq!(h.usecase.stop_cast(), Ok(()));
    assert_eq!(h.usecase.stop_cast(), Ok(()));
    assert!(h.facade.calls().is_empty());
}

#[test]
fn controls_require_an_active_session() {
    let h = harness();
    h.usecase.on_playback_eligible();
    assert_eq!(h.usecase.pause(), Err(CastError::NoActiveSession));
    assert_eq!(h.usecase.seek(30), Err(CastError::NoActiveSession));
}

#[test]
fn stale_snapshots_are_fenced_and_never_converge_resources() {
    let h = harness();
    h.usecase.on_playback_eligible();
    let CastStartOutcome::Casting(session) = h
        .usecase
        .start_cast(&request(&h.device, HeadersClass::None))
    else {
        panic!("expected casting")
    };
    // Drain the Starting/Active events so `last_applied` is current.
    let stats = h.usecase.drain_session_events();
    assert_eq!(stats.applied, 2);
    assert_eq!(h.usecase.phase(), CastPhase::Casting);
    // A same-generation older-revision terminal is a stale event.
    h.facade.push_session_snapshot(snapshot(
        &session,
        CastSessionPhase::Terminated,
        0,
        Some(CastTerminalReason::StoppedByReceiver),
    ));
    let stats = h.usecase.drain_session_events();
    assert_eq!(stats.dropped_stale, 1);
    assert_eq!(stats.terminal_converged, 0);
    assert_eq!(h.usecase.phase(), CastPhase::Casting);
    assert_eq!(h.usecase.active_session(), Some(session));
    assert!(
        h.revocation.calls().is_empty(),
        "stale events never revoke relay sessions"
    );
}

#[test]
fn foreign_generation_event_is_applied_but_does_not_cleanup() {
    let h = harness();
    h.usecase.on_playback_eligible();
    let CastStartOutcome::Casting(session) = h
        .usecase
        .start_cast(&request(&h.device, HeadersClass::None))
    else {
        panic!("expected casting")
    };
    h.usecase.drain_session_events();
    // A newer-generation snapshot for an unknown session identity: fencing
    // accepts it (it supersedes), but it is not ours — no convergence.
    let foreign = CastSessionRef::new(
        SessionId::new("fake-session-foreign").unwrap(),
        SessionGeneration::INITIAL
            .advance()
            .unwrap()
            .advance()
            .unwrap(),
    );
    h.facade
        .push_session_snapshot(snapshot(&foreign, CastSessionPhase::Active, 0, None));
    let stats = h.usecase.drain_session_events();
    assert_eq!(stats.applied, 1);
    assert_eq!(stats.terminal_converged, 0);
    assert_eq!(h.usecase.active_session(), Some(session));
    assert!(h.revocation.calls().is_empty());
}

#[test]
fn bounded_queue_coalesces_non_terminal_but_never_terminal() {
    let mut queue = EventQueue::default();
    let session = CastSessionRef::new(
        SessionId::new("fake-session-q").unwrap(),
        SessionGeneration::INITIAL,
    );
    for revision in 0..MAX_PENDING_SESSION_EVENTS as u64 + 10 {
        queue.push(snapshot(&session, CastSessionPhase::Active, revision, None));
    }
    assert_eq!(queue.events.len(), MAX_PENDING_SESSION_EVENTS);
    assert_eq!(queue.coalesced_non_terminal, 10);
    assert_eq!(queue.dropped_terminal, 0);
    // Terminal snapshots are never coalesced: a full queue of terminals
    // forces the bounded drop counter instead.
    let mut queue = EventQueue::default();
    for index in 0..MAX_PENDING_SESSION_EVENTS as u64 + 5 {
        let terminal = CastSessionRef::new(
            SessionId::new(&format!("fake-session-t{index}")).unwrap(),
            SessionGeneration::INITIAL,
        );
        queue.push(snapshot(
            &terminal,
            CastSessionPhase::Terminated,
            0,
            Some(CastTerminalReason::EndedNormally),
        ));
    }
    assert_eq!(queue.events.len(), MAX_PENDING_SESSION_EVENTS);
    assert_eq!(queue.coalesced_non_terminal, 0);
    assert_eq!(queue.dropped_terminal, 5);
}

#[test]
fn failed_stop_reverts_stopping_phase() {
    let h = harness();
    h.usecase.on_playback_eligible();
    let outcome = h
        .usecase
        .start_cast(&request(&h.device, HeadersClass::None));
    assert!(matches!(outcome, CastStartOutcome::Casting(_)));
    h.usecase.drain_session_events();
    h.facade.fail_next_control(CastError::ReceiverProtocol);
    assert_eq!(h.usecase.stop_cast(), Err(CastError::ReceiverProtocol));
    assert_eq!(
        h.usecase.phase(),
        CastPhase::Casting,
        "a failed stop returns to Casting so the user can retry"
    );
    assert_eq!(h.usecase.stop_cast(), Ok(()));
    assert_eq!(h.usecase.phase(), CastPhase::Stopping);
}

#[test]
fn backend_is_not_touched_for_direct_or_rejected_plans() {
    let h = harness();
    h.usecase.on_playback_eligible();
    // Direct plan: no session backend call.
    let outcome = h
        .usecase
        .start_cast(&request(&h.device, HeadersClass::None));
    assert!(matches!(outcome, CastStartOutcome::Casting(_)));
    assert!(h.backend.opened().is_empty());
    // Re-plan with DRM: rejected before any backend/facade delivery work.
    let mut drm = request(&h.device, HeadersClass::RefererOnly);
    drm.protection = Protection::DrmProtected;
    assert_eq!(
        h.usecase.start_cast(&drm),
        CastStartOutcome::Rejected(crayon_domain::CoreError::DrmProtected)
    );
    assert!(h.backend.opened().is_empty());
}
