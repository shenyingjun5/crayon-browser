//! SessionRegistry contract (MED-09): RL-002 token entropy/shape, RL-003
//! authorization before upstream, RL-004 stop finality, RL-005 revocation
//! triggers, TTL and bounded capacity.

use crayon_domain::{DeviceId, ResourceId};
use crayon_relay::session::{
    RevokeReason, SessionAuthError, SessionRegistry, SessionToken, DEFAULT_SESSION_TTL_MS,
    MAX_SESSIONS,
};
use std::net::{IpAddr, Ipv4Addr};

fn device(id: &str) -> DeviceId {
    DeviceId::new(id).unwrap()
}

fn resource(id: &str) -> ResourceId {
    ResourceId::new(id).unwrap()
}

fn registry_with_session() -> (SessionRegistry, String) {
    let mut registry = SessionRegistry::new();
    let grant = registry
        .create_session(
            device("dev-01"),
            Some(IpAddr::V4(Ipv4Addr::new(192, 168, 1, 8))),
            vec!["cdn.example.com".to_string()],
            DEFAULT_SESSION_TTL_MS,
            1000,
        )
        .unwrap();
    registry
        .register_resource(&grant.token_hex, resource("res-01"), "cdn.example.com")
        .unwrap();
    (registry, grant.token_hex)
}

#[test]
fn rl_002_tokens_are_128_bit_csprng_and_carry_no_upstream_url() {
    let a = SessionToken::generate();
    let b = SessionToken::generate();
    assert_ne!(a, b, "CSPRNG token 不得重复");
    let hex = a.as_hex();
    assert_eq!(hex.len(), 32, "128-bit = 32 hex chars");
    assert!(hex.chars().all(|c| c.is_ascii_hexdigit()));
    // token 纯随机，不含任何 URL 信息
    assert!(!hex.contains("http"));
    assert_eq!(format!("{a:?}"), "SessionToken(REDACTED)");
    // hex 往返
    assert_eq!(SessionToken::from_hex(&hex), Some(a));
    assert_eq!(SessionToken::from_hex("zz"), None);
    assert_eq!(SessionToken::from_hex("abcd"), None);
}

#[test]
fn rl_003_authorization_runs_before_upstream_access() {
    let (registry, token) = registry_with_session();
    let ok_ip = Some(IpAddr::V4(Ipv4Addr::new(192, 168, 1, 8)));
    let wrong_ip = Some(IpAddr::V4(Ipv4Addr::new(192, 168, 1, 9)));

    // 未知 token → 401 类
    assert_eq!(
        registry.authorize("0".repeat(32).as_str(), &resource("res-01"), ok_ip, 2000),
        Err(SessionAuthError::UnknownSession)
    );
    // 错误 IP → 403 类
    assert_eq!(
        registry.authorize(&token, &resource("res-01"), wrong_ip, 2000),
        Err(SessionAuthError::ReceiverMismatch)
    );
    // 未注册资源
    assert_eq!(
        registry.authorize(&token, &resource("res-99"), ok_ip, 2000),
        Err(SessionAuthError::UnknownResource)
    );
    // 合法访问
    let access = registry
        .authorize(&token, &resource("res-01"), ok_ip, 2000)
        .unwrap();
    assert_eq!(access.resource.upstream_host, "cdn.example.com");
}

#[test]
fn allow_set_is_fixed_at_creation() {
    let (mut registry, token) = registry_with_session();
    assert_eq!(
        registry.register_resource(&token, resource("res-02"), "evil.example.org"),
        Err(SessionAuthError::ReceiverMismatch),
        "allow-set 外的主机不得注册（运行时不可扩张）"
    );
    // 幂等重复注册
    assert!(registry
        .register_resource(&token, resource("res-01"), "cdn.example.com")
        .is_ok());
}

#[test]
fn rl_004_stop_is_immediate_and_idempotent() {
    let (mut registry, token) = registry_with_session();
    let ok_ip = Some(IpAddr::V4(Ipv4Addr::new(192, 168, 1, 8)));
    assert!(registry.stop(&token));
    assert!(!registry.stop(&token), "重复 stop 幂等");
    assert_eq!(
        registry.authorize(&token, &resource("res-01"), ok_ip, 2000),
        Err(SessionAuthError::UnknownSession),
        "停止后立即失效（10s 门禁的上界内）"
    );
    assert!(
        registry.is_empty(),
        "registry 清空（secret 随记录 Drop 零化）"
    );
}

#[test]
fn rl_005_every_trigger_revokes_sessions() {
    // 全量触发器
    for reason in [
        RevokeReason::Navigation,
        RevokeReason::ProfileDestroyed,
        RevokeReason::AppExit,
    ] {
        let (mut registry, _) = registry_with_session();
        assert_eq!(registry.revoke(reason, None), 1, "{reason:?}");
        assert!(registry.is_empty());
    }
    // 设备级触发器只撤销绑定该设备的 session
    let mut registry = SessionRegistry::new();
    let g1 = registry
        .create_session(device("dev-01"), None, vec![], DEFAULT_SESSION_TTL_MS, 1000)
        .unwrap();
    registry
        .create_session(device("dev-02"), None, vec![], DEFAULT_SESSION_TTL_MS, 1000)
        .unwrap();
    assert_eq!(
        registry.revoke(RevokeReason::RouteLost, Some(&device("dev-01"))),
        1
    );
    assert_eq!(registry.len(), 1, "其他设备的 session 保留");
    assert!(
        !registry.stop(&g1.token_hex),
        "route lost 后旧 session 已撤销（stop 幂等返回 false）"
    );
}

#[test]
fn ttl_expiry_blocks_and_purges() {
    let (mut registry, token) = registry_with_session();
    let ok_ip = Some(IpAddr::V4(Ipv4Addr::new(192, 168, 1, 8)));
    let expired_at = 1000 + DEFAULT_SESSION_TTL_MS + 1;
    assert_eq!(
        registry.authorize(&token, &resource("res-01"), ok_ip, expired_at),
        Err(SessionAuthError::SessionExpired)
    );
    // 边界：恰好 TTL 时刻仍有效
    assert!(registry
        .authorize(
            &token,
            &resource("res-01"),
            ok_ip,
            1000 + DEFAULT_SESSION_TTL_MS
        )
        .is_ok());
    assert_eq!(registry.expire(expired_at), 1);
    assert!(registry.is_empty());
}

#[test]
fn capacity_is_bounded() {
    let mut registry = SessionRegistry::new();
    for _ in 0..MAX_SESSIONS {
        assert!(registry
            .create_session(device("dev-01"), None, vec![], DEFAULT_SESSION_TTL_MS, 1000)
            .is_some());
    }
    assert!(registry
        .create_session(device("dev-01"), None, vec![], DEFAULT_SESSION_TTL_MS, 1000)
        .is_none());
}
