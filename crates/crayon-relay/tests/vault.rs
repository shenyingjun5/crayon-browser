//! RecipeVault contract (MED-10): scope enforcement, per-hop header scope
//! (RL-015), leak-free Debug/log surface (RL-014), revocation and bounds.

use crayon_domain::{ResourceId, SessionId};
use crayon_relay::vault::{HopScope, RecipeVault, ScopeError, UpstreamRecipe};
use test_support::leak_scanner::LeakScanner;

fn make_recipe() -> UpstreamRecipe {
    UpstreamRecipe::new(
        "https://cdn.example.com/live/master.m3u8?sign=abc123&token=xyz",
        Some("https://example.com".to_string()),
        Some("TestUA/1.0".to_string()),
    )
    .unwrap()
}

fn session() -> SessionId {
    SessionId::new("sess-01").unwrap()
}

#[test]
fn recipe_parses_origin_and_path_scope() {
    let recipe = make_recipe();
    assert_eq!(recipe.origin(), "https://cdn.example.com");
    assert_eq!(recipe.path_prefix(), "/live/");
    // scoped headers 只有 Referer/UA，类型层面无 Cookie/Authorization
    let headers = recipe.scoped_headers();
    assert_eq!(headers.len(), 2);
    assert!(headers
        .iter()
        .all(|(name, _)| matches!(*name, "Referer" | "User-Agent")));
}

#[test]
fn resolve_stays_on_origin() {
    let recipe = make_recipe();
    assert_eq!(
        recipe.resolve("seg0.ts").unwrap(),
        "https://cdn.example.com/live/seg0.ts"
    );
    assert_eq!(
        recipe.resolve("/other/seg1.ts").unwrap(),
        "https://cdn.example.com/other/seg1.ts"
    );
    assert_eq!(
        recipe.resolve("https://evil.example.org/seg.ts"),
        Err(ScopeError::OriginEscape)
    );
    assert_eq!(
        recipe.resolve("file:///etc/passwd"),
        Err(ScopeError::UnsupportedScheme)
    );
    assert!(UpstreamRecipe::new("ftp://cdn.example.com/v.mp4", None, None).is_err());
}

#[test]
fn rl_015_per_hop_header_scope() {
    let recipe = make_recipe();
    assert_eq!(
        recipe.header_scope_for("https://cdn.example.com/live/seg0.ts?x=1"),
        Ok(HopScope::CarryHeaders)
    );
    assert_eq!(
        recipe.header_scope_for("https://other-cdn.example.net/seg0.ts"),
        Ok(HopScope::StripHeaders),
        "跨 origin 跳转不得携带 Referer/UA"
    );
    assert_eq!(
        recipe.header_scope_for("ftp://cdn.example.com/x"),
        Err(ScopeError::UnsupportedScheme)
    );
}

#[test]
fn rl_014_debug_surface_carries_no_secrets() {
    let recipe = make_recipe();
    let debug = format!("{recipe:?}");
    for leak in ["sign=abc123", "token=xyz", "master.m3u8", "TestUA"] {
        assert!(!debug.contains(leak), "debug 不得含 {leak}: {debug}");
    }
    assert!(
        debug.contains("https://cdn.example.com"),
        "脱敏 origin 可见"
    );

    // LeakScanner 全量扫描 Debug 输出
    let findings = LeakScanner::scan_text(&debug, &[]);
    assert!(findings.is_empty(), "{findings:?}");

    // Vault 自身 Debug 同样脱敏
    let mut vault = RecipeVault::new();
    vault
        .store(
            &session(),
            ResourceId::new("res-01").unwrap(),
            make_recipe(),
        )
        .unwrap();
    let findings = LeakScanner::scan_text(&format!("{vault:?}"), &[]);
    assert!(findings.is_empty(), "{findings:?}");
}

#[test]
fn rl_004_005_revocation_drops_recipes() {
    let mut vault = RecipeVault::new();
    let session = session();
    vault
        .store(&session, ResourceId::new("res-01").unwrap(), make_recipe())
        .unwrap();
    vault
        .store(&session, ResourceId::new("res-02").unwrap(), make_recipe())
        .unwrap();
    assert_eq!(vault.session_len(&session), 2);

    assert_eq!(vault.revoke_session(&session), 2);
    assert_eq!(vault.revoke_session(&session), 0, "幂等");
    assert!(vault
        .get(&session, &ResourceId::new("res-01").unwrap())
        .is_none());

    // revoke_all
    vault
        .store(&session, ResourceId::new("res-03").unwrap(), make_recipe())
        .unwrap();
    vault.revoke_all();
    assert_eq!(vault.session_len(&session), 0);
}

#[test]
fn capacity_is_bounded_and_restore_replaces() {
    let mut vault = RecipeVault::new();
    let session = session();
    for i in 0..128 {
        vault
            .store(
                &session,
                ResourceId::new(&format!("res-{i:03}")).unwrap(),
                make_recipe(),
            )
            .unwrap();
    }
    assert_eq!(
        vault.store(&session, ResourceId::new("res-129").unwrap(), make_recipe()),
        Err(ScopeError::CapacityExceeded)
    );
    // 同资源重复 store 是替换而非增长
    assert!(vault
        .store(&session, ResourceId::new("res-000").unwrap(), make_recipe())
        .is_ok());
    assert_eq!(vault.session_len(&session), 128);
}
