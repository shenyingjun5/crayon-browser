//! Frozen precondition evaluation (ACT-05, AC-005).
//!
//! Preconditions are checked deterministically against verified Browser
//! facts immediately before an action executes. The evaluation is pure and
//! fail closed: any violated check denies the action with a stable reason
//! and no input, navigation or network side effect. Model, page and
//! connector input can never turn a violation into a hold.

use crayon_domain::{ActionKind, ElementState, SemanticNodeKind, SemanticSchemaError};
use serde::{Deserialize, Serialize};

/// Maximum violations one report can carry; the closed check set bounds
/// this well below the limit.
pub const MAX_PRECONDITION_VIOLATIONS: usize = 8;

/// Closed precondition checks, evaluated in declaration order.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PreconditionCheck {
    /// The target is visible.
    Visible,
    /// The target is enabled.
    Enabled,
    /// The closed v1 action set can act on the element kind.
    ActionableKind,
    /// The page origin still matches the origin the handle was bound to.
    SameOrigin,
    /// The page revision still matches the verified revision.
    RevisionCurrent,
    /// Discovery resolved exactly one target.
    UniqueTarget,
}

impl PreconditionCheck {
    /// All checks; the closed set locked by golden tests.
    pub const ALL: [Self; 6] = [
        Self::Visible,
        Self::Enabled,
        Self::ActionableKind,
        Self::SameOrigin,
        Self::RevisionCurrent,
        Self::UniqueTarget,
    ];
}

/// Stable precondition violations. Adding one is backward-compatible;
/// renaming or removing one is not.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PreconditionViolation {
    /// The target is hidden.
    Hidden,
    /// The target is disabled.
    Disabled,
    /// The kind cannot perform the requested action.
    KindMismatch,
    /// The target is a sensitive surface excluded from execution.
    SensitiveTarget,
    /// The origin changed after the handle was issued.
    OriginMismatch,
    /// The verified revision advanced after the handle was issued.
    RevisionStale,
    /// Discovery did not resolve exactly one target.
    AmbiguousTarget,
}

impl PreconditionViolation {
    /// All violations; the closed set locked by golden tests.
    pub const ALL: [Self; 7] = [
        Self::Hidden,
        Self::Disabled,
        Self::KindMismatch,
        Self::SensitiveTarget,
        Self::OriginMismatch,
        Self::RevisionStale,
        Self::AmbiguousTarget,
    ];
}

/// Verified facts one evaluation runs against. All inputs are Browser-
/// verified; nothing here accepts page or model verdicts.
#[derive(Clone, Debug)]
pub struct PreconditionInput<'a> {
    pub kind: SemanticNodeKind,
    pub state: &'a ElementState,
    pub action: ActionKind,
    /// Origin the handle/map was bound to (validated at issue time).
    pub bound_origin: &'a str,
    /// Current verified page origin.
    pub current_origin: &'a str,
    /// Revision the verified map was produced at.
    pub bound_revision: u64,
    /// Current verified page revision.
    pub current_revision: u64,
    /// Whether discovery resolved exactly one target.
    pub unique_target: bool,
}

/// Fail-closed evaluation report: the closed violation set, in stable
/// `PreconditionViolation::ALL` order, deduplicated and bounded.
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PreconditionReport {
    #[serde(default)]
    pub violations: Vec<PreconditionViolation>,
}

impl PreconditionReport {
    /// Whether every check holds and the action may proceed.
    #[must_use]
    pub fn holds(&self) -> bool {
        self.violations.is_empty()
    }
}

/// Whether the closed v1 action set can act on the element kind.
/// Sensitive kinds are excluded outright; they are never actionable.
#[must_use]
pub fn is_actionable(kind: SemanticNodeKind, action: ActionKind) -> bool {
    if kind.sensitive() {
        return false;
    }
    matches!(
        (kind, action),
        (
            SemanticNodeKind::Button
                | SemanticNodeKind::Link
                | SemanticNodeKind::Tab
                | SemanticNodeKind::MenuItem,
            ActionKind::Click
        ) | (
            SemanticNodeKind::TextInput | SemanticNodeKind::Textarea,
            ActionKind::SetText | ActionKind::Clear
        ) | (SemanticNodeKind::Select, ActionKind::SelectOption)
            | (
                SemanticNodeKind::Checkbox | SemanticNodeKind::Radio,
                ActionKind::Check | ActionKind::Uncheck
            )
    )
}

/// Evaluates every precondition and reports all violations in stable order.
/// The evaluation performs no side effects; the caller owns any execution.
pub fn evaluate(input: &PreconditionInput<'_>) -> Result<PreconditionReport, SemanticSchemaError> {
    if !crayon_domain::is_valid_origin(input.bound_origin)
        || !crayon_domain::is_valid_origin(input.current_origin)
    {
        return Err(SemanticSchemaError::OriginInvalid);
    }
    let mut violations = Vec::new();
    let push = |violation: PreconditionViolation, violations: &mut Vec<PreconditionViolation>| {
        if violations.len() == MAX_PRECONDITION_VIOLATIONS {
            return Err(SemanticSchemaError::BudgetExceeded("violations"));
        }
        violations.push(violation);
        Ok(())
    };
    if !input.state.visible {
        push(PreconditionViolation::Hidden, &mut violations)?;
    }
    if !input.state.enabled {
        push(PreconditionViolation::Disabled, &mut violations)?;
    }
    if input.kind.sensitive() {
        push(PreconditionViolation::SensitiveTarget, &mut violations)?;
    } else if !is_actionable(input.kind, input.action) {
        push(PreconditionViolation::KindMismatch, &mut violations)?;
    }
    if input.bound_origin != input.current_origin {
        push(PreconditionViolation::OriginMismatch, &mut violations)?;
    }
    if input.bound_revision != input.current_revision {
        push(PreconditionViolation::RevisionStale, &mut violations)?;
    }
    if !input.unique_target {
        push(PreconditionViolation::AmbiguousTarget, &mut violations)?;
    }
    violations.sort();
    violations.dedup();
    Ok(PreconditionReport { violations })
}
