//! Detail profile tests (ACT-02, AC-002): closed profile set with stable
//! wire names, per-profile budgets with reported truncation, raw-surface
//! non-expressibility on oversized pages, and fail-closed byte bounds.

use crayon_domain::{
    ActionKind, ActionOffer, ElementState, FormField, FormMap, MediaElement, MediaKind, MediaState,
    PageMap, RiskEntry, RiskLevel, RiskReason, SemanticNode, SemanticNodeId, SemanticNodeKind,
    SemanticSchemaError, SemanticTruncation, SessionGeneration, TabId,
};
use crayon_semantic_action::{
    render_compact, render_internal_full, render_standard, CompactMap, DetailBudget, DetailProfile,
    InternalFullMap, MAX_COMPACT_ACTIONS, MAX_COMPACT_NODES, MAX_STANDARD_NODES,
};

fn node_id(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn node(raw: &str, kind: SemanticNodeKind) -> SemanticNode {
    SemanticNode::new(
        node_id(raw),
        kind,
        "Sample".to_owned(),
        ElementState {
            enabled: true,
            visible: true,
            ..ElementState::default()
        },
    )
    .expect("valid node")
}

/// Builds a map with `count` nodes, one action on every third node, plus a
/// form, a media element and a risk entry anchored inside the kept range.
fn large_map(count: u32) -> PageMap {
    let nodes: Vec<SemanticNode> = (0..count)
        .map(|index| node(&format!("n-{index:06}"), SemanticNodeKind::Button))
        .collect();
    let actions: Vec<ActionOffer> = (0..count)
        .step_by(3)
        .map(|index| {
            ActionOffer::new(
                node_id(&format!("n-{index:06}")),
                ActionKind::Click,
                "Submit".to_owned(),
            )
            .expect("valid offer")
        })
        .collect();
    PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(1),
        7,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        nodes,
        actions,
        vec![FormMap::new(
            node_id("n-000000"),
            vec![FormField {
                node: node_id("n-000001"),
                kind: SemanticNodeKind::TextInput,
                label: "Email".to_owned(),
                required: true,
                read_only: false,
                max_length: Some(64),
                filled: false,
                error_text: None,
            }]
            .into_iter()
            .collect(),
        )
        .expect("valid form")]
        .into_iter()
        .collect(),
        vec![MediaElement::new(
            node_id("n-000002"),
            MediaKind::Video,
            MediaState::Paused,
            false,
            true,
            None,
        )
        .expect("valid media")]
        .into_iter()
        .collect(),
        vec![RiskEntry::new(
            node_id("n-000003"),
            RiskLevel::R4,
            vec![RiskReason::AmbiguousMatch],
        )
        .expect("valid risk")]
        .into_iter()
        .collect(),
        SemanticTruncation::default(),
    )
    .expect("valid map")
}

// ---------- Closed profile vocabulary ----------

#[test]
fn profiles_are_closed_with_stable_wire_names() {
    assert_eq!(DetailProfile::ALL.len(), 3);
    assert_eq!(DetailProfile::default(), DetailProfile::Compact);
    // Budgets differ per profile and are frozen.
    let (compact, standard, internal) = (
        DetailProfile::Compact.budget(),
        DetailProfile::Standard.budget(),
        DetailProfile::InternalFull.budget(),
    );
    assert_eq!(compact.max_nodes, MAX_COMPACT_NODES);
    assert_eq!(compact.max_actions, MAX_COMPACT_ACTIONS);
    assert_eq!(standard.max_nodes, MAX_STANDARD_NODES);
    assert!(compact.max_bytes < standard.max_bytes);
    assert!(standard.max_bytes < internal.max_bytes);
    // A budget can never exceed the frozen map maxima.
    assert_eq!(
        DetailBudget::new(513, 10, 0, 0, 0, 1024),
        Err(SemanticSchemaError::BudgetExceeded("detail budget"))
    );
    assert_eq!(
        DetailBudget::new(0, 10, 0, 0, 0, 1024),
        Err(SemanticSchemaError::BudgetExceeded("detail budget"))
    );
    assert!(DetailBudget::new(8, 4, 1, 1, 1, 4096).is_ok());
}

// ---------- Compact projection ----------

#[test]
fn compact_omits_state_and_sensitive_collections_by_design() {
    let map = large_map(12);
    let compact = render_compact(&map, &DetailProfile::Compact.budget()).expect("compact render");
    let wire = serde_json::to_string(&compact).expect("serialize");
    // No state, form structure, media facts or risk entries on the wire.
    for forbidden in ["\"state\"", "\"forms\"", "\"media\"", "\"risk\""] {
        assert!(!wire.contains(forbidden), "{forbidden} leaked: {wire}");
    }
    assert!(wire.contains("\"form_count\":1"));
    assert!(wire.contains("\"media_count\":1"));
    assert!(wire.contains("\"risk_count\":1"));
    assert_eq!(compact.nodes.len(), 12);
    assert_eq!(compact.actions.len(), 4);
    assert_eq!(compact.truncation, SemanticTruncation::default());
}

#[test]
fn compact_truncates_oversized_pages_with_reported_counts() {
    let map = large_map(MAX_COMPACT_NODES as u32 + 24);
    let compact = render_compact(&map, &DetailProfile::Compact.budget()).expect("compact render");
    assert_eq!(compact.nodes.len(), MAX_COMPACT_NODES);
    assert!(compact.truncation.nodes_omitted >= 24);
    assert!(compact.truncation.any());
    // Offers anchored on omitted nodes are omitted as actions.
    assert!(compact.actions.len() <= MAX_COMPACT_ACTIONS);
    // Counts still describe the full verified map.
    assert_eq!(compact.form_count, 1);
    assert_eq!(compact.media_count, 1);
    assert_eq!(compact.risk_count, 1);
    // The frozen map itself caps at 512 nodes; 512 renders without error.
    let full = large_map(512);
    let rendered = render_compact(&full, &DetailProfile::Compact.budget()).expect("512 nodes fit");
    assert_eq!(rendered.nodes.len(), MAX_COMPACT_NODES);
}

// ---------- Standard projection ----------

#[test]
fn standard_returns_the_frozen_map_within_budget() {
    let map = large_map(32);
    let standard =
        render_standard(&map, &DetailProfile::Standard.budget()).expect("standard render");
    assert_eq!(standard, map);
    // A budget smaller than the map fails closed instead of truncating.
    let tiny = DetailBudget::new(8, 8, 1, 1, 1, 1_048_576).expect("valid budget");
    assert_eq!(
        render_standard(&map, &tiny),
        Err(SemanticSchemaError::BudgetExceeded("standard map"))
    );
}

#[test]
fn standard_never_surfaces_raw_surfaces_on_large_pages() {
    let map = large_map(MAX_STANDARD_NODES as u32);
    let standard = render_standard(&map, &DetailProfile::Standard.budget()).expect("render");
    let wire = serde_json::to_string(&standard).expect("serialize");
    for forbidden in ["selector", "\"html\"", "\"dom\"", "xpath", "javascript"] {
        assert!(!wire.contains(forbidden), "{forbidden} leaked");
    }
}

// ---------- Internal-full projection ----------

#[test]
fn internal_full_adds_closed_annotations_only() {
    let mut map = large_map(6);
    // Sensitivity is derived from the frozen kind policy.
    map.nodes
        .push(node("n-900001", SemanticNodeKind::PasswordInput));
    map.nodes
        .push(node("n-900002", SemanticNodeKind::FileInput));
    let map = PageMap::new(
        map.tab_id.clone(),
        map.generation,
        map.revision,
        map.origin.clone(),
        map.title.clone(),
        map.nodes,
        map.actions,
        map.forms,
        map.media,
        map.risk,
        SemanticTruncation::default(),
    )
    .expect("valid map");
    let internal =
        render_internal_full(&map, &DetailProfile::InternalFull.budget()).expect("internal render");
    assert_eq!(internal.annotations.len(), 8);
    assert_eq!(internal.annotations[0].ordinal, 0);
    assert!(!internal.annotations[0].sensitive);
    assert!(internal.annotations[6].sensitive);
    assert!(internal.annotations[7].sensitive);
    let wire = serde_json::to_string(&internal).expect("serialize");
    for forbidden in ["selector", "\"html\"", "\"dom\"", "xpath", "javascript"] {
        assert!(!wire.contains(forbidden), "{forbidden} leaked");
    }
    assert!(wire.contains("\"sensitive\":true"));
    // The embedded map is the frozen map unchanged.
    assert_eq!(internal.map, map);
}

// ---------- Byte budget fails closed ----------

#[test]
fn byte_budget_fails_closed_instead_of_streaming() {
    let map = large_map(64);
    let compact = render_compact(&map, &DetailProfile::Compact.budget()).expect("compact render");
    let real_bytes = serde_json::to_vec(&compact).expect("serialize").len();
    let too_small = DetailBudget::new(128, 64, 0, 0, 0, real_bytes - 1).expect("valid budget");
    assert_eq!(
        render_compact(&map, &too_small),
        Err(SemanticSchemaError::BudgetExceeded("detail response bytes"))
    );
    let fits = DetailBudget::new(128, 64, 0, 0, 0, real_bytes).expect("valid budget");
    assert!(render_compact(&map, &fits).is_ok());
}

// ---------- Wire form denies unknown fields ----------

#[test]
fn compact_wire_rejects_unknown_and_raw_fields() {
    let raw = r#"{"schema_version":1,"tab_id":"tab-1","generation":1,"revision":1,"origin":"https://example.com","title":"t","nodes":[],"actions":[],"form_count":0,"media_count":0,"risk_count":0,"truncation":{},"dom":"<html>"}"#;
    let parsed: Result<CompactMap, _> = serde_json::from_str(raw);
    assert!(parsed.is_err(), "raw DOM payloads must be rejected");
    let raw = r##"{"map":{"schema_version":1,"tab_id":"tab-1","generation":1,"revision":1,"origin":"https://example.com","title":"t","nodes":[],"actions":[],"forms":[],"media":[],"risk":[],"truncation":{}},"annotations":[],"selector":"#btn"}"##;
    let parsed: Result<InternalFullMap, _> = serde_json::from_str(raw);
    assert!(parsed.is_err(), "selectors must be rejected");
}
