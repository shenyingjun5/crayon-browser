//! Precondition evaluation tests (ACT-05, AC-005): fail-closed denials for
//! stale, hidden, disabled, cross-origin, ambiguous and sensitive targets;
//! closed check/violation vocabularies; stable ordering and side-effect-free
//! holds.

use crayon_domain::{ActionKind, ElementState, SemanticNodeKind, SemanticSchemaError};
use crayon_semantic_action::{
    evaluate, is_actionable, PreconditionCheck, PreconditionInput, PreconditionViolation,
};

const ORIGIN: &str = "https://example.com";
const OTHER_ORIGIN: &str = "https://other.example";

fn visible_enabled() -> ElementState {
    ElementState {
        enabled: true,
        visible: true,
        ..ElementState::default()
    }
}

fn input<'a>(
    kind: SemanticNodeKind,
    state: &'a ElementState,
    action: ActionKind,
) -> PreconditionInput<'a> {
    PreconditionInput {
        kind,
        state,
        action,
        bound_origin: ORIGIN,
        current_origin: ORIGIN,
        bound_revision: 7,
        current_revision: 7,
        unique_target: true,
    }
}

// ---------- Closed vocabularies ----------

#[test]
fn checks_and_violations_are_closed() {
    assert_eq!(PreconditionCheck::ALL.len(), 6);
    assert_eq!(PreconditionViolation::ALL.len(), 7);
    let wire = serde_json::to_string(&PreconditionViolation::Hidden).expect("serialize");
    assert_eq!(wire, "\"hidden\"");
    let wire = serde_json::to_string(&PreconditionCheck::RevisionCurrent).expect("serialize");
    assert_eq!(wire, "\"revision_current\"");
}

// ---------- Holds ----------

#[test]
fn all_holds_allow_execution_without_side_effects() {
    let state = visible_enabled();
    let report =
        evaluate(&input(SemanticNodeKind::Button, &state, ActionKind::Click)).expect("valid input");
    assert!(report.holds());
    assert!(report.violations.is_empty());
    let wire = serde_json::to_string(&report).expect("serialize");
    assert_eq!(wire, r#"{"violations":[]}"#);
}

// ---------- Individual fail-closed denials ----------

#[test]
fn hidden_disabled_and_revision_stale_fail_closed() {
    let mut state = visible_enabled();
    state.visible = false;
    let report =
        evaluate(&input(SemanticNodeKind::Button, &state, ActionKind::Click)).expect("valid input");
    assert_eq!(report.violations, vec![PreconditionViolation::Hidden]);

    let mut state = visible_enabled();
    state.enabled = false;
    let report =
        evaluate(&input(SemanticNodeKind::Button, &state, ActionKind::Click)).expect("valid input");
    assert_eq!(report.violations, vec![PreconditionViolation::Disabled]);

    let state = visible_enabled();
    let mut stale = input(SemanticNodeKind::Button, &state, ActionKind::Click);
    stale.current_revision = 8;
    let report = evaluate(&stale).expect("valid input");
    assert_eq!(
        report.violations,
        vec![PreconditionViolation::RevisionStale]
    );
}

#[test]
fn cross_origin_fails_closed() {
    let state = visible_enabled();
    let mut crossed = input(SemanticNodeKind::Button, &state, ActionKind::Click);
    crossed.current_origin = OTHER_ORIGIN;
    let report = evaluate(&crossed).expect("valid input");
    assert_eq!(
        report.violations,
        vec![PreconditionViolation::OriginMismatch]
    );
}

#[test]
fn ambiguous_target_fails_closed() {
    let state = visible_enabled();
    let mut ambiguous = input(SemanticNodeKind::Button, &state, ActionKind::Click);
    ambiguous.unique_target = false;
    let report = evaluate(&ambiguous).expect("valid input");
    assert_eq!(
        report.violations,
        vec![PreconditionViolation::AmbiguousTarget]
    );
}

// ---------- Kind actionability ----------

#[test]
fn kind_action_table_is_closed_and_excludes_sensitive_kinds() {
    // Positive rows of the frozen table.
    assert!(is_actionable(SemanticNodeKind::Button, ActionKind::Click));
    assert!(is_actionable(SemanticNodeKind::Link, ActionKind::Click));
    assert!(is_actionable(
        SemanticNodeKind::TextInput,
        ActionKind::SetText
    ));
    assert!(is_actionable(
        SemanticNodeKind::TextInput,
        ActionKind::Clear
    ));
    assert!(is_actionable(
        SemanticNodeKind::Textarea,
        ActionKind::SetText
    ));
    assert!(is_actionable(
        SemanticNodeKind::Select,
        ActionKind::SelectOption
    ));
    assert!(is_actionable(SemanticNodeKind::Checkbox, ActionKind::Check));
    assert!(is_actionable(SemanticNodeKind::Radio, ActionKind::Uncheck));
    // Mismatched pairs.
    assert!(!is_actionable(
        SemanticNodeKind::Button,
        ActionKind::SetText
    ));
    assert!(!is_actionable(
        SemanticNodeKind::TextInput,
        ActionKind::Click
    ));
    assert!(!is_actionable(SemanticNodeKind::Heading, ActionKind::Click));
    // Sensitive kinds are never actionable, for any action.
    for action in [
        ActionKind::Click,
        ActionKind::SetText,
        ActionKind::SelectOption,
        ActionKind::Check,
        ActionKind::Uncheck,
        ActionKind::Clear,
    ] {
        assert!(!is_actionable(SemanticNodeKind::PasswordInput, action));
        assert!(!is_actionable(SemanticNodeKind::FileInput, action));
    }
}

#[test]
fn sensitive_target_is_reported_not_kind_mismatch() {
    let state = visible_enabled();
    let report = evaluate(&input(
        SemanticNodeKind::PasswordInput,
        &state,
        ActionKind::SetText,
    ))
    .expect("valid input");
    assert_eq!(
        report.violations,
        vec![PreconditionViolation::SensitiveTarget]
    );
}

#[test]
fn violations_are_stable_ordered_and_deduplicated() {
    let mut state = visible_enabled();
    state.visible = false;
    state.enabled = false;
    let mut worst = input(SemanticNodeKind::PasswordInput, &state, ActionKind::Click);
    worst.current_origin = OTHER_ORIGIN;
    worst.current_revision = 9;
    worst.unique_target = false;
    let report = evaluate(&worst).expect("valid input");
    assert_eq!(
        report.violations,
        vec![
            PreconditionViolation::Hidden,
            PreconditionViolation::Disabled,
            PreconditionViolation::SensitiveTarget,
            PreconditionViolation::OriginMismatch,
            PreconditionViolation::RevisionStale,
            PreconditionViolation::AmbiguousTarget,
        ]
    );
}

// ---------- Invalid origin fails the evaluation itself ----------

#[test]
fn malformed_origins_are_rejected_not_matched() {
    let state = visible_enabled();
    let mut malformed = input(SemanticNodeKind::Button, &state, ActionKind::Click);
    malformed.bound_origin = "https://example.com/path";
    assert_eq!(
        evaluate(&malformed),
        Err(SemanticSchemaError::OriginInvalid)
    );
    malformed.bound_origin = ORIGIN;
    malformed.current_origin = "";
    assert_eq!(
        evaluate(&malformed),
        Err(SemanticSchemaError::OriginInvalid)
    );
}

// ---------- Wire form rejects unknown fields ----------

#[test]
fn report_wire_rejects_unknown_fields() {
    let raw = r#"{"violations":[],"dom":"<html>"}"#;
    let parsed: Result<crayon_semantic_action::PreconditionReport, _> = serde_json::from_str(raw);
    assert!(parsed.is_err(), "raw payloads must be rejected");
}
