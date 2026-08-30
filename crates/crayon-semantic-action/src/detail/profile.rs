//! Profile selection and per-profile resource budgets (ACT-02).
//!
//! Budgets are named constants here; the validating render functions
//! enforce them and report omissions through the frozen
//! [`SemanticTruncation`] vocabulary instead of failing open.

use crate::detail::compact::{MAX_COMPACT_ACTIONS, MAX_COMPACT_NODES};
use crayon_domain::{
    PageMap, SemanticSchemaError, SemanticTruncation, MAX_ACTION_OFFERS, MAX_MEDIA_ELEMENTS,
    MAX_RISK_ENTRIES, MAX_SEMANTIC_NODES,
};

/// Upper node bound of the `standard` profile; the frozen map budget.
pub const MAX_STANDARD_NODES: usize = MAX_SEMANTIC_NODES;

/// Upper action bound of the `standard` profile; the frozen map budget.
pub const MAX_STANDARD_ACTIONS: usize = MAX_ACTION_OFFERS;

/// Upper node bound of the `internal_full` profile; the frozen map budget.
pub const MAX_INTERNAL_FULL_NODES: usize = MAX_SEMANTIC_NODES;

/// Upper action bound of the `internal_full` profile; the frozen map budget.
pub const MAX_INTERNAL_FULL_ACTIONS: usize = MAX_ACTION_OFFERS;

/// Frozen v1 output profiles of the semantic map. Only `Compact` and
/// `Standard` are external surfaces; `InternalFull` is the bounded internal
/// profile for engine-side semantic tasks and must never be served through
/// an external transport.
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum DetailProfile {
    /// Reduced surface for tight budgets (pickers, previews, cheap reads).
    #[default]
    Compact,
    /// The frozen public map surface for on-screen reading and R1 tools.
    Standard,
    /// Bounded internal profile: the full map plus closed, non-raw internal
    /// annotations. Never leaves the Browser process.
    InternalFull,
}

impl DetailProfile {
    /// All v1 profiles; the closed set locked by golden tests.
    pub const ALL: [Self; 3] = [Self::Compact, Self::Standard, Self::InternalFull];

    /// The frozen budget of this profile.
    #[must_use]
    pub const fn budget(self) -> DetailBudget {
        match self {
            Self::Compact => DetailBudget {
                max_nodes: MAX_COMPACT_NODES,
                max_actions: MAX_COMPACT_ACTIONS,
                max_forms: 0,
                max_media: 0,
                max_risk: 0,
                max_bytes: 262_144,
            },
            Self::Standard => DetailBudget {
                max_nodes: MAX_STANDARD_NODES,
                max_actions: MAX_STANDARD_ACTIONS,
                max_forms: 16,
                max_media: MAX_MEDIA_ELEMENTS,
                max_risk: MAX_RISK_ENTRIES,
                max_bytes: 1_048_576,
            },
            Self::InternalFull => DetailBudget {
                max_nodes: MAX_INTERNAL_FULL_NODES,
                max_actions: MAX_INTERNAL_FULL_ACTIONS,
                max_forms: 16,
                max_media: MAX_MEDIA_ELEMENTS,
                max_risk: MAX_RISK_ENTRIES,
                max_bytes: 2_097_152,
            },
        }
    }
}

/// Per-profile output budget. Entry bounds cap how many items a rendered
/// profile may carry; `max_bytes` caps the serialized wire form.
///
/// `max_forms` uses 16 directly because the frozen form budget lives in a
/// non-const context on the domain side (`MAX_FORMS` is a plain constant
/// but forms are additionally capped per form map).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DetailBudget {
    pub max_nodes: usize,
    pub max_actions: usize,
    pub max_forms: usize,
    pub max_media: usize,
    pub max_risk: usize,
    pub max_bytes: usize,
}

impl DetailBudget {
    /// Validates a custom budget against the frozen map maxima; a profile
    /// budget may never exceed what the map itself can carry.
    pub fn new(
        max_nodes: usize,
        max_actions: usize,
        max_forms: usize,
        max_media: usize,
        max_risk: usize,
        max_bytes: usize,
    ) -> Result<Self, SemanticSchemaError> {
        if max_nodes == 0
            || max_actions == 0
            || max_bytes == 0
            || max_nodes > MAX_SEMANTIC_NODES
            || max_actions > MAX_ACTION_OFFERS
            || max_forms > 16
            || max_media > MAX_MEDIA_ELEMENTS
            || max_risk > MAX_RISK_ENTRIES
        {
            return Err(SemanticSchemaError::BudgetExceeded("detail budget"));
        }
        Ok(Self {
            max_nodes,
            max_actions,
            max_forms,
            max_media,
            max_risk,
            max_bytes,
        })
    }

    /// Whether the frozen map fits this budget entry-wise.
    pub(crate) fn fits_map(&self, map: &PageMap) -> bool {
        map.nodes.len() <= self.max_nodes
            && map.actions.len() <= self.max_actions
            && map.forms.len() <= self.max_forms
            && map.media.len() <= self.max_media
            && map.risk.len() <= self.max_risk
    }

    /// Enforces the serialized byte bound; a profile response that cannot
    /// fit fails closed instead of streaming unbounded output.
    pub(crate) fn check_bytes<T: serde::Serialize>(
        &self,
        wire: &T,
    ) -> Result<(), SemanticSchemaError> {
        let size = serde_json::to_vec(wire)
            .map_err(|_| SemanticSchemaError::BudgetExceeded("detail serialization"))?
            .len();
        if size > self.max_bytes {
            return Err(SemanticSchemaError::BudgetExceeded("detail response bytes"));
        }
        Ok(())
    }
}

/// Renders the frozen public map unchanged after budget enforcement.
pub fn render_standard(
    map: &PageMap,
    budget: &DetailBudget,
) -> Result<PageMap, SemanticSchemaError> {
    if !budget.fits_map(map) {
        return Err(SemanticSchemaError::BudgetExceeded("standard map"));
    }
    let rendered = map.clone();
    budget.check_bytes(&rendered)?;
    Ok(rendered)
}

/// Zero omissions; shared by the projections when nothing was truncated.
pub(crate) const NO_TRUNCATION: SemanticTruncation = SemanticTruncation {
    nodes_omitted: 0,
    actions_omitted: 0,
    forms_omitted: 0,
    media_omitted: 0,
    risk_omitted: 0,
};
