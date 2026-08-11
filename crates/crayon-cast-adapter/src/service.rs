//! `SenderCastFacade`: the real `CastFacade` over the pinned Cast-SDK
//! `SenderCommandService` (SDK-05).
//!
//! Lifecycle and concurrency contract (AGENTS §9, roadmap Review 专项):
//! - construction is pure allocation plus the SDK session-event dispatch
//!   thread (`cast-session-events`, joined by the SDK hub on drop); no
//!   discovery worker, control server or port exists until first use;
//! - all SDK state stays inside `SenderCommandService` (single owner); the
//!   adapter keeps no mirrored device/session state — every read is served
//!   from the SDK and every fence decision re-reads the SDK supervisor;
//! - the adapter's only lock guards the `Option<SenderCommandService>` slot
//!   and is held exclusively for clone/take of the Arc-based handle, never
//!   across an SDK call, callback, join or network operation;
//! - session events are bridged on the SDK hub dispatch thread, which
//!   already delivers outside any SDK lock (with per-listener generation
//!   fencing); the bridge is a pure conversion plus the product listener
//!   call, so no adapter lock is ever held inside a callback;
//! - `shutdown`/`Drop` tear down in reverse acquisition order: supervised
//!   session stop (in-memory) -> device disconnect (at most one bounded
//!   SOAP stop when media is active) -> discovery stop (joins the worker)
//!   -> service drop (joins the event hub, closes the loopback control
//!   server). Everything is best-effort and idempotent: an SDK runtime
//!   failure never panics and never blocks exit beyond one SDK control
//!   timeout. After shutdown every fallible call fails closed with
//!   `CastError::InvalidState` and every infallible read degrades to empty.
//!
//! Restart semantics: a dropped/shutdown facade is never reused; a new
//! `SenderCastFacade` builds a fresh SDK service. All ports are ephemeral
//! loopback (`local_http_port: None`, session-control `127.0.0.1:0`), so a
//! restart never collides with the previous instance's sockets, and
//! device/session state deliberately does not carry over.
//!
//! Deliberately out of scope here (later roadmap tasks): cast-code branch
//! mapping including the missing cancel API (SDK-07), capability caching/TTL
//! (SDK-08), delivery orchestration policy (SDK-09).
//!
//! Discovery snapshot semantics (finalized in SDK-06, CS-001/CS-002):
//! - `list_devices` serves the SDK product-visible list — connectable
//!   receivers only; aged-out (stale/offline), unresolved and
//!   placeholder-named devices never appear, so expiry shows up as
//!   disappearance, not as a degraded entry;
//! - stopping discovery never clears the snapshot (the pinned SDK keeps its
//!   device registry across stop; the snapshot is a known fact);
//! - the adapter collapses duplicate SDK registrations of one logical
//!   receiver (UDN conflict, cast-code + SSDP double registration) into one
//!   entry per stable `DeviceId` and imposes a deterministic total order, so
//!   the product snapshot never flickers (see `device_snapshot_of`).

use crate::dto::AssessmentStatus;
use crate::dto::{
    CastCode, CastMediaKind, CastMediaRequest, CastPlaybackState, CastSessionPhase, CastSessionRef,
    CastSessionSnapshot, CastTerminalReason, DeliveryProtocol, DeviceState, DiscoveredDevice,
    PlaybackPosition, ReceiverAssessment, Volume,
};
use crate::error::{CastError, SenderErrorKind};
use crate::facade::{CastFacade, CastSessionListener, CastSessionSubscription};
use cast_sender_core::{
    CastDevice, CastSenderError, DeviceDiscoveryState, ErrorKind, SenderConfig,
};
use cast_sender_service::{CapabilityStatus, CastMediaType, SenderCommandService};
use cast_sender_session::{
    CastSessionHandle, PlaybackState as SdkPlaybackState, SessionListener as SdkSessionListener,
    SessionPhase as SdkSessionPhase, SessionSubscription as SdkSessionSubscription,
    StopReason as SdkStopReason, TerminalReason as SdkTerminalReason,
};
use crayon_domain::{DeviceId, SessionGeneration, SessionId};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

/// Default SSDP discovery cycle timeout handed to the SDK. The SDK default
/// is 10 s; naming it here keeps the product value explicit and changeable
/// without an SDK revision bump.
const DEFAULT_DISCOVERY_TIMEOUT_MS: u64 = 10_000;

/// Product configuration for the real sender facade.
///
/// Only values the product legitimately tunes are exposed. The rest are
/// fixed by the adapter: discovery listens on all non-virtual interfaces,
/// the local HTTP asset server and the session-control server bind
/// ephemeral loopback ports (restart-safe, never a fixed port), and the
/// SDK asset token stays enabled.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SenderCastFacadeConfig {
    /// Sender name reported to receivers.
    pub app_name: String,
    /// SSDP discovery cycle timeout in milliseconds.
    pub discovery_timeout_ms: u64,
}

impl Default for SenderCastFacadeConfig {
    fn default() -> Self {
        Self {
            app_name: "Crayon Browser".to_string(),
            discovery_timeout_ms: DEFAULT_DISCOVERY_TIMEOUT_MS,
        }
    }
}

impl SenderCastFacadeConfig {
    fn sdk_config(&self) -> SenderConfig {
        SenderConfig {
            app_name: self.app_name.clone(),
            discovery_timeout_ms: self.discovery_timeout_ms,
            discovery_interface_ips: Vec::new(),
            discovery_include_virtual_interfaces: false,
            local_http_port: None,
            enable_token: true,
        }
    }
}

/// Real `CastFacade` backed by the pinned `SenderCommandService` (SDK-05).
///
/// `Send + Sync` and safe to call from any thread; every method is
/// idempotent or explicitly fenced. The SDK facade is synchronous and may
/// block on LAN I/O (discovery, SOAP control) — that blocking contract is
/// owned by the `CastFacade` trait; async scheduling is a caller concern.
pub struct SenderCastFacade {
    /// `None` after `shutdown`/`Drop` began: fail closed, never panic.
    service: Mutex<Option<SenderCommandService>>,
}

impl SenderCastFacade {
    /// Builds the facade and the underlying SDK service. Spawns exactly one
    /// thread (the SDK session-event dispatcher); no discovery worker,
    /// network socket or port is created here.
    #[must_use]
    pub fn new(config: SenderCastFacadeConfig) -> Self {
        Self {
            service: Mutex::new(Some(SenderCommandService::new(config.sdk_config()))),
        }
    }

    /// Idempotent, best-effort teardown; also runs on `Drop`.
    ///
    /// Reverse acquisition order: terminate the supervised session through
    /// normal supervision (in-memory), disconnect the device (the SDK sends
    /// at most one bounded SOAP stop when media is active), stop discovery
    /// (joins the worker thread), then drop the service (joins the event
    /// dispatcher, closes the loopback control server). SDK failures are
    /// swallowed on purpose: teardown must not block or panic browser exit.
    pub fn shutdown(&self) {
        let service = self
            .service
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .take();
        if let Some(service) = service {
            Self::teardown(&service);
        }
    }

    /// Shared teardown used by `shutdown` and `disconnect` paths.
    fn teardown(service: &SenderCommandService) {
        Self::stop_supervised_session(service);
        let _ = service.disconnect_device();
        let _ = service.stop_discovery();
    }

    /// Ends a live supervised session through the supervision state machine
    /// (pure state transition; no receiver I/O).
    fn stop_supervised_session(service: &SenderCommandService) {
        if let Some(snapshot) = service.current_cast_session() {
            if !snapshot.is_terminal() {
                let _ = service.stop_cast_session(&snapshot.handle, SdkStopReason::UserRequest);
            }
        }
    }

    /// Clones the Arc-based SDK handle under a brief lock; the lock is never
    /// held across an SDK call. Fails closed once shutdown began.
    fn service(&self) -> Result<SenderCommandService, CastError> {
        self.service
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone()
            .ok_or(CastError::InvalidState)
    }

    /// Finds the SDK device id behind a product `DeviceId` (the SDK stable
    /// device key) in the current snapshot. Unknown devices fail closed with
    /// `DeviceNotFound` before any SDK call.
    fn find_sdk_device_id(
        service: &SenderCommandService,
        device: &DeviceId,
    ) -> Result<String, CastError> {
        service
            .list_devices()
            .into_iter()
            .find(|candidate| device_id_of(candidate).as_ref() == Some(device))
            .map(|candidate| candidate.id)
            .ok_or(CastError::DeviceNotFound)
    }

    /// Currently connected product device id, if any.
    fn connected_device_of(service: &SenderCommandService) -> Option<DeviceId> {
        device_id_of(service.get_session_state().device.as_ref()?)
    }

    /// CS-006 fencing shared by every session-bound control, mirroring the
    /// SDK-04 fake: no session -> `NoActiveSession`; older generation ->
    /// `StaleSessionGeneration`; same/newer generation but unknown identity
    /// -> `NoActiveSession` (foreign handle); a terminal session rejects
    /// everything except an idempotent `stop`. On success returns the real
    /// SDK handle (carrying the media kind the product ref does not model).
    ///
    /// The decision re-reads the SDK supervisor at call time; a concurrent
    /// replacement racing past this fence is still fenced inside the SDK
    /// (`CAST_SESSION_STALE_GENERATION`) and surfaces as the same stable
    /// error through `map_error`.
    fn fence_current(
        service: &SenderCommandService,
        session: &CastSessionRef,
        allow_terminal: bool,
    ) -> Result<CastSessionHandle, CastError> {
        let Some(current) = service.current_cast_session() else {
            return Err(CastError::NoActiveSession);
        };
        if SessionGeneration::from_raw(current.handle.generation).supersedes(session.generation()) {
            return Err(CastError::StaleSessionGeneration);
        }
        if current.handle.session_id != session.session_id().as_str()
            || current.handle.generation != session.generation().get()
        {
            return Err(CastError::NoActiveSession);
        }
        if current.is_terminal() && !allow_terminal {
            return Err(CastError::NoActiveSession);
        }
        Ok(current.handle)
    }
}

impl Drop for SenderCastFacade {
    fn drop(&mut self) {
        self.shutdown();
    }
}

impl CastFacade for SenderCastFacade {
    fn start_discovery(&self) -> Result<(), CastError> {
        self.service()?.start_discovery().map_err(map_error)
    }

    fn stop_discovery(&self) -> Result<(), CastError> {
        self.service()?.stop_discovery().map_err(map_error)
    }

    fn refresh_discovery(&self) -> Result<(), CastError> {
        self.service()?.refresh_discovery().map_err(map_error)
    }

    fn list_devices(&self) -> Vec<DiscoveredDevice> {
        match self.service() {
            Ok(service) => device_snapshot_of(service.list_devices()),
            Err(_) => Vec::new(),
        }
    }

    fn is_discovery_running(&self) -> bool {
        self.service()
            .is_ok_and(|service| service.is_discovery_running())
    }

    fn resolve_device_by_cast_code(&self, code: &CastCode) -> Result<DiscoveredDevice, CastError> {
        // NOTE: the pinned SDK reports cast-code decode failures and argument
        // errors under the same `InvalidInput` category; contextual remapping
        // to `InvalidCastCode` and the missing cancel API are SDK-07 scope.
        let device = self
            .service()?
            .resolve_device_by_cast_code(code.as_str())
            .map_err(map_error)?;
        discovered_device_of(&device).ok_or(CastError::Internal)
    }

    fn connect(&self, device: &DeviceId) -> Result<(), CastError> {
        let service = self.service()?;
        let sdk_id = Self::find_sdk_device_id(&service, device)?;
        service.connect_device(&sdk_id).map_err(map_error)?;
        Ok(())
    }

    fn disconnect(&self) {
        if let Ok(service) = self.service() {
            // An active session is torn down through normal supervision
            // before the device connection drops (SDK-03 contract).
            Self::stop_supervised_session(&service);
            let _ = service.disconnect_device();
        }
    }

    fn connected_device(&self) -> Option<DeviceId> {
        Self::connected_device_of(&self.service().ok()?)
    }

    fn assess_receiver(
        &self,
        device: &DeviceId,
        media: CastMediaKind,
    ) -> Result<ReceiverAssessment, CastError> {
        let service = self.service()?;
        let sdk_id = Self::find_sdk_device_id(&service, device)?;
        let assessment = service
            .assess_cast(&sdk_id, sdk_media_type(media))
            .map_err(map_error)?;
        Ok(ReceiverAssessment::new(
            device.clone(),
            media,
            assessment_status_of(assessment.status),
        ))
    }

    fn cast_media(&self, request: &CastMediaRequest) -> Result<CastSessionRef, CastError> {
        let service = self.service()?;
        // Fail closed before touching the receiver: the request device must
        // be the connected one (SDK-03 contract).
        if Self::connected_device_of(&service).as_ref() != Some(request.device_id()) {
            return Err(CastError::InvalidState);
        }
        match request.protocol() {
            DeliveryProtocol::Mp4 => service.cast_video_url(request.url().as_str()),
            DeliveryProtocol::Hls => service.cast_hls_url(request.url().as_str()),
        }
        .map_err(map_error)?;
        // The successful cast just became the supervised current session;
        // any previous session was replaced and reports `ReplacedByNewCast`
        // through the supervision subscription.
        let snapshot = service.current_cast_session().ok_or(CastError::Internal)?;
        session_ref_of(&snapshot).ok_or(CastError::Internal)
    }

    fn play(&self, session: &CastSessionRef) -> Result<(), CastError> {
        let service = self.service()?;
        let handle = Self::fence_current(&service, session, false)?;
        service.play_cast_session(&handle).map_err(map_error)?;
        Ok(())
    }

    fn pause(&self, session: &CastSessionRef) -> Result<(), CastError> {
        let service = self.service()?;
        let handle = Self::fence_current(&service, session, false)?;
        service.pause_cast_session(&handle).map_err(map_error)?;
        Ok(())
    }

    fn seek(&self, session: &CastSessionRef, position_seconds: u64) -> Result<(), CastError> {
        let service = self.service()?;
        let handle = Self::fence_current(&service, session, false)?;
        service
            .seek_cast_session(&handle, position_seconds)
            .map_err(map_error)?;
        Ok(())
    }

    fn set_volume(&self, session: &CastSessionRef, volume: Volume) -> Result<(), CastError> {
        let service = self.service()?;
        let handle = Self::fence_current(&service, session, false)?;
        service
            .set_cast_session_volume(&handle, volume.get())
            .map_err(map_error)?;
        Ok(())
    }

    fn set_muted(&self, session: &CastSessionRef, muted: bool) -> Result<(), CastError> {
        let service = self.service()?;
        let handle = Self::fence_current(&service, session, false)?;
        service
            .set_cast_session_muted(&handle, muted)
            .map_err(map_error)?;
        Ok(())
    }

    fn stop(&self, session: &CastSessionRef) -> Result<(), CastError> {
        let service = self.service()?;
        let handle = Self::fence_current(&service, session, true)?;
        // The SDK reports an already-terminated current session as Ok, so
        // stop stays idempotent; only stale/foreign handles error.
        service
            .stop_cast_session(&handle, SdkStopReason::UserRequest)
            .map_err(map_error)?;
        Ok(())
    }

    fn playback_position(&self, session: &CastSessionRef) -> Result<PlaybackPosition, CastError> {
        let service = self.service()?;
        let _handle = Self::fence_current(&service, session, false)?;
        let position = service.get_playback_position().map_err(map_error)?;
        // The SDK `track_uri` (the media URL) is dropped here (RL-014).
        Ok(PlaybackPosition::new(
            position.position_seconds,
            position.duration_seconds,
        ))
    }

    fn current_session(&self) -> Option<CastSessionSnapshot> {
        let service = self.service().ok()?;
        session_snapshot_of(&service.current_cast_session()?)
    }

    fn subscribe_session_events(
        &self,
        listener: Arc<dyn CastSessionListener>,
        notify_immediately: bool,
    ) -> Box<dyn CastSessionSubscription> {
        let inner = self.service().ok().map(|service| {
            service.subscribe_cast_session(Arc::new(SessionBridge { listener }), notify_immediately)
        });
        // After shutdown the subscription is an inert handle: dropping it is
        // still the (idempotent) unsubscribe.
        Box::new(SenderSessionSubscription { _inner: inner })
    }
}

/// Product subscription handle over the SDK hub subscription. The SDK
/// subscription is held purely for its `Drop`, which unsubscribes
/// idempotently; `None` after shutdown means there is nothing to
/// unsubscribe.
struct SenderSessionSubscription {
    _inner: Option<SdkSessionSubscription>,
}

impl CastSessionSubscription for SenderSessionSubscription {}

/// Bridges SDK session supervision events onto the product listener.
///
/// Runs on the SDK hub dispatch thread. The hub publishes only snapshots
/// that strictly supersede its current one and additionally filters per
/// listener, so old-generation events never reach this bridge (CS-007).
/// The bridge itself is a pure conversion plus the listener call: it owns
/// no state and touches no lock, so a listener may re-enter the facade.
struct SessionBridge {
    listener: Arc<dyn CastSessionListener>,
}

impl SdkSessionListener for SessionBridge {
    fn on_session_changed(&self, snapshot: cast_sender_session::CastSessionSnapshot) {
        if let Some(mapped) = session_snapshot_of(&snapshot) {
            self.listener.on_session_changed(mapped);
        }
    }
}

/// Maps an SDK error onto the stable product error (CS-008), consulting
/// only the category and the stable machine code — never the message.
fn map_error(error: CastSenderError) -> CastError {
    CastError::from_sender_error(sender_error_kind_of(&error), &error.code)
}

/// Exhaustive conversion pinned to the SDK enum: an added or renamed
/// `ErrorKind` variant breaks this build instead of silently mismapping.
fn sender_error_kind_of(error: &CastSenderError) -> SenderErrorKind {
    match error.kind {
        ErrorKind::Device => SenderErrorKind::Device,
        ErrorKind::Network => SenderErrorKind::Network,
        ErrorKind::Http => SenderErrorKind::Http,
        ErrorKind::Image => SenderErrorKind::Image,
        ErrorKind::Control => SenderErrorKind::Control,
        ErrorKind::InvalidInput => SenderErrorKind::InvalidInput,
        ErrorKind::State => SenderErrorKind::State,
    }
}

/// Product device id = the SDK stable device key (16 lowercase hex), which
/// keeps identity across IP changes and same-name/UDN conflicts (SDK-03
/// decision). `DeviceId` validation cannot fail on that charset, but the
/// conversion stays total: an unconvertible device is skipped/fails closed.
fn device_id_of(device: &CastDevice) -> Option<DeviceId> {
    DeviceId::new(&device.stable_device_key()).ok()
}

fn discovered_device_of(device: &CastDevice) -> Option<DiscoveredDevice> {
    Some(DiscoveredDevice::new(
        device_id_of(device)?,
        device.friendly_name.clone(),
        device_state_of(&device.discovery_state),
        device.is_labi_receiver,
    ))
}

/// Maps the SDK product-visible device list onto the product snapshot
/// (SDK-06, CS-002).
///
/// The SDK registry keys entries by announcement id, so one logical receiver
/// can occupy two entries that resolve to the same stable device key — a
/// duplicate-UDN registration or a double registration via cast code and
/// SSDP with differing description locations. The snapshot shows one entry
/// per stable `DeviceId`; the representative is the registration with the
/// smallest SDK id, a rule independent of the SDK registry's `HashMap`
/// iteration order. For the same reason the output is re-sorted into a
/// deterministic total order (friendly name, then device id): the SDK only
/// sorts by name, leaving same-name receivers in random hash order.
fn device_snapshot_of(devices: Vec<CastDevice>) -> Vec<DiscoveredDevice> {
    let mut representative: HashMap<DeviceId, &str> = HashMap::new();
    for device in &devices {
        if let Some(device_id) = device_id_of(device) {
            representative
                .entry(device_id)
                .and_modify(|sdk_id| {
                    if device.id.as_str() < *sdk_id {
                        *sdk_id = device.id.as_str();
                    }
                })
                .or_insert(device.id.as_str());
        }
    }
    let mut snapshot: Vec<DiscoveredDevice> = devices
        .iter()
        .filter(|device| {
            device_id_of(device)
                .as_ref()
                .and_then(|device_id| representative.get(device_id))
                .is_some_and(|sdk_id| *sdk_id == device.id.as_str())
        })
        .filter_map(discovered_device_of)
        .collect();
    snapshot.sort_by(|left, right| {
        left.friendly_name()
            .cmp(right.friendly_name())
            .then_with(|| left.device_id().as_str().cmp(right.device_id().as_str()))
    });
    snapshot
}

fn device_state_of(state: &DeviceDiscoveryState) -> DeviceState {
    match state {
        DeviceDiscoveryState::Ready => DeviceState::Ready,
        DeviceDiscoveryState::Incomplete => DeviceState::Incomplete,
        DeviceDiscoveryState::RequiresAuthorization => DeviceState::RequiresAuthorization,
        DeviceDiscoveryState::Stale => DeviceState::Stale,
        DeviceDiscoveryState::Offline => DeviceState::Offline,
    }
}

fn sdk_media_type(media: CastMediaKind) -> CastMediaType {
    match media {
        CastMediaKind::Video => CastMediaType::Video,
        CastMediaKind::Hls => CastMediaType::Hls,
    }
}

fn assessment_status_of(status: CapabilityStatus) -> AssessmentStatus {
    match status {
        CapabilityStatus::Supported => AssessmentStatus::Supported,
        CapabilityStatus::Risky => AssessmentStatus::Risky,
        CapabilityStatus::Unsupported => AssessmentStatus::Unsupported,
        CapabilityStatus::Unknown => AssessmentStatus::Unknown,
    }
}

fn session_ref_of(snapshot: &cast_sender_session::CastSessionSnapshot) -> Option<CastSessionRef> {
    Some(CastSessionRef::new(
        SessionId::new(&snapshot.handle.session_id).ok()?,
        SessionGeneration::from_raw(snapshot.handle.generation),
    ))
}

/// Converts an SDK supervision snapshot into the product DTO. SDK-internal
/// fields (health, ownership, receiver kind/connection, media revision,
/// elapsed timestamps, receiver generation/epoch, error code) stay inside
/// the adapter; `error_code` is diagnostic and never crosses. Returns `None`
/// when the session id cannot be represented as a product `SessionId` —
/// the event is dropped rather than leaking an unvalidated identity.
fn session_snapshot_of(
    snapshot: &cast_sender_session::CastSessionSnapshot,
) -> Option<CastSessionSnapshot> {
    Some(CastSessionSnapshot::new(
        session_ref_of(snapshot)?,
        session_phase_of(snapshot.phase),
        playback_state_of(snapshot.playback_state),
        snapshot.state_revision,
        snapshot.terminal_reason.map(terminal_reason_of),
    ))
}

fn session_phase_of(phase: SdkSessionPhase) -> CastSessionPhase {
    match phase {
        SdkSessionPhase::Starting => CastSessionPhase::Starting,
        SdkSessionPhase::Active => CastSessionPhase::Active,
        SdkSessionPhase::Suspended => CastSessionPhase::Suspended,
        SdkSessionPhase::Recovering => CastSessionPhase::Recovering,
        SdkSessionPhase::Terminating => CastSessionPhase::Terminating,
        SdkSessionPhase::Terminated => CastSessionPhase::Terminated,
    }
}

fn playback_state_of(state: SdkPlaybackState) -> CastPlaybackState {
    match state {
        SdkPlaybackState::Unknown => CastPlaybackState::Unknown,
        SdkPlaybackState::Preparing => CastPlaybackState::Preparing,
        SdkPlaybackState::Buffering => CastPlaybackState::Buffering,
        SdkPlaybackState::Playing => CastPlaybackState::Playing,
        SdkPlaybackState::Paused => CastPlaybackState::Paused,
        // The facade never delivers images (SDK-03), so the SDK static-image
        // state has no product counterpart and degrades to `Unknown`.
        SdkPlaybackState::PresentingStatic => CastPlaybackState::Unknown,
        SdkPlaybackState::Ended => CastPlaybackState::Ended,
        SdkPlaybackState::Stopped => CastPlaybackState::Stopped,
        SdkPlaybackState::Failed => CastPlaybackState::Failed,
    }
}

fn terminal_reason_of(reason: SdkTerminalReason) -> CastTerminalReason {
    match reason {
        SdkTerminalReason::StoppedBySender => CastTerminalReason::StoppedBySender,
        SdkTerminalReason::StoppedByReceiver => CastTerminalReason::StoppedByReceiver,
        SdkTerminalReason::EndedNormally => CastTerminalReason::EndedNormally,
        SdkTerminalReason::ReplacedByNewCast => CastTerminalReason::ReplacedByNewCast,
        SdkTerminalReason::ReplacedByOtherController => {
            CastTerminalReason::ReplacedByOtherController
        }
        SdkTerminalReason::ReceiverShutdown => CastTerminalReason::ReceiverShutdown,
        SdkTerminalReason::ReceiverSessionLost => CastTerminalReason::ReceiverSessionLost,
        SdkTerminalReason::ReceiverUnreachable => CastTerminalReason::ReceiverUnreachable,
        SdkTerminalReason::PlaybackFailed => CastTerminalReason::PlaybackFailed,
        SdkTerminalReason::SourceFailed => CastTerminalReason::SourceFailed,
        SdkTerminalReason::ProtocolError => CastTerminalReason::ProtocolError,
    }
}

#[cfg(test)]
#[path = "service_tests.rs"]
mod tests;
