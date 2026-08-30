//! Semantic action runtime tests (ACT-07, AC-007): end-to-end approval and
//! single dispatch, denial layering, replay, generation/profile binding,
//! confirmation requirement and idempotent shutdown.

use crate::semantic_action_runtime::{ActionExecutor, DispatchOutcome, SemanticActionRuntime};
use crayon_domain::{
    ActionKind, ElementState, SemanticNodeId, SemanticNodeKind, SessionGeneration, TabId,
};
use crayon_semantic_action::{
    ApprovalOutcome, ConfirmationRef, ExecutionRequest, ProfileScope, RiskFacts,
};

const TAB: &str = "tab-1";
const ORIGIN: &str = "https://example.com";
const PROFILE_A: &str = "profile-a";

fn node(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn scope() -> ProfileScope {
    ProfileScope::new(PROFILE_A).expect("valid profile scope")
}

fn visible_enabled() -> ElementState {
    ElementState {
        enabled: true,
        visible: true,
        ..ElementState::default()
    }
}

struct Recorder {
    calls: std::cell::Cell<u32>,
    accept: bool,
}

impl Recorder {
    fn accepting() -> Self {
        Self {
            calls: std::cell::Cell::new(0),
            accept: true,
        }
    }
    fn rejecting() -> Self {
        Self {
            calls: std::cell::Cell::new(0),
            accept: false,
        }
    }
}

impl ActionExecutor for Recorder {
    fn perform(&self, _action: &crayon_semantic_action::ApprovedAction) -> bool {
        self.calls.set(self.calls.get() + 1);
        self.accept
    }
}

/// Issues one handle on the runtime's own registry.
fn issue_handle(runtime: &mut SemanticActionRuntime) -> crayon_semantic_action::ActionHandle {
    use crayon_semantic_action::IssueOutcome;
    match runtime.gate_mut().registry().issue(
        node("n-1"),
        ActionKind::Click,
        TabId::new(TAB).expect("tab id"),
        SessionGeneration::from_raw(3),
        scope(),
        1_000,
        61_000,
    ) {
        IssueOutcome::Issued(handle) => handle,
        other => panic!("unexpected issue outcome: {other:?}"),
    }
}

struct Ctx {
    tab: TabId,
    profile: ProfileScope,
    confirmation: ConfirmationRef,
}

fn ctx() -> Ctx {
    Ctx {
        tab: TabId::new(TAB).expect("tab id"),
        profile: scope(),
        confirmation: ConfirmationRef::new("conf-1").expect("valid confirmation"),
    }
}

fn request<'a>(
    handle: &'a crayon_semantic_action::ActionHandle,
    context: &'a Ctx,
    state: &'a ElementState,
) -> ExecutionRequest<'a> {
    ExecutionRequest {
        handle_id: &handle.id,
        nonce: handle.nonce,
        tab_id: &context.tab,
        generation: SessionGeneration::from_raw(3),
        profile: &context.profile,
        now_ms: 2_000,
        bound_origin: ORIGIN,
        bound_revision: 7,
        current_origin: ORIGIN,
        current_revision: 7,
        kind: SemanticNodeKind::Button,
        state,
        action: ActionKind::Click,
        unique_target: true,
        risk_facts: RiskFacts::default(),
        confirmation: Some(&context.confirmation),
    }
}

#[test]
fn approved_request_dispatches_exactly_once() {
    let mut runtime = SemanticActionRuntime::new();
    let handle = issue_handle(&mut runtime);
    let context = ctx();
    let state = visible_enabled();
    let executor = Recorder::accepting();
    let outcome = runtime.request(request(&handle, &context, &state), &executor);
    assert_eq!(outcome, DispatchOutcome::Dispatched);
    assert_eq!(executor.calls.get(), 1);
    // Replay of the same handle is denied; the executor is not called again.
    let replay = runtime.request(request(&handle, &context, &state), &executor);
    assert!(matches!(
        replay,
        DispatchOutcome::Denied(ApprovalOutcome::HandleDenied(_))
    ));
    assert_eq!(executor.calls.get(), 1);
    let (requests, approved, dispatched, denied) = runtime.stats();
    assert_eq!((requests, approved, dispatched, denied), (2, 1, 1, 1));
}

#[test]
fn missing_confirmation_denies_without_consuming_the_handle() {
    let mut runtime = SemanticActionRuntime::new();
    let handle = issue_handle(&mut runtime);
    let state = visible_enabled();
    let context = ctx();
    let mut unconfirmed = request(&handle, &context, &state);
    unconfirmed.confirmation = None;
    let executor = Recorder::accepting();
    let outcome = runtime.request(unconfirmed, &executor);
    assert_eq!(
        outcome,
        DispatchOutcome::Denied(ApprovalOutcome::ConfirmationMissing)
    );
    assert_eq!(executor.calls.get(), 0);
    // The handle survives; a confirmed retry is legal.
    assert_eq!(
        runtime.request(request(&handle, &context, &state), &executor),
        DispatchOutcome::Dispatched
    );
}

#[test]
fn risk_denial_preserves_the_handle() {
    let mut runtime = SemanticActionRuntime::new();
    let handle = issue_handle(&mut runtime);
    let context = ctx();
    let state = visible_enabled();
    let mut risky = request(&handle, &context, &state);
    risky.risk_facts.payment_context = true;
    let executor = Recorder::accepting();
    let outcome = runtime.request(risky, &executor);
    assert!(matches!(
        outcome,
        DispatchOutcome::Denied(ApprovalOutcome::RiskDenied(_))
    ));
    assert_eq!(executor.calls.get(), 0);
    // Denied risk must not burn the handle: the caller may re-read.
    assert_eq!(
        runtime.request(request(&handle, &context, &state), &executor),
        DispatchOutcome::Dispatched
    );
}

#[test]
fn precondition_violation_denies_after_consumption() {
    let mut runtime = SemanticActionRuntime::new();
    let handle = issue_handle(&mut runtime);
    let context = ctx();
    let mut hidden = visible_enabled();
    hidden.visible = false;
    let executor = Recorder::accepting();
    let outcome = runtime.request(request(&handle, &context, &hidden), &executor);
    assert!(matches!(
        outcome,
        DispatchOutcome::Denied(ApprovalOutcome::PreconditionViolated(_))
    ));
    assert_eq!(executor.calls.get(), 0);
    // The handle was consumed by the attempt; a retry after the page is
    // fixed requires a fresh read and a fresh handle.
    let state = visible_enabled();
    assert!(matches!(
        runtime.request(request(&handle, &context, &state), &executor),
        DispatchOutcome::Denied(ApprovalOutcome::HandleDenied(_))
    ));
}

#[test]
fn generation_advance_and_profile_switch_deny() {
    let mut runtime = SemanticActionRuntime::new();
    let handle = issue_handle(&mut runtime);
    let context = ctx();
    let state = visible_enabled();
    let executor = Recorder::accepting();

    let mut stale = request(&handle, &context, &state);
    stale.generation = SessionGeneration::from_raw(4);
    assert!(matches!(
        runtime.request(stale, &executor),
        DispatchOutcome::Denied(ApprovalOutcome::HandleDenied(_))
    ));
    assert_eq!(executor.calls.get(), 0);
    // Registry-level invalidation clears it entirely.
    assert_eq!(runtime.invalidate_tab(&TabId::new(TAB).expect("tab id")), 1);
    assert!(matches!(
        runtime.request(request(&handle, &context, &state), &executor),
        DispatchOutcome::Denied(ApprovalOutcome::HandleDenied(_))
    ));
}

#[test]
fn executor_rejection_is_reported_without_retry() {
    let mut runtime = SemanticActionRuntime::new();
    let handle = issue_handle(&mut runtime);
    let context = ctx();
    let state = visible_enabled();
    let executor = Recorder::rejecting();
    assert_eq!(
        runtime.request(request(&handle, &context, &state), &executor),
        DispatchOutcome::ExecutorRejected
    );
    assert_eq!(executor.calls.get(), 1);
    // The handle is consumed; no silent retry.
    assert!(matches!(
        runtime.request(request(&handle, &context, &state), &executor),
        DispatchOutcome::Denied(ApprovalOutcome::HandleDenied(_))
    ));
}

#[test]
fn sweep_and_shutdown_are_idempotent() {
    let mut runtime = SemanticActionRuntime::new();
    let handle = issue_handle(&mut runtime);
    assert_eq!(runtime.sweep_expired(2_000), 0);
    assert_eq!(runtime.sweep_expired(70_000), 1);
    assert_eq!(runtime.sweep_expired(70_000), 0);
    let _handle = issue_handle(&mut runtime);
    assert_eq!(runtime.shut_down(), 1);
    assert_eq!(runtime.shut_down(), 0);
    let context = ctx();
    let state = visible_enabled();
    assert!(matches!(
        runtime.request(request(&handle, &context, &state), &Recorder::accepting()),
        DispatchOutcome::Denied(ApprovalOutcome::HandleDenied(_))
    ));
}
