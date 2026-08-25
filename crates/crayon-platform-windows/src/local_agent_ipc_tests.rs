//! Behaviour tests for the named-pipe agent endpoint (real machine).
//!
//! Same-user clients are exercised end to end; a different-user client
//! cannot be spawned without elevation, so the cross-user rejection is a
//! CP-W01 device-matrix item (the gate logic itself is covered by the
//! PLT-01 contract tests).

use super::*;
use crayon_platform_api::local_agent_ipc::LocalAgentIpcEndpoint as Trait;
use std::sync::mpsc;
use std::sync::Arc;

#[test]
fn start_stop_matrix_and_gate_passthrough() {
    let mut endpoint = WindowsAgentIpcEndpoint::new("w04c-matrix").expect("construct");
    // Gate before start: NotRunning wins over peer facts.
    assert_eq!(
        Trait::admit_peer(&endpoint, PeerIdentity::new(true, true)),
        Err(LocalAgentIpcError::NotRunning)
    );
    Trait::start(&mut endpoint).expect("start");
    assert_eq!(
        Trait::start(&mut endpoint),
        Err(LocalAgentIpcError::AlreadyRunning)
    );
    assert!(Trait::is_running(&endpoint));
    // Shared conjunction gate: only same_user ∧ loopback proceeds.
    assert!(Trait::admit_peer(&endpoint, PeerIdentity::new(true, true)).is_ok());
    assert_eq!(
        Trait::admit_peer(&endpoint, PeerIdentity::new(false, true)),
        Err(LocalAgentIpcError::PeerRejected)
    );
    assert_eq!(
        Trait::admit_peer(&endpoint, PeerIdentity::new(true, false)),
        Err(LocalAgentIpcError::PeerRejected)
    );
    endpoint.stop().expect("stop");
    endpoint.stop().expect("stop idempotent");
    assert!(!Trait::is_running(&endpoint));
}

#[test]
fn same_user_client_is_admitted_end_to_end() {
    let mut endpoint = WindowsAgentIpcEndpoint::new("w04c-connect").expect("construct");
    Trait::start(&mut endpoint).expect("start");
    let path = endpoint.pipe_path_for_connect();
    let shared = Arc::new(endpoint);

    let accept_side = Arc::clone(&shared);
    let (tx, rx) = mpsc::channel::<&'static str>();
    let acceptor = std::thread::spawn(move || {
        let _ = tx.send("accepting");
        accept_side.accept_verified_client()
    });
    assert_eq!(
        rx.recv_timeout(std::time::Duration::from_secs(5)),
        Ok("accepting")
    );
    // SAFETY: NUL-terminated UTF-16 from pipe_path_for_connect.
    let client = unsafe { connect_client(&path) };
    let invalid = client.is_null() || client == (-1isize as HANDLE);
    assert!(!invalid, "client handle must be valid");

    match acceptor.join().expect("accept thread") {
        Ok(verified) => {
            // SAFETY: transport-owned handle in this test.
            unsafe { CloseHandle(verified.raw()) };
        }
        Err(failure) => panic!("same-user client must be admitted: {failure:?}"),
    }

    // Sole ownership returns once the accept thread joined.
    let mut endpoint = Arc::try_unwrap(shared).ok().expect("sole owner");
    endpoint.stop().expect("stop releases listener");
}
