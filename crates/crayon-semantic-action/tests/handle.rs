//! Action handle tests (ACT-03, AC-003): issuance within a bounded
//! registry, single-use consumption, nonce replay denial, TTL expiry,
//! generation and profile invalidation — all over an injected clock.

use crayon_domain::{ActionKind, SemanticNodeId, SessionGeneration, TabId};
use crayon_semantic_action::{
    ActionHandle, ActionHandleId, ConsumeOutcome, HandleIdError, HandleIssueError, HandleNonce,
    HandleRegistry, IssueOutcome, ProfileScope, Resolution, MAX_ACTIVE_HANDLES, MAX_HANDLE_TTL_MS,
};

fn node(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn profile(raw: &str) -> ProfileScope {
    ProfileScope::new(raw).expect("valid profile scope")
}

const TAB: &str = "tab-1";
const PROFILE_A: &str = "profile-a";
const PROFILE_B: &str = "profile-b";

struct Issued {
    handle: ActionHandle,
}

/// Issues one handle at `now = 1_000`, TTL 60s, generation 3, profile-a.
fn issue_one(registry: &mut HandleRegistry) -> Issued {
    match registry.issue(
        node("n-1"),
        ActionKind::Click,
        TabId::new(TAB).expect("tab id"),
        SessionGeneration::from_raw(3),
        profile(PROFILE_A),
        1_000,
        61_000,
    ) {
        IssueOutcome::Issued(handle) => Issued { handle },
        other => panic!("unexpected issue outcome: {other:?}"),
    }
}

fn nonce_of(handle: &ActionHandle) -> HandleNonce {
    handle.nonce
}

// ---------- Identity ----------

#[test]
fn minted_ids_are_closed_tokens_with_entropy() {
    let first = ActionHandleId::generate().expect("entropy");
    let second = ActionHandleId::generate().expect("entropy");
    assert_ne!(first, second);
    assert_eq!(first.as_str().len(), 27);
    assert!(first.as_str().starts_with('h'));
    assert!(first
        .as_str()
        .bytes()
        .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit()));
    // Foreign shapes are rejected.
    assert_eq!(
        ActionHandleId::new("div > button"),
        Err(HandleIdError::Invalid)
    );
    assert_eq!(ActionHandleId::new(""), Err(HandleIdError::Invalid));
    assert_eq!(
        ActionHandleId::new(&format!("h{}", "a".repeat(27))),
        Err(HandleIdError::Invalid)
    );
}

// ---------- Issuance bounds ----------

#[test]
fn issuance_rejects_out_of_bounds_ttl_and_saturates() {
    let mut registry = HandleRegistry::new();
    let tab = TabId::new(TAB).expect("tab id");
    let generation = SessionGeneration::from_raw(1);
    let scope = profile(PROFILE_A);
    // TTL zero and beyond the maximum both fail closed.
    assert_eq!(
        registry.issue(
            node("n-1"),
            ActionKind::Click,
            tab.clone(),
            generation,
            scope.clone(),
            1_000,
            1_000
        ),
        IssueOutcome::Rejected(HandleIssueError::TtlOutOfBounds)
    );
    assert_eq!(
        registry.issue(
            node("n-1"),
            ActionKind::Click,
            tab.clone(),
            generation,
            scope.clone(),
            1_000,
            1_001 + MAX_HANDLE_TTL_MS
        ),
        IssueOutcome::Rejected(HandleIssueError::TtlOutOfBounds)
    );
    // The registry is bounded; the last mint before saturation is dropped.
    for _ in 0..MAX_ACTIVE_HANDLES {
        assert!(matches!(
            registry.issue(
                node("n-1"),
                ActionKind::Click,
                tab.clone(),
                generation,
                scope.clone(),
                1_000,
                2_000
            ),
            IssueOutcome::Issued(_)
        ));
    }
    assert_eq!(
        registry.issue(
            node("n-1"),
            ActionKind::Click,
            tab,
            generation,
            scope,
            1_000,
            2_000
        ),
        IssueOutcome::Saturated
    );
    assert_eq!(registry.len(), MAX_ACTIVE_HANDLES);
}

// ---------- Resolve and single-use consumption ----------

#[test]
fn resolved_handle_consumes_once_and_replay_fails() {
    let mut registry = HandleRegistry::new();
    let Issued { handle } = issue_one(&mut registry);
    let tab = TabId::new(TAB).expect("tab id");
    let generation = SessionGeneration::from_raw(3);
    let scope = profile(PROFILE_A);
    let nonce = nonce_of(&handle);

    // Same-generation re-reads resolve stably within the window.
    for _ in 0..3 {
        assert_eq!(
            registry.resolve(&handle.id, nonce, &tab, generation, &scope, 2_000),
            Resolution::Resolved
        );
    }
    assert_eq!(registry.len(), 1);

    // First consumption succeeds and removes the handle.
    let consumed = registry.consume(&handle.id, nonce, &tab, generation, &scope, 2_000);
    assert_eq!(consumed, ConsumeOutcome::Consumed(handle.clone()));
    assert!(registry.is_empty());

    // Replay of the same handle+nonce is denied.
    assert_eq!(
        registry.consume(&handle.id, nonce, &tab, generation, &scope, 2_000),
        ConsumeOutcome::Unknown
    );
    assert_eq!(
        registry.resolve(&handle.id, nonce, &tab, generation, &scope, 2_000),
        Resolution::Unknown
    );
}

#[test]
fn nonce_mismatch_denies_and_destroys() {
    let mut registry = HandleRegistry::new();
    let Issued { handle } = issue_one(&mut registry);
    let tab = TabId::new(TAB).expect("tab id");
    let generation = SessionGeneration::from_raw(3);
    let scope = profile(PROFILE_A);
    let wrong = HandleNonce::new(handle.nonce.get() ^ 1);

    assert_eq!(
        registry.resolve(&handle.id, wrong, &tab, generation, &scope, 2_000),
        Resolution::NonceMismatch
    );
    // A consumption attempt with a wrong nonce destroys the handle, so a
    // subsequent correct presentation can never succeed either.
    assert_eq!(
        registry.consume(&handle.id, wrong, &tab, generation, &scope, 2_000),
        ConsumeOutcome::NonceMismatch
    );
    assert_eq!(
        registry.consume(
            &handle.id,
            nonce_of(&handle),
            &tab,
            generation,
            &scope,
            2_000
        ),
        ConsumeOutcome::Unknown
    );
}

// ---------- TTL ----------

#[test]
fn ttl_expiry_denies_and_sweeps() {
    let mut registry = HandleRegistry::new();
    let Issued { handle } = issue_one(&mut registry);
    let tab = TabId::new(TAB).expect("tab id");
    let generation = SessionGeneration::from_raw(3);
    let scope = profile(PROFILE_A);
    let nonce = nonce_of(&handle);

    assert_eq!(
        registry.resolve(&handle.id, nonce, &tab, generation, &scope, 60_999),
        Resolution::Resolved
    );
    assert_eq!(
        registry.resolve(&handle.id, nonce, &tab, generation, &scope, 61_000),
        Resolution::Expired
    );
    assert_eq!(
        registry.consume(&handle.id, nonce, &tab, generation, &scope, 70_000),
        ConsumeOutcome::Expired
    );
    assert_eq!(registry.sweep_expired(70_000), 1);
    assert!(registry.is_empty());
}

// ---------- Generation binding ----------

#[test]
fn generation_advance_invalidates_older_handles_only() {
    let mut registry = HandleRegistry::new();
    let Issued { handle } = issue_one(&mut registry);
    let tab = TabId::new(TAB).expect("tab id");
    let scope = profile(PROFILE_A);
    let nonce = nonce_of(&handle);

    // Same generation re-read still resolves.
    assert_eq!(
        registry.resolve(
            &handle.id,
            nonce,
            &tab,
            SessionGeneration::from_raw(3),
            &scope,
            2_000
        ),
        Resolution::Resolved
    );
    // A newer generation supersedes it.
    assert_eq!(
        registry.resolve(
            &handle.id,
            nonce,
            &tab,
            SessionGeneration::from_raw(4),
            &scope,
            2_000
        ),
        Resolution::StaleGeneration
    );
    assert_eq!(
        registry.invalidate_before_generation(&tab, SessionGeneration::from_raw(4)),
        1
    );
    assert!(registry.is_empty());
    // Same-or-newer generation invalidation is a no-op for live handles.
    let Issued { handle: fresh } = issue_one(&mut registry);
    assert_eq!(
        registry.invalidate_before_generation(&tab, SessionGeneration::from_raw(3)),
        0
    );
    assert_eq!(registry.len(), 1);
    assert_eq!(
        registry.consume(
            &fresh.id,
            nonce_of(&fresh),
            &tab,
            SessionGeneration::from_raw(3),
            &scope,
            2_000
        ),
        ConsumeOutcome::Consumed(fresh)
    );
}

// ---------- Profile binding ----------

#[test]
fn profile_switch_invalidates_and_blocks_cross_profile_use() {
    let mut registry = HandleRegistry::new();
    let Issued { handle } = issue_one(&mut registry);
    let tab = TabId::new(TAB).expect("tab id");
    let generation = SessionGeneration::from_raw(3);
    let nonce = nonce_of(&handle);
    let other = profile(PROFILE_B);

    assert_eq!(
        registry.resolve(&handle.id, nonce, &tab, generation, &other, 2_000),
        Resolution::ProfileMismatch
    );
    assert_eq!(
        registry.consume(&handle.id, nonce, &tab, generation, &other, 2_000),
        ConsumeOutcome::ProfileMismatch
    );
    // The handle survives a mismatched attempt but dies on real invalidation.
    assert_eq!(registry.invalidate_profile(&profile(PROFILE_B)), 0);
    assert_eq!(registry.invalidate_profile(&profile(PROFILE_A)), 1);
    assert!(registry.is_empty());
}

// ---------- Target binding and foreign tabs ----------

#[test]
fn foreign_tab_is_indistinguishable_from_unknown() {
    let mut registry = HandleRegistry::new();
    let Issued { handle } = issue_one(&mut registry);
    let generation = SessionGeneration::from_raw(3);
    let scope = profile(PROFILE_A);
    let nonce = nonce_of(&handle);
    let other_tab = TabId::new("tab-2").expect("tab id");

    assert_eq!(
        registry.resolve(&handle.id, nonce, &other_tab, generation, &scope, 2_000),
        Resolution::Unknown
    );
    assert_eq!(
        registry.consume(&handle.id, nonce, &other_tab, generation, &scope, 2_000),
        ConsumeOutcome::Unknown
    );
    // The handle is untouched by foreign-tab attempts.
    assert_eq!(registry.len(), 1);
    assert_eq!(registry.invalidate_tab(&other_tab), 0);
    assert_eq!(
        registry.invalidate_tab(&TabId::new(TAB).expect("tab id")),
        1
    );
}

// ---------- Descriptor surface ----------

#[test]
fn descriptor_carries_no_page_content() {
    let mut registry = HandleRegistry::new();
    let Issued { handle } = issue_one(&mut registry);
    let descriptor = handle.descriptor();
    let wire = serde_json::to_string(&descriptor).expect("serialize");
    for forbidden in [
        "selector",
        "\"html\"",
        "\"dom\"",
        "xpath",
        "profile-a",
        "tab-1",
    ] {
        assert!(!wire.contains(forbidden), "{forbidden} leaked: {wire}");
    }
    assert!(wire.contains(&format!("\"id\":\"{}\"", handle.id)));
}
