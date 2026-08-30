//! ChangeSet schema (ACT-01).
//!
//! A ChangeSet is the delta between two verified page revisions of the same
//! navigation generation. Stale increments (older `to_revision` than the
//! consumer's state) are dropped by the owner; this module freezes the
//! shape, the bounds and the truncation report.

use crate::ids::{SessionGeneration, TabId};
use crate::semantic::node::{SemanticNode, SemanticNodeId};
use crate::semantic::{SemanticSchemaError, MAX_SEMANTIC_NODES, SEMANTIC_MAP_SCHEMA_VERSION};
use serde::{Deserialize, Serialize};

/// One generation-fenced delta between two page revisions.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChangeSet {
    pub schema_version: u32,
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub from_revision: u64,
    pub to_revision: u64,
    /// True when further increments exist beyond this batch (pagination).
    pub more_available: bool,
    pub truncation: super::SemanticTruncation,
    #[serde(default)]
    pub added: Vec<SemanticNode>,
    #[serde(default)]
    pub updated: Vec<SemanticNode>,
    #[serde(default)]
    pub removed: Vec<SemanticNodeId>,
}

impl ChangeSet {
    /// Validates version, monotonic revisions and bounds; wraps a ChangeSet.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        tab_id: TabId,
        generation: SessionGeneration,
        from_revision: u64,
        to_revision: u64,
        more_available: bool,
        truncation: super::SemanticTruncation,
        added: Vec<SemanticNode>,
        updated: Vec<SemanticNode>,
        removed: Vec<SemanticNodeId>,
    ) -> Result<Self, SemanticSchemaError> {
        if from_revision >= to_revision {
            return Err(SemanticSchemaError::RevisionNotMonotonic);
        }
        if added.len() > MAX_SEMANTIC_NODES
            || updated.len() > MAX_SEMANTIC_NODES
            || removed.len() > MAX_SEMANTIC_NODES
        {
            return Err(SemanticSchemaError::BudgetExceeded("change batch"));
        }
        Ok(Self {
            schema_version: SEMANTIC_MAP_SCHEMA_VERSION,
            tab_id,
            generation,
            from_revision,
            to_revision,
            more_available,
            truncation,
            added,
            updated,
            removed,
        })
    }
}
