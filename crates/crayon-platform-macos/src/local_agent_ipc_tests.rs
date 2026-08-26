//! M04c UDS endpoint tests: purpose validation, start/stop lifecycle,
//! AG-012 gate, real socket bind on loopback.

use super::*;
use crayon_platform_api::local_agent_ipc::{LocalAgentIpcEndpoint, PeerIdentity};

#[test]
fn purpose_token_matrix() {
    assert!(is_valid_purpose("caap"));
    assert!(is_valid_purpose("test-1"));
    assert!(!is_valid_purpose(""));
    assert!(!is_valid_purpose("UPPER"));
    assert!(!is_valid_purpose("has/slash"));
    assert!(!is_valid_purpose(&"p".repeat(MAX_PURPOSE_LEN + 1)));
}

#[test]
fn socket_path_format() {
    let ep = MacUdsEndpoint::new("caap").expect("endpoint");
    assert_eq!(ep.socket_path(), "/tmp/crayon-agent-caap.sock");
}

#[test]
fn start_stop_is_idempotent() {
    let mut ep = MacUdsEndpoint::new("test-idem").expect("endpoint");
    assert!(!LocalAgentIpcEndpoint::is_running(&ep));
    // Start → running.
    ep.start().expect("start");
    assert!(LocalAgentIpcEndpoint::is_running(&ep));
    // Double start fails.
    assert_eq!(ep.start(), Err(LocalAgentIpcError::AlreadyRunning));
    // Stop → not running.
    ep.stop().expect("stop");
    assert!(!LocalAgentIpcEndpoint::is_running(&ep));
    // Double stop is Ok.
    ep.stop().expect("double stop");
    // Restart works after stop.
    ep.start().expect("restart");
    ep.stop().expect("final stop");
    // Socket file cleaned up.
    assert!(!std::path::Path::new(&ep.socket_path()).exists());
}

#[test]
fn start_rejects_invalid_purpose() {
    let result = MacUdsEndpoint::new("UPPER");
    assert!(result.is_err());
}

#[test]
fn uid_is_current_process() {
    let ep = MacUdsEndpoint::new("caap").expect("endpoint");
    // The endpoint captures the current process's uid.
    assert!(ep.uid() > 0);
}

#[test]
fn peer_gate_is_conjunctive() {
    // The default admit_peer from the trait is inherited.
    let ep = MacUdsEndpoint::new("caap").expect("endpoint");
    // Not running: rejected before gate.
    assert_eq!(
        LocalAgentIpcEndpoint::admit_peer(&ep, PeerIdentity::new(true, true)),
        Err(LocalAgentIpcError::NotRunning)
    );
    // Start, then test the gate.
    let mut ep = ep;
    ep.start().expect("start");
    assert!(LocalAgentIpcEndpoint::admit_peer(&ep, PeerIdentity::new(true, true)).is_ok());
    assert_eq!(
        LocalAgentIpcEndpoint::admit_peer(&ep, PeerIdentity::new(true, false)),
        Err(LocalAgentIpcError::PeerRejected)
    );
    assert_eq!(
        LocalAgentIpcEndpoint::admit_peer(&ep, PeerIdentity::new(false, true)),
        Err(LocalAgentIpcError::PeerRejected)
    );
    ep.stop().expect("stop");
}

#[test]
fn real_bind_accept_and_uid_check() {
    let mut server = MacUdsEndpoint::new("test-real").expect("endpoint");
    server.start().expect("start");

    // Connect a client from the same process (same uid).
    let path = server.socket_path();
    // SAFETY: socket() is a standard syscall; AF_UNIX = 1, SOCK_STREAM = 1.
    let client_fd = unsafe { ffi::socket(1, 1, 0) };
    assert!(client_fd >= 0, "client socket");
    let mut addr = [0u8; 106];
    addr[0] = 1; // AF_UNIX
    let path_bytes = path.as_bytes();
    addr[2..2 + path_bytes.len()].copy_from_slice(path_bytes);
    // SAFETY: client_fd is a valid socket; addr is a valid sockaddr.
    let connect_result =
        unsafe { ffi::connect(client_fd, addr.as_ptr().cast(), path_bytes.len() + 2) };
    assert!(connect_result == 0, "client connect should succeed");

    // Same-user peer passes the gate.
    assert!(LocalAgentIpcEndpoint::admit_peer(&server, PeerIdentity::new(true, true)).is_ok());
    server.stop().expect("stop");
}
