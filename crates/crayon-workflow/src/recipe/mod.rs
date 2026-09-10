//! Verified-success-only recipe generation (WFL-08).
//!
//! A recipe candidate may only originate from a task attempt that ended in
//! verified success. Failures, cancellations, unfinished challenges and
//! indeterminate outcomes never learn; a trace with any non-verified step is
//! rejected outright. The gate is a pure function over frozen domain types.

use crayon_domain::{EffectOutcome, Recipe, RecipeError, RecipeStep, WorkflowTrace};

/// Closed terminal outcome of one recorded task attempt.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AttemptOutcome {
    /// Every authorized step ran and the task's verified effects completed.
    VerifiedSuccess,
    Failed,
    Cancelled,
    /// The attempt was paused by a challenge and never finished.
    ChallengeIncomplete,
    /// The attempt ended without an attributable outcome.
    Indeterminate,
}

/// Deterministic rejection reasons; rejection is a normal, expected result.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RecipeRejectReason {
    /// Only `VerifiedSuccess` attempts may learn.
    NotVerifiedSuccess,
    /// The trace recorded no verified step.
    EmptyTrace,
    /// A trace step is not `EffectOutcome::Verified`.
    UnverifiedStep,
    /// More steps than the recipe budget allows.
    StepBudgetExceeded,
}

/// The generation result: a candidate recipe or a deterministic rejection.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum RecipeCandidate {
    Candidate(Recipe),
    Rejected(RecipeRejectReason),
}

/// Identity failure surfaced by the frozen domain constructor; the only
/// error path. Stable variants carry no caller content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RecipeGenerationError {
    /// Origin/name/version failed `Recipe::new` validation.
    InvalidIdentity,
}

impl From<RecipeError> for RecipeGenerationError {
    fn from(_: RecipeError) -> Self {
        Self::InvalidIdentity
    }
}

/// Maps a recorded trace attempt to a recipe candidate. The mapping is
/// faithful (node, action, summary per step) and adds nothing.
///
/// # Errors
///
/// Returns [`RecipeGenerationError::InvalidIdentity`] when the caller
/// supplied identity fields fail the frozen domain validation.
pub fn generate_candidate(
    attempt: AttemptOutcome,
    trace: &WorkflowTrace,
    name: &str,
    version: u32,
) -> Result<RecipeCandidate, RecipeGenerationError> {
    if attempt != AttemptOutcome::VerifiedSuccess {
        return Ok(RecipeCandidate::Rejected(
            RecipeRejectReason::NotVerifiedSuccess,
        ));
    }
    if trace.steps.is_empty() {
        return Ok(RecipeCandidate::Rejected(RecipeRejectReason::EmptyTrace));
    }
    if trace
        .steps
        .iter()
        .any(|step| step.outcome != EffectOutcome::Verified)
    {
        return Ok(RecipeCandidate::Rejected(
            RecipeRejectReason::UnverifiedStep,
        ));
    }
    if trace.steps.len() > crayon_domain::MAX_RECIPE_STEPS {
        return Ok(RecipeCandidate::Rejected(
            RecipeRejectReason::StepBudgetExceeded,
        ));
    }
    let steps: Vec<RecipeStep> = trace
        .steps
        .iter()
        .map(|step| RecipeStep {
            node: step.node.clone(),
            action: step.action,
            summary: step.summary.clone(),
        })
        .collect();
    let recipe = Recipe::new(trace.origin.clone(), name, version, steps)?;
    Ok(RecipeCandidate::Candidate(recipe))
}

/// Convenience re-export so callers cannot accidentally construct
/// [`AttemptOutcome`] from non-closed data.
#[must_use]
pub const fn learnable(outcome: AttemptOutcome) -> bool {
    matches!(outcome, AttemptOutcome::VerifiedSuccess)
}

#[cfg(test)]
mod recipe_tests;
