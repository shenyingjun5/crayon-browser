//! Form projection tests (ACT-09, AC-009): no value surface, sensitive and
//! file exclusions, hidden/disabled/read-only fail-closed rows, filled and
//! error state passthrough, and wire rejection of unknown/raw fields.

use crayon_domain::{
    ElementState, FormField, FormMap, PageMap, SemanticNode, SemanticNodeId, SemanticNodeKind,
    SemanticTruncation, SessionGeneration, TabId,
};
use crayon_semantic_action::{project_form, project_forms, FieldExclusion};

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

fn field(node: &str, kind: SemanticNodeKind, label: &str) -> FormField {
    FormField {
        node: node_id(node),
        kind,
        label: label.to_owned(),
        required: true,
        read_only: false,
        max_length: Some(64),
        filled: false,
        error_text: None,
    }
}

fn form_with(fields: Vec<FormField>) -> FormMap {
    FormMap::new(node_id("n-form"), fields).expect("valid form")
}

#[test]
fn projection_never_carries_values_or_pattern_surfaces() {
    let form = form_with(vec![field("n-1", SemanticNodeKind::TextInput, "Email")]);
    let view = project_form(&form, &|_| Some(visible_enabled()));
    let wire = serde_json::to_string(&view).expect("serialize");
    // No value/pattern/placeholder surface exists in the wire form.
    for forbidden in [
        "\"value\"",
        "\"pattern\"",
        "\"placeholder\"",
        "\"dom\"",
        "selector",
    ] {
        assert!(!wire.contains(forbidden), "{forbidden} leaked: {wire}");
    }
    assert!(wire.contains("\"filled\":false"));
    assert!(wire.contains("\"max_length\":64"));
}

fn visible_enabled() -> ElementState {
    ElementState {
        enabled: true,
        visible: true,
        ..ElementState::default()
    }
}

#[test]
fn sensitive_and_file_fields_are_always_excluded() {
    let form = form_with(vec![
        field("n-1", SemanticNodeKind::TextInput, "Email"),
        field("n-2", SemanticNodeKind::PasswordInput, "Password"),
        field("n-3", SemanticNodeKind::FileInput, "Attachment"),
    ]);
    let view = project_form(&form, &|id| {
        (id.as_str() != "n-3").then_some(visible_enabled())
    });
    assert_eq!(view.fields[0].excluded, None);
    assert_eq!(
        view.fields[1].excluded,
        Some(FieldExclusion::SensitiveCredential)
    );
    assert_eq!(view.fields[2].excluded, Some(FieldExclusion::FileUpload));
    // Excluded rows never survive the actionable iterator, even when the
    // state lookup tries to rehabilitate them.
    let actionable: Vec<_> = view.actionable_fields().map(|f| f.node.as_str()).collect();
    assert_eq!(actionful_vec(actionable), vec!["n-1"]);
}

fn actionful_vec(items: Vec<&str>) -> Vec<String> {
    items
        .into_iter()
        .map(std::string::ToString::to_string)
        .collect()
}

#[test]
fn hidden_disabled_and_read_only_fail_closed() {
    let mut read_only_field = field("n-1", SemanticNodeKind::TextInput, "Frozen");
    read_only_field.read_only = true;
    let form = form_with(vec![
        read_only_field,
        field("n-2", SemanticNodeKind::TextInput, "Missing"),
        field("n-3", SemanticNodeKind::TextInput, "Hidden"),
        field("n-4", SemanticNodeKind::TextInput, "Disabled"),
    ]);
    let view = project_form(&form, &|id| match id.as_str() {
        "n-2" => None, // fail closed: no verified state means hidden
        "n-3" => Some(ElementState {
            enabled: true,
            visible: false,
            ..ElementState::default()
        }),
        "n-4" => Some(ElementState {
            enabled: false,
            visible: true,
            ..ElementState::default()
        }),
        _ => Some(visible_enabled()),
    });
    assert_eq!(view.fields[0].excluded, Some(FieldExclusion::ReadOnly));
    assert_eq!(view.fields[1].excluded, Some(FieldExclusion::Hidden));
    assert_eq!(view.fields[2].excluded, Some(FieldExclusion::Hidden));
    assert_eq!(view.fields[3].excluded, Some(FieldExclusion::Disabled));
    assert!(view.actionable_fields().next().is_none());
}

#[test]
fn filled_and_error_state_pass_through_bounded() {
    let mut error_field = field("n-1", SemanticNodeKind::TextInput, "Email");
    error_field.filled = true;
    error_field.error_text = Some("invalid email".to_owned());
    let form = form_with(vec![error_field]);
    let view = project_form(&form, &|_| Some(visible_enabled()));
    assert!(view.fields[0].filled);
    assert_eq!(view.fields[0].error_text.as_deref(), Some("invalid email"));
}

#[test]
fn page_map_projection_covers_all_forms() {
    let map = PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        7,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        vec![
            node("n-form", SemanticNodeKind::Form),
            node("n-1", SemanticNodeKind::TextInput),
            node("n-2", SemanticNodeKind::PasswordInput),
        ],
        Vec::new(),
        vec![form_with(vec![
            field("n-1", SemanticNodeKind::TextInput, "Email"),
            field("n-2", SemanticNodeKind::PasswordInput, "Password"),
        ])],
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    )
    .expect("valid map");
    let views = project_forms(&map);
    assert_eq!(views.len(), 1);
    assert_eq!(views[0].fields.len(), 2);
    assert_eq!(
        views[0].fields[1].excluded,
        Some(FieldExclusion::SensitiveCredential)
    );
    let wire = serde_json::to_string(&views).expect("serialize");
    assert!(wire.contains("\"required\":true"));
    // Wire rejects raw payload injection and unknown fields.
    let raw = r#"{"node":"n-form","fields":[],"dom":"<html>"}"#;
    let parsed: Result<crayon_semantic_action::FormView, _> = serde_json::from_str(raw);
    assert!(parsed.is_err(), "raw payloads must be rejected");
}
