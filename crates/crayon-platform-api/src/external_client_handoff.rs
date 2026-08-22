//! External cast-client handoff (Roadmap §2 contract).
//!
//! The handoff only ever downloads or launches the standalone cast
//! client, and only after explicit user confirmation.
//! `HandoffOutcome` deliberately has no "mirroring started" variant:
//! adapters cannot claim casting success.  Requests carry no page URL,
//! cookie, authorization data or browsing history — the only string
//! surface is the validated closed-charset purpose token.

use crate::token::validate_token;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum length of the handoff purpose token, in bytes.
const MAX_PURPOSE_LEN: usize = 32;

/// Why the browser offers the external client.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffReason {
    /// Neither Direct nor Relay casting is available for the page.
    NoRouteAvailable,
    /// The user explicitly chose the external client.
    UserChoice,
}

/// What the confirmed handoff should do.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffAction {
    /// Download the standalone client.
    DownloadClient,
    /// Launch the installed client.
    LaunchClient,
}

/// A validated handoff request.  Contains no page data.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HandoffRequest {
    reason: HandoffReason,
    action: HandoffAction,
    purpose: String,
}

impl HandoffRequest {
    /// Creates a request; `purpose` is a closed-charset token used only
    /// for diagnostics correlation.
    pub fn new(
        reason: HandoffReason,
        action: HandoffAction,
        purpose: &str,
    ) -> Result<Self, HandoffError> {
        validate_token(purpose, MAX_PURPOSE_LEN).map_err(|_| HandoffError::Unavailable)?;
        Ok(Self {
            reason,
            action,
            purpose: purpose.to_owned(),
        })
    }

    #[must_use]
    pub const fn reason(&self) -> HandoffReason {
        self.reason
    }

    #[must_use]
    pub const fn action(&self) -> HandoffAction {
        self.action
    }

    #[must_use]
    pub fn purpose(&self) -> &str {
        &self.purpose
    }
}

/// Closed handoff outcomes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffOutcome {
    /// The client download started.
    DownloadStarted,
    /// The installed client was asked to launch.
    LaunchRequested,
    /// The client is not installed; only download guidance may follow.
    NotInstalled,
    /// The user cancelled the flow.
    Cancelled,
    /// The handoff failed.
    Failed,
}

/// Handoff failure.  Variants are stable and carry no URLs or user data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffError {
    /// The flow reached the platform without the required user
    /// confirmation.
    NotConfirmed,
    /// The platform cannot perform the handoff right now.
    Unavailable,
}

impl Display for HandoffError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::NotConfirmed => "external client handoff was not confirmed by the user",
            Self::Unavailable => "external client handoff unavailable",
        };
        formatter.write_str(message)
    }
}

impl Error for HandoffError {}

/// Standalone cast-client handoff.  The browser explains first; the
/// adapter performs exactly the requested action and reports the closed
/// outcome.
pub trait ExternalClientHandoff: Send {
    /// Performs the confirmed handoff described by `request`.
    fn perform(&mut self, request: &HandoffRequest) -> Result<HandoffOutcome, HandoffError>;
}

#[cfg(test)]
#[path = "external_client_handoff_tests.rs"]
mod tests;
