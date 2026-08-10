//! `FakeReceiver` / `FakeCastFacade`: scripted receiver implementing the
//! Cast-SDK facade behaviour contract (architecture §4) without any network:
//! discovery, connect, playback control, stale-generation discard, route lost
//! and failure injection. Every interaction is recorded for assertions.

use crayon_domain::{DeviceId, ReceiverCapabilities, SessionGeneration, SessionId};
use std::sync::Mutex;

/// Receiver playback state as reported through the facade.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ReceiverPlaybackState {
    Idle,
    Connected,
    Playing,
    Paused,
    Stopped,
}

/// A facade interaction recorded in call order.
#[derive(Clone, Debug, PartialEq)]
pub enum FacadeCall {
    StartDiscovery,
    StopDiscovery,
    ListDevices,
    Connect(DeviceId),
    Disconnect(DeviceId),
    Play {
        session: SessionId,
        generation: SessionGeneration,
    },
    Pause(SessionId),
    Resume(SessionId),
    Seek {
        session: SessionId,
        position_seconds: f64,
    },
    SetVolume {
        session: SessionId,
        volume: u8,
    },
    Stop(SessionId),
}

/// Events a test can inject to simulate receiver-side changes.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ReceiverEvent {
    RouteLost(DeviceId),
    ReceiverStopped(SessionId),
}

/// Stable facade failure modes (no natural-language matching allowed).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FacadeError {
    Unsupported,
    Protocol,
    Permission,
    DeviceUnreachable,
    StaleGeneration,
}

/// A fake receiver device with fixed identity and capabilities.
#[derive(Clone)]
pub struct FakeReceiver {
    id: DeviceId,
    name: String,
    capabilities: ReceiverCapabilities,
}

impl FakeReceiver {
    #[must_use]
    pub fn new(id: &str, name: &str, capabilities: ReceiverCapabilities) -> Self {
        Self {
            id: DeviceId::new(id).expect("test device id must be valid"),
            name: name.to_string(),
            capabilities,
        }
    }

    #[must_use]
    pub fn id(&self) -> &DeviceId {
        &self.id
    }

    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }

    #[must_use]
    pub const fn capabilities(&self) -> ReceiverCapabilities {
        self.capabilities
    }
}

struct FacadeState {
    discovering: bool,
    connected: Option<DeviceId>,
    playback: ReceiverPlaybackState,
    current_generation: Option<SessionGeneration>,
    calls: Vec<FacadeCall>,
    events: Vec<ReceiverEvent>,
    fail_next_connect: Option<FacadeError>,
}

/// Maximum recorded calls/events (bounded buffers rule).
const MAX_RECORDED: usize = 512;

/// Scripted Cast-SDK facade double. Defaults: one discovered device, connect
/// succeeds, playback controls succeed while a session is current.
pub struct FakeCastFacade {
    receivers: Vec<FakeReceiver>,
    state: Mutex<FacadeState>,
}

impl FakeCastFacade {
    #[must_use]
    pub fn new(receivers: Vec<FakeReceiver>) -> Self {
        Self {
            receivers,
            state: Mutex::new(FacadeState {
                discovering: false,
                connected: None,
                playback: ReceiverPlaybackState::Idle,
                current_generation: None,
                calls: Vec::new(),
                events: Vec::new(),
                fail_next_connect: None,
            }),
        }
    }

    pub fn start_discovery(&self) {
        let mut state = self.state.lock().unwrap();
        state.discovering = true;
        Self::record(&mut state, FacadeCall::StartDiscovery);
    }

    pub fn stop_discovery(&self) {
        let mut state = self.state.lock().unwrap();
        state.discovering = false;
        Self::record(&mut state, FacadeCall::StopDiscovery);
    }

    /// Devices are visible only while discovery is running (facade contract).
    #[must_use]
    pub fn list_devices(&self) -> Vec<FakeReceiver> {
        let mut state = self.state.lock().unwrap();
        Self::record(&mut state, FacadeCall::ListDevices);
        if state.discovering {
            self.receivers.clone()
        } else {
            Vec::new()
        }
    }

    pub fn connect_device(&self, id: &DeviceId) -> Result<(), FacadeError> {
        let mut state = self.state.lock().unwrap();
        Self::record(&mut state, FacadeCall::Connect(id.clone()));
        if let Some(error) = state.fail_next_connect.take() {
            return Err(error);
        }
        if !self.receivers.iter().any(|r| r.id() == id) {
            return Err(FacadeError::DeviceUnreachable);
        }
        state.connected = Some(id.clone());
        state.playback = ReceiverPlaybackState::Connected;
        Ok(())
    }

    pub fn disconnect_device(&self, id: &DeviceId) {
        let mut state = self.state.lock().unwrap();
        Self::record(&mut state, FacadeCall::Disconnect(id.clone()));
        if state.connected.as_ref() == Some(id) {
            state.connected = None;
            state.playback = ReceiverPlaybackState::Idle;
        }
    }

    /// Starts playback for `session`; an older generation is discarded as
    /// stale (state-machine rule) and reported, never applied.
    pub fn play(
        &self,
        session: SessionId,
        generation: SessionGeneration,
    ) -> Result<(), FacadeError> {
        let mut state = self.state.lock().unwrap();
        if let Some(current) = state.current_generation {
            if generation < current {
                return Err(FacadeError::StaleGeneration);
            }
        }
        state.current_generation = Some(generation);
        state.playback = ReceiverPlaybackState::Playing;
        Self::record(
            &mut state,
            FacadeCall::Play {
                session,
                generation,
            },
        );
        Ok(())
    }

    pub fn pause(&self, session: &SessionId) {
        let mut state = self.state.lock().unwrap();
        state.playback = ReceiverPlaybackState::Paused;
        Self::record(&mut state, FacadeCall::Pause(session.clone()));
    }

    pub fn resume(&self, session: &SessionId) {
        let mut state = self.state.lock().unwrap();
        state.playback = ReceiverPlaybackState::Playing;
        Self::record(&mut state, FacadeCall::Resume(session.clone()));
    }

    pub fn seek(&self, session: &SessionId, position_seconds: f64) {
        let mut state = self.state.lock().unwrap();
        Self::record(
            &mut state,
            FacadeCall::Seek {
                session: session.clone(),
                position_seconds,
            },
        );
    }

    pub fn set_volume(&self, session: &SessionId, volume: u8) {
        let mut state = self.state.lock().unwrap();
        Self::record(
            &mut state,
            FacadeCall::SetVolume {
                session: session.clone(),
                volume,
            },
        );
    }

    pub fn stop(&self, session: &SessionId) {
        let mut state = self.state.lock().unwrap();
        state.playback = ReceiverPlaybackState::Stopped;
        Self::record(&mut state, FacadeCall::Stop(session.clone()));
    }

    // ---- test controls ----

    /// Makes the next `connect_device` fail with the given stable error.
    pub fn fail_next_connect(&self, error: FacadeError) {
        self.state.lock().unwrap().fail_next_connect = Some(error);
    }

    /// Injects a receiver-side event (route lost / receiver stop).
    pub fn inject_event(&self, event: ReceiverEvent) {
        let mut state = self.state.lock().unwrap();
        if state.events.len() < MAX_RECORDED {
            state.events.push(event);
        }
    }

    #[must_use]
    pub fn playback_state(&self) -> ReceiverPlaybackState {
        self.state.lock().unwrap().playback
    }

    /// Recorded facade calls in order (bounded).
    #[must_use]
    pub fn calls(&self) -> Vec<FacadeCall> {
        self.state.lock().unwrap().calls.clone()
    }

    /// Injected events in order (bounded).
    #[must_use]
    pub fn events(&self) -> Vec<ReceiverEvent> {
        self.state.lock().unwrap().events.clone()
    }

    fn record(state: &mut FacadeState, call: FacadeCall) {
        if state.calls.len() < MAX_RECORDED {
            state.calls.push(call);
        }
    }
}
