//! Compact projection of the frozen v1 semantic map (ACT-02).
//!
//! The compact profile keeps identity, kind and accessible name for nodes
//! plus the action offers, and reduces forms/media/risk to counts. Element
//! state, form structure, media facts and risk entries are omitted by
//! design; consumers that need them must request `standard`.

use crayon_domain::{
    PageMap, SemanticNode, SemanticNodeId, SemanticNodeKind, SemanticSchemaError,
    SemanticTruncation,
};
use serde::{Deserialize, Serialize};

use crate::detail::profile::{DetailBudget, NO_TRUNCATION};

/// Upper node bound of the compact profile.
pub const MAX_COMPACT_NODES: usize = 128;

/// Upper action bound of the compact profile.
pub const MAX_COMPACT_ACTIONS: usize = 64;

/// One node as seen through the compact profile: identity, closed kind and
/// bounded accessible name. No state, no geometry, no attributes.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CompactNode {
    pub id: SemanticNodeId,
    pub kind: SemanticNodeKind,
    pub name: String,
}

/// One action offer as seen through the compact profile.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CompactAction {
    pub node: SemanticNodeId,
    pub kind: crayon_domain::ActionKind,
    pub summary: String,
}

/// The compact profile of one verified page state. Forms, media and risk
/// appear only as counts; `truncation` reports what the profile omitted.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CompactMap {
    pub schema_version: u32,
    pub tab_id: crayon_domain::TabId,
    pub generation: crayon_domain::SessionGeneration,
    pub revision: u64,
    pub origin: String,
    pub title: String,
    pub nodes: Vec<CompactNode>,
    pub actions: Vec<CompactAction>,
    pub form_count: u32,
    pub media_count: u32,
    pub risk_count: u32,
    pub truncation: SemanticTruncation,
}

/// Renders the compact profile within the given budget; entries beyond the
/// budget are omitted and reported through `truncation`, never dropped
/// silently. Offers whose node was omitted count as omitted actions.
pub fn render_compact(
    map: &PageMap,
    budget: &DetailBudget,
) -> Result<CompactMap, SemanticSchemaError> {
    let kept_nodes = map.nodes.len().min(budget.max_nodes);
    let kept: std::collections::BTreeSet<&str> = map.nodes[..kept_nodes]
        .iter()
        .map(|node| node.id.as_str())
        .collect();
    let mut kept_actions = Vec::new();
    for offer in &map.actions {
        if kept_actions.len() == budget.max_actions {
            break;
        }
        if kept.contains(offer.node.as_str()) {
            kept_actions.push(CompactAction {
                node: offer.node.clone(),
                kind: offer.kind,
                summary: offer.summary.clone(),
            });
        }
    }
    // Forms, media and risk are omitted by the profile's field set, not by
    // the budget; their `*_count` fields carry the information, so only
    // node/action omissions are truncation events.
    let truncation = if map.nodes.len() == kept_nodes && map.actions.len() == kept_actions.len() {
        NO_TRUNCATION
    } else {
        SemanticTruncation {
            nodes_omitted: u32::try_from(map.nodes.len() - kept_nodes)
                .map_err(|_| SemanticSchemaError::BudgetExceeded("node count"))?,
            actions_omitted: u32::try_from(map.actions.len() - kept_actions.len())
                .map_err(|_| SemanticSchemaError::BudgetExceeded("action count"))?,
            forms_omitted: 0,
            media_omitted: 0,
            risk_omitted: 0,
        }
    };
    let compact = CompactMap {
        schema_version: map.schema_version,
        tab_id: map.tab_id.clone(),
        generation: map.generation,
        revision: map.revision,
        origin: map.origin.clone(),
        title: map.title.clone(),
        nodes: map.nodes[..kept_nodes].iter().map(compact_node).collect(),
        actions: kept_actions,
        form_count: map.forms.len() as u32,
        media_count: map.media.len() as u32,
        risk_count: map.risk.len() as u32,
        truncation,
    };
    budget.check_bytes(&compact)?;
    Ok(compact)
}

fn compact_node(node: &SemanticNode) -> CompactNode {
    CompactNode {
        id: node.id.clone(),
        kind: node.kind,
        name: node.name.clone(),
    }
}
