use super::media_cast_draft::{
    CastDraftError, DraftEffectKind, DraftScope, MediaCastDraftOwner, MAX_CAST_DRAFTS,
    PREPARED_LIFETIME_MS,
};
use crayon_ipc_schema::media_host_v2::{DraftMediaRef, DraftPhase, DraftRoute};

fn scope(tab_id: u32) -> DraftScope {
    DraftScope {
        profile_id: "profile-a".into(),
        tab_id,
        navigation_id: 7,
        tab_generation: 9,
    }
}

fn media(revision: u64) -> DraftMediaRef {
    DraftMediaRef {
        instance_id: 11,
        source_revision: revision,
    }
}

#[test]
fn device_can_connect_before_media_and_never_commits_implicitly() {
    let mut owner = MediaCastDraftOwner::new();
    let opened = owner.open(scope(1)).unwrap();
    let selected = owner
        .select_device(
            &opened.scope,
            opened.draft_id,
            opened.revision,
            "tv-1".into(),
            true,
        )
        .unwrap();
    let connect = owner
        .request_connect(&selected.scope, selected.draft_id, selected.revision)
        .unwrap();
    assert_eq!(connect.kind, DraftEffectKind::Connect);
    assert_eq!(connect.media, None);
    let connected = owner.complete_connect(&connect, true).unwrap();
    assert!(connected.device_connected);
    assert_eq!(connected.phase, DraftPhase::Choosing);
    let selected = owner
        .select_media(
            &connected.scope,
            connected.draft_id,
            connected.revision,
            media(13),
            true,
        )
        .unwrap();
    assert!(selected.device_connected);
    assert!(owner
        .request_commit(&selected.scope, selected.draft_id, selected.revision, 100,)
        .is_err());
}

#[test]
fn prepare_has_exact_deadline_and_commit_is_single_use() {
    let mut owner = MediaCastDraftOwner::new();
    let opened = owner.open(scope(1)).unwrap();
    let selected = owner
        .select_media(
            &opened.scope,
            opened.draft_id,
            opened.revision,
            media(13),
            true,
        )
        .unwrap();
    let selected = owner
        .select_device(
            &selected.scope,
            selected.draft_id,
            selected.revision,
            "tv-1".into(),
            true,
        )
        .unwrap();
    let connect = owner
        .request_connect(&selected.scope, selected.draft_id, selected.revision)
        .unwrap();
    let connected = owner.complete_connect(&connect, true).unwrap();
    let prepare = owner
        .request_prepare(
            &connected.scope,
            connected.draft_id,
            connected.revision,
            false,
        )
        .unwrap();
    assert_eq!(prepare.kind, DraftEffectKind::Prepare);
    let prepared = owner
        .complete_prepare(&prepare, Some(DraftRoute::Direct), 100)
        .unwrap();
    assert_eq!(prepared.prepared_until_ms, Some(100 + PREPARED_LIFETIME_MS));
    let commit = owner
        .request_commit(
            &prepared.scope,
            prepared.draft_id,
            prepared.revision,
            100 + PREPARED_LIFETIME_MS - 1,
        )
        .unwrap();
    assert_eq!(commit.kind, DraftEffectKind::Commit);
    let completed = owner.complete_commit(&commit, Some(9)).unwrap();
    assert_eq!(completed.route, DraftRoute::None);
    assert_eq!(completed.phase, DraftPhase::Committed);
    assert_eq!(completed.session_generation, Some(9));
    assert_eq!(
        owner.complete_commit(&commit, Some(9)),
        Err(CastDraftError::Stale)
    );
}

#[test]
fn deadline_selection_and_late_effects_are_fenced() {
    let mut owner = MediaCastDraftOwner::new();
    let opened = owner.open(scope(1)).unwrap();
    let selected = owner
        .select_media(
            &opened.scope,
            opened.draft_id,
            opened.revision,
            media(13),
            true,
        )
        .unwrap();
    let selected = owner
        .select_device(
            &selected.scope,
            selected.draft_id,
            selected.revision,
            "tv-1".into(),
            true,
        )
        .unwrap();
    let connect = owner
        .request_connect(&selected.scope, selected.draft_id, selected.revision)
        .unwrap();
    let connected = owner.complete_connect(&connect, true).unwrap();
    let prepare = owner
        .request_prepare(
            &connected.scope,
            connected.draft_id,
            connected.revision,
            false,
        )
        .unwrap();
    let prepared = owner
        .complete_prepare(&prepare, Some(DraftRoute::Relay), 5)
        .unwrap();
    assert_eq!(
        owner.request_commit(
            &prepared.scope,
            prepared.draft_id,
            prepared.revision,
            5 + PREPARED_LIFETIME_MS,
        ),
        Err(CastDraftError::Expired)
    );
    assert_eq!(
        owner.complete_prepare(&prepare, Some(DraftRoute::Direct), 6),
        Err(CastDraftError::Stale)
    );

    let reopened = owner.open(scope(1)).unwrap();
    assert_ne!(reopened.draft_id, prepared.draft_id);
    assert_eq!(
        owner.snapshot(&reopened.scope).unwrap().draft_id,
        reopened.draft_id
    );
}

#[test]
fn replacement_cancel_and_revocation_invalidate_old_work() {
    let mut owner = MediaCastDraftOwner::new();
    let opened = owner.open(scope(1)).unwrap();
    let selected = owner
        .select_media(
            &opened.scope,
            opened.draft_id,
            opened.revision,
            media(13),
            true,
        )
        .unwrap();
    let selected = owner
        .select_device(
            &selected.scope,
            selected.draft_id,
            selected.revision,
            "tv-1".into(),
            true,
        )
        .unwrap();
    let connect = owner
        .request_connect(&selected.scope, selected.draft_id, selected.revision)
        .unwrap();
    let connected = owner.complete_connect(&connect, true).unwrap();
    assert_eq!(
        owner.request_prepare(
            &connected.scope,
            connected.draft_id,
            connected.revision,
            true,
        ),
        Err(CastDraftError::ConfirmationRequired)
    );
    let confirmation = owner.snapshot(&connected.scope).unwrap();
    let confirmed = owner
        .confirm_replacement(
            &confirmation.scope,
            confirmation.draft_id,
            confirmation.revision,
        )
        .unwrap();
    let prepare = owner
        .request_prepare(
            &confirmed.scope,
            confirmed.draft_id,
            confirmed.revision,
            true,
        )
        .expect("one explicit confirmation authorizes one prepare");
    let cancelled = owner
        .cancel(&prepare.scope, prepare.draft_id, prepare.revision)
        .unwrap();
    assert_eq!(cancelled.phase, DraftPhase::Cancelled);
    assert_eq!(owner.invalidate_media(media(13)), 1);
    assert!(owner.snapshot(&cancelled.scope).is_none());

    let next = owner.open(scope(2)).unwrap();
    assert_eq!(owner.invalidate_scope("profile-a", 2), 1);
    assert!(owner.snapshot(&next.scope).is_none());

    let first = owner.open(scope(3)).unwrap();
    let first = owner
        .select_media(
            &first.scope,
            first.draft_id,
            first.revision,
            media(21),
            true,
        )
        .unwrap();
    let second = owner.open(scope(4)).unwrap();
    let second = owner
        .select_media(
            &second.scope,
            second.draft_id,
            second.revision,
            media(21),
            true,
        )
        .unwrap();
    assert_eq!(owner.invalidate_player(3, 7, 9, media(21)), 1);
    assert!(owner.snapshot(&first.scope).is_none());
    assert!(owner.snapshot(&second.scope).is_some());
}

#[test]
fn capacity_and_invalid_inputs_are_bounded() {
    let mut owner = MediaCastDraftOwner::new();
    for tab in 1..=MAX_CAST_DRAFTS as u32 {
        owner.open(scope(tab)).unwrap();
    }
    assert_eq!(owner.len(), MAX_CAST_DRAFTS);
    assert_eq!(owner.open(scope(100)), Err(CastDraftError::Capacity));
    assert_eq!(owner.dropped_capacity_total(), 1);
    let mut invalid = scope(1);
    invalid.profile_id = "bad\u{202e}profile".into();
    assert_eq!(owner.open(invalid), Err(CastDraftError::Invalid));
    let first = owner.snapshot(&scope(1)).unwrap();
    assert_eq!(
        owner.select_media(
            &first.scope,
            first.draft_id,
            first.revision,
            media(13),
            false,
        ),
        Err(CastDraftError::Unavailable)
    );
}
