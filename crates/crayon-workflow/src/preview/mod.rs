//! Skill preview and save confirmation (WFL-09).
//!
//! The controller is the single owner of the preview lifecycle for one
//! candidate recipe: the user sees name/origin/version/steps/risk/
//! permission scope/data-flow disclosure and explicitly confirms before
//! the candidate moves to storage (WFL-10). New candidates invalidate the
//! open preview; expired previews refuse confirmation. Terminals are
//! idempotent.

use std::sync::Arc;

use crayon_domain::{ActionKind, Recipe, RecipeError, RiskLevel, MAX_RECIPE_STEPS};

/// Preview session lifetime bound (injected clock; matches the handoff
/// TTL ceiling so previews never outlive their task context).
pub const MAX_PREVIEW_TTL_MS: u64 = 300_000;

/// Closed preview phases.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PreviewPhase {
    Reviewing,
    Confirmed,
    Rejected,
    Expired,
}

/// Preview terminal/consumption outcome.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SaveDecision {
    /// The user confirmed; the candidate is released exactly once for
    /// storage (WFL-10).
    Confirmed(Arc<Recipe>),
    /// Confirmation refused: the preview expired or was already consumed.
    Refused,
}

/// Derived data-flow disclosure flags (closed mapping from the recipe's
/// action kinds — WF-009 数据流预览). Recipes carry no values, so the
/// disclosure is about surfaces, never data.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct DataFlowDisclosure {
    /// Any SetText/Clear step: text will be written into page fields.
    pub writes_fields: bool,
    /// Any Click step: navigation/activation may occur.
    pub activates: bool,
    /// Any SelectOption/Check/Uncheck: form choices change.
    pub changes_choices: bool,
}

/// Preview data derived from the candidate; disclosed to the UI.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PreviewData {
    pub recipe_name: String,
    pub origin: String,
    pub version: u32,
    pub step_count: usize,
    /// Per-step summaries (bounded by the recipe step budget).
    pub step_summaries: Vec<String>,
    /// Host-assigned risk (not derived from partner/user data).
    pub risk: RiskLevel,
    /// The one capability index the saved skill will run under (closed
    /// caller-owned vocabulary).
    pub capability: u16,
    pub data_flow: DataFlowDisclosure,
}

/// Preview failure; content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PreviewError {
    /// TTL elapsed before confirmation.
    Expired,
    /// The preview already reached a terminal phase.
    SessionClosed,
    /// TTL out of bounds.
    InvalidTtl,
    /// The recipe failed domain re-validation.
    RecipeRejected,
}

impl std::fmt::Display for PreviewError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Expired => formatter.write_str("preview expired"),
            Self::SessionClosed => formatter.write_str("preview session closed"),
            Self::InvalidTtl => formatter.write_str("preview ttl out of bounds"),
            Self::RecipeRejected => formatter.write_str("candidate recipe rejected"),
        }
    }
}

impl std::error::Error for PreviewError {}

impl From<RecipeError> for PreviewError {
    fn from(_: RecipeError) -> Self {
        Self::RecipeRejected
    }
}

/// Single-owner preview controller.
pub struct SkillPreviewController {
    candidate: Arc<Recipe>,
    data: PreviewData,
    expires_at_ms: u64,
    phase: PreviewPhase,
    fingerprint: u64,
}

fn fingerprint_of(recipe: &Recipe) -> u64 {
    // FNV-1a over the serialized identity: origin/name/version/step summary
    // text. Deterministic; a changed candidate always differs.
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    let mut absorb = |bytes: &[u8]| {
        for byte in bytes {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
    };
    absorb(recipe.origin.as_bytes());
    absorb(recipe.name.as_bytes());
    absorb(&recipe.version.to_le_bytes());
    for step in &recipe.steps {
        absorb(step.summary.as_bytes());
        absorb(&[step.action as u8]);
    }
    hash
}

fn disclosure_for(actions: impl Iterator<Item = ActionKind>) -> DataFlowDisclosure {
    let mut flow = DataFlowDisclosure::default();
    for action in actions {
        match action {
            ActionKind::Click => flow.activates = true,
            ActionKind::SetText | ActionKind::Clear => flow.writes_fields = true,
            ActionKind::SelectOption | ActionKind::Check | ActionKind::Uncheck => {
                flow.changes_choices = true;
            }
        }
    }
    flow
}

impl SkillPreviewController {
    /// Opens a preview for one candidate. A different candidate while a
    /// preview is open closes the old session implicitly (the caller
    /// drops the previous controller).
    pub fn open(
        candidate: Recipe,
        risk: RiskLevel,
        capability: u16,
        now_ms: u64,
        expires_at_ms: u64,
    ) -> Result<Self, PreviewError> {
        if expires_at_ms <= now_ms || expires_at_ms - now_ms > MAX_PREVIEW_TTL_MS {
            return Err(PreviewError::InvalidTtl);
        }
        // Re-validate through the domain constructor: budget/charset
        // guarantees are re-checked, failing closed.
        let steps_len = candidate.steps.len();
        let recipe = Recipe::new(
            candidate.origin.clone(),
            &candidate.name,
            candidate.version,
            candidate
                .steps
                .iter()
                .map(|step| crayon_domain::RecipeStep {
                    node: step.node.clone(),
                    action: step.action,
                    summary: step.summary.clone(),
                })
                .collect(),
        )
        .map_err(|_| PreviewError::RecipeRejected)?;
        if steps_len > MAX_RECIPE_STEPS {
            return Err(PreviewError::RecipeRejected);
        }
        let fingerprint = fingerprint_of(&recipe);
        let data = PreviewData {
            recipe_name: recipe.name.clone(),
            origin: recipe.origin.clone(),
            version: recipe.version,
            step_count: recipe.steps.len(),
            step_summaries: recipe.steps.iter().map(|s| s.summary.clone()).collect(),
            risk,
            capability,
            data_flow: disclosure_for(recipe.steps.iter().map(|s| s.action)),
        };
        Ok(Self {
            candidate: Arc::new(recipe),
            data,
            expires_at_ms,
            phase: PreviewPhase::Reviewing,
            fingerprint,
        })
    }

    #[must_use]
    pub const fn phase(&self) -> PreviewPhase {
        self.phase
    }

    /// The preview disclosure (fields for the UI).
    #[must_use]
    pub fn data(&self) -> &PreviewData {
        &self.data
    }

    /// Whether the same candidate is still being previewed (re-confirm
    /// guard: a changed candidate must open a fresh preview).
    #[must_use]
    pub fn matches_candidate(&self, candidate_fingerprint: u64) -> bool {
        self.fingerprint == candidate_fingerprint
    }

    #[must_use]
    pub fn fingerprint(&self) -> u64 {
        self.fingerprint
    }

    /// User confirms the save. Exactly once; expired previews refuse.
    pub fn confirm(&mut self, now_ms: u64) -> Result<SaveDecision, PreviewError> {
        if self.phase == PreviewPhase::Expired || now_ms >= self.expires_at_ms {
            self.phase = PreviewPhase::Expired;
            return Err(PreviewError::Expired);
        }
        if self.phase != PreviewPhase::Reviewing {
            return Err(PreviewError::SessionClosed);
        }
        self.phase = PreviewPhase::Confirmed;
        Ok(SaveDecision::Confirmed(Arc::clone(&self.candidate)))
    }

    /// User declines. Idempotent terminal.
    pub fn reject(&mut self) {
        if self.phase == PreviewPhase::Reviewing {
            self.phase = PreviewPhase::Rejected;
        }
    }

    /// TTL lapse. Idempotent terminal.
    pub fn expire(&mut self) {
        if self.phase == PreviewPhase::Reviewing {
            self.phase = PreviewPhase::Expired;
        }
    }
}

#[cfg(test)]
mod preview_tests;
