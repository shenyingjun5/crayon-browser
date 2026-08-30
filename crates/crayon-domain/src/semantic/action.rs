//! Semantic action vocabulary (ACT-01).
//!
//! The v1 action set is closed. There is no arbitrary JavaScript, no
//! script evaluation, no drag, no keyboard macro and no upload action;
//! extending the set is a protocol-versioned change gated by ACT-12.

use crate::semantic::node::SemanticNodeId;
use crate::semantic::{SemanticSchemaError, MAX_ACTION_OFFERS, MAX_ACTION_SUMMARY_BYTES};
use serde::{Deserialize, Serialize};

/// Closed v1 action kinds offered by verified page facts.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ActionKind {
    /// Activate the element (buttons, links, tabs, menu items).
    Click,
    /// Replace the text content of a non-sensitive text field.
    SetText,
    /// Select one option of a `select` element.
    SelectOption,
    /// Move a checkbox or radio into the checked state.
    Check,
    /// Move a checkbox into the unchecked state.
    Uncheck,
    /// Remove the text content of a non-sensitive text field.
    Clear,
}

impl ActionKind {
    /// All v1 action kinds; the closed set locked by golden tests.
    pub const ALL: [Self; 6] = [
        Self::Click,
        Self::SetText,
        Self::SelectOption,
        Self::Check,
        Self::Uncheck,
        Self::Clear,
    ];
}

/// One actionable element discovered in the verified page facts. The
/// summary describes the intended effect in bounded text supplied by the
/// Browser process; it is never a selector and never a script.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ActionOffer {
    pub node: SemanticNodeId,
    pub kind: ActionKind,
    pub summary: String,
}

impl ActionOffer {
    /// Validates bounds and wraps an action offer.
    pub fn new(
        node: SemanticNodeId,
        kind: ActionKind,
        summary: String,
    ) -> Result<Self, SemanticSchemaError> {
        if summary.len() > MAX_ACTION_SUMMARY_BYTES {
            return Err(SemanticSchemaError::BoundExceeded("action summary"));
        }
        Ok(Self {
            node,
            kind,
            summary,
        })
    }
}

/// Bounds check helper for the map assembly; the offer list itself lives in
/// `PageMap`.
pub(crate) fn validate_offers(offers: &[ActionOffer]) -> Result<(), SemanticSchemaError> {
    if offers.len() > MAX_ACTION_OFFERS {
        return Err(SemanticSchemaError::BudgetExceeded("action offers"));
    }
    Ok(())
}
