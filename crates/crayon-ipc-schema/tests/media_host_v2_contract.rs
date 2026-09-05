use crayon_ipc_schema::media_host_v2::{
    decode, decode_draft_message, decode_player_message, decode_player_page_message, encode,
    encode_draft_message, encode_player_message, encode_player_page_message, matches_hello,
    DraftAction, DraftCommand, DraftContext, DraftError, DraftMediaRef, DraftMessage, DraftPhase,
    DraftReason, DraftRoute, DraftStateReply, Handshake, Kind, PlayerContext, PlayerFact,
    PlayerListRequest, PlayerMessage, PlayerPageContext, PlayerPageMessage, PlayerPageReply,
    PlayerPageStatus, PlayerProjection, PlayerSourceKind,
};
use crayon_ipc_schema::{decode_media_host_message, encode_media_host_message, MediaHostMessage};

fn hello() -> Handshake {
    Handshake {
        kind: Kind::Hello,
        session_id: 7,
        generation: 9,
        capabilities: 15,
        max_frame_bytes: 16_384,
        max_page_items: 16,
    }
}
fn welcome() -> Handshake {
    Handshake {
        kind: Kind::Welcome,
        capabilities: 1,
        max_frame_bytes: 8192,
        max_page_items: 8,
        ..hello()
    }
}
fn bytes(hex: &str) -> Vec<u8> {
    hex.as_bytes()
        .chunks_exact(2)
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
        .collect()
}

fn player_context() -> PlayerContext {
    PlayerContext {
        session_id: 7,
        host_generation: 9,
        tab_id: 11,
        navigation_id: 13,
        tab_generation: 17,
        instance_id: 19,
        source_revision: 23,
    }
}

fn player_fact() -> PlayerFact {
    PlayerFact {
        context: player_context(),
        observed_at_ms: 29,
        source_kind: PlayerSourceKind::HttpUrl,
        position_ms: 31,
        duration_ms: Some(37),
        is_live: false,
        has_video: true,
        has_audio: true,
        visible: true,
        eme_encrypted: false,
        visible_fraction_ppm: 41,
        page_url: "https://page.test/a".into(),
        media_url: "https://cdn.test/v.mp4".into(),
    }
}

fn page_context() -> PlayerPageContext {
    PlayerPageContext {
        session_id: 7,
        host_generation: 9,
        request_id: 29,
        tab_id: 11,
        navigation_id: 13,
        tab_generation: 17,
    }
}

fn list_request() -> PlayerListRequest {
    PlayerListRequest {
        context: page_context(),
        snapshot_revision: 0,
        offset: 2,
        max_items: 2,
    }
}

fn page_reply() -> PlayerPageReply {
    PlayerPageReply {
        context: page_context(),
        snapshot_revision: 31,
        status: PlayerPageStatus::Ok,
        offset: 2,
        next_offset: Some(4),
        players: vec![
            PlayerProjection {
                instance_id: 19,
                source_revision: 23,
                source_kind: PlayerSourceKind::HttpUrl,
                has_video: true,
                has_audio: true,
                visible: true,
                eme_encrypted: false,
                redacted_origin: "https://cdn.test".into(),
            },
            PlayerProjection {
                instance_id: 37,
                source_revision: 41,
                source_kind: PlayerSourceKind::BlobUrl,
                has_video: true,
                has_audio: false,
                visible: false,
                eme_encrypted: false,
                redacted_origin: String::new(),
            },
        ],
    }
}

fn stale_reply() -> PlayerPageReply {
    PlayerPageReply {
        status: PlayerPageStatus::Stale,
        next_offset: None,
        players: Vec::new(),
        ..page_reply()
    }
}

fn draft_context() -> DraftContext {
    DraftContext {
        session_id: 7,
        host_generation: 9,
        request_id: 29,
        profile_id: "profile-a".into(),
        tab_id: 11,
        navigation_id: 13,
        tab_generation: 17,
    }
}

fn draft_command() -> DraftCommand {
    DraftCommand {
        context: draft_context(),
        action: DraftAction::SelectMedia,
        draft_id: 31,
        draft_revision: 37,
        media: Some(DraftMediaRef {
            instance_id: 19,
            source_revision: 23,
        }),
        device_id: String::new(),
    }
}

fn draft_state() -> DraftStateReply {
    DraftStateReply {
        context: draft_context(),
        draft_id: 31,
        draft_revision: 41,
        phase: DraftPhase::Prepared,
        error: DraftError::None,
        media: Some(DraftMediaRef {
            instance_id: 19,
            source_revision: 23,
        }),
        device_id: "device-1".into(),
        device_connected: true,
        replacement_confirmation_required: false,
        route: DraftRoute::Direct,
        prepared_until_ms: Some(43),
        reason: DraftReason::None,
        session_generation: None,
    }
}

#[test]
fn shared_golden_and_previous_wire_are_distinct() {
    let golden = include_str!("../../../tests/contracts/media_host_v2_handshake.golden");
    let mut count = 0;
    for line in golden.lines() {
        let (name, hex) = line.split_once(' ').unwrap();
        let expected = match name {
            "hello" => hello(),
            "welcome" => welcome(),
            "hello-boundary" => Handshake {
                session_id: u64::MAX,
                generation: 0x0102_0304_0506_0708,
                capabilities: 0,
                max_frame_bytes: 34,
                max_page_items: 1,
                ..hello()
            },
            _ => panic!("unknown golden"),
        };
        let wire = bytes(hex);
        assert_eq!(encode(expected).unwrap(), wire);
        assert_eq!(decode(&wire).unwrap(), expected);
        assert!(decode_media_host_message(&wire).is_err());
        count += 1;
    }
    assert_eq!(count, 3);
    let v1 = encode_media_host_message(&MediaHostMessage::Shutdown).unwrap();
    assert!(decode(&v1).is_err());
}

#[test]
fn malformed_frames_fail_closed() {
    let wire = encode(hello()).unwrap();
    for length in 0..wire.len() {
        assert!(decode(&wire[..length]).is_err());
    }
    let mut trailing = wire.clone();
    trailing.push(0);
    assert!(decode(&trailing).is_err());
    assert!(decode(&vec![0; 16_385]).is_err());
    for (index, byte) in [(0, b'X'), (5, 1), (6, 0), (6, 3), (7, 1), (27, 64)] {
        let mut bad = wire.clone();
        bad[index] = byte;
        assert!(decode(&bad).is_err(), "offset {index}");
    }
    for (begin, end) in [(8, 16), (16, 24), (28, 32), (32, 34)] {
        let mut bad = wire.clone();
        bad[begin..end].fill(0);
        assert!(decode(&bad).is_err());
    }
    for (index, byte) in [(31, 1), (33, 17)] {
        let mut bad = wire.clone();
        bad[index] = byte;
        assert!(decode(&bad).is_err());
    }
}

#[test]
fn local_invalid_values_and_negotiation_expansion_are_rejected() {
    assert!(matches_hello(hello(), welcome()));
    assert!(!matches_hello(welcome(), hello()));
    for bad in [
        Handshake {
            session_id: 0,
            ..hello()
        },
        Handshake {
            generation: 0,
            ..hello()
        },
        Handshake {
            capabilities: 64,
            ..hello()
        },
        Handshake {
            max_frame_bytes: 33,
            ..hello()
        },
        Handshake {
            max_frame_bytes: 16_385,
            ..hello()
        },
        Handshake {
            max_page_items: 0,
            ..hello()
        },
        Handshake {
            max_page_items: 17,
            ..hello()
        },
    ] {
        assert!(encode(bad).is_err());
        assert!(!matches_hello(bad, welcome()));
    }
    for bad in [
        Handshake {
            session_id: 8,
            ..welcome()
        },
        Handshake {
            generation: 10,
            ..welcome()
        },
        Handshake {
            capabilities: 64,
            ..welcome()
        },
        Handshake {
            max_frame_bytes: 16_385,
            ..welcome()
        },
    ] {
        assert!(!matches_hello(hello(), bad));
    }
    let restricted = Handshake {
        capabilities: 1,
        max_frame_bytes: 1024,
        max_page_items: 1,
        ..hello()
    };
    assert!(!matches_hello(restricted, welcome()));
    assert!(!matches_hello(
        restricted,
        Handshake {
            kind: Kind::Welcome,
            max_frame_bytes: 1025,
            ..restricted
        }
    ));
    assert!(!matches_hello(
        restricted,
        Handshake {
            kind: Kind::Welcome,
            capabilities: 2,
            ..restricted
        }
    ));
    assert!(!matches_hello(
        restricted,
        Handshake {
            kind: Kind::Welcome,
            max_page_items: 2,
            ..restricted
        }
    ));
    let empty = Handshake {
        capabilities: 0,
        max_frame_bytes: 34,
        max_page_items: 1,
        ..hello()
    };
    assert_eq!(decode(&encode(empty).unwrap()).unwrap(), empty);
    assert!(matches_hello(
        empty,
        Handshake {
            kind: Kind::Welcome,
            ..empty
        }
    ));
    assert!(!matches_hello(empty, welcome()));
}

#[test]
fn player_messages_round_trip_and_remain_v1_isolated() {
    for message in [
        PlayerMessage::Upsert(player_fact()),
        PlayerMessage::Remove(player_context()),
    ] {
        let wire = encode_player_message(&message).unwrap();
        assert_eq!(decode_player_message(&wire).unwrap(), message);
        assert!(decode(&wire).is_err());
        assert!(decode_media_host_message(&wire).is_err());
    }
    let v1 = encode_media_host_message(&MediaHostMessage::Shutdown).unwrap();
    assert!(decode_player_message(&v1).is_err());

    let mut url_less = player_fact();
    url_less.source_kind = PlayerSourceKind::BlobUrl;
    url_less.media_url.clear();
    assert_eq!(
        decode_player_message(
            &encode_player_message(&PlayerMessage::Upsert(url_less.clone())).unwrap()
        )
        .unwrap(),
        PlayerMessage::Upsert(url_less)
    );
}

#[test]
fn shared_player_golden_is_exact() {
    let golden = include_str!("../../../tests/contracts/media_host_v2_player.golden");
    let mut count = 0;
    for line in golden.lines() {
        let (name, hex) = line.split_once(' ').unwrap();
        let message = match name {
            "player-upsert" => PlayerMessage::Upsert(player_fact()),
            "player-remove" => PlayerMessage::Remove(player_context()),
            _ => panic!("unknown player golden"),
        };
        let wire = bytes(hex);
        assert_eq!(encode_player_message(&message).unwrap(), wire);
        assert_eq!(decode_player_message(&wire).unwrap(), message);
        count += 1;
    }
    assert_eq!(count, 2);
}

#[test]
fn invalid_player_values_fail_closed() {
    for mutate in 0..7 {
        let mut context = player_context();
        match mutate {
            0 => context.session_id = 0,
            1 => context.host_generation = 0,
            2 => context.tab_id = 0,
            3 => context.navigation_id = 0,
            4 => context.tab_generation = 0,
            5 => context.instance_id = 0,
            _ => context.source_revision = 0,
        }
        assert!(encode_player_message(&PlayerMessage::Remove(context)).is_err());
    }
    let mut fact = player_fact();
    fact.observed_at_ms = 0;
    assert!(encode_player_message(&PlayerMessage::Upsert(fact)).is_err());
    for (kind, media_url) in [
        (PlayerSourceKind::HttpUrl, ""),
        (PlayerSourceKind::BlobUrl, "https://cdn.test/v.mp4"),
        (PlayerSourceKind::MediaStream, "https://cdn.test/v.mp4"),
    ] {
        let mut fact = player_fact();
        fact.source_kind = kind;
        fact.media_url = media_url.into();
        assert!(encode_player_message(&PlayerMessage::Upsert(fact)).is_err());
    }
    for bad_url in ["file:///tmp/a", "https://", "https://a.test/\nsecret"] {
        let mut fact = player_fact();
        fact.page_url = bad_url.into();
        assert!(encode_player_message(&PlayerMessage::Upsert(fact)).is_err());
    }
    let mut oversized = player_fact();
    oversized.page_url = format!("https://a.test/{}", "a".repeat(2048));
    assert!(encode_player_message(&PlayerMessage::Upsert(oversized)).is_err());
}

#[test]
fn malformed_player_frames_fail_closed() {
    let wire = encode_player_message(&PlayerMessage::Upsert(player_fact())).unwrap();
    for length in 0..wire.len() {
        assert!(
            decode_player_message(&wire[..length]).is_err(),
            "length {length}"
        );
    }
    let mut trailing = wire.clone();
    trailing.push(0);
    assert!(decode_player_message(&trailing).is_err());
    assert!(decode_player_message(&vec![0; 16_385]).is_err());
    for (index, byte) in [(0, b'X'), (5, 1), (6, 9), (7, 1), (64, 9)] {
        let mut bad = wire.clone();
        bad[index] = byte;
        assert!(decode_player_message(&bad).is_err(), "offset {index}");
    }
    let mut bad_utf8 = wire;
    let page_offset = bad_utf8
        .windows("https://page.test/a".len())
        .position(|window| window == b"https://page.test/a")
        .unwrap();
    bad_utf8[page_offset] = 0xff;
    assert!(decode_player_message(&bad_utf8).is_err());
}

#[test]
fn shared_player_page_golden_is_exact_and_isolated() {
    let golden = include_str!("../../../tests/contracts/media_host_v2_player_page.golden");
    let mut count = 0;
    for line in golden.lines() {
        let (name, hex) = line.split_once(' ').unwrap();
        let message = match name {
            "player-list" => PlayerPageMessage::List(list_request()),
            "player-page" => PlayerPageMessage::Page(page_reply()),
            "player-page-stale" => PlayerPageMessage::Page(stale_reply()),
            _ => panic!("unknown player page golden"),
        };
        let wire = bytes(hex);
        assert_eq!(encode_player_page_message(&message).unwrap(), wire);
        assert_eq!(decode_player_page_message(&wire).unwrap(), message);
        assert!(decode(&wire).is_err());
        assert!(decode_player_message(&wire).is_err());
        assert!(decode_media_host_message(&wire).is_err());
        count += 1;
    }
    assert_eq!(count, 3);
}

#[test]
fn invalid_player_page_values_fail_closed() {
    for field in 0..6 {
        let mut context = page_context();
        match field {
            0 => context.session_id = 0,
            1 => context.host_generation = 0,
            2 => context.request_id = 0,
            3 => context.tab_id = 0,
            4 => context.navigation_id = 0,
            _ => context.tab_generation = 0,
        }
        let mut request = list_request();
        request.context = context;
        assert!(encode_player_page_message(&PlayerPageMessage::List(request)).is_err());
    }
    for max_items in [0, 17] {
        let mut request = list_request();
        request.max_items = max_items;
        assert!(encode_player_page_message(&PlayerPageMessage::List(request)).is_err());
    }
    let mut reply = page_reply();
    reply.snapshot_revision = 0;
    assert!(encode_player_page_message(&PlayerPageMessage::Page(reply)).is_err());
    let mut reply = page_reply();
    reply.next_offset = Some(5);
    assert!(encode_player_page_message(&PlayerPageMessage::Page(reply)).is_err());
    let mut reply = stale_reply();
    reply.players.push(page_reply().players[0].clone());
    assert!(encode_player_page_message(&PlayerPageMessage::Page(reply)).is_err());
    for origin in [
        "https://cdn.test/path",
        "https://user@cdn.test",
        "https://CDN.test",
        "https://cdn.test:443",
        "file:///tmp/a",
    ] {
        let mut reply = page_reply();
        reply.players[0].redacted_origin = origin.into();
        assert!(encode_player_page_message(&PlayerPageMessage::Page(reply)).is_err());
    }
}

#[test]
fn malformed_player_page_frames_fail_closed() {
    let wire = encode_player_page_message(&PlayerPageMessage::Page(page_reply())).unwrap();
    for length in 0..wire.len() {
        assert!(
            decode_player_page_message(&wire[..length]).is_err(),
            "length {length}"
        );
    }
    let mut trailing = wire.clone();
    trailing.push(0);
    assert!(decode_player_page_message(&trailing).is_err());
    for (index, byte) in [(0, b'X'), (5, 1), (6, 9), (7, 1), (56, 9), (59, 2)] {
        let mut bad = wire.clone();
        bad[index] = byte;
        assert!(decode_player_page_message(&bad).is_err(), "offset {index}");
    }
    let mut bad_utf8 = wire;
    let origin_offset = bad_utf8
        .windows("https://cdn.test".len())
        .position(|window| window == b"https://cdn.test")
        .unwrap();
    bad_utf8[origin_offset] = 0xff;
    assert!(decode_player_page_message(&bad_utf8).is_err());
}

#[test]
fn shared_draft_golden_is_exact_and_isolated() {
    let golden = include_str!("../../../tests/contracts/media_host_v2_draft.golden");
    let mut count = 0;
    for line in golden.lines() {
        let (name, hex) = line.split_once(' ').unwrap();
        let message = match name {
            "draft-select-media" => DraftMessage::Command(draft_command()),
            "draft-prepared" => DraftMessage::State(draft_state()),
            _ => panic!("unknown draft golden"),
        };
        let wire = bytes(hex);
        assert_eq!(encode_draft_message(&message).unwrap(), wire);
        assert_eq!(decode_draft_message(&wire).unwrap(), message);
        assert!(decode(&wire).is_err());
        assert!(decode_player_message(&wire).is_err());
        assert!(decode_player_page_message(&wire).is_err());
        assert!(decode_media_host_message(&wire).is_err());
        count += 1;
    }
    assert_eq!(count, 2);
}

#[test]
fn enhanced_draft_reason_golden_is_exact_and_old_state_remains_decodable() {
    let (_, hex) = include_str!("../../../tests/contracts/media_host_v2_draft_reason.golden")
        .trim()
        .split_once(' ')
        .unwrap();
    let mut expected = draft_state();
    expected.reason = DraftReason::Recognized;
    let message = DraftMessage::State(expected);
    let wire = bytes(hex);
    assert_eq!(encode_draft_message(&message).unwrap(), wire);
    assert_eq!(decode_draft_message(&wire).unwrap(), message);

    let old_wire = encode_draft_message(&DraftMessage::State(draft_state())).unwrap();
    assert_eq!(old_wire[6], 8);
    assert_eq!(
        decode_draft_message(&old_wire).unwrap(),
        DraftMessage::State(draft_state())
    );
}

#[test]
fn committed_session_golden_is_exact_and_binds_controls() {
    let (_, hex) = include_str!("../../../tests/contracts/media_host_v2_draft_session.golden")
        .trim()
        .split_once(' ')
        .unwrap();
    let mut expected = draft_state();
    expected.phase = DraftPhase::Committed;
    expected.route = DraftRoute::None;
    expected.prepared_until_ms = None;
    expected.session_generation = Some(47);
    let message = DraftMessage::State(expected);
    let wire = bytes(hex);
    assert_eq!(encode_draft_message(&message).unwrap(), wire);
    assert_eq!(decode_draft_message(&wire).unwrap(), message);
}

#[test]
fn draft_actions_and_states_round_trip() {
    for action in [
        DraftAction::Open,
        DraftAction::SelectMedia,
        DraftAction::SelectDevice,
        DraftAction::Connect,
        DraftAction::Prepare,
        DraftAction::ConfirmReplacement,
        DraftAction::Commit,
        DraftAction::Cancel,
    ] {
        let mut value = draft_command();
        value.action = action;
        value.media = (action == DraftAction::SelectMedia).then_some(DraftMediaRef {
            instance_id: 19,
            source_revision: 23,
        });
        value.device_id = if action == DraftAction::SelectDevice {
            "device-1".into()
        } else {
            String::new()
        };
        if action == DraftAction::Open {
            value.draft_id = 0;
            value.draft_revision = 0;
        }
        let message = DraftMessage::Command(value);
        let wire = encode_draft_message(&message).unwrap();
        assert_eq!(decode_draft_message(&wire).unwrap(), message);
    }
    for (phase, error) in [
        (DraftPhase::Choosing, DraftError::None),
        (DraftPhase::Connecting, DraftError::None),
        (DraftPhase::Preparing, DraftError::None),
        (DraftPhase::Prepared, DraftError::None),
        (DraftPhase::Committing, DraftError::None),
        (DraftPhase::Failed, DraftError::Denied),
        (DraftPhase::Expired, DraftError::Expired),
        (DraftPhase::Cancelled, DraftError::None),
        (DraftPhase::Committed, DraftError::None),
    ] {
        let mut value = draft_state();
        value.phase = phase;
        value.error = error;
        value.prepared_until_ms = (phase == DraftPhase::Prepared).then_some(43);
        value.session_generation = (phase == DraftPhase::Committed).then_some(47);
        if !matches!(phase, DraftPhase::Prepared | DraftPhase::Committing) {
            value.device_connected = false;
            value.route = DraftRoute::None;
        }
        let message = DraftMessage::State(value);
        let wire = encode_draft_message(&message).unwrap();
        assert_eq!(decode_draft_message(&wire).unwrap(), message);
    }
    for reason in [
        DraftReason::Credentials,
        DraftReason::Protection,
        DraftReason::Recognized,
        DraftReason::Unrecognized,
        DraftReason::RedirectRefused,
        DraftReason::UpstreamRejected,
        DraftReason::AddressRejected,
        DraftReason::Dns,
        DraftReason::Connect,
        DraftReason::Timeout,
        DraftReason::Transport,
        DraftReason::InvalidTarget,
    ] {
        let mut value = draft_state();
        value.reason = reason;
        let message = DraftMessage::State(value);
        let wire = encode_draft_message(&message).unwrap();
        assert_eq!(wire[6], 9);
        assert_eq!(decode_draft_message(&wire).unwrap(), message);
    }
}

#[test]
fn invalid_draft_values_and_frames_fail_closed() {
    let mut open = draft_command();
    open.action = DraftAction::Open;
    assert!(encode_draft_message(&DraftMessage::Command(open)).is_err());
    let mut select = draft_command();
    select.media = None;
    assert!(encode_draft_message(&DraftMessage::Command(select)).is_err());
    let mut device = draft_command();
    device.action = DraftAction::SelectDevice;
    device.media = None;
    device.device_id = "bad\u{202e}id".into();
    assert!(encode_draft_message(&DraftMessage::Command(device)).is_err());
    let mut prepared = draft_state();
    prepared.prepared_until_ms = None;
    assert!(encode_draft_message(&DraftMessage::State(prepared)).is_err());
    let mut failed = draft_state();
    failed.phase = DraftPhase::Failed;
    failed.error = DraftError::None;
    failed.prepared_until_ms = None;
    failed.device_connected = false;
    failed.route = DraftRoute::None;
    assert!(encode_draft_message(&DraftMessage::State(failed)).is_err());
    let mut impossible = draft_state();
    impossible.phase = DraftPhase::Choosing;
    impossible.prepared_until_ms = None;
    impossible.route = DraftRoute::Direct;
    assert!(encode_draft_message(&DraftMessage::State(impossible)).is_err());
    let mut impossible_reason = draft_state();
    impossible_reason.phase = DraftPhase::Choosing;
    impossible_reason.prepared_until_ms = None;
    impossible_reason.route = DraftRoute::None;
    impossible_reason.device_connected = false;
    impossible_reason.reason = DraftReason::Timeout;
    assert!(encode_draft_message(&DraftMessage::State(impossible_reason)).is_err());
    let mut impossible_session = draft_state();
    impossible_session.session_generation = Some(47);
    assert!(encode_draft_message(&DraftMessage::State(impossible_session)).is_err());

    let wire = encode_draft_message(&DraftMessage::State(draft_state())).unwrap();
    for length in 0..wire.len() {
        assert!(
            decode_draft_message(&wire[..length]).is_err(),
            "length {length}"
        );
    }
    let mut trailing = wire.clone();
    trailing.push(0);
    assert!(decode_draft_message(&trailing).is_err());
    for (index, byte) in [(0, b'X'), (5, 1), (6, 11), (7, 1)] {
        let mut bad = wire.clone();
        bad[index] = byte;
        assert!(decode_draft_message(&bad).is_err(), "offset {index}");
    }
    let mut bad_utf8 = wire;
    let profile = bad_utf8
        .windows("profile-a".len())
        .position(|window| window == b"profile-a")
        .unwrap();
    bad_utf8[profile] = 0xff;
    assert!(decode_draft_message(&bad_utf8).is_err());

    let mut enhanced = draft_state();
    enhanced.reason = DraftReason::Timeout;
    let enhanced_wire = encode_draft_message(&DraftMessage::State(enhanced)).unwrap();
    assert!(decode_draft_message(&enhanced_wire[..enhanced_wire.len() - 1]).is_err());
    let mut unknown_reason = enhanced_wire.clone();
    *unknown_reason.last_mut().unwrap() = 0;
    assert!(decode_draft_message(&unknown_reason).is_err());
    *unknown_reason.last_mut().unwrap() = 13;
    assert!(decode_draft_message(&unknown_reason).is_err());

    let mut committed = draft_state();
    committed.phase = DraftPhase::Committed;
    committed.route = DraftRoute::None;
    committed.prepared_until_ms = None;
    committed.session_generation = Some(47);
    let committed_wire = encode_draft_message(&DraftMessage::State(committed)).unwrap();
    assert!(decode_draft_message(&committed_wire[..committed_wire.len() - 1]).is_err());
    let mut zero_session = committed_wire;
    let session_offset = zero_session.len() - 8;
    zero_session[session_offset..].fill(0);
    assert!(decode_draft_message(&zero_session).is_err());
}
