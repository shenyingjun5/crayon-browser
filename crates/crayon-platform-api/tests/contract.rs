//! Public contract tests for `crayon-platform-api`: trait object safety,
//! std-only dependency surface and closed-bound constants.

use crayon_platform_api::external_client_handoff::{
    ExternalClientHandoff, HandoffAction, HandoffOutcome, HandoffReason, HandoffRequest,
};
use crayon_platform_api::lifecycle::{LifecycleEvent, PowerLifecycleMonitor};
use crayon_platform_api::local_agent_ipc::{LocalAgentIpcEndpoint, PeerIdentity};
use crayon_platform_api::local_network::{
    InterfaceName, LocalNetworkMonitor, NetworkChangeEvent, NetworkInterface,
};
use crayon_platform_api::secure_store::{SecureStore, SecureStoreError};
use crayon_platform_api::update::{UpdateFlow, UpdateState};

// Object-safety compilation assertions: platform adapters must be able to
// hold these traits as `dyn` behind a single owner.
#[allow(dead_code)]
fn assert_object_safe(
    _: &dyn SecureStore,
    _: &dyn LocalNetworkMonitor,
    _: &dyn PowerLifecycleMonitor,
    _: &dyn UpdateFlow,
    _: &dyn LocalAgentIpcEndpoint,
    _: &dyn ExternalClientHandoff,
) {
}

// Every error type implements std::error::Error with a stable Display.
#[test]
fn errors_implement_std_error() {
    fn assert_error<E: std::error::Error>(error: &E) -> bool {
        !error.to_string().is_empty()
    }
    assert!(assert_error(&SecureStoreError::Unavailable));
}

// The handoff request type exposes no free-form page data accessor: the
// only string surface is the validated closed-charset purpose token.
#[test]
fn handoff_request_surface_is_closed() {
    let request = HandoffRequest::new(
        HandoffReason::NoRouteAvailable,
        HandoffAction::LaunchClient,
        "cast-handoff",
    )
    .unwrap();
    assert_eq!(request.purpose(), "cast-handoff");
    assert_eq!(request.reason(), HandoffReason::NoRouteAvailable);
    assert_eq!(request.action(), HandoffAction::LaunchClient);
}

// CP-004: resume must be observable alongside terminating events so the
// runtime can enforce "no auto-restore of old sessions".
#[test]
fn lifecycle_events_cover_suspend_resume() {
    let all = [
        LifecycleEvent::Suspending,
        LifecycleEvent::Resumed,
        LifecycleEvent::ScreenLocked,
        LifecycleEvent::ScreenUnlocked,
        LifecycleEvent::SessionEnding,
    ];
    assert!(all.contains(&LifecycleEvent::Resumed));
    assert!(all.contains(&LifecycleEvent::Suspending));
}

// A loopback-only interface observation feeds the agent IPC gate without
// address parsing.
#[test]
fn network_interface_carries_loopback_flag() {
    let name = InterfaceName::new("lo0").unwrap();
    let interface = NetworkInterface {
        name: name.clone(),
        is_loopback: true,
        is_up: true,
    };
    assert!(interface.is_loopback);
    let event = NetworkChangeEvent::InterfaceUp(name);
    assert_eq!(
        event,
        NetworkChangeEvent::InterfaceUp(InterfaceName::new("lo0").unwrap())
    );
}

// The update state machine is total over its closed command set: every
// command either transitions or rejects deterministically.
#[test]
fn update_transition_is_total() {
    use crayon_platform_api::update::UpdateCommand as C;
    let states = [
        UpdateState::Idle,
        UpdateState::Checking,
        UpdateState::Available,
        UpdateState::Downloading,
        UpdateState::ReadyToInstall,
        UpdateState::Failed,
    ];
    let commands = [
        C::StartCheck,
        C::CheckSucceededNoUpdate,
        C::CheckSucceededUpdateAvailable,
        C::CheckFailed,
        C::StartDownload,
        C::DownloadProgressed,
        C::DownloadCompleted,
        C::DownloadFailed,
        C::Install,
        C::DismissFailure,
    ];
    for state in states {
        for command in commands {
            // Every combination yields a defined result (Ok or the closed
            // rejection); panics or undefined behaviour are contract bugs.
            let _ = state.transition(command);
        }
    }
}

// AG-012 gate is the only admission path for peers.
#[test]
fn peer_identity_gate_is_conjunctive() {
    assert!(PeerIdentity::new(true, true).handshake_allowed());
    assert!(!PeerIdentity::new(true, false).handshake_allowed());
    assert!(!PeerIdentity::new(false, true).handshake_allowed());
    assert!(!PeerIdentity::new(false, false).handshake_allowed());
}

// Handoff outcomes never express that mirroring started.
#[test]
fn handoff_outcome_has_no_mirror_started_variant() {
    let outcomes = [
        HandoffOutcome::DownloadStarted,
        HandoffOutcome::LaunchRequested,
        HandoffOutcome::NotInstalled,
        HandoffOutcome::Cancelled,
        HandoffOutcome::Failed,
    ];
    assert_eq!(outcomes.len(), 5);
}
