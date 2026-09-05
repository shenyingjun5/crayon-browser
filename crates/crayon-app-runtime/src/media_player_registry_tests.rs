use super::media_player_registry::{
    MediaPlayerRegistry, PlayerRegistryError, MAX_PLAYERS_PER_PAGE,
};
use crayon_ipc_schema::media_host_v2::{
    PlayerContext, PlayerFact, PlayerListRequest, PlayerMessage, PlayerPageContext,
    PlayerPageStatus, PlayerSourceKind,
};

fn context(instance_id: u64, source_revision: u64) -> PlayerContext {
    PlayerContext {
        session_id: 7,
        host_generation: 9,
        tab_id: 1,
        navigation_id: 2,
        tab_generation: 3,
        instance_id,
        source_revision,
    }
}

fn fact(instance_id: u64, source_revision: u64, media_url: &str) -> PlayerFact {
    PlayerFact {
        context: context(instance_id, source_revision),
        observed_at_ms: 10,
        source_kind: PlayerSourceKind::HttpUrl,
        position_ms: 20,
        duration_ms: Some(30),
        is_live: false,
        has_video: true,
        has_audio: true,
        visible: true,
        eme_encrypted: false,
        visible_fraction_ppm: 40,
        page_url: "https://page.test/watch".into(),
        media_url: media_url.into(),
    }
}

fn request(snapshot_revision: u64, offset: u16, max_items: u16) -> PlayerListRequest {
    PlayerListRequest {
        context: PlayerPageContext {
            session_id: 7,
            host_generation: 9,
            request_id: 31,
            tab_id: 1,
            navigation_id: 2,
            tab_generation: 3,
        },
        snapshot_revision,
        offset,
        max_items,
    }
}

#[test]
fn same_url_players_keep_distinct_identity_and_redacted_projection() {
    let mut registry = MediaPlayerRegistry::new(7, 9).unwrap();
    registry
        .apply(PlayerMessage::Upsert(fact(
            11,
            1,
            "https://cdn.test/v?secret=1",
        )))
        .unwrap();
    registry
        .apply(PlayerMessage::Upsert(fact(
            12,
            1,
            "https://cdn.test/v?secret=2",
        )))
        .unwrap();
    let snapshot = registry.snapshot();
    assert_eq!(snapshot.len(), 2);
    assert_eq!(snapshot[0].context.instance_id, 11);
    assert_eq!(snapshot[1].context.instance_id, 12);
    assert_eq!(snapshot[0].redacted_origin, "https://cdn.test");
    assert!(!format!("{snapshot:?}").contains("secret"));
}

#[test]
fn revision_remove_navigation_and_session_fences_are_exact() {
    let mut registry = MediaPlayerRegistry::new(7, 9).unwrap();
    registry
        .apply(PlayerMessage::Upsert(fact(11, 1, "https://cdn.test/a")))
        .unwrap();
    registry
        .apply(PlayerMessage::Upsert(fact(11, 2, "https://cdn.test/b")))
        .unwrap();
    let mut conflicting = fact(11, 2, "https://cdn.test/conflict");
    conflicting.observed_at_ms = 11;
    assert!(matches!(
        registry.apply(PlayerMessage::Upsert(conflicting)),
        Err(PlayerRegistryError::StaleContext)
    ));
    assert!(matches!(
        registry.apply(PlayerMessage::Upsert(fact(11, 1, "https://cdn.test/a"))),
        Err(PlayerRegistryError::StaleContext)
    ));
    assert!(matches!(
        registry.apply(PlayerMessage::Remove(context(11, 1))),
        Err(PlayerRegistryError::StaleContext)
    ));
    registry
        .apply(PlayerMessage::Remove(context(11, 2)))
        .unwrap();
    assert!(registry.is_empty());

    registry
        .apply(PlayerMessage::Upsert(fact(12, 1, "https://cdn.test/c")))
        .unwrap();
    let mut forged_remove = context(12, 1);
    forged_remove.navigation_id = 4;
    forged_remove.tab_generation = 4;
    assert!(matches!(
        registry.apply(PlayerMessage::Remove(forged_remove)),
        Err(PlayerRegistryError::StaleContext)
    ));
    assert_eq!(registry.len(), 1);
    let mut next = fact(13, 1, "https://cdn.test/d");
    next.context.navigation_id = 4;
    next.context.tab_generation = 4;
    registry.apply(PlayerMessage::Upsert(next)).unwrap();
    assert_eq!(registry.len(), 1);
    assert_eq!(registry.snapshot()[0].context.instance_id, 13);

    let mut stale = fact(14, 1, "https://cdn.test/e");
    stale.context.session_id = 8;
    assert!(matches!(
        registry.apply(PlayerMessage::Upsert(stale)),
        Err(PlayerRegistryError::StaleContext)
    ));
}

#[test]
fn per_page_capacity_is_bounded_without_evicting_current_players() {
    let mut registry = MediaPlayerRegistry::new(7, 9).unwrap();
    for index in 0..MAX_PLAYERS_PER_PAGE {
        registry
            .apply(PlayerMessage::Upsert(fact(
                index as u64 + 1,
                1,
                &format!("https://cdn.test/{index}"),
            )))
            .unwrap();
    }
    assert!(matches!(
        registry.apply(PlayerMessage::Upsert(fact(99, 1, "https://cdn.test/full"))),
        Err(PlayerRegistryError::CapacityExceeded)
    ));
    assert_eq!(registry.len(), MAX_PLAYERS_PER_PAGE);
    assert_eq!(registry.dropped_capacity_total(), 1);
}

#[test]
fn pages_are_stable_and_mutation_makes_the_old_revision_stale() {
    let mut registry = MediaPlayerRegistry::new(7, 9).unwrap();
    registry
        .apply(PlayerMessage::Upsert(fact(
            12,
            1,
            "https://b.test/v?token=2",
        )))
        .unwrap();
    registry
        .apply(PlayerMessage::Upsert(fact(
            11,
            1,
            "https://a.test/v?token=1",
        )))
        .unwrap();

    let first = registry.page(request(0, 0, 1)).unwrap();
    assert_eq!(first.status, PlayerPageStatus::Ok);
    assert_eq!(first.snapshot_revision, 3);
    assert_eq!(first.next_offset, Some(1));
    assert_eq!(first.players.len(), 1);
    assert_eq!(first.players[0].instance_id, 11);
    assert_eq!(first.players[0].redacted_origin, "https://a.test");

    let second = registry
        .page(request(first.snapshot_revision, 1, 1))
        .unwrap();
    assert_eq!(second.status, PlayerPageStatus::Ok);
    assert_eq!(second.next_offset, None);
    assert_eq!(second.players[0].instance_id, 12);

    let mut updated = fact(12, 1, "https://b.test/v?token=2");
    updated.observed_at_ms = 11;
    registry.apply(PlayerMessage::Upsert(updated)).unwrap();
    let stale = registry
        .page(request(first.snapshot_revision, 1, 1))
        .unwrap();
    assert_eq!(stale.status, PlayerPageStatus::Stale);
    assert_eq!(stale.snapshot_revision, 4);
    assert!(stale.players.is_empty());

    let empty = registry.page(request(0, 99, 1)).unwrap();
    assert_eq!(empty.status, PlayerPageStatus::Ok);
    assert!(empty.players.is_empty());
    assert_eq!(empty.next_offset, None);

    let mut wrong = request(0, 0, 1);
    wrong.context.tab_generation = 4;
    assert!(matches!(
        registry.page(wrong),
        Err(PlayerRegistryError::StaleContext)
    ));
}
