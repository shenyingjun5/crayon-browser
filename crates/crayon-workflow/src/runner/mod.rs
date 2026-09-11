//! Site skill runner (WFL-12).
//!
//! Executes an Enabled skill's steps in order. Every run requires a fresh
//! authorization per step (`ApprovedAction`, supplied one-per-step by the
//! host; reuse is rejected). Cancellation, deadline expiry, port failure
//! and challenge interruption all terminate the run fail-closed — the
//! runner never retries, never re-authorizes and never resumes silently
//! (WFL-05 owns any resume).

use std::sync::Arc;

use crayon_semantic_action::ApprovedAction;

use crate::validation::{validate_run, ReportedEffect, ValidationFixture};
use crayon_domain::{CaapError, SiteSkill};

/// Cooperative cancellation polled between and during steps.
#[derive(Clone, Default)]
pub struct RunCancel(Arc<std::sync::atomic::AtomicBool>);

impl RunCancel {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn cancel(&self) {
        self.0.store(true, std::sync::atomic::Ordering::SeqCst);
    }

    #[must_use]
    pub fn is_cancelled(&self) -> bool {
        self.0.load(std::sync::atomic::Ordering::SeqCst)
    }
}

/// Host-injected execution port: performs one confirmed, authorized
/// semantic action on the live page. Real implementations are product
/// assembly (AGT-12Cc); tests inject fakes.
pub trait RunnerPort {
    /// Executes one step. Returns the observed effect. Implementations
    /// poll |cancel| at their own bounded checkpoints.
    fn execute_step(
        &mut self,
        approved: &ApprovedAction,
        cancel: &RunCancel,
    ) -> Result<crayon_domain::EffectReport, CaapError>;
}

/// Why a run terminated before completing all steps. Content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RunTermination {
    Cancelled,
    DeadlineExceeded,
    /// A challenge was detected mid-run; automation stops (WFL-02/05 own
    /// the pause/resume cycle).
    ChallengeDetected,
    /// The execution port refused or the effect did not verify.
    StepFailed,
}

/// Terminal result of one run.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RunOutcome {
    /// All steps executed and the post-run validation passed.
    Completed,
    Terminated(RunTermination),
}

/// Single-owner runner for one Enabled skill.
pub struct SkillRunner<'a> {
    skill: &'a SiteSkill,
    fixture: &'a ValidationFixture,
    tab_id: crayon_domain::TabId,
    generation: crayon_domain::SessionGeneration,
    deadline_ms: u64,
}

impl<'a> SkillRunner<'a> {
    /// Prepares a run. Only `Enabled` skills are runnable; every run still
    /// requires fresh per-step authorization through the port.
    #[must_use]
    pub fn prepare(
        skill: &'a SiteSkill,
        fixture: &'a ValidationFixture,
        tab_id: crayon_domain::TabId,
        generation: crayon_domain::SessionGeneration,
        deadline_ms: u64,
    ) -> Self {
        Self {
            skill,
            fixture,
            tab_id,
            generation,
            deadline_ms,
        }
    }

    #[must_use]
    pub const fn skill(&self) -> &SiteSkill {
        self.skill
    }

    /// Executes the run. `approvals` must yield exactly one fresh
    /// `ApprovedAction` per step, in order, each matching the step and
    /// carrying the current action authorization; reuse or mismatch is
    /// rejected before execution.
    pub fn run<X, A, C, F>(
        &self,
        port: &mut X,
        mut approvals: A,
        cancel: &RunCancel,
        challenge: C,
        clock: F,
    ) -> RunOutcome
    where
        X: RunnerPort,
        A: FnMut(usize) -> Option<ApprovedAction>,
        // C: host-supplied challenge detector; a true reading terminates
        // the run immediately (WFL-02/05 own the pause/resume cycle).
        C: Fn() -> bool,
        F: Fn() -> u64,
    {
        if !self.skill.runnable() {
            return RunOutcome::Terminated(RunTermination::StepFailed);
        }
        let mut reported: Vec<ReportedEffect> = Vec::with_capacity(self.skill.recipe.steps.len());
        for (index, step) in self.skill.recipe.steps.iter().enumerate() {
            if cancel.is_cancelled() {
                return RunOutcome::Terminated(RunTermination::Cancelled);
            }
            let now = clock();
            if now >= self.deadline_ms {
                return RunOutcome::Terminated(RunTermination::DeadlineExceeded);
            }
            // Fresh per-step authorization: reuse across steps is rejected.
            let Some(approved) = approvals(index) else {
                return RunOutcome::Terminated(RunTermination::StepFailed);
            };
            if approved.node != step.node
                || approved.action != step.action
                || approved.tab_id != self.tab_id
                || approved.generation != self.generation
                || approved.deadline_ms > self.deadline_ms
                || approved.deadline_ms < now
            {
                return RunOutcome::Terminated(RunTermination::StepFailed);
            }
            // A challenge surfacing mid-run stops automation immediately;
            // WFL-05 owns any resume.
            if challenge() {
                return RunOutcome::Terminated(RunTermination::ChallengeDetected);
            }
            match port.execute_step(&approved, cancel) {
                Ok(effect) => {
                    if effect.tab_id != self.tab_id
                        || effect.generation != self.generation
                        || effect.node != step.node
                        || effect.action != step.action
                        || effect.outcome != crayon_domain::EffectOutcome::Verified
                    {
                        return RunOutcome::Terminated(RunTermination::StepFailed);
                    }
                    reported.push(ReportedEffect {
                        node: effect.node,
                        action: effect.action,
                        outcome: effect.outcome,
                    });
                }
                Err(_) => return RunOutcome::Terminated(RunTermination::StepFailed),
            }
        }
        // Post-run line-up: 1:1 against the skill, fixture-consistent.
        if validate_run(self.skill, &reported, self.fixture).is_ok() {
            RunOutcome::Completed
        } else {
            RunOutcome::Terminated(RunTermination::StepFailed)
        }
    }
}

#[cfg(test)]
mod runner_tests;
