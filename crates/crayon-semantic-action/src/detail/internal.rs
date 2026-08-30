//! Bounded internal-full profile of the frozen v1 semantic map (ACT-02).
//!
//! `InternalFullMap` is the internal profile consumed by engine-side
//! semantic tasks (discovery, preconditions, execution). It carries the
//! full frozen map plus closed, non-raw annotations derived from verified
//! facts only. It must never be served through an external transport; raw
//! DOM/HTML/CDP and pointers are structurally inexpressible here.

use crayon_domain::{PageMap, SemanticSchemaError};
use serde::{Deserialize, Serialize};

use crate::detail::profile::DetailBudget;

/// One closed internal annotation of a mapped node. The ordinal is the
/// node's stable position in the map; `sensitive` mirrors the frozen
/// sensitive-kind policy. No selector, no attribute, no geometry.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SemanticNodeAnnotation {
    pub node: crayon_domain::SemanticNodeId,
    pub ordinal: u32,
    pub sensitive: bool,
}

/// The internal-full profile: the frozen map plus bounded internal
/// annotations. The internal profile always fits the map budget, so it
/// never truncates; `truncation` in the embedded map stays authoritative.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InternalFullMap {
    pub map: PageMap,
    pub annotations: Vec<SemanticNodeAnnotation>,
}

/// Renders the internal-full profile within the given budget.
pub fn render_internal_full(
    map: &PageMap,
    budget: &DetailBudget,
) -> Result<InternalFullMap, SemanticSchemaError> {
    if !budget.fits_map(map) {
        return Err(SemanticSchemaError::BudgetExceeded("internal full map"));
    }
    let annotations = map
        .nodes
        .iter()
        .enumerate()
        .map(|(index, node)| SemanticNodeAnnotation {
            node: node.id.clone(),
            ordinal: index as u32,
            sensitive: node.kind.sensitive(),
        })
        .collect();
    let internal = InternalFullMap {
        map: map.clone(),
        annotations,
    };
    budget.check_bytes(&internal)?;
    Ok(internal)
}
