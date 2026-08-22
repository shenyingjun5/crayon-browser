use super::*;

#[test]
fn peer_gate_matrix() {
    let cases: &[(PeerIdentity, bool)] = &[
        (PeerIdentity::new(true, true), true),
        (PeerIdentity::new(true, false), false),
        (PeerIdentity::new(false, true), false),
        (PeerIdentity::new(false, false), false),
    ];
    for (peer, allowed) in cases {
        assert_eq!(peer.handshake_allowed(), *allowed, "{peer:?}");
    }
}

#[test]
fn error_display_golden() {
    let cases: &[(LocalAgentIpcError, &str)] = &[
        (
            LocalAgentIpcError::NotRunning,
            "local agent endpoint is not running",
        ),
        (
            LocalAgentIpcError::PeerRejected,
            "local agent peer rejected before handshake",
        ),
        (
            LocalAgentIpcError::HandshakeFailed,
            "local agent handshake failed",
        ),
        (
            LocalAgentIpcError::AlreadyRunning,
            "local agent endpoint already running",
        ),
    ];
    for (error, expected) in cases {
        assert_eq!(error.to_string(), *expected);
    }
}
