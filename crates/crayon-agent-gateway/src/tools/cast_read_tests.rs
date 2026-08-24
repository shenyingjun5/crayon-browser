//! AGT-08 cast read tool tests: sanitized summaries, closed bounds,
//! generation surfacing and error mapping (AG-007).

use super::*;
use crayon_domain::ReceiverCapabilities;

fn capabilities(mp4: bool, hls: bool, max_height: u16) -> ReceiverCapabilities {
    ReceiverCapabilities::new(mp4, hls, false, false, false, false, max_height)
}

fn entry(id: &str, name: &str, caps: ReceiverCapabilities) -> ReceiverEntry {
    ReceiverEntry {
        device_id: id.to_owned(),
        name: name.to_owned(),
        capabilities: caps,
    }
}

/// Two devices deliberately share the same display name; ids differ.
struct FixtureSource {
    receivers: Vec<ReceiverEntry>,
    state: CastStateSnapshot,
}

impl Default for FixtureSource {
    fn default() -> Self {
        Self {
            receivers: vec![
                entry("device-b", "客厅电视", capabilities(true, true, 2160)),
                entry("device-a", "客厅电视", capabilities(true, false, 1080)),
            ],
            state: CastStateSnapshot {
                state: CastPlaybackState::Idle,
                receiver_id: None,
                position_ms: 0,
                duration_ms: 0,
                generation: 7,
            },
        }
    }
}

impl CastReadSource for FixtureSource {
    fn list_receivers(&self) -> Result<Vec<ReceiverEntry>, CastReadError> {
        Ok(self.receivers.clone())
    }

    fn get_state(&self) -> Result<CastStateSnapshot, CastReadError> {
        Ok(self.state.clone())
    }
}

#[test]
fn same_name_devices_are_listed_side_by_side() {
    let source = FixtureSource::default();
    let snapshot = list_receivers(&source, 7).expect("listing succeeds");
    assert_eq!(snapshot.receivers.len(), 2);
    // Deterministic id order regardless of discovery order; names equal.
    let ids: Vec<&str> = snapshot
        .receivers
        .iter()
        .map(|receiver| receiver.device_id.as_str())
        .collect();
    assert_eq!(ids, vec!["device-a", "device-b"]);
    assert_eq!(snapshot.receivers[0].name, snapshot.receivers[1].name);
    // Capabilities survive verbatim.
    assert!(snapshot.receivers[1].capabilities.mp4());
    assert_eq!(snapshot.receivers[1].capabilities.max_height(), 2160);
}

#[test]
fn snapshot_golden_is_locked_and_secret_free() {
    let mut source = FixtureSource::default();
    source.receivers.insert(
        0,
        entry(
            "device-c\\x",
            "会议室|投屏",
            capabilities(false, false, 720),
        ),
    );
    source.state = CastStateSnapshot {
        state: CastPlaybackState::Playing,
        receiver_id: Some("device-b".to_owned()),
        position_ms: 91_500,
        duration_ms: 3_600_000,
        generation: 12,
    };
    let listing = list_receivers(&source, 12).expect("listing succeeds");
    let state = get_state(&source).expect("state succeeds");
    let actual = format!("{}{}", listing.snapshot(), state.snapshot());

    let golden = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("tests")
            .join("cast_read_v1_snapshot.txt"),
    )
    .expect("cast read golden must exist");
    assert_eq!(actual, golden);

    // Negative assertions: no network-location or token material can leak
    // because the types cannot carry it — the wire grammar stays closed.
    assert!(!actual.contains("192.168."));
    assert!(!actual.contains("http"));
    assert!(!actual.contains("token"));
}

#[test]
fn bounds_are_enforced_on_source_data() {
    struct BurstingSource;
    impl CastReadSource for BurstingSource {
        fn list_receivers(&self) -> Result<Vec<ReceiverEntry>, CastReadError> {
            Ok((0..MAX_RECEIVERS + 1)
                .map(|index| ReceiverEntry {
                    device_id: format!("device-{index}"),
                    name: "tv".to_owned(),
                    capabilities: ReceiverCapabilities::new(
                        false, false, false, false, false, false, 0,
                    ),
                })
                .collect())
        }
        fn get_state(&self) -> Result<CastStateSnapshot, CastReadError> {
            unreachable!()
        }
    }
    assert_eq!(
        list_receivers(&BurstingSource, 1),
        Err(CastReadError::CapacityExceeded)
    );

    struct BadNameSource;
    impl CastReadSource for BadNameSource {
        fn list_receivers(&self) -> Result<Vec<ReceiverEntry>, CastReadError> {
            Ok(vec![ReceiverEntry {
                device_id: "device-x".to_owned(),
                name: "bad\nname".to_owned(),
                capabilities: ReceiverCapabilities::new(
                    false, false, false, false, false, false, 0,
                ),
            }])
        }
        fn get_state(&self) -> Result<CastStateSnapshot, CastReadError> {
            unreachable!()
        }
    }
    assert_eq!(
        list_receivers(&BadNameSource, 1),
        Err(CastReadError::InvalidDeviceData)
    );

    let overlong_id = "d".repeat(MAX_DEVICE_ID_LEN + 1);
    let source = FixtureSource {
        receivers: vec![entry(&overlong_id, "tv", capabilities(false, false, 0))],
        state: CastStateSnapshot {
            state: CastPlaybackState::Idle,
            receiver_id: None,
            position_ms: 0,
            duration_ms: 0,
            generation: 1,
        },
    };
    assert_eq!(
        list_receivers(&source, 1),
        Err(CastReadError::InvalidDeviceData)
    );
}

#[test]
fn sessionless_state_is_idle_not_error() {
    let source = FixtureSource::default();
    let snapshot = get_state(&source).expect("idle state succeeds");
    assert_eq!(snapshot.state, CastPlaybackState::Idle);
    assert_eq!(snapshot.receiver_id, None);
    assert_eq!(
        snapshot.snapshot(),
        "state=idle|receiver=none|pos_ms=0|dur_ms=0|generation=7\n"
    );
}

/// AG-007: reads expose the SDK generation so callers can fence stale
/// results; a newer generation is distinguishable from an older one.
#[test]
fn generation_is_surfaced_for_fencing() {
    let mut source = FixtureSource::default();
    let old = get_state(&source).expect("state");
    source.state.generation = 8;
    let new = get_state(&source).expect("state");
    assert!(new.generation > old.generation);

    let listing_old = list_receivers(&source, old.generation).expect("listing");
    let listing_new = list_receivers(&source, new.generation).expect("listing");
    assert_ne!(listing_old.generation, listing_new.generation);
}

#[test]
fn error_mapping_is_stable() {
    assert_eq!(
        CastReadError::SourceUnavailable.to_caap_error(),
        CaapError::CapabilityDenied
    );
    assert_eq!(
        CastReadError::InvalidDeviceData.to_caap_error(),
        CaapError::InvalidMessage
    );
    assert_eq!(
        CastReadError::CapacityExceeded.to_caap_error(),
        CaapError::QueueFull
    );
}
