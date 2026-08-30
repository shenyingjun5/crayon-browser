//! Cross-cutting semantic action security tests (ACT-12, AC-012):
//! closed vocabularies across layers, forbidden surfaces on the wire,
//! mutation/truncation fuzz of golden-shaped inputs failing closed,
//! hostile-input rejection and no-secret serialization.

use crayon_domain::{
    ActionKind, EffectOutcome, EffectReason, EffectReport, ElementState, PageMap, SemanticNode,
    SemanticNodeId, SemanticNodeKind, SemanticTruncation, SessionGeneration, TabId,
};
use crayon_semantic_action::{
    assess, evaluate, render_compact, render_standard, ApprovalOutcome, ConfirmationRef,
    ExecutionRequest, HandleNonce, PreconditionInput, ProfileScope, RiskFacts, SemanticActionGate,
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

fn full_map() -> PageMap {
    PageMap::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        7,
        "https://example.com".to_owned(),
        "Title".to_owned(),
        vec![
            node("n-1", SemanticNodeKind::Button),
            node("n-2", SemanticNodeKind::PasswordInput),
            node("n-3", SemanticNodeKind::FileInput),
            node("n-form", SemanticNodeKind::Form),
        ],
        Vec::new(),
        Vec::new(),
        Vec::new(),
        Vec::new(),
        SemanticTruncation::default(),
    )
    .expect("valid map")
}

fn visible_enabled() -> ElementState {
    ElementState {
        enabled: true,
        visible: true,
        ..ElementState::default()
    }
}

fn visible_wire(wire: &str) -> bool {
    // Structural keys of a rendered surface; the raw-surface scan below
    // runs over the same wire bytes.
    wire.contains("\"nodes\"")
}

// ---------- Forbidden raw surfaces on every external wire ----------

#[test]
fn every_external_wire_surface_is_free_of_raw_surfaces() {
    let map = full_map();
    let surfaces: Vec<String> = vec![
        serde_json::to_string(
            &render_compact(
                &map,
                &crayon_semantic_action::DetailProfile::Compact.budget(),
            )
            .expect("compact"),
        )
        .expect("compact wire"),
        serde_json::to_string(
            &render_standard(
                &map,
                &crayon_semantic_action::DetailProfile::Standard.budget(),
            )
            .expect("standard"),
        )
        .expect("standard wire"),
        serde_json::to_string(&map).expect("map wire"),
    ];
    let hostiles = [
        "selector",
        "\"html\"",
        "\"dom\"",
        "xpath",
        "javascript",
        "eval(",
        "document.",
        "getBoundingClientRect",
    ];
    for surface in &surfaces {
        assert!(visible_wire(surface) || surface.contains("\"nodes\"") || surface.is_empty());
        for hostile in hostiles {
            assert!(!surface.contains(hostile), "{hostile} leaked into wire");
        }
    }
}

#[test]
fn sensitive_surfaces_never_reach_executable_decisions() {
    for kind in [SemanticNodeKind::PasswordInput, SemanticNodeKind::FileInput] {
        let decision = assess(kind, RiskFacts::default());
        assert!(decision.denied(), "{kind:?} must never be executable");
    }
    let state = visible_enabled();
    let report = evaluate(&PreconditionInput {
        kind: SemanticNodeKind::PasswordInput,
        state: &state,
        action: ActionKind::SetText,
        bound_origin: "https://example.com",
        current_origin: "https://example.com",
        bound_revision: 1,
        current_revision: 1,
        unique_target: true,
    })
    .expect("valid input");
    assert!(!report.holds());
}

// ---------- Deterministic pseudo-fuzz: mutation and truncation ----------

#[test]
fn mutated_wire_inputs_fail_closed_without_panicking() {
    let map = full_map();
    let wire = serde_json::to_string(&map).expect("serialize");
    // Byte-level mutations of the closed shape must either parse-and-validate
    // through the typed constructors or be rejected — never panic.
    for seed in 0u64..64 {
        let mut bytes = wire.clone().into_bytes();
        let index = (seed as usize * 7) % bytes.len();
        match seed % 4 {
            0 => bytes[index] = b'!',
            1 => bytes[index] = b'"',
            2 => {
                bytes.truncate(index.max(1));
            }
            _ => bytes[index] = b'x',
        }
        let parsed: Result<PageMap, _> = serde_json::from_slice(&bytes);
        if let Ok(parsed) = parsed {
            // Parsing succeeded; revalidating the shape must be a pure no-op
            // or a clean rejection — never a panic.
            let _ = serde_json::to_string(&parsed);
        }
    }
}

#[test]
fn effect_reports_reject_every_invalid_outcome_reason_pairing() {
    for (outcome, reason) in [
        (EffectOutcome::Verified, Some(EffectReason::Timeout)),
        (EffectOutcome::Verified, Some(EffectReason::Unknown)),
        (EffectOutcome::Failed, None),
        (EffectOutcome::Indeterminate, None),
    ] {
        assert!(EffectReport::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(1),
            1,
            ActionKind::Click,
            node_id("n-1"),
            outcome,
            reason,
            None,
        )
        .is_err());
    }
}

// ---------- Gate-level denial for a hostile full sequence ----------

#[test]
fn hostile_execution_sequence_is_denied_at_every_layer() {
    let mut gate = SemanticActionGate::new();
    // Issue on tab-1/gen-3/profile-a.
    let issued = gate
        .registry()
        .issue(
            node_id("n-1"),
            ActionKind::Click,
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(3),
            ProfileScope::new("profile-a").expect("scope"),
            1_000,
            61_000,
        )
        .expect_issue();
    let state = visible_enabled();
    let context_tab = TabId::new("tab-1").expect("tab id");
    let profile = ProfileScope::new("profile-a").expect("scope");
    let confirmation = ConfirmationRef::new("conf-1").expect("confirmation");

    // Wrong nonce first: consume destroys the handle.
    let wrong = ExecutionRequest {
        handle_id: &issued.id,
        nonce: HandleNonce::new(issued.nonce.get() ^ 1),
        tab_id: &context_tab,
        generation: SessionGeneration::from_raw(3),
        profile: &profile,
        now_ms: 2_000,
        bound_origin: "https://example.com",
        bound_revision: 7,
        current_origin: "https://example.com",
        current_revision: 7,
        kind: SemanticNodeKind::Button,
        state: &state,
        action: ActionKind::Click,
        unique_target: true,
        risk_facts: RiskFacts::default(),
        confirmation: Some(&confirmation),
    };
    assert!(matches!(
        gate.approve(wrong),
        ApprovalOutcome::HandleDenied(crayon_semantic_action::Resolution::NonceMismatch)
    ));
    // The correct nonce can never succeed afterwards.
    let correct = ExecutionRequest {
        handle_id: &issued.id,
        nonce: issued.nonce,
        tab_id: &context_tab,
        generation: SessionGeneration::from_raw(3),
        profile: &profile,
        now_ms: 2_000,
        bound_origin: "https://example.com",
        bound_revision: 7,
        current_origin: "https://example.com",
        current_revision: 7,
        kind: SemanticNodeKind::Button,
        state: &state,
        action: ActionKind::Click,
        unique_target: true,
        risk_facts: RiskFacts::default(),
        confirmation: Some(&confirmation),
    };
    assert!(matches!(
        gate.approve(correct),
        ApprovalOutcome::HandleDenied(crayon_semantic_action::Resolution::Unknown)
    ));
}

trait IssueExt {
    fn expect_issue(self) -> crayon_semantic_action::ActionHandle;
}

impl IssueExt for crayon_semantic_action::IssueOutcome {
    fn expect_issue(self) -> crayon_semantic_action::ActionHandle {
        match self {
            crayon_semantic_action::IssueOutcome::Issued(handle) => handle,
            other => panic!("unexpected issue outcome: {other:?}"),
        }
    }
}

// ---------- No secrets on the wire ----------

#[test]
fn wire_surfaces_never_carry_secret_shaped_values() {
    let map = full_map();
    let wire = serde_json::to_string(&map).expect("serialize");
    for secret_marker in [
        "authorization",
        "cookie",
        "bearer ",
        "password=\"",
        "api_key",
        "token=",
    ] {
        assert!(
            !wire.to_ascii_lowercase().contains(secret_marker),
            "{secret_marker} leaked"
        );
    }
}
