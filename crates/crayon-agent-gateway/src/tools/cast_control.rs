//! R3 cast control tools (AGT-10): `cast.select_receiver`, `cast.start`,
//! `cast.pause`, `cast.seek` and `cast.stop`.
//!
//! Every command is risk-R3: execution requires a user confirmation
//! bound to an exact [`CastContext`] fingerprint.  If the receiver,
//! media generation or session state changed after the confirmation was
//! captured, execution is refused with [`CastControlError::ContextStale`]
//! and the caller must present and confirm again — there is no bypass.
//!
//! The normal playback/DRM/ad/policy gates stay owned by app-runtime:
//! this layer forwards their verdicts verbatim inside
//! [`CastControlError::GateRejected`] and never re-interprets them.
//! Sessions served by the external client handoff are not controllable.

use crate::tools::cast_read::{valid_id, CastPlaybackState};
use crayon_domain::{CaapError, CoreError};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum receiver id length in bytes (same bound as the read side).
pub const MAX_RECEIVER_ID_LEN: usize = 128;

/// Closed R3 cast commands.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CastCommand {
    /// Choose the target receiver; establishes the session context.
    SelectReceiver { receiver_id: String },
    /// Start casting on the selected receiver.
    Start,
    /// Pause playback.
    Pause,
    /// Seek to an absolute position in milliseconds.
    Seek { position_ms: u64 },
    /// Stop and tear down the session.
    Stop,
}

impl CastCommand {
    /// Stable wire name used by snapshots and diagnostics.
    #[must_use]
    pub fn wire_name(&self) -> &str {
        match self {
            Self::SelectReceiver { .. } => "select_receiver",
            Self::Start => "start",
            Self::Pause => "pause",
            Self::Seek { .. } => "seek",
            Self::Stop => "stop",
        }
    }
}

/// The control-relevant context snapshot used both for confirmation
/// fencing and for pre-execution validation.  `media_generation` advances
/// whenever the routed media identity changes.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CastContext {
    pub session_state: CastPlaybackState,
    pub receiver_id: Option<String>,
    pub media_generation: u64,
    /// True while the route is an external client handoff: those sessions
    /// belong to a separate client process and are never controllable.
    pub external_client_handoff: bool,
}

/// A command bound to the exact context the user confirmed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConfirmedCommand {
    pub command: CastCommand,
    pub confirmed_context: CastContext,
}

/// Port over the runtime's controlled cast execution.  Implementations
/// enforce the normal playback/DRM/ad/policy gates and return their
/// verdicts; this layer adds no judgment of its own.
pub trait CastControlPort {
    /// The current control context.
    fn current_context(&self) -> Result<CastContext, CastControlError>;
    /// Executes an already fence-checked command through the normal
    /// use cases.
    fn execute(&self, command: &CastCommand) -> Result<(), CastControlError>;
}

/// Control failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CastControlError {
    /// The runtime cannot answer right now.
    SourceUnavailable,
    /// The command needs an active session but none exists.
    NoSession,
    /// Context changed after confirmation; re-present and re-confirm.
    ContextStale,
    /// External client handoff sessions are not controllable.
    ExternalClientNotControllable,
    /// The receiver id violates closed shape or bounds.
    InvalidReceiver,
    /// The normal cast gates refused the command; the inner code is the
    /// stable domain verdict (DRM/policy/receiver/...).
    GateRejected(CoreError),
}

impl CastControlError {
    /// Stable mapping into CAAP error codes: context changes surface as
    /// stale targets; gate refusals and uncontrollable external sessions
    /// mean the capability cannot be exercised.
    #[must_use]
    pub const fn to_caap_error(self) -> CaapError {
        match self {
            Self::SourceUnavailable => CaapError::CapabilityDenied,
            Self::NoSession | Self::InvalidReceiver => CaapError::TargetInvalid,
            Self::ContextStale => CaapError::TargetStale,
            Self::ExternalClientNotControllable | Self::GateRejected(_) => {
                CaapError::CapabilityDenied
            }
        }
    }
}

impl Display for CastControlError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::SourceUnavailable => "cast control source is unavailable",
            Self::NoSession => "no active cast session for this command",
            Self::ContextStale => "context changed after confirmation",
            Self::ExternalClientNotControllable => {
                "external client handoff sessions are not controllable"
            }
            Self::InvalidReceiver => "receiver id violates shape or bounds",
            Self::GateRejected(error) => return write!(formatter, "cast gate rejected: {error}"),
        };
        formatter.write_str(message)
    }
}

impl Error for CastControlError {}

/// Fence-checks and executes an R3 command.  Order of checks is stable:
/// shape → external handoff → session presence → confirmation fencing;
/// only after all pass does the port execute under the normal gates.
pub fn execute_confirmed(
    port: &dyn CastControlPort,
    confirmed: &ConfirmedCommand,
) -> Result<(), CastControlError> {
    if let CastCommand::SelectReceiver { receiver_id } = &confirmed.command {
        if !valid_id(receiver_id, MAX_RECEIVER_ID_LEN) {
            return Err(CastControlError::InvalidReceiver);
        }
    }
    let current = port.current_context()?;
    if current.external_client_handoff {
        return Err(CastControlError::ExternalClientNotControllable);
    }
    let needs_session = !matches!(confirmed.command, CastCommand::SelectReceiver { .. });
    if needs_session && current.session_state == CastPlaybackState::Idle {
        return Err(CastControlError::NoSession);
    }
    if current != confirmed.confirmed_context {
        return Err(CastControlError::ContextStale);
    }
    port.execute(&confirmed.command)
}

#[cfg(test)]
#[path = "cast_control_tests.rs"]
mod tests;
