//! AGT-10 cast control tool tests: confirmation fencing, external
//! handoff isolation and gate-verdict passthrough (AG-009).

use super::*;
use crate::tools::cast_read::CastPlaybackState;
use std::cell::RefCell;

fn context(state: CastPlaybackState, receiver: Option<&str>, media_generation: u64) -> CastContext {
    CastContext {
        session_state: state,
        receiver_id: receiver.map(str::to_owned),
        media_generation,
        external_client_handoff: false,
    }
}

fn confirmed(command: CastCommand, confirmed_context: &CastContext) -> ConfirmedCommand {
    ConfirmedCommand {
        command,
        confirmed_context: confirmed_context.clone(),
    }
}

/// Fixture port: the current context is mutable to simulate mid-flight
/// device/media/route changes; `execute` records commands and optionally
/// refuses through a normal-gate verdict.
struct FixturePort {
    current: RefCell<CastContext>,
    executed: RefCell<Vec<String>>,
    gate_refusal: Option<CoreError>,
}

impl FixturePort {
    fn new(current: CastContext) -> Self {
        Self {
            current: RefCell::new(current),
            executed: RefCell::new(Vec::new()),
            gate_refusal: None,
        }
    }

    fn executed(&self) -> Vec<String> {
        self.executed.borrow().clone()
    }
}

impl CastControlPort for FixturePort {
    fn current_context(&self) -> Result<CastContext, CastControlError> {
        Ok(self.current.borrow().clone())
    }

    fn execute(&self, command: &CastCommand) -> Result<(), CastControlError> {
        if let Some(refusal) = self.gate_refusal {
            return Err(CastControlError::GateRejected(refusal));
        }
        self.executed
            .borrow_mut()
            .push(command.wire_name().to_owned());
        Ok(())
    }
}

#[test]
fn command_wire_names_are_closed() {
    let commands = [
        CastCommand::SelectReceiver {
            receiver_id: "device-a".to_owned(),
        },
        CastCommand::Start,
        CastCommand::Pause,
        CastCommand::Seek { position_ms: 1_500 },
        CastCommand::Stop,
    ];
    let wires: Vec<&str> = commands.iter().map(|command| command.wire_name()).collect();
    assert_eq!(
        wires,
        vec!["select_receiver", "start", "pause", "seek", "stop"]
    );
}

#[test]
fn matching_context_executes_through_the_port() {
    let context = context(CastPlaybackState::Playing, Some("device-a"), 3);
    let port = FixturePort::new(context.clone());
    execute_confirmed(&port, &confirmed(CastCommand::Pause, &context)).expect("executes");
    execute_confirmed(
        &port,
        &confirmed(
            CastCommand::Seek {
                position_ms: 42_000,
            },
            &context,
        ),
    )
    .expect("executes");
    assert_eq!(port.executed(), vec!["pause".to_owned(), "seek".to_owned()]);
}

/// AG-009 core property: any change of receiver, media generation or
/// session state after confirmation forces re-confirmation.
#[test]
fn context_changes_require_reconfirmation() {
    let confirmed_context = context(CastPlaybackState::Playing, Some("device-a"), 3);
    // Receiver changed (user switched device in the UI).
    let port = FixturePort::new(context(CastPlaybackState::Playing, Some("device-b"), 3));
    assert_eq!(
        execute_confirmed(&port, &confirmed(CastCommand::Pause, &confirmed_context)),
        Err(CastControlError::ContextStale)
    );
    // Media generation advanced (route/identity changed mid-session).
    let port = FixturePort::new(context(CastPlaybackState::Playing, Some("device-a"), 4));
    assert_eq!(
        execute_confirmed(
            &port,
            &confirmed(CastCommand::Seek { position_ms: 5 }, &confirmed_context)
        ),
        Err(CastControlError::ContextStale)
    );
    // Session state moved on (playback ended by itself).
    let port = FixturePort::new(context(CastPlaybackState::Stopped, Some("device-a"), 3));
    assert_eq!(
        execute_confirmed(&port, &confirmed(CastCommand::Pause, &confirmed_context)),
        Err(CastControlError::ContextStale)
    );
    // Nothing reached the port through any stale path.
    assert!(port.executed().is_empty());
}

#[test]
fn external_client_handoff_is_never_controllable() {
    let handoff_context = CastContext {
        session_state: CastPlaybackState::Playing,
        receiver_id: Some("client-1".to_owned()),
        media_generation: 2,
        external_client_handoff: true,
    };
    let port = FixturePort::new(handoff_context.clone());
    for command in [
        CastCommand::Pause,
        CastCommand::Stop,
        CastCommand::Seek { position_ms: 0 },
        CastCommand::Start,
        CastCommand::SelectReceiver {
            receiver_id: "device-a".to_owned(),
        },
    ] {
        assert_eq!(
            execute_confirmed(&port, &confirmed(command, &handoff_context)),
            Err(CastControlError::ExternalClientNotControllable)
        );
    }
    assert!(port.executed().is_empty());
}

#[test]
fn session_commands_need_an_active_session() {
    let idle = context(CastPlaybackState::Idle, None, 0);
    for command in [
        CastCommand::Start,
        CastCommand::Pause,
        CastCommand::Seek { position_ms: 1 },
        CastCommand::Stop,
    ] {
        let port = FixturePort::new(idle.clone());
        assert_eq!(
            execute_confirmed(&port, &confirmed(command, &idle)),
            Err(CastControlError::NoSession),
            "no-session commands must be refused"
        );
        assert!(port.executed().is_empty());
    }
    // select_receiver is exactly the command that establishes a session.
    let port = FixturePort::new(idle);
    execute_confirmed(
        &port,
        &confirmed(
            CastCommand::SelectReceiver {
                receiver_id: "device-a".to_owned(),
            },
            &context(CastPlaybackState::Idle, None, 0),
        ),
    )
    .expect("select works from idle");
    assert_eq!(port.executed(), vec!["select_receiver".to_owned()]);
}

#[test]
fn invalid_receiver_is_refused_before_the_port() {
    let idle = context(CastPlaybackState::Idle, None, 0);
    let port = FixturePort::new(idle.clone());
    for bad in [
        "",
        "bad receiver",
        "bad\nreceiver",
        &"x".repeat(MAX_RECEIVER_ID_LEN + 1),
    ] {
        assert_eq!(
            execute_confirmed(
                &port,
                &confirmed(
                    CastCommand::SelectReceiver {
                        receiver_id: bad.to_owned()
                    },
                    &idle,
                ),
            ),
            Err(CastControlError::InvalidReceiver),
            "receiver {bad:?} must be refused"
        );
    }
    assert!(port.executed().is_empty());
}

/// The normal cast gates stay authoritative: their verdicts pass through
/// verbatim as GateRejected with the stable domain code.
#[test]
fn gate_verdicts_are_forwarded_verbatim() {
    let playing = context(CastPlaybackState::Playing, Some("device-a"), 9);
    for refusal in [
        CoreError::DrmProtected,
        CoreError::PolicyDenied,
        CoreError::ReceiverIncompatible,
        CoreError::MissingUserActivation,
    ] {
        let mut port = FixturePort::new(playing.clone());
        port.gate_refusal = Some(refusal);
        assert_eq!(
            execute_confirmed(&port, &confirmed(CastCommand::Start, &playing)),
            Err(CastControlError::GateRejected(refusal))
        );
        assert_eq!(
            CastControlError::GateRejected(refusal).to_caap_error(),
            CaapError::CapabilityDenied
        );
    }
}

#[test]
fn error_mapping_is_stable() {
    assert_eq!(
        CastControlError::SourceUnavailable.to_caap_error(),
        CaapError::CapabilityDenied
    );
    assert_eq!(
        CastControlError::NoSession.to_caap_error(),
        CaapError::TargetInvalid
    );
    assert_eq!(
        CastControlError::InvalidReceiver.to_caap_error(),
        CaapError::TargetInvalid
    );
    assert_eq!(
        CastControlError::ContextStale.to_caap_error(),
        CaapError::TargetStale
    );
    assert_eq!(
        CastControlError::ExternalClientNotControllable.to_caap_error(),
        CaapError::CapabilityDenied
    );
}
