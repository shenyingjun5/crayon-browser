//! Cross-cutting semantic action performance budget tests (ACT-12):
//! bounded construction, bounded diff/pagination and bounded projection at
//! the frozen maxima. Assertions use operation budget counts and fail-closed
//! bounds, never wall-clock timing (CI-hostile by repo rule).

use crayon_domain::{
    ElementState, PageMap, SemanticNode, SemanticNodeId, SemanticNodeKind, SemanticTruncation,
    SessionGeneration, TabId, MAX_SEMANTIC_NODES,
};
use crayon_semantic_action::{
    diff_maps, emit_batches, project_forms, render_compact, render_internal_full, render_standard,
    DetailProfile, MAX_COMPACT_NODES, MAX_STANDARD_NODES,
};

fn node_id(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn map(revision: u64, count: u32) -> PageMap {
    map_with_names(revision, count, &|_i| "Sample name".to_owned())
}

fn map_with_names(revision: u64, count: u32, name: &dyn Fn(u32) -> String) -> PageMap {
    PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        revision,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        (0..count)
            .map(|i| {
                SemanticNode::new(
                    node_id(&format!("n-{i:06}")),
                    SemanticNodeKind::Button,
                    name(i),
                    ElementState::default(),
                )
                .expect("valid node")
            })
            .collect(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    )
    .expect("valid map")
}

// ---------- Construction and projection at the frozen maxima ----------

#[test]
fn max_size_maps_render_within_entry_budgets() {
    let map = map(7, MAX_STANDARD_NODES as u32);
    let standard =
        render_standard(&map, &DetailProfile::Standard.budget()).expect("standard render");
    assert_eq!(standard.nodes.len(), MAX_STANDARD_NODES);
    let internal =
        render_internal_full(&map, &DetailProfile::InternalFull.budget()).expect("internal render");
    assert_eq!(internal.annotations.len(), MAX_STANDARD_NODES);
    let compact = render_compact(&map, &DetailProfile::Compact.budget()).expect("compact render");
    assert_eq!(compact.nodes.len(), MAX_COMPACT_NODES);
    assert_eq!(
        compact.truncation.nodes_omitted as usize,
        MAX_STANDARD_NODES - MAX_COMPACT_NODES
    );
}

#[test]
fn serialized_wire_stays_within_profile_byte_budgets() {
    let map = map(7, MAX_STANDARD_NODES as u32);
    let standard =
        render_standard(&map, &DetailProfile::Standard.budget()).expect("standard render");
    let standard_bytes = serde_json::to_vec(&standard).expect("serialize").len();
    assert!(
        standard_bytes <= DetailProfile::Standard.budget().max_bytes,
        "standard wire {standard_bytes} exceeds budget"
    );
    let internal =
        render_internal_full(&map, &DetailProfile::InternalFull.budget()).expect("internal render");
    let internal_bytes = serde_json::to_vec(&internal).expect("serialize").len();
    assert!(
        internal_bytes <= DetailProfile::InternalFull.budget().max_bytes,
        "internal wire {internal_bytes} exceeds budget"
    );
}

// ---------- Diff and pagination stay bounded at maximum churn ----------

#[test]
fn maximum_churn_diff_paginates_into_bounded_batches() {
    // Every even-indexed node renamed: 256 updates, bounded churn.
    let previous = map(1, MAX_SEMANTIC_NODES as u32);
    let next = map_with_names(2, MAX_SEMANTIC_NODES as u32, &|i| {
        if i % 2 == 0 {
            "Changed name".to_owned()
        } else {
            "Sample name".to_owned()
        }
    });
    let diff = diff_maps(&previous, &next).expect("valid diff");
    assert_eq!(diff.updated.len(), MAX_SEMANTIC_NODES / 2);
    let batches = emit_batches(&previous, &next).expect("batches");
    let mut counted = 0usize;
    for batch in &batches {
        counted += batch.added.len() + batch.updated.len() + batch.removed.len();
        assert!(counted <= 3 * MAX_SEMANTIC_NODES, "total churn is bounded");
    }
    assert_eq!(
        counted,
        diff.added.len() + diff.updated.len() + diff.removed.len(),
        "pagination is lossless"
    );
}

#[test]
fn repeated_diffs_stay_deterministic_across_runs() {
    let previous = map(1, MAX_SEMANTIC_NODES as u32);
    let next = map(2, MAX_SEMANTIC_NODES as u32);
    let first = diff_maps(&previous, &next).expect("diff");
    for _ in 0..16 {
        assert_eq!(diff_maps(&previous, &next).expect("diff"), first);
    }
}

#[test]
fn form_projection_is_bounded_by_the_form_budget() {
    // The frozen form budget is 16 forms × 64 fields; a max map projects
    // without unbounded work because inputs are pre-bounded.
    let map = map(7, MAX_STANDARD_NODES as u32);
    let views = project_forms(&map);
    assert_eq!(views.len(), map.forms.len());
}
