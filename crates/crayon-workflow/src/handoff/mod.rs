//! Human handoff lifecycle for detected challenges (WFL-03).
//!
//! This controller is the sole owner of the `AwaitingHuman` transition. It
//! exposes presentation facts and terminal intent only; it cannot solve a
//! challenge or resume browser automation.

use crayon_domain::{
    ChallengeEvidence, ChallengeKind, ChallengeSession, SessionGeneration, TabId,
    MAX_CHECKPOINT_TTL_MS,
};

/// Closed terminal outcomes. `ResumeRequested` requires WFL-05 revalidation;
/// it does not authorize or execute any browser action.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffOutcome {
    AwaitingHuman,
    ResumeRequested,
    Cancelled,
    NavigationInvalidated,
    TabClosed,
    Expired,
}

/// Stable presentation reason, mapped to locale keys by the desktop UI.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffReason {
    Captcha,
    LoginRequired,
    RiskCheck,
    Unknown,
}

impl HandoffReason {
    /// Locale key for the visible and accessible challenge reason.
    #[must_use]
    pub const fn locale_key(self) -> &'static str {
        match self {
            Self::Captcha => "workflow.handoff.reason.captcha",
            Self::LoginRequired => "workflow.handoff.reason.login_required",
            Self::RiskCheck => "workflow.handoff.reason.risk_check",
            Self::Unknown => "workflow.handoff.reason.unknown",
        }
    }
}

/// Data-free view snapshot. No evidence note or page content crosses the UI
/// boundary.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HandoffView {
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub origin: String,
    pub reason: HandoffReason,
    pub remaining_ms: u64,
    pub outcome: HandoffOutcome,
    pub automation_allowed: bool,
}

/// Creation failure with no caller-controlled content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandoffError {
    TtlOutOfBounds,
    ChallengeStateInvalid,
}

impl std::fmt::Display for HandoffError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::TtlOutOfBounds => formatter.write_str("handoff TTL is out of bounds"),
            Self::ChallengeStateInvalid => formatter.write_str("challenge state is invalid"),
        }
    }
}

impl std::error::Error for HandoffError {}

/// Single-owner human handoff controller.
pub struct HandoffController {
    session: ChallengeSession,
    tab_id: TabId,
    generation: SessionGeneration,
    expires_at_ms: u64,
    outcome: HandoffOutcome,
}

impl HandoffController {
    /// Opens a paused handoff with an injected-clock TTL.
    pub fn open(
        evidence: ChallengeEvidence,
        tab_id: TabId,
        generation: SessionGeneration,
        now_ms: u64,
        expires_at_ms: u64,
    ) -> Result<Self, HandoffError> {
        if expires_at_ms <= now_ms || expires_at_ms - now_ms > MAX_CHECKPOINT_TTL_MS {
            return Err(HandoffError::TtlOutOfBounds);
        }
        let mut session =
            ChallengeSession::detect(evidence).map_err(|_| HandoffError::ChallengeStateInvalid)?;
        session
            .await_human()
            .map_err(|_| HandoffError::ChallengeStateInvalid)?;
        Ok(Self {
            session,
            tab_id,
            generation,
            expires_at_ms,
            outcome: HandoffOutcome::AwaitingHuman,
        })
    }

    /// Returns the bounded presentation snapshot and expires at the exact TTL
    /// boundary before exposing state.
    pub fn view(&mut self, now_ms: u64) -> HandoffView {
        self.expire_if_due(now_ms);
        HandoffView {
            tab_id: self.tab_id.clone(),
            generation: self.generation,
            origin: self.session.evidence.origin.clone(),
            reason: reason_for(self.session.evidence.kind),
            remaining_ms: self.expires_at_ms.saturating_sub(now_ms),
            outcome: self.outcome,
            automation_allowed: false,
        }
    }

    /// User finished the visible challenge. This closes the handoff and asks
    /// WFL-05 to perform a fresh snapshot/risk/grant/precondition cycle.
    pub fn continue_after_human(&mut self, now_ms: u64) -> HandoffOutcome {
        if self.expire_if_due(now_ms) || self.outcome != HandoffOutcome::AwaitingHuman {
            return self.outcome;
        }
        if self.session.resume().is_ok() {
            self.outcome = HandoffOutcome::ResumeRequested;
        }
        self.outcome
    }

    /// User explicitly cancels the paused task.
    pub fn cancel(&mut self, now_ms: u64) -> HandoffOutcome {
        self.finish(now_ms, HandoffOutcome::Cancelled)
    }

    /// Any navigation invalidates the paused context and terminates it.
    pub fn on_navigation(&mut self, now_ms: u64) -> HandoffOutcome {
        self.finish(now_ms, HandoffOutcome::NavigationInvalidated)
    }

    /// Closing the owning tab terminates the paused context.
    pub fn on_tab_closed(&mut self, now_ms: u64) -> HandoffOutcome {
        self.finish(now_ms, HandoffOutcome::TabClosed)
    }

    fn finish(&mut self, now_ms: u64, terminal: HandoffOutcome) -> HandoffOutcome {
        if self.expire_if_due(now_ms) || self.outcome != HandoffOutcome::AwaitingHuman {
            return self.outcome;
        }
        if self.session.cancel().is_ok() {
            self.outcome = terminal;
        }
        self.outcome
    }

    fn expire_if_due(&mut self, now_ms: u64) -> bool {
        if self.outcome == HandoffOutcome::AwaitingHuman
            && now_ms >= self.expires_at_ms
            && self.session.expire().is_ok()
        {
            self.outcome = HandoffOutcome::Expired;
        }
        self.outcome == HandoffOutcome::Expired
    }
}

const fn reason_for(kind: ChallengeKind) -> HandoffReason {
    match kind {
        ChallengeKind::Captcha => HandoffReason::Captcha,
        ChallengeKind::LoginRequired => HandoffReason::LoginRequired,
        ChallengeKind::RiskCheck => HandoffReason::RiskCheck,
        ChallengeKind::Unknown => HandoffReason::Unknown,
    }
}
