//! ChangeSet production and stale-drop tests (ACT-10, AC-010): bounded
//! pagination, deterministic diff, monotonic consumer state, stale and
//! unfenced drop, generation re-basing and high-frequency replay.

use crayon_domain::MAX_SEMANTIC_NODES;
use crayon_domain::{
    ElementState, PageMap, SemanticNode, SemanticNodeId, SemanticNodeKind, SemanticTruncation,
    SessionGeneration, TabId,
};
use crayon_semantic_action::{diff_maps, emit_batches, ApplyOutcome, ChangeConsumer, ChangeError};

fn node_id(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn node(raw: &str, kind: SemanticNodeKind, name: &str) -> SemanticNode {
    SemanticNode::new(node_id(raw), kind, name.to_owned(), ElementState::default())
        .expect("valid node")
}

fn map(revision: u64, generation: u64, nodes: Vec<SemanticNode>) -> PageMap {
    PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(generation),
        revision,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        nodes,
        Vec::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    )
    .expect("valid map")
}

fn button(raw: &str, name: &str) -> SemanticNode {
    node(raw, SemanticNodeKind::Button, name)
}

// ---------- Diff ----------

#[test]
fn diff_detects_added_updated_and_removed_deterministically() {
    let previous = map(1, 3, vec![button("n-1", "Old name"), button("n-2", "Gone")]);
    let next = map(
        2,
        3,
        vec![button("n-1", "New name"), button("n-3", "Added")],
    );
    let diff = diff_maps(&previous, &next).expect("valid diff");
    assert_eq!(diff.added.len(), 1);
    assert_eq!(diff.added[0].id, node_id("n-3"));
    assert_eq!(diff.updated.len(), 1);
    assert_eq!(diff.updated[0].id, node_id("n-1"));
    assert_eq!(diff.removed, vec![node_id("n-2")]);
    // Same inputs, same diff.
    assert_eq!(diff_maps(&previous, &next).expect("valid"), diff);
}

#[test]
fn diff_rejects_mismatched_fencing_and_stale_revisions() {
    let previous = map(1, 3, vec![]);
    assert_eq!(
        diff_maps(&previous, &previous),
        Err(ChangeError::RevisionNotMonotonic)
    );
    let other_tab = PageMap::new(
        TabId::new("tab-2").expect("tab id"),
        SessionGeneration::from_raw(3),
        2,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    )
    .expect("valid map");
    assert_eq!(
        diff_maps(&previous, &other_tab),
        Err(ChangeError::TabMismatch)
    );
    let newer_generation = map(2, 4, vec![]);
    assert_eq!(
        diff_maps(&previous, &newer_generation),
        Err(ChangeError::GenerationMismatch)
    );
}

// ---------- Pagination ----------

#[test]
fn pagination_is_bounded_ordered_and_lossless() {
    let previous = map(
        1,
        3,
        (0..MAX_SEMANTIC_NODES as u32)
            .map(|i| button(&format!("n-{i:06}"), "x"))
            .collect(),
    );
    // Remove the first 300, update 200 of the rest, add 300 → 800 items.
    let mut next_nodes: Vec<SemanticNode> = (300..MAX_SEMANTIC_NODES as u32)
        .map(|i| {
            if i >= 312 {
                button(&format!("n-{i:06}"), "updated")
            } else {
                button(&format!("n-{i:06}"), "x")
            }
        })
        .collect();
    for i in 0..300u32 {
        next_nodes.push(button(&format!("n-new-{i:06}"), "added"));
    }
    let next = map(2, 3, next_nodes);
    let diff = diff_maps(&previous, &next).expect("valid diff");
    assert_eq!(diff.added.len(), 300);
    assert_eq!(diff.updated.len(), 200);
    assert_eq!(diff.removed.len(), 300);
    let batches = emit_batches(&previous, &next).expect("batches");
    assert!(batches.len() >= 2, "800 items paginate across batches");
    let mut total = 0usize;
    for (index, batch) in batches.iter().enumerate() {
        assert_eq!(batch.from_revision, 1);
        assert_eq!(batch.to_revision, 2);
        let count = batch.added.len() + batch.updated.len() + batch.removed.len();
        assert!(count <= MAX_SEMANTIC_NODES);
        assert_eq!(batch.more_available, index + 1 < batches.len());
        total += count;
    }
    assert_eq!(
        total,
        diff.added.len() + diff.updated.len() + diff.removed.len()
    );
    // Nothing was truncated by pagination.
    assert!(batches
        .iter()
        .all(|b| b.truncation == SemanticTruncation::default()));
}

// ---------- Consumer stale drop ----------

#[test]
fn consumer_drops_stale_replayed_and_unfenced_batches() {
    let mut consumer = ChangeConsumer::new();
    let first = map(1, 3, vec![]);
    let second = map(2, 3, vec![]);
    let third = map(5, 3, vec![]);
    let batches = emit_batches(&first, &second).expect("batches");
    assert_eq!(consumer.apply(&batches[0]), ApplyOutcome::Applied);
    assert_eq!(consumer.last_applied_revision(), Some(2));
    // Replay of the same revision range is stale.
    assert_eq!(consumer.apply(&batches[0]), ApplyOutcome::StaleDropped);
    // An older increment is stale.
    let old = map(1, 3, vec![]);
    let older = emit_batches(&old, &second).expect("batches");
    assert_eq!(consumer.apply(&older[0]), ApplyOutcome::StaleDropped);
    // A newer increment applies.
    let newer = emit_batches(&second, &third).expect("batches");
    assert_eq!(consumer.apply(&newer[0]), ApplyOutcome::Applied);
    assert_eq!(consumer.last_applied_revision(), Some(5));
}

#[test]
fn consumer_rebases_on_newer_generation_and_drops_older_ones() {
    let mut consumer = ChangeConsumer::new();
    let gen3_first = map(1, 3, vec![]);
    let gen3_second = map(2, 3, vec![]);
    let batches = emit_batches(&gen3_first, &gen3_second).expect("batches");
    assert_eq!(consumer.apply(&batches[0]), ApplyOutcome::Applied);
    // An older generation is unfenced and dropped.
    let gen2 = map(9, 2, vec![]);
    let gen2_first = map(8, 2, vec![]);
    let old_batches = emit_batches(&gen2_first, &gen2).expect("batches");
    assert_eq!(consumer.apply(&old_batches[0]), ApplyOutcome::Unfenced);
    // A newer generation re-bases the tracked state.
    let gen4_first = map(1, 4, vec![]);
    let gen4_second = map(3, 4, vec![]);
    let new_batches = emit_batches(&gen4_first, &gen4_second).expect("batches");
    assert_eq!(consumer.apply(&new_batches[0]), ApplyOutcome::Applied);
    assert_eq!(consumer.last_applied_revision(), Some(3));
}

#[test]
fn empty_diff_still_emits_a_revision_batch() {
    let first = map(1, 3, vec![button("n-1", "same")]);
    let second = map(2, 3, vec![button("n-1", "same")]);
    let diff = diff_maps(&first, &second).expect("valid diff");
    assert!(diff.added.is_empty() && diff.updated.is_empty() && diff.removed.is_empty());
    let batches = emit_batches(&first, &second).expect("batches");
    assert_eq!(batches.len(), 1);
    assert!(!batches[0].more_available);
    assert!(batches[0].added.is_empty());
    // Consumers still advance to the new revision.
    let mut consumer = ChangeConsumer::new();
    assert_eq!(consumer.apply(&batches[0]), ApplyOutcome::Applied);
    assert_eq!(consumer.last_applied_revision(), Some(2));
}
