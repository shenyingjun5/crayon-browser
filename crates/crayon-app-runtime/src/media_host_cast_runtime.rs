//! MHV1 Cast execution adapter over the unique product `CastUsecase`.
//!
//! Device locators and media URLs remain behind the facade/usecase boundary;
//! only bounded device presentation facts and fenced session state reach MHV1.

use crate::cast_usecase::{CastPhase, CastStartOutcome, CastUsecase, RelayRevocation};
use crate::delivery::{DeliveryRequest, SessionBackend};
use crate::media_host_runtime::MediaHostRuntimeError;
use crayon_cast_adapter::{
    CastError, CastFacade, CastPlaybackState, CastSessionPhase, CastTerminalReason, DeliveryRoute,
    DeviceState, DiscoveredDevice, ReceiverCapabilityCache,
};
use crayon_domain::DeviceId;
use crayon_ipc_schema::{
    MediaHostCastErrorCode, MediaHostCastStartOutcome, MediaHostDeliveryRoute, MediaHostDevice,
    MediaHostDeviceState, MediaHostDiscoveryAction, MediaHostMessage, MediaHostSessionEvent,
    MediaHostSessionPhase, MediaHostSessionPlayback, MediaHostTerminalReason,
    MAX_MEDIA_HOST_DEVICES, MAX_MEDIA_HOST_DEVICE_NAME_BYTES, MAX_MEDIA_HOST_DEVICE_PAGE,
    MAX_MEDIA_HOST_SESSION_EVENTS,
};
use std::sync::{Arc, Mutex, MutexGuard};

struct RuntimeState {
    devices: Vec<DiscoveredDevice>,
    revision: u64,
    dropped_events: u64,
    last_queue_dropped: u64,
}

/// Single Cast execution owner embedded in the Rust media-host process.
pub struct MediaHostCastRuntime {
    facade: Arc<dyn CastFacade>,
    usecase: Arc<CastUsecase>,
    state: Mutex<RuntimeState>,
}

impl MediaHostCastRuntime {
    #[must_use]
    pub fn new(
        facade: Arc<dyn CastFacade>,
        capabilities: Arc<ReceiverCapabilityCache>,
        backend: Box<dyn SessionBackend + Send>,
        revocation: Arc<dyn RelayRevocation>,
    ) -> Self {
        let usecase = Arc::new(CastUsecase::new(
            Arc::clone(&facade),
            capabilities,
            backend,
            revocation,
        ));
        Self {
            facade,
            usecase,
            state: Mutex::new(RuntimeState {
                devices: Vec::new(),
                revision: 1,
                dropped_events: 0,
                last_queue_dropped: 0,
            }),
        }
    }

    pub fn discovery(
        &self,
        request_id: String,
        action: MediaHostDiscoveryAction,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        let result = match action {
            MediaHostDiscoveryAction::Start => self.facade.start_discovery(),
            MediaHostDiscoveryAction::Stop => self.facade.stop_discovery(),
            MediaHostDiscoveryAction::Refresh => self.facade.refresh_discovery(),
        };
        result.map_err(map_command_error)?;
        self.sync_devices()?;
        Ok(MediaHostMessage::Ack { request_id })
    }

    pub fn list_devices(
        &self,
        request_id: String,
        snapshot_revision: Option<u64>,
        offset: u16,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        if snapshot_revision.is_none() {
            self.sync_devices()?;
        }
        let state = lock(&self.state);
        if snapshot_revision.is_some_and(|revision| revision != state.revision) {
            return Err(MediaHostRuntimeError::StaleContext);
        }
        let offset = offset as usize;
        if (state.devices.is_empty() && offset != 0)
            || (!state.devices.is_empty() && offset >= state.devices.len())
        {
            return Err(MediaHostRuntimeError::InvalidMessage);
        }
        let end = offset
            .saturating_add(MAX_MEDIA_HOST_DEVICE_PAGE)
            .min(state.devices.len());
        let devices = state.devices[offset..end].iter().map(wire_device).collect();
        Ok(MediaHostMessage::DevicePageReply {
            request_id,
            snapshot_revision: state.revision,
            offset: offset as u16,
            next_offset: (end < state.devices.len()).then_some(end as u16),
            devices,
        })
    }

    #[must_use]
    pub fn has_device(&self, device: &DeviceId) -> bool {
        lock(&self.state)
            .devices
            .iter()
            .any(|known| known.device_id() == device)
    }

    pub async fn start_cast(
        &self,
        request_id: String,
        request: DeliveryRequest,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        let usecase = Arc::clone(&self.usecase);
        let outcome = tokio::task::spawn_blocking(move || {
            match usecase.phase() {
                CastPhase::Idle | CastPhase::Browsing => {
                    usecase.on_playback_eligible();
                    usecase.open_receiver_picker()?;
                }
                CastPhase::PlaybackEligible => usecase.open_receiver_picker()?,
                CastPhase::SelectingReceiver | CastPhase::Casting | CastPhase::Failed => {}
                CastPhase::Planning | CastPhase::Starting | CastPhase::Stopping => {
                    return Err(CastError::InvalidState);
                }
            }
            Ok(usecase.start_cast(&request))
        })
        .await
        .map_err(|_| MediaHostRuntimeError::HostUnavailable)?
        .map_err(map_command_error)?;
        let outcome = match outcome {
            CastStartOutcome::Casting(session) => {
                let route = self
                    .usecase
                    .active_route()
                    .ok_or(MediaHostRuntimeError::InvalidState)?;
                let generation = wire_counter(session.generation().get())?;
                MediaHostCastStartOutcome::Casting {
                    session_generation: generation,
                    route: wire_route(route),
                }
            }
            CastStartOutcome::HandoffSuggested(handoff) => MediaHostCastStartOutcome::Handoff {
                reason: handoff.reason(),
            },
            CastStartOutcome::Rejected(reason) => MediaHostCastStartOutcome::Rejected { reason },
            CastStartOutcome::Failed(error) => MediaHostCastStartOutcome::Failed {
                code: wire_cast_error(error),
            },
        };
        Ok(MediaHostMessage::StartCastReply {
            request_id,
            outcome,
        })
    }

    pub async fn stop_cast(
        &self,
        request_id: String,
        session_generation: u64,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        let Some(active) = self.usecase.active_session() else {
            return Ok(MediaHostMessage::Ack { request_id });
        };
        if wire_counter(active.generation().get())? != session_generation {
            return Err(MediaHostRuntimeError::StaleContext);
        }
        let usecase = Arc::clone(&self.usecase);
        tokio::task::spawn_blocking(move || usecase.stop_cast())
            .await
            .map_err(|_| MediaHostRuntimeError::HostUnavailable)?
            .map_err(map_command_error)?;
        Ok(MediaHostMessage::Ack { request_id })
    }

    pub fn poll_session_events(
        &self,
        request_id: String,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        let drained = self
            .usecase
            .drain_session_event_batch(MAX_MEDIA_HOST_SESSION_EVENTS);
        let mut state = lock(&self.state);
        let queue_delta = drained
            .cumulative_queue_dropped
            .saturating_sub(state.last_queue_dropped);
        state.last_queue_dropped = drained.cumulative_queue_dropped;
        state.dropped_events = state
            .dropped_events
            .saturating_add(queue_delta)
            .saturating_add(drained.stats.dropped_stale as u64);
        let events = drained
            .snapshots
            .iter()
            .map(wire_session_event)
            .collect::<Result<Vec<_>, _>>()?;
        Ok(MediaHostMessage::SessionEventsReply {
            request_id,
            dropped_events: state.dropped_events,
            events,
        })
    }

    pub fn on_app_exit(&self) {
        self.usecase.on_app_exit();
    }

    fn sync_devices(&self) -> Result<(), MediaHostRuntimeError> {
        let devices = self.facade.list_devices();
        validate_devices(&devices)?;
        let mut state = lock(&self.state);
        if state.devices != devices {
            state.revision = state
                .revision
                .checked_add(1)
                .ok_or(MediaHostRuntimeError::CapacityExceeded)?;
            state.devices = devices;
        }
        Ok(())
    }
}

fn validate_devices(devices: &[DiscoveredDevice]) -> Result<(), MediaHostRuntimeError> {
    if devices.len() > MAX_MEDIA_HOST_DEVICES {
        return Err(MediaHostRuntimeError::CapacityExceeded);
    }
    for (index, device) in devices.iter().enumerate() {
        let name = device.friendly_name();
        if name.is_empty()
            || name.len() > MAX_MEDIA_HOST_DEVICE_NAME_BYTES
            || name.chars().any(|character| {
                character.is_control() || (0x80..=0x9f).contains(&(character as u32))
            })
            || devices[..index]
                .iter()
                .any(|prior| prior.device_id() == device.device_id())
        {
            return Err(MediaHostRuntimeError::InvalidMessage);
        }
    }
    Ok(())
}

fn wire_device(device: &DiscoveredDevice) -> MediaHostDevice {
    MediaHostDevice {
        device_id: device.device_id().as_str().to_owned(),
        display_name: device.friendly_name().to_owned(),
        state: match device.state() {
            DeviceState::Ready => MediaHostDeviceState::Ready,
            DeviceState::Incomplete => MediaHostDeviceState::Incomplete,
            DeviceState::RequiresAuthorization => MediaHostDeviceState::RequiresAuthorization,
            DeviceState::Stale => MediaHostDeviceState::Stale,
            DeviceState::Offline => MediaHostDeviceState::Offline,
        },
        is_crayon_receiver: device.is_crayon_receiver(),
    }
}

fn wire_session_event(
    snapshot: &crayon_cast_adapter::CastSessionSnapshot,
) -> Result<MediaHostSessionEvent, MediaHostRuntimeError> {
    let generation = wire_counter(snapshot.session().generation().get())?;
    let revision = wire_counter(snapshot.state_revision())?;
    Ok(MediaHostSessionEvent {
        session_generation: generation,
        state_revision: revision,
        phase: match snapshot.phase() {
            CastSessionPhase::Starting => MediaHostSessionPhase::Starting,
            CastSessionPhase::Active => MediaHostSessionPhase::Active,
            CastSessionPhase::Suspended => MediaHostSessionPhase::Suspended,
            CastSessionPhase::Recovering => MediaHostSessionPhase::Recovering,
            CastSessionPhase::Terminating => MediaHostSessionPhase::Terminating,
            CastSessionPhase::Terminated => MediaHostSessionPhase::Terminated,
        },
        playback: match snapshot.playback() {
            CastPlaybackState::Unknown => MediaHostSessionPlayback::Unknown,
            CastPlaybackState::Preparing => MediaHostSessionPlayback::Preparing,
            CastPlaybackState::Buffering => MediaHostSessionPlayback::Buffering,
            CastPlaybackState::Playing => MediaHostSessionPlayback::Playing,
            CastPlaybackState::Paused => MediaHostSessionPlayback::Paused,
            CastPlaybackState::Ended => MediaHostSessionPlayback::Ended,
            CastPlaybackState::Stopped => MediaHostSessionPlayback::Stopped,
            CastPlaybackState::Failed => MediaHostSessionPlayback::Failed,
        },
        terminal_reason: snapshot.terminal_reason().map(wire_terminal_reason),
    })
}

fn wire_counter(internal: u64) -> Result<u64, MediaHostRuntimeError> {
    // CastFacade generations/revisions begin at zero; MHV1 reserves zero as
    // invalid. Private wire counters are the monotonic internal value plus
    // one, and Stop performs the same generation translation before compare.
    internal
        .checked_add(1)
        .ok_or(MediaHostRuntimeError::CapacityExceeded)
}

fn wire_terminal_reason(reason: CastTerminalReason) -> MediaHostTerminalReason {
    match reason {
        CastTerminalReason::StoppedBySender => MediaHostTerminalReason::StoppedBySender,
        CastTerminalReason::StoppedByReceiver => MediaHostTerminalReason::StoppedByReceiver,
        CastTerminalReason::EndedNormally => MediaHostTerminalReason::EndedNormally,
        CastTerminalReason::ReplacedByNewCast => MediaHostTerminalReason::ReplacedByNewCast,
        CastTerminalReason::ReplacedByOtherController => {
            MediaHostTerminalReason::ReplacedByOtherController
        }
        CastTerminalReason::ReceiverShutdown => MediaHostTerminalReason::ReceiverShutdown,
        CastTerminalReason::ReceiverSessionLost => MediaHostTerminalReason::ReceiverSessionLost,
        CastTerminalReason::ReceiverUnreachable => MediaHostTerminalReason::ReceiverUnreachable,
        CastTerminalReason::PlaybackFailed => MediaHostTerminalReason::PlaybackFailed,
        CastTerminalReason::SourceFailed => MediaHostTerminalReason::SourceFailed,
        CastTerminalReason::ProtocolError => MediaHostTerminalReason::ProtocolError,
    }
}

fn wire_route(route: DeliveryRoute) -> MediaHostDeliveryRoute {
    match route {
        DeliveryRoute::Direct => MediaHostDeliveryRoute::Direct,
        DeliveryRoute::Relay => MediaHostDeliveryRoute::Relay,
    }
}

fn wire_cast_error(error: CastError) -> MediaHostCastErrorCode {
    match error {
        CastError::DeviceNotFound => MediaHostCastErrorCode::DeviceNotFound,
        CastError::InvalidCastCode => MediaHostCastErrorCode::InvalidCastCode,
        CastError::InvalidInput => MediaHostCastErrorCode::InvalidInput,
        CastError::InvalidState => MediaHostCastErrorCode::InvalidState,
        CastError::NoActiveSession => MediaHostCastErrorCode::NoActiveSession,
        CastError::StaleSessionGeneration => MediaHostCastErrorCode::StaleSessionGeneration,
        CastError::CastStartFailed => MediaHostCastErrorCode::CastStartFailed,
        CastError::UnsupportedByReceiver => MediaHostCastErrorCode::UnsupportedByReceiver,
        CastError::RouteLost => MediaHostCastErrorCode::RouteLost,
        CastError::NetworkUnavailable => MediaHostCastErrorCode::NetworkUnavailable,
        CastError::ReceiverUnreachable => MediaHostCastErrorCode::ReceiverUnreachable,
        CastError::ReceiverProtocol => MediaHostCastErrorCode::ReceiverProtocol,
        CastError::Internal => MediaHostCastErrorCode::Internal,
    }
}

fn map_command_error(error: CastError) -> MediaHostRuntimeError {
    match error {
        CastError::DeviceNotFound => MediaHostRuntimeError::CandidateUnavailable,
        CastError::StaleSessionGeneration => MediaHostRuntimeError::StaleContext,
        CastError::NetworkUnavailable
        | CastError::ReceiverUnreachable
        | CastError::ReceiverProtocol
        | CastError::Internal => MediaHostRuntimeError::HostUnavailable,
        CastError::InvalidCastCode
        | CastError::InvalidInput
        | CastError::InvalidState
        | CastError::NoActiveSession
        | CastError::CastStartFailed
        | CastError::UnsupportedByReceiver
        | CastError::RouteLost => MediaHostRuntimeError::InvalidState,
    }
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|error| error.into_inner())
}
