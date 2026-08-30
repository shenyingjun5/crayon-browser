//! Challenge session schema and state machine (WFL-01).
//!
//! The session records detection evidence and phases only. There is no
//! solving surface: no solution fields, no third-party services, no
//! automatic interaction. A challenge is always resolved by the human;
//! resume requires a fresh read and fresh authorization downstream.

use crate::semantic::{is_valid_origin, SemanticSchemaError};
use serde::{Deserialize, Serialize};

/// Maximum bytes of one bounded evidence note (never page content).
pub const MAX_CHALLENGE_EVIDENCE_BYTES: usize = 128;

/// Closed challenge kinds. `Unknown` exists so undetectable challenges
/// still pause honestly instead of being classified wrongly.
#[derive(Clone, Copy, Debug, Default, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChallengeKind {
    Captcha,
    LoginRequired,
    RiskCheck,
    #[default]
    Unknown,
}

/// Closed challenge phases.
#[derive(Clone, Copy, Debug, Default, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChallengePhase {
    /// Deterministic detector raised evidence.
    #[default]
    Detected,
    /// Paused for the human; no automation runs.
    AwaitingHuman,
    /// The human completed the challenge and the task may re-verify.
    Resumed,
    /// The user cancelled the task.
    Cancelled,
    /// The session lapsed before the human responded.
    Expired,
}

/// Illegal phase transition; stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ChallengeTransitionError {
    /// The transition is not part of the closed set.
    IllegalTransition,
    /// The session already ended.
    SessionClosed,
    /// The evidence note exceeds its bound.
    EvidenceTooLong,
}

impl std::fmt::Display for ChallengeTransitionError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::IllegalTransition => formatter.write_str("phase transition is not allowed"),
            Self::SessionClosed => formatter.write_str("challenge session already ended"),
            Self::EvidenceTooLong => formatter.write_str("evidence note exceeds the bound"),
        }
    }
}

impl std::error::Error for ChallengeTransitionError {}

/// Bounded detection evidence. `note` is a data-free classification hint;
/// the schema has no place for page content or solutions.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChallengeEvidence {
    pub kind: ChallengeKind,
    /// Validated `http(s)` origin the challenge was detected on.
    pub origin: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub note: Option<String>,
}

impl ChallengeEvidence {
    /// Validates origin and evidence bound; wraps evidence.
    pub fn new(
        kind: ChallengeKind,
        origin: String,
        note: Option<String>,
    ) -> Result<Self, SemanticSchemaError> {
        if !is_valid_origin(&origin) {
            return Err(SemanticSchemaError::OriginInvalid);
        }
        if let Some(note) = &note {
            if note.len() > MAX_CHALLENGE_EVIDENCE_BYTES {
                return Err(SemanticSchemaError::BoundExceeded("challenge evidence"));
            }
        }
        Ok(Self { kind, origin, note })
    }
}

/// Single-owner challenge session state machine. Transitions are closed:
/// `Detected -> AwaitingHuman`, then one terminal of
/// `Resumed | Cancelled | Expired`. No transition returns to automation.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChallengeSession {
    pub evidence: ChallengeEvidence,
    pub phase: ChallengePhase,
}

impl ChallengeSession {
    /// Opens a session from fresh detection evidence.
    pub fn detect(evidence: ChallengeEvidence) -> Result<Self, ChallengeTransitionError> {
        if let Some(note) = &evidence.note {
            if note.len() > MAX_CHALLENGE_EVIDENCE_BYTES {
                return Err(ChallengeTransitionError::EvidenceTooLong);
            }
        }
        Ok(Self {
            evidence,
            phase: ChallengePhase::Detected,
        })
    }

    /// Pauses for the human; the only path out of `Detected`.
    pub fn await_human(&mut self) -> Result<(), ChallengeTransitionError> {
        if self.closed() {
            return Err(ChallengeTransitionError::SessionClosed);
        }
        if self.phase != ChallengePhase::Detected {
            return Err(ChallengeTransitionError::IllegalTransition);
        }
        self.phase = ChallengePhase::AwaitingHuman;
        Ok(())
    }

    /// Marks the human as done; allowed only from `AwaitingHuman`.
    pub fn resume(&mut self) -> Result<(), ChallengeTransitionError> {
        if self.closed() {
            return Err(ChallengeTransitionError::SessionClosed);
        }
        if self.phase != ChallengePhase::AwaitingHuman {
            return Err(ChallengeTransitionError::IllegalTransition);
        }
        self.phase = ChallengePhase::Resumed;
        Ok(())
    }

    /// User cancellation; allowed only from `AwaitingHuman`.
    pub fn cancel(&mut self) -> Result<(), ChallengeTransitionError> {
        if self.closed() {
            return Err(ChallengeTransitionError::SessionClosed);
        }
        if self.phase != ChallengePhase::AwaitingHuman {
            return Err(ChallengeTransitionError::IllegalTransition);
        }
        self.phase = ChallengePhase::Cancelled;
        Ok(())
    }

    /// Lapse before human response; allowed only from `AwaitingHuman`.
    pub fn expire(&mut self) -> Result<(), ChallengeTransitionError> {
        if self.closed() {
            return Err(ChallengeTransitionError::SessionClosed);
        }
        if self.phase != ChallengePhase::AwaitingHuman {
            return Err(ChallengeTransitionError::IllegalTransition);
        }
        self.phase = ChallengePhase::Expired;
        Ok(())
    }

    /// Whether the session reached a terminal phase.
    #[must_use]
    pub const fn closed(&self) -> bool {
        matches!(
            self.phase,
            ChallengePhase::Resumed | ChallengePhase::Cancelled | ChallengePhase::Expired
        )
    }
}
