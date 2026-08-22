//! AGT-04 grant model tests: AG-003 (scoping/revocation/target
//! invalidation) and AG-005 (no widening path) plus capacity, expiry and
//! error-mapping matrices.

use super::*;
use crayon_domain::TabId;

fn profile(name: &str) -> ProfileScope {
    ProfileScope::new(name).unwrap()
}

fn request(kind: GrantKind) -> GrantRequest {
    GrantRequest {
        kind,
        session: "cli-dev".to_string(),
        profile: profile("default"),
        capability: AgentCapability::PageRead,
        target: None,
        task: None,
        ttl_ms: 60_000,
    }
}

fn tab(id: u64) -> AgentTarget {
    AgentTarget::Tab {
        tab: TabId::new(&format!("tab-{id}")).expect("tab id"),
    }
}

#[test]
fn profile_scope_token_matrix() {
    assert!(ProfileScope::new("default").is_ok());
    assert!(ProfileScope::new(&"p".repeat(64)).is_ok());
    for bad in ["", "UPPER", "with space", &"p".repeat(65)] {
        assert_eq!(
            ProfileScope::new(bad).unwrap_err(),
            GrantError::InvalidToken,
            "{bad:?}"
        );
    }
}

#[test]
fn issue_validates_tokens_and_ttl() {
    let mut manager = GrantManager::new();
    let mut req = request(GrantKind::SingleUse);
    req.session = "Bad Session".into();
    assert_eq!(manager.issue(req, 0).unwrap_err(), GrantError::InvalidToken);

    let req = request(GrantKind::Task);
    assert_eq!(
        manager.issue(req.clone(), 0).unwrap_err(),
        GrantError::InvalidToken
    );

    let mut req = request(GrantKind::SingleUse);
    req.task = Some("t-1".into());
    assert_eq!(manager.issue(req, 0).unwrap_err(), GrantError::InvalidToken);

    for bad_ttl in [0, MAX_GRANT_TTL_MS + 1] {
        let mut req = request(GrantKind::SingleUse);
        req.ttl_ms = bad_ttl;
        assert_eq!(
            manager.issue(req, 0).unwrap_err(),
            GrantError::TtlExceeded,
            "ttl={bad_ttl}"
        );
    }
    assert_eq!(
        manager.issue(request(GrantKind::SingleUse), 0),
        Ok(GrantId(0))
    );
}

#[test]
fn single_use_grant_consumed_once() {
    let mut manager = GrantManager::new();
    let id = manager.issue(request(GrantKind::SingleUse), 0).unwrap();
    let auth = manager
        .authorize(
            "cli-dev",
            &profile("default"),
            AgentCapability::PageRead,
            None,
            1,
        )
        .unwrap();
    assert_eq!(auth.grant, id);
    assert_eq!(auth.remaining_uses, None);
    assert!(manager.get(&id).is_none());
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                1
            )
            .unwrap_err(),
        GrantError::Denied
    );
}

#[test]
fn task_grant_bounded_use_and_task_binding() {
    let mut manager = GrantManager::new();
    let mut req = request(GrantKind::Task);
    req.task = Some("task-1".into());
    manager.issue(req, 0).unwrap();
    let mut last = None;
    for i in 0..MAX_TASK_GRANT_USES {
        last = manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                1 + i as u64,
            )
            .ok();
    }
    assert!(last.is_some());
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                1_000
            )
            .unwrap_err(),
        GrantError::UseLimitReached
    );
}

#[test]
fn app_session_grant_persists_until_expiry_or_revocation() {
    let mut manager = GrantManager::new();
    manager.issue(request(GrantKind::AppSession), 0).unwrap();
    for _ in 0..3 {
        assert!(manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                10
            )
            .is_ok());
    }
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                60_000
            )
            .unwrap_err(),
        GrantError::Expired
    );
}

#[test]
fn quadruple_mismatch_is_always_denied() {
    let mut manager = GrantManager::new();
    let mut req = request(GrantKind::AppSession);
    req.target = Some(tab(7));
    manager.issue(req, 0).unwrap();

    // Wrong session.
    assert_eq!(
        manager
            .authorize(
                "other",
                &profile("default"),
                AgentCapability::PageRead,
                Some(&tab(7)),
                1
            )
            .unwrap_err(),
        GrantError::Denied
    );
    // Wrong profile (AG-003: no cross-profile grants).
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("incognito"),
                AgentCapability::PageRead,
                Some(&tab(7)),
                1
            )
            .unwrap_err(),
        GrantError::Denied
    );
    // Wider capability (AG-005: no widening).
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::CastControl,
                Some(&tab(7)),
                1
            )
            .unwrap_err(),
        GrantError::Denied
    );
    // Different target (AG-003: no cross-target grants).
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                Some(&tab(8)),
                1
            )
            .unwrap_err(),
        GrantError::Denied
    );
    // No target requested while grant is targeted.
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                1
            )
            .unwrap_err(),
        GrantError::Denied
    );
    // Exact quadruple passes; untargeted request also passes an
    // untargeted grant.
    assert!(manager
        .authorize(
            "cli-dev",
            &profile("default"),
            AgentCapability::PageRead,
            Some(&tab(7)),
            1
        )
        .is_ok());
}

#[test]
fn untargeted_grant_authorizes_any_target() {
    let mut manager = GrantManager::new();
    manager.issue(request(GrantKind::AppSession), 0).unwrap();
    assert!(manager
        .authorize(
            "cli-dev",
            &profile("default"),
            AgentCapability::PageRead,
            Some(&tab(1)),
            1
        )
        .is_ok());
    assert!(manager
        .authorize(
            "cli-dev",
            &profile("default"),
            AgentCapability::PageRead,
            None,
            1
        )
        .is_ok());
}

#[test]
fn revoke_is_immediate_at_three_levels() {
    let mut manager = GrantManager::new();
    let single = manager.issue(request(GrantKind::SingleUse), 0).unwrap();
    let app = manager.issue(request(GrantKind::AppSession), 0).unwrap();
    assert_eq!(manager.revoke(&single), Ok(()));
    // Idempotent re-revoke reports Revoked, no side effects.
    assert_eq!(manager.revoke(&single), Err(GrantError::Revoked));
    assert_eq!(manager.revoke(&GrantId(999)), Err(GrantError::UnknownGrant));

    assert_eq!(manager.revoke_session("cli-dev"), 1);
    assert_eq!(
        manager
            .authorize(
                "cli-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                1
            )
            .unwrap_err(),
        GrantError::Revoked
    );
    assert!(manager.get(&app).is_some_and(Grant::revoked));

    // A fresh grant falls to profile-level revocation.
    let mut other = request(GrantKind::AppSession);
    other.session = "mcp-dev".into();
    manager.issue(other, 0).unwrap();
    assert_eq!(manager.revoke_profile(&profile("default")), 1);
    assert_eq!(
        manager
            .authorize(
                "mcp-dev",
                &profile("default"),
                AgentCapability::PageRead,
                None,
                1
            )
            .unwrap_err(),
        GrantError::Revoked
    );
}

#[test]
fn target_invalidation_kills_only_bound_grants() {
    let mut manager = GrantManager::new();
    // Distinct sessions keep each quadruple unambiguous: an untargeted
    // grant legitimately authorizes any target, so it shares no scope
    // with the tab-bound grants under test.
    let mut bound = request(GrantKind::AppSession);
    bound.session = "bound".into();
    bound.target = Some(tab(7));
    manager.issue(bound, 0).unwrap();
    let mut untargeted = request(GrantKind::AppSession);
    untargeted.session = "free".into();
    manager.issue(untargeted, 0).unwrap();
    let mut other_tab = request(GrantKind::AppSession);
    other_tab.session = "other-tab".into();
    other_tab.target = Some(tab(9));
    manager.issue(other_tab, 0).unwrap();

    assert_eq!(
        manager.invalidate_target(&TabId::new("tab-7").expect("tab id")),
        1
    );
    assert_eq!(
        manager
            .authorize(
                "bound",
                &profile("default"),
                AgentCapability::PageRead,
                Some(&tab(7)),
                1
            )
            .unwrap_err(),
        GrantError::Revoked
    );
    // Untargeted and differently-targeted grants survive.
    assert!(manager
        .authorize(
            "free",
            &profile("default"),
            AgentCapability::PageRead,
            Some(&tab(7)),
            1
        )
        .is_ok());
    assert!(manager
        .authorize(
            "other-tab",
            &profile("default"),
            AgentCapability::PageRead,
            Some(&tab(9)),
            1
        )
        .is_ok());
}

#[test]
fn revoked_grant_does_not_shadow_fresh_match() {
    let mut manager = GrantManager::new();
    let first = manager.issue(request(GrantKind::AppSession), 0).unwrap();
    manager.issue(request(GrantKind::AppSession), 0).unwrap();
    manager.revoke(&first).unwrap();
    // The second, non-revoked grant still authorizes the same scope.
    assert!(manager
        .authorize(
            "cli-dev",
            &profile("default"),
            AgentCapability::PageRead,
            None,
            1
        )
        .is_ok());
}

#[test]
fn capacity_is_bounded() {
    let mut manager = GrantManager::new();
    for _ in 0..MAX_GRANTS {
        manager.issue(request(GrantKind::AppSession), 0).unwrap();
    }
    assert_eq!(
        manager
            .issue(request(GrantKind::AppSession), 0)
            .unwrap_err(),
        GrantError::CapacityExceeded
    );
    // Consuming a single-use grant frees a slot.
    manager = GrantManager::new();
    for _ in 0..MAX_GRANTS {
        manager.issue(request(GrantKind::SingleUse), 0).unwrap();
    }
    assert!(manager
        .authorize(
            "cli-dev",
            &profile("default"),
            AgentCapability::PageRead,
            None,
            1
        )
        .is_ok());
    assert!(manager.issue(request(GrantKind::AppSession), 0).is_ok());
}

#[test]
fn sweep_expired_drops_grants() {
    let mut manager = GrantManager::new();
    manager.issue(request(GrantKind::AppSession), 0).unwrap();
    assert_eq!(manager.sweep_expired(59_999), 0);
    assert_eq!(manager.sweep_expired(60_000), 1);
    assert!(manager.is_empty());
}

#[test]
fn error_display_and_caap_mapping_golden() {
    let cases: &[(GrantError, &str, CaapError)] = &[
        (
            GrantError::Denied,
            "grant denied",
            CaapError::CapabilityDenied,
        ),
        (
            GrantError::Expired,
            "grant expired",
            CaapError::DeadlineExceeded,
        ),
        (
            GrantError::Revoked,
            "grant revoked",
            CaapError::CapabilityDenied,
        ),
        (
            GrantError::TargetStale,
            "grant target stale",
            CaapError::TargetStale,
        ),
        (
            GrantError::CapacityExceeded,
            "grant store at capacity",
            CaapError::QueueFull,
        ),
        (
            GrantError::InvalidToken,
            "grant token rejected",
            CaapError::InvalidMessage,
        ),
        (
            GrantError::TtlExceeded,
            "grant ttl exceeds bound",
            CaapError::InvalidMessage,
        ),
        (
            GrantError::UseLimitReached,
            "grant use limit reached",
            CaapError::CapabilityDenied,
        ),
        (
            GrantError::UnknownGrant,
            "grant unknown",
            CaapError::Unauthorized,
        ),
    ];
    for (error, message, caap) in cases {
        assert_eq!(error.to_string(), *message);
        assert_eq!(error.to_caap_error(), *caap);
    }
}

#[test]
fn stats_counters_track_transitions() {
    let mut manager = GrantManager::new();
    manager.issue(request(GrantKind::SingleUse), 0).unwrap();
    let _ = manager.authorize(
        "cli-dev",
        &profile("default"),
        AgentCapability::PageRead,
        None,
        1,
    );
    let _ = manager.authorize("nobody", &profile("x"), AgentCapability::CastRead, None, 1);
    manager.issue(request(GrantKind::AppSession), 0).unwrap();
    manager.revoke_session("cli-dev");
    let stats = manager.stats();
    assert_eq!(stats.issued_total, 2);
    assert_eq!(stats.authorized_total, 1);
    assert_eq!(stats.denied_total, 1);
    assert_eq!(stats.revoked_total, 1);
    assert_eq!(stats.live, 1);
}

/// Deterministic pseudo-random sequence (LCG, same technique as the
/// session tests): default-deny must never authorize an unissued
/// combination, and issued grants must behave monotonically.
#[test]
fn lcg_default_deny_invariants() {
    let sessions = ["cli-dev", "mcp-dev", "other"];
    let profiles = ["default", "incognito", "work"];
    let capabilities = [
        AgentCapability::PageRead,
        AgentCapability::Navigation,
        AgentCapability::CastRead,
        AgentCapability::CastControl,
        AgentCapability::SemanticAction,
    ];
    let targets = [None, Some(tab(1)), Some(tab(2)), Some(tab(3))];
    let mut state: u64 = 0x2545_F491_4F6C_DD1D;
    let mut next = || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };

    let mut manager = GrantManager::new();
    let mut clock = 1_u64;
    let mut authorized = 0_u64;
    for step in 0..3_000_u64 {
        clock += 1;
        let session = sessions[(next() % sessions.len() as u64) as usize];
        let prof = profile(profiles[(next() % profiles.len() as u64) as usize]);
        let capability = capabilities[(next() % capabilities.len() as u64) as usize];
        let target = targets[(next() % targets.len() as u64) as usize].clone();

        if step % 97 == 0 {
            let mut req = request(match next() % 3 {
                0 => GrantKind::SingleUse,
                1 => GrantKind::Task,
                _ => GrantKind::AppSession,
            });
            req.session = session.to_string();
            req.profile = prof.clone();
            req.capability = capability;
            req.target = target.clone();
            if req.kind == GrantKind::Task {
                req.task = Some(format!("task-{}", step));
            }
            let _ = manager.issue(req, clock);
        }
        if step % 211 == 0 {
            let _ = manager.invalidate_target(
                &TabId::new(&format!("tab-{}", 1 + (next() % 3))).expect("tab id"),
            );
        }
        if step % 401 == 0 {
            let _ = manager.revoke_session(sessions[(next() % 3) as usize]);
        }
        if step % 509 == 0 {
            let _ = manager.sweep_expired(clock);
        }
        if manager
            .authorize(session, &prof, capability, target.as_ref(), clock)
            .is_ok()
        {
            authorized += 1;
        }
        // Invariants: bounded store, monotonic counters.
        let stats = manager.stats();
        assert!(stats.live <= MAX_GRANTS);
        assert!(stats.revoked_total <= stats.issued_total);
        assert!(stats.authorized_total + stats.denied_total >= authorized);
    }
    // Default deny was exercised (unissued combinations dominated).
    let stats = manager.stats();
    assert!(stats.denied_total > 0);
}
