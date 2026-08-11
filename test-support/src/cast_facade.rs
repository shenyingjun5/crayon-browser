//! `FakeCastFacade`: a scripted, deterministic implementation of the product
//! `CastFacade` contract (SDK-03) for dev/test targets only (SDK-04).
//!
//! Discovery snapshot semantics (finalized in SDK-06) mirror the real
//! facade: `list_devices` exposes connectable receivers only — devices
//! orchestrated with a non-`Ready` state stay in the fake's registry (so
//! `connect` can tell an aged-out device apart from a never-seen one) but
//! never appear in the snapshot — and `stop_discovery` never clears the
//! snapshot. The fake approximates the SDK's product-visible gate (ready
//! state plus control URLs and a non-placeholder name) with the
//! `DeviceState::Ready` check, the only part expressible at the product DTO
//! level. The snapshot is sorted by friendly name then device id, matching
//! the real facade's deterministic total order.
//!
//! Connection semantics (finalized in SDK-07) also mirror the real facade:
//! `connect` is idempotent for the same device and switches when another
//! device is connected; a device absent from the snapshot — unknown or
//! aged-out — reports `DeviceNotFound`. The real facade reserves `RouteLost`
//! for a visible device whose validated route expired before connect; the
//! fake has no route-TTL concept, so that branch is orchestrated with
//! `fail_next_connect(CastError::RouteLost)`.
//!
//! Every facade behaviour is orchestrated from the test: device snapshots and
//! incremental changes (same-name/UDN-conflict/multi-interface receivers),
//! cast-code resolution branches (success / not found / expired / scripted
//! failure), connect/disconnect, capability assessments (including
//! point-in-time changes), delivery success/failure, playback-control
//! responses and full session-supervision sequences (route lost, natural end,
//! receiver stop, replacement, stale-generation injection). There is no
//! network, thread, clock or sleep anywhere: every state transition happens
//! synchronously inside the call that triggers it.
//!
//! Recording policy: only calls that pass facade-side validation and are
//! actually attempted against the fake receiver are recorded. Calls rejected
//! by fencing (`StaleSessionGeneration`) or fail-closed validation
//! (`InvalidState`) are not recorded, so CS-006 can assert a stale handle
//! never reaches the receiver.
//!
//! Concurrency rules (AGENTS §9): one `state` mutex guards all mutable facade
//! state and a separate `listeners` mutex guards subscriptions. Lock order is
//! always `state` -> `listeners`, and listener callbacks run only after both
//! locks are released, so a listener may re-enter the facade without
//! deadlock. Dropping a subscription unsubscribes idempotently.

use crayon_cast_adapter::{
    AssessmentStatus, CastCode, CastError, CastFacade, CastMediaKind, CastMediaRequest,
    CastPlaybackState, CastSessionListener, CastSessionPhase, CastSessionRef, CastSessionSnapshot,
    CastSessionSubscription, CastTerminalReason, DeliveryProtocol, DeviceState, DiscoveredDevice,
    PlaybackPosition, ReceiverAssessment, Volume,
};
use crayon_domain::{DeviceId, SessionGeneration, SessionId};
use std::collections::VecDeque;
use std::sync::{Arc, Mutex, Weak};

/// Upper bound for recorded calls and queued scripted errors (bounded-buffer
/// rule; no test approaches these limits).
const MAX_RECORDED_CALLS: usize = 512;
const MAX_SCRIPTED_ERRORS: usize = 64;

/// Default receiver volume applied to newly created sessions.
const DEFAULT_VOLUME: u8 = 50;

/// One facade interaction attempted against the fake receiver, in call order.
///
/// The media URL is recorded verbatim so delivery tests can assert the facade
/// forwarded it unchanged (CS-005); this is test-observation state, never a
/// log or wire type.
#[derive(Clone, Debug, PartialEq)]
pub enum FakeCall {
    StartDiscovery,
    StopDiscovery,
    RefreshDiscovery,
    ListDevices,
    ResolveCastCode(String),
    Connect(DeviceId),
    Disconnect,
    AssessReceiver(DeviceId, CastMediaKind),
    CastMedia {
        device: DeviceId,
        protocol: DeliveryProtocol,
        url: String,
    },
    Play(CastSessionRef),
    Pause(CastSessionRef),
    Seek {
        session: CastSessionRef,
        position_seconds: u64,
    },
    SetVolume {
        session: CastSessionRef,
        volume: u8,
    },
    SetMuted {
        session: CastSessionRef,
        muted: bool,
    },
    Stop(CastSessionRef),
    PlaybackPosition(CastSessionRef),
}

/// One-shot scripted failures, consumed FIFO by the next matching call.
#[derive(Default)]
struct ScriptedErrors {
    start_discovery: VecDeque<CastError>,
    stop_discovery: VecDeque<CastError>,
    refresh_discovery: VecDeque<CastError>,
    resolve_cast_code: VecDeque<CastError>,
    connect: VecDeque<CastError>,
    assess_receiver: VecDeque<CastError>,
    cast_media: VecDeque<CastError>,
    control: VecDeque<CastError>,
}

impl ScriptedErrors {
    fn take(queue: &mut VecDeque<CastError>) -> Result<(), CastError> {
        match queue.pop_front() {
            Some(error) => Err(error),
            None => Ok(()),
        }
    }

    fn push(queue: &mut VecDeque<CastError>, error: CastError) {
        if queue.len() < MAX_SCRIPTED_ERRORS {
            queue.push_back(error);
        }
    }
}

/// Internal state of the current supervised session, if any. A terminated
/// session is kept so `current_session` still reports it and `stop` stays
/// idempotent; it is replaced by the next successful `cast_media`.
struct FakeSession {
    session: CastSessionRef,
    phase: CastSessionPhase,
    playback: CastPlaybackState,
    state_revision: u64,
    terminal_reason: Option<CastTerminalReason>,
    position: PlaybackPosition,
    volume: u8,
    muted: bool,
}

impl FakeSession {
    fn snapshot(&self) -> CastSessionSnapshot {
        CastSessionSnapshot::new(
            self.session.clone(),
            self.phase,
            self.playback,
            self.state_revision,
            self.terminal_reason,
        )
    }

    fn is_terminal(&self) -> bool {
        matches!(self.phase, CastSessionPhase::Terminated)
    }

    fn terminate(&mut self, playback: CastPlaybackState, reason: CastTerminalReason) {
        self.phase = CastSessionPhase::Terminated;
        self.playback = playback;
        self.terminal_reason = Some(reason);
        self.state_revision += 1;
    }
}

#[derive(Default)]
struct ListenerRegistry {
    next_id: u64,
    listeners: Vec<(u64, Arc<dyn CastSessionListener>)>,
}

struct FakeState {
    discovery_running: bool,
    devices: Vec<DiscoveredDevice>,
    cast_codes: Vec<(String, DeviceId)>,
    assessments: Vec<(DeviceId, CastMediaKind, AssessmentStatus)>,
    connected: Option<DeviceId>,
    session: Option<FakeSession>,
    next_session_number: u64,
    next_generation: SessionGeneration,
    position_template: PlaybackPosition,
    scripted: ScriptedErrors,
    calls: Vec<FakeCall>,
}

impl FakeState {
    fn record(&mut self, call: FakeCall) {
        if self.calls.len() < MAX_RECORDED_CALLS {
            self.calls.push(call);
        }
    }

    fn device(&self, device: &DeviceId) -> Option<&DiscoveredDevice> {
        self.devices.iter().find(|item| item.device_id() == device)
    }
}

/// CS-006 fencing shared by every session-bound control. `allow_terminal`
/// models `stop` idempotency: stopping an already-terminated session is a
/// success, while every other control on it fails.
fn fence(
    state: &FakeState,
    session: &CastSessionRef,
    allow_terminal: bool,
) -> Result<(), CastError> {
    let Some(current) = &state.session else {
        return Err(CastError::NoActiveSession);
    };
    if current
        .session
        .generation()
        .supersedes(session.generation())
    {
        // Older generation: rejected before reaching the receiver (CS-006).
        return Err(CastError::StaleSessionGeneration);
    }
    if current.session != *session {
        // Same or newer generation but unknown identity: a foreign handle.
        return Err(CastError::NoActiveSession);
    }
    if current.is_terminal() && !allow_terminal {
        return Err(CastError::NoActiveSession);
    }
    Ok(())
}

/// Scripted deterministic `CastFacade` double (SDK-04). Defaults: no devices,
/// discovery stopped, every operation succeeds once orchestrated.
pub struct FakeCastFacade {
    state: Mutex<FakeState>,
    listeners: Arc<Mutex<ListenerRegistry>>,
}

impl Default for FakeCastFacade {
    fn default() -> Self {
        Self::new()
    }
}

impl FakeCastFacade {
    #[must_use]
    pub fn new() -> Self {
        Self {
            state: Mutex::new(FakeState {
                discovery_running: false,
                devices: Vec::new(),
                cast_codes: Vec::new(),
                assessments: Vec::new(),
                connected: None,
                session: None,
                next_session_number: 1,
                next_generation: SessionGeneration::INITIAL,
                position_template: PlaybackPosition::new(Some(0), None),
                scripted: ScriptedErrors::default(),
                calls: Vec::new(),
            }),
            listeners: Arc::new(Mutex::new(ListenerRegistry::default())),
        }
    }

    // -- Discovery orchestration -------------------------------------------

    /// Adds a device or replaces the entry with the same stable `DeviceId`
    /// (same-name/UDN-conflict/multi-interface receivers keep one identity).
    pub fn upsert_device(&self, device: DiscoveredDevice) {
        let mut state = self.state.lock().unwrap();
        if let Some(existing) = state
            .devices
            .iter_mut()
            .find(|item| item.device_id() == device.device_id())
        {
            *existing = device;
        } else {
            state.devices.push(device);
        }
    }

    pub fn remove_device(&self, device: &DeviceId) {
        let mut state = self.state.lock().unwrap();
        state.devices.retain(|item| item.device_id() != device);
    }

    /// Binds a normalized cast code to a device (CS-003 success branch).
    pub fn bind_cast_code(&self, code: &CastCode, device: &DeviceId) {
        let mut state = self.state.lock().unwrap();
        state
            .cast_codes
            .retain(|(existing, _)| existing != code.as_str());
        state
            .cast_codes
            .push((code.as_str().to_owned(), device.clone()));
    }

    // -- Capability orchestration -------------------------------------------

    /// Sets the point-in-time assessment for one device/media pair. Repeated
    /// calls model capability changes: the facade always answers with the
    /// latest value, so a consumer caching an older answer goes stale
    /// (CS-004). Unscripted pairs report `Unknown` (fail closed).
    pub fn set_assessment(
        &self,
        device: &DeviceId,
        media: CastMediaKind,
        status: AssessmentStatus,
    ) {
        let mut state = self.state.lock().unwrap();
        if let Some(entry) = state
            .assessments
            .iter_mut()
            .find(|(id, kind, _)| id == device && *kind == media)
        {
            entry.2 = status;
        } else {
            state.assessments.push((device.clone(), media, status));
        }
    }

    // -- Session orchestration ----------------------------------------------

    /// Sets the position reported by current and future sessions.
    pub fn set_playback_position(&self, position: PlaybackPosition) {
        let mut state = self.state.lock().unwrap();
        state.position_template = position;
        if let Some(session) = state.session.as_mut() {
            session.position = position;
        }
    }

    /// Advances the active session to a new supervised state, bumping
    /// `state_revision` and emitting one snapshot. A no-op when there is no
    /// non-terminal session.
    pub fn drive_session(
        &self,
        phase: CastSessionPhase,
        playback: CastPlaybackState,
        terminal_reason: Option<CastTerminalReason>,
    ) {
        let event = {
            let mut state = self.state.lock().unwrap();
            state
                .session
                .as_mut()
                .filter(|session| !session.is_terminal())
                .map(|session| {
                    session.phase = phase;
                    session.playback = playback;
                    session.terminal_reason = terminal_reason;
                    session.state_revision += 1;
                    session.snapshot()
                })
        };
        if let Some(event) = event {
            self.dispatch(vec![event]);
        }
    }

    /// Receiver played the media to its natural end (CS-007).
    pub fn simulate_natural_end(&self) {
        self.drive_session(
            CastSessionPhase::Terminated,
            CastPlaybackState::Ended,
            Some(CastTerminalReason::EndedNormally),
        );
    }

    /// The receiver itself stopped playback (TV-side stop, CS-007).
    pub fn simulate_receiver_stop(&self) {
        self.drive_session(
            CastSessionPhase::Terminated,
            CastPlaybackState::Stopped,
            Some(CastTerminalReason::StoppedByReceiver),
        );
    }

    /// The LAN route to the receiver was lost mid-session (CS-007).
    ///
    /// Playback mirrors the pinned SDK terminal mapping 1:1
    /// (`terminate_snapshot`): `ReceiverUnreachable` terminates with
    /// `Stopped`, not `Failed` — only playback/source/protocol failures map
    /// to `Failed`.
    pub fn simulate_route_lost(&self) {
        self.drive_session(
            CastSessionPhase::Terminated,
            CastPlaybackState::Stopped,
            Some(CastTerminalReason::ReceiverUnreachable),
        );
    }

    /// Another controller replaced this sender's session (CS-007).
    pub fn simulate_replaced_by_other_controller(&self) {
        self.drive_session(
            CastSessionPhase::Terminated,
            CastPlaybackState::Stopped,
            Some(CastTerminalReason::ReplacedByOtherController),
        );
    }

    /// Injects an arbitrary supervision snapshot — including stale-generation
    /// or foreign-session events. Listeners always receive it (consumers own
    /// fencing); `current_session` adopts it only when it `supersedes` the
    /// current snapshot, mirroring the SDK event hub.
    pub fn push_session_snapshot(&self, snapshot: CastSessionSnapshot) {
        {
            let mut state = self.state.lock().unwrap();
            let adopt = state
                .session
                .as_ref()
                .is_none_or(|current| snapshot.supersedes(&current.snapshot()));
            if adopt {
                state.session = Some(FakeSession {
                    session: snapshot.session().clone(),
                    phase: snapshot.phase(),
                    playback: snapshot.playback(),
                    state_revision: snapshot.state_revision(),
                    terminal_reason: snapshot.terminal_reason(),
                    position: state.position_template,
                    volume: DEFAULT_VOLUME,
                    muted: false,
                });
            }
        }
        self.dispatch(vec![snapshot]);
    }

    // -- Failure scripting ---------------------------------------------------
    // Each queues a one-shot error consumed by the next matching facade call.

    pub fn fail_next_start_discovery(&self, error: CastError) {
        ScriptedErrors::push(
            &mut self.state.lock().unwrap().scripted.start_discovery,
            error,
        );
    }

    pub fn fail_next_stop_discovery(&self, error: CastError) {
        ScriptedErrors::push(
            &mut self.state.lock().unwrap().scripted.stop_discovery,
            error,
        );
    }

    pub fn fail_next_refresh_discovery(&self, error: CastError) {
        ScriptedErrors::push(
            &mut self.state.lock().unwrap().scripted.refresh_discovery,
            error,
        );
    }

    pub fn fail_next_resolve_cast_code(&self, error: CastError) {
        ScriptedErrors::push(
            &mut self.state.lock().unwrap().scripted.resolve_cast_code,
            error,
        );
    }

    pub fn fail_next_connect(&self, error: CastError) {
        ScriptedErrors::push(&mut self.state.lock().unwrap().scripted.connect, error);
    }

    pub fn fail_next_assess_receiver(&self, error: CastError) {
        ScriptedErrors::push(
            &mut self.state.lock().unwrap().scripted.assess_receiver,
            error,
        );
    }

    pub fn fail_next_cast_media(&self, error: CastError) {
        ScriptedErrors::push(&mut self.state.lock().unwrap().scripted.cast_media, error);
    }

    /// Scripts the next session-bound control call (play/pause/seek/volume/
    /// mute/stop/position) that passes fencing.
    pub fn fail_next_control(&self, error: CastError) {
        ScriptedErrors::push(&mut self.state.lock().unwrap().scripted.control, error);
    }

    // -- Test observation ------------------------------------------------------

    /// Recorded calls that reached the fake receiver, in order.
    #[must_use]
    pub fn calls(&self) -> Vec<FakeCall> {
        self.state.lock().unwrap().calls.clone()
    }

    /// Live subscription count (dropping a subscription must decrement it).
    #[must_use]
    pub fn listener_count(&self) -> usize {
        self.listeners.lock().unwrap().listeners.len()
    }

    /// Delivers snapshots to subscribers after all locks are released.
    fn dispatch(&self, events: Vec<CastSessionSnapshot>) {
        if events.is_empty() {
            return;
        }
        let listeners: Vec<Arc<dyn CastSessionListener>> = self
            .listeners
            .lock()
            .unwrap()
            .listeners
            .iter()
            .map(|(_, listener)| Arc::clone(listener))
            .collect();
        for event in events {
            for listener in &listeners {
                listener.on_session_changed(event.clone());
            }
        }
    }
}

impl CastFacade for FakeCastFacade {
    fn start_discovery(&self) -> Result<(), CastError> {
        let mut state = self.state.lock().unwrap();
        state.record(FakeCall::StartDiscovery);
        ScriptedErrors::take(&mut state.scripted.start_discovery)?;
        state.discovery_running = true;
        Ok(())
    }

    fn stop_discovery(&self) -> Result<(), CastError> {
        let mut state = self.state.lock().unwrap();
        state.record(FakeCall::StopDiscovery);
        ScriptedErrors::take(&mut state.scripted.stop_discovery)?;
        state.discovery_running = false;
        Ok(())
    }

    fn refresh_discovery(&self) -> Result<(), CastError> {
        let mut state = self.state.lock().unwrap();
        state.record(FakeCall::RefreshDiscovery);
        ScriptedErrors::take(&mut state.scripted.refresh_discovery)
    }

    fn list_devices(&self) -> Vec<DiscoveredDevice> {
        let mut state = self.state.lock().unwrap();
        state.record(FakeCall::ListDevices);
        // SDK-06 snapshot semantics: connectable receivers only, in the same
        // deterministic total order as the real facade; stopping discovery
        // never clears the snapshot.
        let mut snapshot: Vec<DiscoveredDevice> = state
            .devices
            .iter()
            .filter(|device| device.state() == DeviceState::Ready)
            .cloned()
            .collect();
        snapshot.sort_by(|left, right| {
            left.friendly_name()
                .cmp(right.friendly_name())
                .then_with(|| left.device_id().as_str().cmp(right.device_id().as_str()))
        });
        snapshot
    }

    fn is_discovery_running(&self) -> bool {
        self.state.lock().unwrap().discovery_running
    }

    fn resolve_device_by_cast_code(&self, code: &CastCode) -> Result<DiscoveredDevice, CastError> {
        let mut state = self.state.lock().unwrap();
        state.record(FakeCall::ResolveCastCode(code.as_str().to_owned()));
        ScriptedErrors::take(&mut state.scripted.resolve_cast_code)?;
        let Some((_, device)) = state
            .cast_codes
            .iter()
            .find(|(bound, _)| bound == code.as_str())
        else {
            return Err(CastError::DeviceNotFound);
        };
        state
            .device(device)
            .cloned()
            .ok_or(CastError::DeviceNotFound)
    }

    fn connect(&self, device: &DeviceId) -> Result<(), CastError> {
        let mut state = self.state.lock().unwrap();
        state.record(FakeCall::Connect(device.clone()));
        ScriptedErrors::take(&mut state.scripted.connect)?;
        // A device absent from the snapshot — unknown or aged-out — is
        // reported as not found, mirroring the real facade (SDK-07); the
        // route-expired `RouteLost` branch is orchestrated via scripting.
        let Some(known) = state.device(device) else {
            return Err(CastError::DeviceNotFound);
        };
        if known.state() != DeviceState::Ready {
            return Err(CastError::DeviceNotFound);
        }
        // Idempotent for the same device; another device switches.
        state.connected = Some(device.clone());
        Ok(())
    }

    fn disconnect(&self) {
        let event = {
            let mut state = self.state.lock().unwrap();
            state.record(FakeCall::Disconnect);
            state.connected = None;
            // An active session is torn down through normal supervision.
            state
                .session
                .as_mut()
                .filter(|session| !session.is_terminal())
                .map(|session| {
                    session.terminate(
                        CastPlaybackState::Stopped,
                        CastTerminalReason::StoppedBySender,
                    );
                    session.snapshot()
                })
        };
        if let Some(event) = event {
            self.dispatch(vec![event]);
        }
    }

    fn connected_device(&self) -> Option<DeviceId> {
        self.state.lock().unwrap().connected.clone()
    }

    fn assess_receiver(
        &self,
        device: &DeviceId,
        media: CastMediaKind,
    ) -> Result<ReceiverAssessment, CastError> {
        let mut state = self.state.lock().unwrap();
        state.record(FakeCall::AssessReceiver(device.clone(), media));
        ScriptedErrors::take(&mut state.scripted.assess_receiver)?;
        if state.device(device).is_none() {
            return Err(CastError::DeviceNotFound);
        }
        let status = state
            .assessments
            .iter()
            .find(|(id, kind, _)| id == device && *kind == media)
            .map_or(AssessmentStatus::Unknown, |(_, _, status)| *status);
        Ok(ReceiverAssessment::new(device.clone(), media, status))
    }

    fn cast_media(&self, request: &CastMediaRequest) -> Result<CastSessionRef, CastError> {
        let mut events = Vec::new();
        let result = {
            let mut state = self.state.lock().unwrap();
            // Adapter-side fail-closed validation; never reaches the receiver.
            if state.connected.as_ref() != Some(request.device_id()) {
                return Err(CastError::InvalidState);
            }
            state.record(FakeCall::CastMedia {
                device: request.device_id().clone(),
                protocol: request.protocol(),
                url: request.url().as_str().to_owned(),
            });
            ScriptedErrors::take(&mut state.scripted.cast_media)?;
            if let Some(previous) = state
                .session
                .as_mut()
                .filter(|session| !session.is_terminal())
            {
                previous.terminate(
                    CastPlaybackState::Stopped,
                    CastTerminalReason::ReplacedByNewCast,
                );
                events.push(previous.snapshot());
            }
            let session_ref = CastSessionRef::new(
                SessionId::new(&format!("fake-session-{}", state.next_session_number))
                    .expect("generated session id is valid"),
                state.next_generation,
            );
            state.next_session_number += 1;
            state.next_generation = state
                .next_generation
                .advance()
                .expect("u64 generation overflow is unreachable in tests");
            let mut session = FakeSession {
                session: session_ref.clone(),
                phase: CastSessionPhase::Starting,
                playback: CastPlaybackState::Preparing,
                state_revision: 0,
                terminal_reason: None,
                position: state.position_template,
                volume: DEFAULT_VOLUME,
                muted: false,
            };
            events.push(session.snapshot());
            session.phase = CastSessionPhase::Active;
            session.playback = CastPlaybackState::Playing;
            session.state_revision = 1;
            events.push(session.snapshot());
            state.session = Some(session);
            Ok(session_ref)
        };
        self.dispatch(events);
        result
    }

    fn play(&self, session: &CastSessionRef) -> Result<(), CastError> {
        let event = {
            let mut state = self.state.lock().unwrap();
            fence(&state, session, false)?;
            state.record(FakeCall::Play(session.clone()));
            ScriptedErrors::take(&mut state.scripted.control)?;
            let current = state.session.as_mut().expect("fenced session exists");
            current.playback = CastPlaybackState::Playing;
            current.state_revision += 1;
            current.snapshot()
        };
        self.dispatch(vec![event]);
        Ok(())
    }

    fn pause(&self, session: &CastSessionRef) -> Result<(), CastError> {
        let event = {
            let mut state = self.state.lock().unwrap();
            fence(&state, session, false)?;
            state.record(FakeCall::Pause(session.clone()));
            ScriptedErrors::take(&mut state.scripted.control)?;
            let current = state.session.as_mut().expect("fenced session exists");
            current.playback = CastPlaybackState::Paused;
            current.state_revision += 1;
            current.snapshot()
        };
        self.dispatch(vec![event]);
        Ok(())
    }

    fn seek(&self, session: &CastSessionRef, position_seconds: u64) -> Result<(), CastError> {
        let event = {
            let mut state = self.state.lock().unwrap();
            fence(&state, session, false)?;
            state.record(FakeCall::Seek {
                session: session.clone(),
                position_seconds,
            });
            ScriptedErrors::take(&mut state.scripted.control)?;
            let current = state.session.as_mut().expect("fenced session exists");
            current.position =
                PlaybackPosition::new(Some(position_seconds), current.position.duration_seconds());
            current.state_revision += 1;
            current.snapshot()
        };
        self.dispatch(vec![event]);
        Ok(())
    }

    fn set_volume(&self, session: &CastSessionRef, volume: Volume) -> Result<(), CastError> {
        let event = {
            let mut state = self.state.lock().unwrap();
            fence(&state, session, false)?;
            state.record(FakeCall::SetVolume {
                session: session.clone(),
                volume: volume.get(),
            });
            ScriptedErrors::take(&mut state.scripted.control)?;
            let current = state.session.as_mut().expect("fenced session exists");
            current.volume = volume.get();
            current.state_revision += 1;
            current.snapshot()
        };
        self.dispatch(vec![event]);
        Ok(())
    }

    fn set_muted(&self, session: &CastSessionRef, muted: bool) -> Result<(), CastError> {
        let event = {
            let mut state = self.state.lock().unwrap();
            fence(&state, session, false)?;
            state.record(FakeCall::SetMuted {
                session: session.clone(),
                muted,
            });
            ScriptedErrors::take(&mut state.scripted.control)?;
            let current = state.session.as_mut().expect("fenced session exists");
            current.muted = muted;
            current.state_revision += 1;
            current.snapshot()
        };
        self.dispatch(vec![event]);
        Ok(())
    }

    fn stop(&self, session: &CastSessionRef) -> Result<(), CastError> {
        let event = {
            let mut state = self.state.lock().unwrap();
            fence(&state, session, true)?;
            let current = state.session.as_mut().expect("fenced session exists");
            if current.is_terminal() {
                // Idempotent: an already-terminated session reports success.
                None
            } else {
                state.record(FakeCall::Stop(session.clone()));
                ScriptedErrors::take(&mut state.scripted.control)?;
                let current = state.session.as_mut().expect("fenced session exists");
                current.terminate(
                    CastPlaybackState::Stopped,
                    CastTerminalReason::StoppedBySender,
                );
                Some(current.snapshot())
            }
        };
        if let Some(event) = event {
            self.dispatch(vec![event]);
        }
        Ok(())
    }

    fn playback_position(&self, session: &CastSessionRef) -> Result<PlaybackPosition, CastError> {
        let mut state = self.state.lock().unwrap();
        fence(&state, session, false)?;
        state.record(FakeCall::PlaybackPosition(session.clone()));
        ScriptedErrors::take(&mut state.scripted.control)?;
        Ok(state
            .session
            .as_ref()
            .expect("fenced session exists")
            .position)
    }

    fn current_session(&self) -> Option<CastSessionSnapshot> {
        self.state
            .lock()
            .unwrap()
            .session
            .as_ref()
            .map(FakeSession::snapshot)
    }

    fn subscribe_session_events(
        &self,
        listener: Arc<dyn CastSessionListener>,
        notify_immediately: bool,
    ) -> Box<dyn CastSessionSubscription> {
        let (id, immediate) = {
            // Lock order: state -> listeners.
            let state = self.state.lock().unwrap();
            let mut registry = self.listeners.lock().unwrap();
            let id = registry.next_id;
            registry.next_id += 1;
            registry.listeners.push((id, Arc::clone(&listener)));
            let immediate = if notify_immediately {
                state.session.as_ref().map(FakeSession::snapshot)
            } else {
                None
            };
            (id, immediate)
        };
        if let Some(snapshot) = immediate {
            listener.on_session_changed(snapshot);
        }
        Box::new(FakeSessionSubscription {
            registry: Arc::downgrade(&self.listeners),
            id,
        })
    }
}

/// Subscription handle; dropping it unsubscribes, idempotently and safely
/// even after the facade itself is gone.
struct FakeSessionSubscription {
    registry: Weak<Mutex<ListenerRegistry>>,
    id: u64,
}

impl Drop for FakeSessionSubscription {
    fn drop(&mut self) {
        if let Some(registry) = self.registry.upgrade() {
            registry
                .lock()
                .unwrap()
                .listeners
                .retain(|(id, _)| *id != self.id);
        }
    }
}

impl CastSessionSubscription for FakeSessionSubscription {}
