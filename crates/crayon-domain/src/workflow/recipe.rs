//! Recipe schema (WFL-01).
//!
//! A recipe is the semantic intent of a verified-successful task: closed
//! action kinds over opaque node ids with bounded summaries. Recipes carry
//! no outcomes, no values and no secrets; they are only ever generated
//! from verified success (WFL-08 owns that gate).

use crate::semantic::{ActionKind, SemanticNodeId};
use crate::workflow::WORKFLOW_SCHEMA_VERSION;
use serde::{Deserialize, Serialize};

/// Maximum steps in one recipe.
pub const MAX_RECIPE_STEPS: usize = 64;

/// Maximum bytes of one recipe name.
pub const MAX_RECIPE_NAME_BYTES: usize = 64;

/// Highest recipe version; versions advance only via explicit user
/// confirmation downstream (WFL-13 owns health/rollback).
pub const MAX_RECIPE_VERSION: u32 = 65_535;

/// Recipe construction failure. Stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RecipeError {
    OriginInvalid,
    NameInvalid,
    StepBudgetExceeded,
    VersionOutOfBounds,
}

impl std::fmt::Display for RecipeError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::OriginInvalid => formatter.write_str("recipe origin failed the closed check"),
            Self::NameInvalid => {
                formatter.write_str("recipe name must be 1..=64 bytes of [a-z0-9_-]")
            }
            Self::StepBudgetExceeded => formatter.write_str("recipe step budget exceeded"),
            Self::VersionOutOfBounds => formatter.write_str("recipe version out of bounds"),
        }
    }
}

impl std::error::Error for RecipeError {}

/// One recipe step: semantic intent only.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecipeStep {
    pub node: SemanticNodeId,
    pub action: ActionKind,
    pub summary: String,
}

/// The frozen v1 recipe of one task on one site.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Recipe {
    pub schema_version: u32,
    /// Validated `http(s)` origin the skill is scoped to.
    pub origin: String,
    /// Closed-charset name `[a-z0-9_-]`, 1..=64 bytes.
    pub name: String,
    pub version: u32,
    pub steps: Vec<RecipeStep>,
}

impl Recipe {
    /// Validates bounds, name charset and origin; wraps a recipe.
    pub fn new(
        origin: String,
        name: &str,
        version: u32,
        steps: Vec<RecipeStep>,
    ) -> Result<Self, RecipeError> {
        if !crate::semantic::is_valid_origin(&origin) {
            return Err(RecipeError::OriginInvalid);
        }
        if name.is_empty()
            || name.len() > MAX_RECIPE_NAME_BYTES
            || !name
                .bytes()
                .all(|b| matches!(b, b'a'..=b'z' | b'0'..=b'9' | b'_' | b'-'))
        {
            return Err(RecipeError::NameInvalid);
        }
        if version == 0 || version > MAX_RECIPE_VERSION {
            return Err(RecipeError::VersionOutOfBounds);
        }
        if steps.len() > MAX_RECIPE_STEPS {
            return Err(RecipeError::StepBudgetExceeded);
        }
        Ok(Self {
            schema_version: WORKFLOW_SCHEMA_VERSION,
            origin,
            name: name.to_owned(),
            version,
            steps,
        })
    }
}
