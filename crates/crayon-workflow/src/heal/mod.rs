//! Controlled heal for low-risk effect drift (WFL-15).
//!
//! A heal is a single re-execution of one drifted step on the same node
//! with the same action, allowed only when the drift is uniquely
//! classified as Effect, the skill is low-risk, and the effect remains
//! verifiable against the local fixture. Everything else requires the
//! human: high-risk, cross-origin, semantic changes, active challenges
//! and cancelled runs all fail the heal to human handling.

use crate::drift::{DriftKind, DriftRisk};
use crate::runner::RunCancel;
use crate::validation::{validate_effect, ExpectedEffect, ReportedEffect, ValidationFixture};
use crayon_domain::{CaapError, EffectReport};

/// Maximum heal attempts per drift: exactly one.
pub const MAX_HEAL_ATTEMPTS: usize = 1;

/// Why a heal was refused; the run ends for human handling. Content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HealRefusal {
    /// The drift kind or risk class is outside the heal channel.
    OutsideHealChannel,
    /// More than one step drifted, or the heal was already attempted.
    NotUnique,
    /// The drifted step's action is not low-risk.
    RiskTooHigh,
    /// A challenge is active or the run was cancelled.
    RunStopped,
    /// The retried effect did not verify: hand off to the human.
    UnverifiedRetry,
}

/// Verdict of the heal attempt.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HealOutcome {
    /// The re-executed step verified; the run may continue.
    Healed,
    /// The heal was refused or failed: hand off to the human.
    NeedsHuman,
}

/// The drifted step to repair: node and action from the skill, plus the
/// fixture-consistent expectation.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HealStep {
    pub node: crayon_domain::SemanticNodeId,
    pub action: crayon_domain::ActionKind,
}

/// Validates a heal request's static gates. Returns Ok when the channel
/// may proceed; Err carries the refusal.
fn check_gates(
    risk: DriftRisk,
    kind: DriftKind,
    step: &HealStep,
    fixture: &ValidationFixture,
    attempts_used: usize,
    challenge_active: bool,
    cancelled: bool,
) -> Result<(), HealRefusal> {
    if cancelled || challenge_active {
        return Err(HealRefusal::RunStopped);
    }
    if kind != DriftKind::Effect {
        return Err(HealRefusal::OutsideHealChannel);
    }
    if risk != DriftRisk::Low {
        return Err(HealRefusal::RiskTooHigh);
    }
    if attempts_used >= MAX_HEAL_ATTEMPTS {
        return Err(HealRefusal::NotUnique);
    }
    // The fixture must know the node and support the action.
    let node = &step.node;
    if !fixture.knows(node) || !fixture.supports(node, step.action) {
        return Err(HealRefusal::OutsideHealChannel);
    }
    Ok(())
}

/// Heal request: bundled parameters (keeps `attempt_heal` under the
/// arity gate). All fields are closed facts from the caller.
pub struct HealRequest<'a> {
    pub step: &'a HealStep,
    pub risk: DriftRisk,
    pub kind: DriftKind,
    pub fixture: &'a ValidationFixture,
    pub cancel: &'a RunCancel,
    pub attempts_used: usize,
    pub challenge_active: bool,
}

/// Executes one controlled heal: `retry` re-performs the drifted action
/// on the same node (host-injected), and the new effect must verify
/// against the fixture. No learning, no persistence, no multi-step plans.
///
/// # Errors
///
/// Returns [`HealRefusal`] when a gate refuses or the retry effect does
/// not verify. The caller maps every refusal to human handling.
pub fn attempt_heal(
    request: &HealRequest<'_>,
    mut retry: impl FnMut() -> Result<EffectReport, CaapError>,
) -> Result<HealOutcome, HealRefusal> {
    check_gates(
        request.risk,
        request.kind,
        request.step,
        request.fixture,
        request.attempts_used,
        request.challenge_active,
        request.cancel.is_cancelled(),
    )?;
    let effect = retry().map_err(|_| HealRefusal::UnverifiedRetry)?;
    // The new effect must land on the same node with the same action and
    // a Verified outcome.
    let expected = ExpectedEffect {
        node: request.step.node.clone(),
        action: request.step.action,
    };
    let reported = ReportedEffect {
        node: effect.node.clone(),
        action: effect.action,
        outcome: effect.outcome,
    };
    validate_effect(&expected, &reported, request.fixture)
        .map_err(|_| HealRefusal::UnverifiedRetry)?;
    Ok(HealOutcome::Healed)
}

#[cfg(test)]
mod heal_tests;
