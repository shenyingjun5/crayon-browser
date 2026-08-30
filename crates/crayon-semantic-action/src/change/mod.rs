//! ChangeSet production and stale-drop consumption (ACT-10, AC-010).
//!
//! The producer diffs two verified page maps of the same tab and
//! generation into ordered, budgeted [`ChangeSet`] batches; the consumer
//! owns the monotonic revision state and drops stale increments. The
//! producer is pure and deterministic: same pair of maps, same batches.

use crayon_domain::{
    ChangeSet, PageMap, SemanticNode, SemanticSchemaError, SemanticTruncation, MAX_SEMANTIC_NODES,
};
use std::collections::BTreeMap;

/// Production failure. Stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ChangeError {
    /// The two maps come from different tabs.
    TabMismatch,
    /// The two maps come from different page generations.
    GenerationMismatch,
    /// The next map's revision does not advance the previous one.
    RevisionNotMonotonic,
    /// Wire-level validation of a batch failed.
    InvalidBatch(SemanticSchemaError),
}

impl std::fmt::Display for ChangeError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::TabMismatch => formatter.write_str("maps belong to different tabs"),
            Self::GenerationMismatch => formatter.write_str("maps belong to different generations"),
            Self::RevisionNotMonotonic => formatter.write_str("revision does not advance"),
            Self::InvalidBatch(error) => write!(formatter, "invalid change batch: {error}"),
        }
    }
}

impl std::error::Error for ChangeError {}

/// Deterministic diff of two verified page maps.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ChangeDiff {
    pub added: Vec<SemanticNode>,
    pub updated: Vec<SemanticNode>,
    pub removed: Vec<crayon_domain::SemanticNodeId>,
}

/// Splits a diff into budgeted, ordered batches; `more_available` is true
/// on every batch except the last. Pagination is not truncation: nothing
/// is dropped, `truncation` stays zero.
#[must_use]
pub fn paginate(diff: &ChangeDiff) -> Vec<UnfencedBatch> {
    let mut items: Vec<BatchItem> = Vec::new();
    for node in &diff.added {
        items.push(BatchItem::Added(node.clone()));
    }
    for node in &diff.updated {
        items.push(BatchItem::Updated(node.clone()));
    }
    for id in &diff.removed {
        items.push(BatchItem::Removed(id.clone()));
    }
    let mut batches = Vec::new();
    for chunk in items.chunks(MAX_SEMANTIC_NODES) {
        let mut batch = UnfencedBatch::default();
        for item in chunk {
            match item {
                BatchItem::Added(node) => batch.added.push(node.clone()),
                BatchItem::Updated(node) => batch.updated.push(node.clone()),
                BatchItem::Removed(id) => batch.removed.push(id.clone()),
            }
        }
        batches.push(batch);
    }
    batches
}

/// One pagination item.
#[derive(Clone, Debug, Eq, PartialEq)]
enum BatchItem {
    Added(SemanticNode),
    Updated(SemanticNode),
    Removed(crayon_domain::SemanticNodeId),
}

/// A batch before revision fencing is attached.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct UnfencedBatch {
    pub added: Vec<SemanticNode>,
    pub updated: Vec<SemanticNode>,
    pub removed: Vec<crayon_domain::SemanticNodeId>,
}

/// Produces the deterministic diff between two verified maps of the same
/// tab and generation. Updates compare kind, name and state; the diff is
/// ordered by node id for stable wire form.
pub fn diff_maps(previous: &PageMap, next: &PageMap) -> Result<ChangeDiff, ChangeError> {
    if previous.tab_id != next.tab_id {
        return Err(ChangeError::TabMismatch);
    }
    if previous.generation != next.generation {
        return Err(ChangeError::GenerationMismatch);
    }
    if next.revision <= previous.revision {
        return Err(ChangeError::RevisionNotMonotonic);
    }
    let previous_nodes: BTreeMap<&str, &SemanticNode> = previous
        .nodes
        .iter()
        .map(|node| (node.id.as_str(), node))
        .collect();
    let next_nodes: BTreeMap<&str, &SemanticNode> = next
        .nodes
        .iter()
        .map(|node| (node.id.as_str(), node))
        .collect();
    let mut diff = ChangeDiff::default();
    for (id, node) in &next_nodes {
        match previous_nodes.get(*id) {
            None => diff.added.push((*node).clone()),
            Some(old) => {
                if old.kind != node.kind || old.name != node.name || old.state != node.state {
                    diff.updated.push((*node).clone());
                }
            }
        }
    }
    for id in previous_nodes.keys() {
        if !next_nodes.contains_key(id) {
            diff.removed.push(
                crayon_domain::SemanticNodeId::new(id)
                    .map_err(|_| ChangeError::InvalidBatch(SemanticSchemaError::TokenInvalid))?,
            );
        }
    }
    Ok(diff)
}

/// Emits budgeted `ChangeSet` batches for one diff; the empty diff emits a
/// single empty batch so consumers still learn the new revision.
pub fn emit_batches(previous: &PageMap, next: &PageMap) -> Result<Vec<ChangeSet>, ChangeError> {
    let diff = diff_maps(previous, next)?;
    let batches = paginate(&diff);
    let batches = if batches.is_empty() {
        vec![UnfencedBatch::default()]
    } else {
        batches
    };
    let total = batches.len();
    let mut emitted = Vec::new();
    for (index, batch) in batches.into_iter().enumerate() {
        let more_available = index + 1 < total;
        let set = ChangeSet::new(
            next.tab_id.clone(),
            next.generation,
            previous.revision,
            next.revision,
            more_available,
            SemanticTruncation::default(),
            batch.added,
            batch.updated,
            batch.removed,
        )
        .map_err(ChangeError::InvalidBatch)?;
        emitted.push(set);
    }
    Ok(emitted)
}

/// Apply outcome of one batch.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ApplyOutcome {
    /// The batch advanced the owned state.
    Applied,
    /// The batch is older than or equal to the owned state; dropped.
    StaleDropped,
    /// The batch belongs to another tab or generation; dropped.
    Unfenced,
}

/// Single owner of applied-revision state for one stream.
#[derive(Debug, Default)]
pub struct ChangeConsumer {
    state: Option<Tracked>,
}

#[derive(Debug)]
struct Tracked {
    tab_id: crayon_domain::TabId,
    generation: crayon_domain::SessionGeneration,
    last_applied_revision: u64,
}

impl ChangeConsumer {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Applies one batch in order; stale and unfenced batches are dropped
    /// without effect. A generation change re-bases the tracked state.
    pub fn apply(&mut self, batch: &ChangeSet) -> ApplyOutcome {
        match &mut self.state {
            None => {
                self.state = Some(Tracked {
                    tab_id: batch.tab_id.clone(),
                    generation: batch.generation,
                    last_applied_revision: batch.to_revision,
                });
                ApplyOutcome::Applied
            }
            Some(tracked) => {
                if tracked.tab_id != batch.tab_id || tracked.generation != batch.generation {
                    // A newer generation re-bases; older ones are unfenced.
                    if batch.generation > tracked.generation {
                        tracked.tab_id = batch.tab_id.clone();
                        tracked.generation = batch.generation;
                        tracked.last_applied_revision = batch.to_revision;
                        return ApplyOutcome::Applied;
                    }
                    return ApplyOutcome::Unfenced;
                }
                if batch.to_revision <= tracked.last_applied_revision {
                    return ApplyOutcome::StaleDropped;
                }
                tracked.last_applied_revision = batch.to_revision;
                ApplyOutcome::Applied
            }
        }
    }

    /// The last applied revision of the tracked stream, if any.
    #[must_use]
    pub fn last_applied_revision(&self) -> Option<u64> {
        self.state
            .as_ref()
            .map(|tracked| tracked.last_applied_revision)
    }
}
