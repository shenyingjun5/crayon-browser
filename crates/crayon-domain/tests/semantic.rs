//! Semantic map schema tests (ACT-01, AC-001): closed vocabularies with
//! exact wire names, forbidden-surface non-expressibility, budget
//! enforcement, cross-reference integrity and outcome/reason pairing.

use crayon_domain::{
    ActionKind, ActionOffer, ChangeSet, EffectOutcome, EffectReason, ElementState, FormField,
    FormMap, MediaElement, MediaKind, MediaState, PageMap, RiskEntry, RiskLevel, RiskReason,
    SemanticError, SemanticNode, SemanticNodeId, SemanticNodeKind, SemanticSchemaError,
    SemanticTruncation, SessionGeneration, TabId, SEMANTIC_MAP_SCHEMA_VERSION,
};

fn node_id(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn sample_node(raw: &str, kind: SemanticNodeKind) -> SemanticNode {
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

// ---------- Closed vocabularies (wire names are the contract) ----------

#[test]
fn node_kinds_are_closed_and_include_sensitive_surfaces() {
    assert_eq!(SemanticNodeKind::ALL.len(), 20);
    let wire: Vec<String> = SemanticNodeKind::ALL
        .iter()
        .map(|kind| serde_json::to_string(kind).expect("serialize"))
        .collect();
    assert_eq!(
        wire,
        vec![
            "\"button\"",
            "\"link\"",
            "\"text_input\"",
            "\"password_input\"",
            "\"file_input\"",
            "\"checkbox\"",
            "\"radio\"",
            "\"select\"",
            "\"slider\"",
            "\"textarea\"",
            "\"tab\"",
            "\"menu_item\"",
            "\"heading\"",
            "\"text\"",
            "\"image\"",
            "\"table\"",
            "\"form\"",
            "\"media\"",
            "\"region\"",
            "\"other\"",
        ]
    );
    assert!(SemanticNodeKind::PasswordInput.sensitive());
    assert!(SemanticNodeKind::FileInput.sensitive());
    assert!(!SemanticNodeKind::Button.sensitive());
}

#[test]
fn action_kinds_are_closed_and_express_no_scripting() {
    assert_eq!(ActionKind::ALL.len(), 6);
    let wire: Vec<String> = ActionKind::ALL
        .iter()
        .map(|kind| serde_json::to_string(kind).expect("serialize"))
        .collect();
    assert_eq!(
        wire,
        vec![
            "\"click\"",
            "\"set_text\"",
            "\"select_option\"",
            "\"check\"",
            "\"uncheck\"",
            "\"clear\"",
        ]
    );
}

#[test]
fn risk_reasons_and_media_vocabularies_are_closed() {
    assert_eq!(RiskReason::ALL.len(), 9);
    assert_eq!(MediaKind::ALL.len(), 2);
    assert_eq!(MediaState::ALL.len(), 5);
    for forbidden in [
        "\"screenshot\"",
        "\"ocr\"",
        "\"javascript\"",
        "\"drag\"",
        "\"upload\"",
        "\"type_password\"",
        "\"wait\"",
    ] {
        assert!(serde_json::from_str::<ActionKind>(forbidden).is_err());
        assert!(serde_json::from_str::<RiskReason>(forbidden).is_err());
    }
}

#[test]
fn error_codes_are_closed_and_stable() {
    assert_eq!(SemanticError::ALL.len(), 11);
    let wire: Vec<String> = SemanticError::ALL
        .iter()
        .map(|code| serde_json::to_string(code).expect("serialize"))
        .collect();
    assert_eq!(
        wire,
        vec![
            "\"map_unavailable\"",
            "\"handle_unknown\"",
            "\"handle_expired\"",
            "\"handle_stale\"",
            "\"precondition_failed\"",
            "\"not_actionable\"",
            "\"sensitive_target_denied\"",
            "\"revision_stale\"",
            "\"profile_mismatch\"",
            "\"effect_indeterminate\"",
            "\"effect_failed\"",
        ]
    );
}

// ---------- Identity, budgets and cross-references ----------

#[test]
fn node_ids_reject_selectors_and_bad_tokens() {
    for hostile in [
        "div.container > button", // CSS selector shape
        "/html/body/div[1]",      // XPath shape
        "javascript:alert(1)",    // scheme
        "",                       // empty
        "UPPER_CASE",             // charset
        "a b",                    // space
        "é",                      // non-ASCII
    ] {
        assert!(SemanticNodeId::new(hostile).is_err(), "{hostile}");
    }
    let long = "a".repeat(65);
    assert_eq!(
        SemanticNodeId::new(&long),
        Err(SemanticSchemaError::TokenInvalid)
    );
    assert!(SemanticNodeId::new("n-0001.abc:x_9").is_ok());
}

#[test]
fn page_map_enforces_bounds_and_references() {
    let mut nodes = Vec::new();
    for index in 0..512u32 {
        nodes.push(sample_node(
            &format!("n-{index:06}"),
            SemanticNodeKind::Button,
        ));
    }
    let extra = sample_node("n-999999", SemanticNodeKind::Button);
    let mut builder_nodes = nodes.clone();
    builder_nodes.push(extra);
    assert!(PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(1),
        7,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        builder_nodes,
        Vec::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    )
    .is_err());

    let map = PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(1),
        7,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        nodes,
        vec![
            ActionOffer::new(node_id("n-000001"), ActionKind::Click, "Submit".to_owned())
                .expect("offer"),
        ],
        Vec::new(),
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    )
    .expect("valid map");
    // An action referencing an unknown node fails closed.
    let dangling = PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(1),
        7,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        Vec::new(),
        vec![
            ActionOffer::new(node_id("n-000001"), ActionKind::Click, "Submit".to_owned())
                .expect("offer"),
        ],
        Vec::new(),
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    );
    assert_eq!(dangling, Err(SemanticSchemaError::UnknownNode));
    assert_eq!(map.schema_version, SEMANTIC_MAP_SCHEMA_VERSION);
}

#[test]
fn origins_are_closed_to_scheme_host_port_only() {
    for ok in ["https://example.com", "http://a.b:8080"] {
        assert!(crayon_domain::is_valid_origin(ok), "{ok}");
    }
    for hostile in [
        "https://example.com/path",
        "https://example.com?q=1",
        "https://example.com#f",
        "https://user@example.com",
        "ftp://example.com",
        "javascript://example.com",
        "https://",
        "",
    ] {
        assert!(!crayon_domain::is_valid_origin(hostile), "{hostile}");
    }
}

#[test]
fn form_maps_carry_no_values_and_restrict_kinds() {
    let form = FormMap::new(
        node_id("n-form"),
        vec![FormField {
            node: node_id("n-input"),
            kind: SemanticNodeKind::TextInput,
            label: "Email".to_owned(),
            required: true,
            read_only: false,
            max_length: Some(64),
            filled: false,
            error_text: None,
        }],
    )
    .expect("valid form");
    let wire = serde_json::to_string(&form).expect("serialize");
    // No value surface exists in the wire form.
    assert!(!wire.contains("value"));
    assert!(FormMap::new(
        node_id("n-form"),
        vec![FormField {
            node: node_id("n-heading"),
            kind: SemanticNodeKind::Heading,
            label: "x".to_owned(),
            required: false,
            read_only: false,
            max_length: None,
            filled: false,
            error_text: None,
        }],
    )
    .is_err());
}

#[test]
fn risk_entries_are_bounded_and_deduplicated() {
    let entry = RiskEntry::new(
        node_id("n-password"),
        RiskLevel::R4,
        vec![
            RiskReason::SensitiveCredential,
            RiskReason::SensitiveCredential,
        ],
    );
    assert_eq!(
        entry,
        Err(SemanticSchemaError::DuplicateEntry("risk reason"))
    );
    let entry = RiskEntry::new(
        node_id("n-password"),
        RiskLevel::R4,
        vec![RiskReason::SensitiveCredential],
    )
    .expect("valid entry");
    assert_eq!(entry.node, node_id("n-password"));
}

#[test]
fn change_sets_require_monotonic_revisions() {
    let base = ChangeSet::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        10,
        11,
        false,
        SemanticTruncation::default(),
        vec![sample_node("n-new", SemanticNodeKind::Link)],
        Vec::new(),
        vec![node_id("n-gone")],
    );
    assert!(base.is_ok());
    assert_eq!(
        ChangeSet::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(3),
            11,
            11,
            false,
            SemanticTruncation::default(),
            Vec::new(),
            Vec::new(),
            Vec::new(),
        ),
        Err(SemanticSchemaError::RevisionNotMonotonic)
    );
}

// ---------- Outcome/reason pairing ----------

#[test]
fn effect_reports_pair_outcomes_and_reasons() {
    let ok = crayon_domain::EffectReport::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(1),
        5,
        ActionKind::Click,
        node_id("n-btn"),
        EffectOutcome::Verified,
        None,
        None,
    );
    assert!(ok.is_ok());
    // verified + reason is not expressible.
    assert_eq!(
        crayon_domain::EffectReport::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(1),
            5,
            ActionKind::Click,
            node_id("n-btn"),
            EffectOutcome::Verified,
            Some(EffectReason::Timeout),
            None,
        ),
        Err(SemanticSchemaError::InvalidOutcome)
    );
    // failed/indeterminate require a reason.
    assert_eq!(
        crayon_domain::EffectReport::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(1),
            5,
            ActionKind::Click,
            node_id("n-btn"),
            EffectOutcome::Indeterminate,
            None,
            None,
        ),
        Err(SemanticSchemaError::InvalidOutcome)
    );
}

// ---------- Wire form denies unknown fields ----------

#[test]
fn wire_forms_deny_unknown_fields() {
    let raw = r#"{"id":"n-1","kind":"button","name":"x","state":{"enabled":true,"visible":true,"pressed":true}}"#;
    let parsed: Result<SemanticNode, _> = serde_json::from_str(raw);
    assert!(parsed.is_err(), "unknown state fields must be rejected");
    let raw = r#"{"schema_version":1,"tab_id":1,"generation":1,"revision":1,"origin":"https://example.com","title":"t","nodes":[],"actions":[],"forms":[],"media":[],"risk":[],"truncation":{},"dom":"<html>"}"#;
    let parsed: Result<PageMap, _> = serde_json::from_str(raw);
    assert!(parsed.is_err(), "raw DOM payloads must be rejected");
}

#[test]
fn media_elements_roundtrip_through_wire() {
    let media = MediaElement::new(
        node_id("n-video"),
        MediaKind::Video,
        MediaState::Paused,
        false,
        true,
        Some(90_000),
    )
    .expect("valid media");
    let wire = serde_json::to_string(&media).expect("serialize");
    assert!(wire.contains("\"video\""));
    assert!(wire.contains("\"paused\""));
}
