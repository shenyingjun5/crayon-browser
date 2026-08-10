//! FakeReceiver/FakeCastFacade self-tests: discovery gating, connect failure
//! injection, stale-generation discard, route lost and call recording.

use crayon_domain::{ReceiverCapabilities, SessionGeneration, SessionId};
use test_support::receiver::{
    FacadeCall, FacadeError, FakeCastFacade, FakeReceiver, ReceiverEvent, ReceiverPlaybackState,
};

fn receiver(id: &str) -> FakeReceiver {
    FakeReceiver::new(
        id,
        "客厅电视",
        ReceiverCapabilities::new(true, true, true, true, false, false, 2160),
    )
}

#[test]
fn devices_visible_only_while_discovering() {
    let facade = FakeCastFacade::new(vec![receiver("dev-01")]);
    assert!(
        facade.list_devices().is_empty(),
        "discovery off: no devices"
    );
    facade.start_discovery();
    let devices = facade.list_devices();
    assert_eq!(devices.len(), 1);
    assert_eq!(devices[0].id().as_str(), "dev-01");
    assert_eq!(devices[0].name(), "客厅电视");
    facade.stop_discovery();
    assert!(facade.list_devices().is_empty());
}

#[test]
fn connect_records_calls_and_honors_injected_failure() {
    let facade = FakeCastFacade::new(vec![receiver("dev-01")]);
    facade.start_discovery();
    let id = facade.list_devices()[0].id().clone();

    facade.fail_next_connect(FacadeError::Permission);
    assert_eq!(facade.connect_device(&id), Err(FacadeError::Permission));
    // Injection is consumed once.
    assert_eq!(facade.connect_device(&id), Ok(()));
    assert_eq!(facade.playback_state(), ReceiverPlaybackState::Connected);

    // Unknown device is unreachable, not a panic.
    let ghost = crayon_domain::DeviceId::new("dev-99").unwrap();
    assert_eq!(
        facade.connect_device(&ghost),
        Err(FacadeError::DeviceUnreachable)
    );

    facade.disconnect_device(&id);
    assert_eq!(facade.playback_state(), ReceiverPlaybackState::Idle);

    let calls = facade.calls();
    assert_eq!(
        calls[..],
        [
            FacadeCall::StartDiscovery,
            FacadeCall::ListDevices,
            FacadeCall::Connect(id.clone()),
            FacadeCall::Connect(id.clone()),
            FacadeCall::Connect(ghost),
            FacadeCall::Disconnect(id),
        ]
    );
}

#[test]
fn stale_generation_is_discarded_not_applied() {
    let facade = FakeCastFacade::new(vec![receiver("dev-01")]);
    let session = SessionId::new("sess-01").unwrap();
    let gen2 = SessionGeneration::INITIAL
        .advance()
        .unwrap()
        .advance()
        .unwrap();
    let gen1 = SessionGeneration::INITIAL.advance().unwrap();

    facade.play(session.clone(), gen2).unwrap();
    assert_eq!(
        facade.play(session.clone(), gen1),
        Err(FacadeError::StaleGeneration)
    );
    // The stale call was rejected before recording.
    assert_eq!(facade.calls().len(), 1);
    // Same generation replays are idempotent-accepted (reconnect path).
    facade.play(session.clone(), gen2).unwrap();
    assert_eq!(facade.calls().len(), 2);
}

#[test]
fn playback_controls_and_events_are_recorded() {
    let facade = FakeCastFacade::new(vec![receiver("dev-01")]);
    let session = SessionId::new("sess-01").unwrap();
    facade
        .play(session.clone(), SessionGeneration::INITIAL)
        .unwrap();
    facade.pause(&session);
    facade.resume(&session);
    facade.seek(&session, 12.5);
    facade.set_volume(&session, 40);
    facade.stop(&session);
    assert_eq!(facade.playback_state(), ReceiverPlaybackState::Stopped);

    facade.inject_event(ReceiverEvent::RouteLost(
        crayon_domain::DeviceId::new("dev-01").unwrap(),
    ));
    assert_eq!(facade.events().len(), 1);

    let calls = facade.calls();
    assert!(matches!(calls[1], FacadeCall::Pause(_)));
    assert!(matches!(
        calls[3],
        FacadeCall::Seek {
            position_seconds,
            ..
        } if position_seconds == 12.5
    ));
}
