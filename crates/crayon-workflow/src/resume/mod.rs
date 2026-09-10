//! Deterministic post-human resume gate (WFL-05).
//!
//! After the handoff reports `ResumeRequested`, automation must not simply
//! continue. This module re-validates the world from fresh, closed facts
//! supplied by trusted adapters — a re-taken snapshot match, a re-run of the
//! challenge detector, the freshness of a newly issued grant and the
//! precondition/effect knowledge — and either approves re-verification or
//! terminates the task. It never executes steps, never issues grants and
//! never sees page content, selectors or challenge values.

use crate::handoff::HandoffOutcome;
use crayon_domain::{is_valid_origin, ChallengeSession, ChallengeTransitionError};

/// Fresh-snapshot page agreement against the pre-challenge precondition.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PageMatch {
    /// The re-taken snapshot still satisfies the recorded precondition.
    Match,
    /// The page moved on; the recorded steps no longer apply.
    Drifted,
    /// The adapter could not produce a trustworthy comparison.
    Unknown,
}

/// Whether the authorized effect chain is still explainable after the pause.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EffectKnowledge {
    /// Every pending step still has a verified, bounded effect mapping.
    Known,
    /// Side effects can no longer be attributed; continuing is unsafe.
    Unknown,
}

/// Freshness of the grant that a resume would rely on (AGT-04 semantics).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GrantFreshness {
    /// A new grant was issued for the same target after the pause.
    Fresh,
    Expired,
    Revoked,
    /// The authorization target changed (tab/navigation/generation).
    TargetChanged,
    Missing,
}

/// Closed, data-free resume facts. Every field must be measured after the
/// human finished; stale cached facts are represented by their Unknown/
/// Missing variants and fail closed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ResumeFacts {
    /// Origin the task was authorized on (validated `http(s)`).
    pub expected_origin: String,
    /// Origin measured by the re-taken snapshot.
    pub current_origin: String,
    /// Fresh detector run still reports a challenge on the current page.
    pub challenge_still_present: bool,
    pub page_match: PageMatch,
    pub effect_knowledge: EffectKnowledge,
    pub grant: GrantFreshness,
}

/// Closed terminal outcomes of the gate.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerminationReason {
    /// A fresh detector run still sees the challenge.
    ChallengeStillPresent,
    /// The page drifted away from the recorded precondition.
    PageDrift,
    /// The current origin differs from the authorized one.
    OriginChanged,
    /// No fresh, target-matching grant exists.
    GrantInvalid,
    /// Pending side effects can no longer be attributed.
    UnknownSideEffect,
}

/// The gate decision. Approval never executes anything — it only permits the
/// downstream re-verification (fresh snapshot read, fresh grant bind) to run.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ResumeDecision {
    Approved,
    Terminated(TerminationReason),
}

/// Closed resume-gate failure; carries no origin or page content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ResumeError {
    /// An origin failed domain validation.
    OriginInvalid,
    /// The challenge session is not awaiting the human.
    SessionNotAwaiting,
    /// The session state machine rejected the resulting transition.
    SessionTransition(ChallengeTransitionError),
}

impl std::fmt::Display for ResumeError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::OriginInvalid => formatter.write_str("resume origin rejected"),
            Self::SessionNotAwaiting => {
                formatter.write_str("challenge session is not awaiting human")
            }
            Self::SessionTransition(error) => error.fmt(formatter),
        }
    }
}

impl std::error::Error for ResumeError {}

impl From<ChallengeTransitionError> for ResumeError {
    fn from(error: ChallengeTransitionError) -> Self {
        Self::SessionTransition(error)
    }
}

/// Single-owner resume gate. The first evaluation decides; repeated
/// evaluations replay the same decision without touching the session again.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ResumeGate {
    decision: Option<ResumeDecision>,
}

impl ResumeGate {
    #[must_use]
    pub const fn new() -> Self {
        Self { decision: None }
    }

    /// Whether the gate already reached its idempotent verdict.
    #[must_use]
    pub const fn decided(&self) -> bool {
        self.decision.is_some()
    }

    /// The recorded verdict, if any.
    #[must_use]
    pub const fn decision(&self) -> Option<ResumeDecision> {
        self.decision
    }

    /// Evaluates fresh facts and advances the session to the matching
    /// terminal phase. Idempotent: after the first verdict the same decision
    /// is returned and the (already terminal) session is left untouched.
    pub fn evaluate(
        &mut self,
        session: &mut ChallengeSession,
        facts: &ResumeFacts,
    ) -> Result<ResumeDecision, ResumeError> {
        if let Some(decision) = self.decision {
            return Ok(decision);
        }
        let decision = Self::decide(facts)?;
        // Only an `AwaitingHuman` session may consume a verdict; the gate
        // rejects the evaluation without recording one otherwise.
        if session.phase != crayon_domain::ChallengePhase::AwaitingHuman {
            return Err(ResumeError::SessionNotAwaiting);
        }
        match decision {
            ResumeDecision::Approved => session.resume()?,
            ResumeDecision::Terminated(_) => session.cancel()?,
        }
        self.decision = Some(decision);
        Ok(decision)
    }

    /// Fixed fail-closed order; the first failing check wins.
    fn decide(facts: &ResumeFacts) -> Result<ResumeDecision, ResumeError> {
        if !is_valid_origin(&facts.expected_origin) || !is_valid_origin(&facts.current_origin) {
            return Err(ResumeError::OriginInvalid);
        }
        if facts.challenge_still_present {
            return Ok(ResumeDecision::Terminated(
                TerminationReason::ChallengeStillPresent,
            ));
        }
        if facts.expected_origin != facts.current_origin {
            return Ok(ResumeDecision::Terminated(TerminationReason::OriginChanged));
        }
        match facts.page_match {
            PageMatch::Match => {}
            PageMatch::Drifted => {
                return Ok(ResumeDecision::Terminated(TerminationReason::PageDrift))
            }
            PageMatch::Unknown => {
                return Ok(ResumeDecision::Terminated(TerminationReason::PageDrift))
            }
        }
        if facts.grant != GrantFreshness::Fresh {
            return Ok(ResumeDecision::Terminated(TerminationReason::GrantInvalid));
        }
        if facts.effect_knowledge != EffectKnowledge::Known {
            return Ok(ResumeDecision::Terminated(
                TerminationReason::UnknownSideEffect,
            ));
        }
        Ok(ResumeDecision::Approved)
    }
}

impl Default for ResumeGate {
    fn default() -> Self {
        Self::new()
    }
}

/// Convenience used by the handoff owner: whether an outcome still requires
/// this gate. `ResumeRequested` is the only outcome that does.
#[must_use]
pub const fn requires_resume_gate(outcome: HandoffOutcome) -> bool {
    matches!(outcome, HandoffOutcome::ResumeRequested)
}

#[cfg(test)]
mod resume_tests;
