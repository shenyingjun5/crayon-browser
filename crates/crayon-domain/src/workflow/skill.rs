//! Site skill schema (WFL-01).
//!
//! A site skill wraps a recipe with a user-controlled lifecycle status and
//! a monotonic revision. Saving a skill always requires a verified success
//! and an explicit user confirmation downstream; this schema freezes only
//! the data shape and the closed status set.

use crate::workflow::{Recipe, RecipeError};
use serde::{Deserialize, Serialize};

/// Highest skill revision; revisions advance only through explicit user
/// confirmations downstream.
pub const MAX_SKILL_REVISION: u64 = 65_535;

/// Skill construction failure. Stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SkillError {
    RecipeInvalid(RecipeError),
    RevisionOutOfBounds,
}

impl std::fmt::Display for SkillError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::RecipeInvalid(error) => write!(formatter, "invalid recipe: {error}"),
            Self::RevisionOutOfBounds => formatter.write_str("skill revision out of bounds"),
        }
    }
}

impl std::error::Error for SkillError {}

/// Closed skill lifecycle statuses. Only `Enabled` skills are runnable,
/// and running one still requires a fresh grant and confirmation.
#[derive(Clone, Copy, Debug, Default, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SkillStatus {
    /// Recorded but never user-confirmed; not runnable.
    #[default]
    Draft,
    /// Verified-success candidate awaiting user preview/confirmation.
    Candidate,
    /// User-confirmed and runnable (fresh authorization each run).
    Enabled,
    /// User- or health-disabled; not runnable.
    Disabled,
}

/// The frozen v1 personal site skill.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SiteSkill {
    pub recipe: Recipe,
    pub status: SkillStatus,
    pub revision: u64,
}

impl SiteSkill {
    /// Validates the revision bound; wraps a skill.
    pub fn new(recipe: Recipe, status: SkillStatus, revision: u64) -> Result<Self, SkillError> {
        if revision == 0 || revision > MAX_SKILL_REVISION {
            return Err(SkillError::RevisionOutOfBounds);
        }
        Ok(Self {
            recipe,
            status,
            revision,
        })
    }

    /// Whether the status permits running (fresh authorization each run).
    #[must_use]
    pub const fn runnable(&self) -> bool {
        matches!(self.status, SkillStatus::Enabled)
    }
}
