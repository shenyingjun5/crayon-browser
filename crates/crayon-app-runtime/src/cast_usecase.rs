//! Cast usecase orchestration (SDK-12): the deterministic state machine that
//! binds UI/runtime intent to `CastFacade` facts — select receiver → connect
//! → policy plan → deliver → session-bound control → terminal convergence.
//!
//! Ownership and boundaries:
//! - the usecase is the sole writer of the product cast state (architecture
//!   red line); facade supervision events are facts, never direct state
//!   writes. Events are fenced with `supersedes` before they are applied, so
//!   an old-generation snapshot can never stop or pollute a newer session
//!   (CS-007);
//! - the session listener only records events (they may arrive on the SDK
//!   dispatch thread). Terminal resource cleanup is orchestrated by
//!   `drain_session_events`, never inside the callback — the official desktop
//!   app uses the same pattern;
//! - terminal convergence (natural end, receiver stop, route lost,
//!   replacement, user stop) revokes the relay sessions bound to the
//!   receiver (RL-004/RL-005), invalidates the capability cache entry
//!   (SDK-08 wiring) and retires the `CastSessionRef`. Navigation, profile
//!   destruction and app exit revoke every relay session and stop any active
//!   session; they do not invalidate capability facts (the device did not
//!   change — SDK-08 assigns invalidation to disconnect/switch/route loss);
//! - ordinary planning/connect failures reject plainly — no privilege
//!   upgrade, no fallback derivation (PL-014). A failed delivery may
//!   downgrade to an external-client handoff suggestion exactly once
//!   (MED-17, design §9.2 step 7, via `downgrade_once`); there is no cyclic
//!   fallback. A handoff suggestion creates no SDK session, no relay token
//!   and never means "casting started" (PL-015);
//! - a plan is bound to one receiver: starting a new attempt supersedes any
//!   active session (old relay sessions revoked, old handle fenced), and the
//!   SDK-09 stale-plan guard fails closed if the receiver changed before
//!   delivery (PL-012);
//! - blocking truth (recorded, not fabricated): the pinned SDK runs one
//!   bounded SOAP exchange per control with a fixed 5 s per-phase timeout,
//!   no retry and no cooperative cancel (SDK-10). The usecase adds no timeout
//!   capability — callers keep controls off the UI thread and own
//!   deadline/cancellation.
//!
//! Concurrency rules (AGENTS §9): the `state` lock is never held across a
//! facade, backend, revocation or capability-cache call; the event queue lock
//! is a leaf taken only to push/drain snapshots; the backend lock covers one
//! `plan_delivery` call and never nests with `state`. Whole start attempts
//! and lifecycle triggers are serialized by `attempt_lock` (never held
//! together with `state`/`backend`), so revocation scopes of two attempts
//! can never interleave. Lock poisoning is tolerated (state stays internally
//! consistent) via `into_inner`.

use crate::delivery::{
    downgrade_once, plan_delivery, DeliveryPlan, DeliveryRequest, SessionBackend, StartOutcome,
};
use crayon_cast_adapter::{
    deliver, CastError, CastFacade, CastMediaUrl, CastSessionRef, CastSessionSnapshot,
    CastSessionSubscription, CastTerminalReason, DeliveryProtocol, DeliveryRoute, PlannedDelivery,
    ReceiverCapabilityCache,
};
use crayon_domain::DeviceId;
use crayon_ipc_schema::{CastPolicyInput, ExternalClientHandoff, ProtocolKind};
use crayon_relay::session::RevokeReason;
use std::collections::VecDeque;
use std::sync::{Arc, Mutex, MutexGuard};

/// Upper bound for recorded-but-undrained supervision events (bounded-queue
/// rule, AGENTS §9). Terminal snapshots are never coalesced (CS-007); when
/// the queue is full the oldest non-terminal snapshot is coalesced away —
/// consumers must already tolerate skipped non-terminal states. A queue full
/// of terminal snapshots only is unreachable in practice (terminal events are
/// exactly-once per session and sessions are serialized); if it ever happens
/// the oldest terminal is dropped and counted in `dropped_terminal`.
pub const MAX_PENDING_SESSION_EVENTS: usize = 128;

/// Phases of the cast usecase (architecture core cast states; MED-19 removed
/// every Mirror/StartingMirror semantic — a handoff suggestion is not a
/// phase, it is an outcome that returns the machine to its standby phase).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CastPhase {
    /// No page facts and no cast activity.
    Idle,
    /// A page is open; no verified eligible playback yet.
    Browsing,
    /// Browser-verified user playback advanced on the page (BR-004).
    PlaybackEligible,
    /// The receiver picker is open.
    SelectingReceiver,
    /// Capability read + policy planning in flight.
    Planning,
    /// Connect/deliver in flight.
    Starting,
    /// A supervised receiver session is active.
    Casting,
    /// User stop issued; waiting for the terminal supervision event.
    Stopping,
    /// Last attempt failed with a stable error; a fresh `start_cast` retries.
    Failed,
}

/// Outcome of one `start_cast` attempt.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CastStartOutcome {
    /// Delivery accepted by the receiver; the session is supervised and its
    /// fencing reference is stored as the active session.
    Casting(CastSessionRef),
    /// Direct/Relay are unavailable or the start downgraded once: an
    /// external-client handoff suggestion. Pure advice — no SDK session, no
    /// relay token, never "casting started"; user confirmation required
    /// (PL-015).
    HandoffSuggested(ExternalClientHandoff),
    /// Stable policy rejection (PL-014: plain, no upgrade, no fallback).
    Rejected(crayon_domain::CoreError),
    /// A facade/connect/delivery failure with its stable code.
    Failed(CastError),
}

/// Relay session revocation seam (RL-004/RL-005): the usecase owns *when*
/// sessions die; the relay owns *how*. Production wires `RelayRuntime`;
/// tests substitute a recording double.
pub trait RelayRevocation: Send + Sync {
    /// Revokes sessions for a lifecycle trigger; idempotent, returns the
    /// number of revoked sessions.
    fn revoke(&self, reason: RevokeReason, receiver: Option<&DeviceId>) -> usize;
}

impl RelayRevocation for crayon_relay::runtime::RelayRuntime {
    fn revoke(&self, reason: RevokeReason, receiver: Option<&DeviceId>) -> usize {
        self.trigger(reason, receiver)
    }
}

/// What `drain_session_events` did.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct DrainStats {
    /// Snapshots applied to the supervised session (passed fencing).
    pub applied: usize,
    /// Snapshots dropped by generation/revision fencing (stale events).
    pub dropped_stale: usize,
    /// Terminal events that converged the active session (cleanup ran).
    pub terminal_converged: usize,
}

/// One bounded event-drain result for the media-host protocol adapter.
/// Snapshots are already generation/revision fenced and contain no media URL.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct DrainedSessionEvents {
    pub stats: DrainStats,
    pub snapshots: Vec<CastSessionSnapshot>,
    /// Cumulative queue loss since this usecase was created. Consumers keep
    /// their own last-seen value to derive a per-drain delta.
    pub cumulative_queue_dropped: u64,
}

/// The session this usecase currently owns, plus the last applied snapshot.
#[derive(Clone, Debug, Eq, PartialEq)]
struct ActiveCast {
    session: CastSessionRef,
    device: DeviceId,
    route: DeliveryRoute,
    observed: Option<CastSessionSnapshot>,
}

#[derive(Default)]
struct EventQueue {
    events: VecDeque<CastSessionSnapshot>,
    coalesced_non_terminal: u64,
    dropped_terminal: u64,
}

impl EventQueue {
    fn push(&mut self, snapshot: CastSessionSnapshot) {
        if self.events.len() < MAX_PENDING_SESSION_EVENTS {
            self.events.push_back(snapshot);
            return;
        }
        if let Some(position) = self.events.iter().position(|event| !event.is_terminal()) {
            // Non-terminal snapshots may coalesce (CS-007); drop the oldest.
            self.events.remove(position);
            self.coalesced_non_terminal += 1;
        } else {
            self.events.pop_front();
            self.dropped_terminal += 1;
        }
        self.events.push_back(snapshot);
    }
}

struct UsecaseState {
    phase: CastPhase,
    /// Resting phase captured when an attempt starts; terminal convergence
    /// and handoff outcomes return here.
    standby_phase: CastPhase,
    /// Monotonic attempt counter: fences a superseded in-flight start.
    attempt: u64,
    /// MED-17 single-downgrade guard for the current attempt.
    downgraded: bool,
    active: Option<ActiveCast>,
    /// Last applied supervision snapshot (fencing reference, CS-007).
    last_applied: Option<CastSessionSnapshot>,
}

/// The cast usecase: deterministic orchestration over `CastFacade` (SDK-12).
///
/// Drivable by the SDK-04 fake — it dispatches supervision events
/// synchronously inside the triggering call, so tests call the trigger and
/// then `drain_session_events`. Production shells drain from their event
/// loop (e.g. on a tick or after a facade call); the listener never blocks
/// the SDK dispatch thread.
pub struct CastUsecase {
    facade: Arc<dyn CastFacade>,
    capabilities: Arc<ReceiverCapabilityCache>,
    backend: Mutex<Box<dyn SessionBackend + Send>>,
    revocation: Arc<dyn RelayRevocation>,
    events: Arc<Mutex<EventQueue>>,
    state: Mutex<UsecaseState>,
    /// Serializes whole start attempts and lifecycle triggers. Attempts are
    /// user-paced and every step is bounded by the pinned SDK's timeouts, so
    /// a second `start_cast` during an in-flight attempt fails fast with
    /// `InvalidState` instead of interleaving: receiver-scoped relay
    /// revocation cannot tell two in-flight attempts' sessions apart, and
    /// serialization keeps a superseded attempt from revoking the newer
    /// attempt's session (and a navigation from leaking one). The lock is
    /// never taken together with `state`/`backend` — it only sequences
    /// callers of those.
    attempt_lock: Mutex<()>,
    /// Keeps the supervision subscription alive; dropped with the usecase.
    // The handle itself is only `Send`; wrapping it makes the usecase `Sync`
    // without ever calling through the handle (it is held purely for Drop).
    _subscription: Mutex<Box<dyn CastSessionSubscription>>,
}

impl CastUsecase {
    /// Assembles the usecase and subscribes to session supervision.
    /// `notify_immediately` is deliberately off: the usecase only ever owns
    /// sessions it created itself.
    #[must_use]
    pub fn new(
        facade: Arc<dyn CastFacade>,
        capabilities: Arc<ReceiverCapabilityCache>,
        backend: Box<dyn SessionBackend + Send>,
        revocation: Arc<dyn RelayRevocation>,
    ) -> Self {
        let events: Arc<Mutex<EventQueue>> = Arc::new(Mutex::new(EventQueue::default()));
        let listener_events = Arc::downgrade(&events);
        let subscription = facade.subscribe_session_events(
            Arc::new(move |snapshot: CastSessionSnapshot| {
                // Record-only callback (may run on the SDK dispatch thread):
                // no cleanup, no facade calls, no state writes here.
                if let Some(queue) = listener_events.upgrade() {
                    lock(&queue).push(snapshot);
                }
            }),
            false,
        );
        Self {
            facade,
            capabilities,
            backend: Mutex::new(backend),
            revocation,
            events,
            state: Mutex::new(UsecaseState {
                phase: CastPhase::Idle,
                standby_phase: CastPhase::Idle,
                attempt: 0,
                downgraded: false,
                active: None,
                last_applied: None,
            }),
            _subscription: Mutex::new(subscription),
            attempt_lock: Mutex::new(()),
        }
    }

    // -- Reads ---------------------------------------------------------------

    #[must_use]
    pub fn phase(&self) -> CastPhase {
        lock(&self.state).phase
    }

    /// Fencing reference of the session this usecase owns, while active.
    #[must_use]
    pub fn active_session(&self) -> Option<CastSessionRef> {
        lock(&self.state)
            .active
            .as_ref()
            .map(|active| active.session.clone())
    }

    /// Delivery route of the active session. Carries no URL or relay token.
    #[must_use]
    pub fn active_route(&self) -> Option<DeliveryRoute> {
        lock(&self.state).active.as_ref().map(|active| active.route)
    }

    /// Last applied supervision snapshot of the active session, if any.
    #[must_use]
    pub fn observed_session(&self) -> Option<CastSessionSnapshot> {
        lock(&self.state)
            .active
            .as_ref()
            .and_then(|active| active.observed.clone())
    }

    // -- Page facts ------------------------------------------------------------

    /// A page is browsing (no verified eligible playback). Resting phases
    /// only — cast activity is never interrupted by a page fact; that is what
    /// the lifecycle triggers are for.
    pub fn on_page_browsing(&self) {
        let mut state = lock(&self.state);
        if is_resting(state.phase) {
            state.phase = CastPhase::Browsing;
        }
    }

    /// Browser-verified user playback advanced (BR-004 gate fact).
    pub fn on_playback_eligible(&self) {
        let mut state = lock(&self.state);
        if is_resting(state.phase) {
            state.phase = CastPhase::PlaybackEligible;
        }
    }

    /// Opens the receiver picker; requires verified eligible playback.
    /// Idempotent while the picker is already open.
    pub fn open_receiver_picker(&self) -> Result<(), CastError> {
        let mut state = lock(&self.state);
        match state.phase {
            CastPhase::PlaybackEligible | CastPhase::SelectingReceiver => {
                state.phase = CastPhase::SelectingReceiver;
                Ok(())
            }
            _ => Err(CastError::InvalidState),
        }
    }

    // -- Start -----------------------------------------------------------------

    /// Runs one full attempt: capability read → policy plan → connect →
    /// deliver. See the module doc for the failure/downgrade semantics.
    ///
    /// Starting a new attempt while `Casting` supersedes the active session
    /// (device switch or re-cast, PL-012): the old session is stopped
    /// best-effort, its relay sessions are revoked (`DeviceReplaced`) and its
    /// capability entry invalidated — all before the new plan is made, so a
    /// same-device re-cast never kills the new relay session.
    pub fn start_cast(&self, request: &DeliveryRequest) -> CastStartOutcome {
        // Whole-attempt serialization (see the field doc): a concurrent
        // attempt fails fast instead of interleaving revocation scopes.
        let _attempt_guard = match self.attempt_lock.try_lock() {
            Ok(guard) => guard,
            Err(std::sync::TryLockError::WouldBlock) => {
                return CastStartOutcome::Failed(CastError::InvalidState);
            }
            Err(std::sync::TryLockError::Poisoned(error)) => error.into_inner(),
        };
        let device = request.receiver.clone();
        let (attempt, old_active) = {
            let mut state = lock(&self.state);
            match state.phase {
                // Eligible phases and retry-after-failure may start; busy
                // phases and not-yet-eligible pages may not.
                CastPhase::PlaybackEligible
                | CastPhase::SelectingReceiver
                | CastPhase::Casting
                | CastPhase::Failed => {}
                CastPhase::Idle
                | CastPhase::Browsing
                | CastPhase::Planning
                | CastPhase::Starting
                | CastPhase::Stopping => {
                    return CastStartOutcome::Failed(CastError::InvalidState);
                }
            }
            state.attempt += 1;
            let attempt = state.attempt;
            // Standby is the resting phase the machine returns to: a re-cast
            // keeps the existing standby (the picker is still conceptually
            // open), and a retry from `Failed` returns to the resting phase
            // the failed attempt started from — not to the error display.
            if !matches!(state.phase, CastPhase::Casting | CastPhase::Failed) {
                state.standby_phase = state.phase;
            }
            state.phase = CastPhase::Planning;
            state.downgraded = false;
            (attempt, state.active.take())
        };
        // Supersede teardown first: a same-device re-cast revokes the old
        // relay sessions before the new plan opens its own (PL-012).
        if let Some(old) = old_active {
            let _ = self.facade.stop(&old.session);
            self.revocation
                .revoke(RevokeReason::DeviceReplaced, Some(&old.device));
            self.capabilities.invalidate(&old.device);
        }

        // Capability read through the SDK-08 cache (fail closed: an
        // assessment error propagates, nothing is guessed).
        let capabilities = match self.capabilities.capabilities(&device) {
            Ok(capabilities) => capabilities,
            Err(error) => return self.fail_attempt(attempt, error),
        };
        let planned_request = DeliveryRequest {
            input: CastPolicyInput::new(
                request.input.page().clone(),
                request.input.playback(),
                request.input.candidate().clone(),
                capabilities,
            ),
            ..request.clone()
        };
        let plan = {
            let mut backend = lock(&self.backend);
            plan_delivery(&planned_request, &mut **backend)
        };
        match plan {
            // PL-014: a planning rejection is plain — no downgrade path.
            DeliveryPlan::Rejected(reason) => {
                self.set_phase_for_attempt(attempt, CastPhase::Failed);
                CastStartOutcome::Rejected(reason)
            }
            // PL-015: advice only — nothing was opened, nothing to revoke.
            DeliveryPlan::ExternalClientHandoff(handoff) => {
                let standby = lock(&self.state).standby_phase;
                self.set_phase_for_attempt(attempt, standby);
                CastStartOutcome::HandoffSuggested(handoff)
            }
            DeliveryPlan::Direct { url } => {
                self.connect_and_deliver(attempt, &device, DeliveryRoute::Direct, &url, request)
            }
            DeliveryPlan::Relay { media_url } => self.connect_and_deliver(
                attempt,
                &device,
                DeliveryRoute::Relay,
                &media_url,
                request,
            ),
        }
    }

    /// Connect + deliver for a Direct/Relay plan. A relay plan that fails
    /// anywhere after planning revokes the session it just opened — no
    /// orphaned tokens. Delivery failure may downgrade once (MED-17).
    fn connect_and_deliver(
        &self,
        attempt: u64,
        device: &DeviceId,
        route: DeliveryRoute,
        url: &str,
        request: &DeliveryRequest,
    ) -> CastStartOutcome {
        let protocol = match request.input.candidate().protocol() {
            ProtocolKind::Mp4 => DeliveryProtocol::Mp4,
            ProtocolKind::Hls => DeliveryProtocol::Hls,
            // Unreachable through this usecase: synthesized capabilities
            // report dash=false, so the policy hands off or rejects a DASH
            // candidate first. Defensive fail-closed, no receiver traffic.
            ProtocolKind::Dash => {
                self.revoke_relay_session(route, device);
                return self.fail_attempt(attempt, CastError::InvalidInput);
            }
        };
        let Ok(media_url) = CastMediaUrl::new(url) else {
            self.revoke_relay_session(route, device);
            return self.fail_attempt(attempt, CastError::InvalidInput);
        };
        self.set_phase_for_attempt(attempt, CastPhase::Starting);
        // Connect failure is a plain failure (PL-014): no downgrade — the
        // user re-selects or re-discovers the receiver.
        if let Err(error) = self.facade.connect(device) {
            self.revoke_relay_session(route, device);
            return self.fail_attempt(attempt, error);
        }
        let planned = PlannedDelivery::new(device.clone(), route, protocol, media_url);
        match deliver(&*self.facade, &planned) {
            Ok(session) => self.register_delivered_session(attempt, session, device, route),
            Err(error) => {
                self.revoke_relay_session(route, device);
                // MED-17 / design §9.2 step 7: exactly one downgrade to an
                // external-client handoff suggestion, never a cycle.
                let failed_plan = match route {
                    DeliveryRoute::Direct => DeliveryPlan::Direct {
                        url: url.to_owned(),
                    },
                    DeliveryRoute::Relay => DeliveryPlan::Relay {
                        media_url: url.to_owned(),
                    },
                };
                let mut state = lock(&self.state);
                if state.attempt == attempt {
                    if let Some(DeliveryPlan::ExternalClientHandoff(handoff)) = downgrade_once(
                        &failed_plan,
                        StartOutcome::Failed,
                        state.downgraded,
                        request.external_client_handoff,
                    ) {
                        state.downgraded = true;
                        state.phase = state.standby_phase;
                        return CastStartOutcome::HandoffSuggested(handoff);
                    }
                    state.phase = CastPhase::Failed;
                }
                CastStartOutcome::Failed(error)
            }
        }
    }

    /// Registers a just-delivered session as active, fencing against a
    /// superseded attempt and against a terminal snapshot that a concurrent
    /// drain already applied for this session (converged inline — the event
    /// is consumed, no later drain can see it again).
    fn register_delivered_session(
        &self,
        attempt: u64,
        session: CastSessionRef,
        device: &DeviceId,
        route: DeliveryRoute,
    ) -> CastStartOutcome {
        let terminal_reason = {
            let mut state = lock(&self.state);
            if state.attempt != attempt {
                None
            } else {
                let terminal_reason = state
                    .last_applied
                    .as_ref()
                    .filter(|last| last.is_terminal() && last.session() == &session)
                    .and_then(|last| last.terminal_reason());
                match terminal_reason {
                    Some(_) => state.phase = state.standby_phase,
                    None => {
                        state.active = Some(ActiveCast {
                            session: session.clone(),
                            device: device.clone(),
                            route,
                            observed: None,
                        });
                        state.phase = CastPhase::Casting;
                    }
                }
                Some(terminal_reason)
            }
        };
        match terminal_reason {
            // Current attempt, live session: casting.
            Some(None) => CastStartOutcome::Casting(session),
            // Current attempt, but the session already terminated: converge
            // its resources inline.
            Some(Some(reason)) => {
                self.revocation
                    .revoke(revoke_reason_for(reason), Some(device));
                self.capabilities.invalidate(device);
                CastStartOutcome::Failed(CastError::InvalidState)
            }
            // Superseded mid-flight: the session we just created is an
            // orphan — stop it and revoke its relay session.
            None => {
                let _ = self.facade.stop(&session);
                self.revoke_relay_session(route, device);
                CastStartOutcome::Failed(CastError::InvalidState)
            }
        }
    }

    // -- Session-bound control (CS-006) -----------------------------------------
    //
    // Every control fences against the stored active session; the facade
    // re-fences authoritatively (stale generation, foreign handle, terminal
    // matrix). Blocking truth: one bounded SOAP exchange per call, fixed 5 s
    // per-phase SDK timeout, no retry/cancel — callers keep controls off the
    // UI thread and own deadline/cancellation.

    /// Active session reference, or `NoActiveSession` outside `Casting`.
    fn require_active(&self) -> Result<CastSessionRef, CastError> {
        let state = lock(&self.state);
        if state.phase != CastPhase::Casting {
            return Err(CastError::NoActiveSession);
        }
        state
            .active
            .as_ref()
            .map(|active| active.session.clone())
            .ok_or(CastError::NoActiveSession)
    }

    pub fn play(&self) -> Result<(), CastError> {
        self.facade.play(&self.require_active()?)
    }

    pub fn pause(&self) -> Result<(), CastError> {
        self.facade.pause(&self.require_active()?)
    }

    pub fn seek(&self, position_seconds: u64) -> Result<(), CastError> {
        self.facade.seek(&self.require_active()?, position_seconds)
    }

    pub fn set_volume(&self, volume: crayon_cast_adapter::Volume) -> Result<(), CastError> {
        self.facade.set_volume(&self.require_active()?, volume)
    }

    pub fn set_muted(&self, muted: bool) -> Result<(), CastError> {
        self.facade.set_muted(&self.require_active()?, muted)
    }

    /// User stop. Idempotent: stopping with no active session is a no-op
    /// success, and a repeated stop while `Stopping` relies on the facade's
    /// terminal-idempotent `stop` (no duplicate receiver Stop). Convergence
    /// and cleanup happen when the terminal supervision event is drained.
    pub fn stop_cast(&self) -> Result<(), CastError> {
        let session = {
            let mut state = lock(&self.state);
            let active = match state.phase {
                CastPhase::Casting | CastPhase::Stopping => state.active.as_ref(),
                // No active session (or already converged): idempotent no-op.
                _ => None,
            };
            match active {
                Some(active) => {
                    let session = active.session.clone();
                    state.phase = CastPhase::Stopping;
                    session
                }
                None => return Ok(()),
            }
        };
        match self.facade.stop(&session) {
            Ok(()) => Ok(()),
            Err(error) => {
                let mut state = lock(&self.state);
                if state.phase == CastPhase::Stopping
                    && state
                        .active
                        .as_ref()
                        .is_some_and(|active| active.session == session)
                {
                    state.phase = CastPhase::Casting;
                }
                Err(error)
            }
        }
    }

    // -- Supervision event pump ---------------------------------------------------

    /// Applies queued supervision events with fencing and orchestrates
    /// terminal convergence (relay revoke + capability invalidation + handle
    /// retirement). Callable from any thread; never invoked by the listener
    /// itself.
    pub fn drain_session_events(&self) -> DrainStats {
        self.drain_session_event_batch(MAX_PENDING_SESSION_EVENTS)
            .stats
    }

    /// Drains at most `limit` already-recorded snapshots. The listener stays
    /// record-only; cleanup and fencing happen here, off the SDK callback.
    /// A zero limit is an explicit no-op.
    pub fn drain_session_event_batch(&self, limit: usize) -> DrainedSessionEvents {
        let (pending, cumulative_queue_dropped): (Vec<CastSessionSnapshot>, u64) = {
            let mut queue = lock(&self.events);
            let count = limit.min(queue.events.len());
            let pending = queue.events.drain(..count).collect();
            let dropped = queue
                .coalesced_non_terminal
                .saturating_add(queue.dropped_terminal);
            (pending, dropped)
        };
        let mut stats = DrainStats::default();
        let mut snapshots = Vec::with_capacity(pending.len());
        let mut cleanups: Vec<(DeviceId, RevokeReason)> = Vec::new();
        {
            let mut state = lock(&self.state);
            for snapshot in pending {
                if state
                    .last_applied
                    .as_ref()
                    .is_some_and(|last| !snapshot.supersedes(last))
                {
                    stats.dropped_stale += 1;
                    continue;
                }
                stats.applied += 1;
                snapshots.push(snapshot.clone());
                let is_terminal = snapshot.is_terminal();
                let terminal_reason = snapshot.terminal_reason();
                state.last_applied = Some(snapshot.clone());
                let Some(active) = state.active.as_mut() else {
                    continue;
                };
                if active.session != *snapshot.session() {
                    // Foreign session event (e.g. the replacement created by
                    // our own newer attempt): fencing applied, no cleanup —
                    // the supersede path already converged its resources.
                    continue;
                }
                active.observed = Some(snapshot);
                if is_terminal {
                    let active = state.active.take().expect("active checked above");
                    cleanups.push((
                        active.device,
                        terminal_reason.map_or(RevokeReason::Stopped, revoke_reason_for),
                    ));
                    state.phase = state.standby_phase;
                    stats.terminal_converged += 1;
                }
            }
        }
        // Cleanup outside the state lock (revocation/cache take own locks).
        for (device, reason) in cleanups {
            self.revocation.revoke(reason, Some(&device));
            self.capabilities.invalidate(&device);
        }
        DrainedSessionEvents {
            stats,
            snapshots,
            cumulative_queue_dropped,
        }
    }

    // -- Lifecycle triggers (RL-005) ----------------------------------------------

    /// Navigation or tab close: stop any active session and revoke every
    /// relay session. Capability facts survive (the device did not change).
    pub fn on_navigation(&self) {
        self.lifecycle_revoke(RevokeReason::Navigation, CastPhase::Browsing);
    }

    /// Profile destruction: same teardown as navigation, converging to Idle.
    pub fn on_profile_destroyed(&self) {
        self.lifecycle_revoke(RevokeReason::ProfileDestroyed, CastPhase::Idle);
    }

    /// App exit: same teardown as navigation, converging to Idle.
    pub fn on_app_exit(&self) {
        self.lifecycle_revoke(RevokeReason::AppExit, CastPhase::Idle);
    }

    fn lifecycle_revoke(&self, reason: RevokeReason, resting: CastPhase) {
        // Sequenced with in-flight attempts: a navigation waits (bounded by
        // the SDK timeouts) rather than leaking a session created
        // concurrently with the trigger.
        let _attempt_guard = lock(&self.attempt_lock);
        let old = {
            let mut state = lock(&self.state);
            state.phase = resting;
            state.active.take()
        };
        if let Some(active) = old {
            let _ = self.facade.stop(&active.session);
        }
        self.revocation.revoke(reason, None);
    }

    // -- Internals --------------------------------------------------------------

    /// Revokes the relay session a failed attempt opened. Only Relay plans
    /// open sessions; a Direct failure revokes nothing (receiver-scoped
    /// revocation could kill an unrelated live session).
    fn revoke_relay_session(&self, route: DeliveryRoute, device: &DeviceId) {
        if route == DeliveryRoute::Relay {
            self.revocation.revoke(RevokeReason::Stopped, Some(device));
        }
    }

    fn set_phase_for_attempt(&self, attempt: u64, phase: CastPhase) {
        let mut state = lock(&self.state);
        if state.attempt == attempt {
            state.phase = phase;
        }
    }

    fn fail_attempt(&self, attempt: u64, error: CastError) -> CastStartOutcome {
        self.set_phase_for_attempt(attempt, CastPhase::Failed);
        CastStartOutcome::Failed(error)
    }
}

/// Resting phases: page facts may move the machine between them.
fn is_resting(phase: CastPhase) -> bool {
    matches!(
        phase,
        CastPhase::Idle
            | CastPhase::Browsing
            | CastPhase::PlaybackEligible
            | CastPhase::SelectingReceiver
            | CastPhase::Failed
    )
}

/// Maps a terminal cause to the relay revocation trigger (RL-005).
fn revoke_reason_for(reason: CastTerminalReason) -> RevokeReason {
    match reason {
        CastTerminalReason::ReceiverUnreachable
        | CastTerminalReason::ReceiverShutdown
        | CastTerminalReason::ReceiverSessionLost => RevokeReason::RouteLost,
        CastTerminalReason::ReplacedByNewCast | CastTerminalReason::ReplacedByOtherController => {
            RevokeReason::DeviceReplaced
        }
        CastTerminalReason::StoppedBySender
        | CastTerminalReason::StoppedByReceiver
        | CastTerminalReason::EndedNormally
        | CastTerminalReason::PlaybackFailed
        | CastTerminalReason::SourceFailed
        | CastTerminalReason::ProtocolError => RevokeReason::Stopped,
    }
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|error| error.into_inner())
}

#[cfg(test)]
#[path = "cast_usecase_tests.rs"]
mod tests;
