use crayon_ipc_schema::media_host_v2::{
    self, DraftAction, DraftCommand, DraftContext, Handshake, Kind,
};

use crate::desktop::{accept_handshake, draft_context_matches};

fn hello() -> Handshake {
    Handshake {
        kind: Kind::Hello,
        session_id: 7,
        generation: 9,
        capabilities: media_host_v2::CAP_MEDIA_READ,
        max_frame_bytes: media_host_v2::MAX_FRAME_BYTES,
        max_page_items: media_host_v2::MAX_PAGE_ITEMS,
    }
}

#[test]
fn accepts_one_hello_and_selects_only_implemented_capabilities() {
    let mut negotiated = None;
    let welcome = accept_handshake(&mut negotiated, hello()).unwrap();
    assert_eq!(welcome.kind, Kind::Welcome);
    assert_eq!(welcome.session_id, 7);
    assert_eq!(welcome.generation, 9);
    assert_eq!(welcome.capabilities, media_host_v2::CAP_MEDIA_READ);
    assert!(media_host_v2::matches_hello(hello(), welcome));
    assert!(negotiated.is_some());
}

#[test]
fn rejects_duplicate_and_welcome_first() {
    let mut negotiated = None;
    assert!(accept_handshake(&mut negotiated, hello()).is_ok());
    assert!(accept_handshake(&mut negotiated, hello()).is_err());

    let mut none = None;
    let mut welcome = hello();
    welcome.kind = Kind::Welcome;
    assert!(accept_handshake(&mut none, welcome).is_err());
    assert!(none.is_none());
}

#[test]
fn draft_command_requires_selected_capability_and_exact_generation() {
    let command = DraftCommand {
        context: DraftContext {
            session_id: 7,
            host_generation: 9,
            request_id: 1,
            profile_id: "default".into(),
            tab_id: 1,
            navigation_id: 2,
            tab_generation: 3,
        },
        action: DraftAction::Open,
        draft_id: 0,
        draft_revision: 0,
        media: None,
        device_id: String::new(),
    };
    let mut negotiated = None;
    let welcome = accept_handshake(
        &mut negotiated,
        Handshake {
            capabilities: media_host_v2::CAP_MEDIA_READ
                | media_host_v2::CAP_DRAFT
                | media_host_v2::CAP_REASON
                | media_host_v2::CAP_SESSION,
            ..hello()
        },
    )
    .unwrap();
    assert_eq!(
        welcome.capabilities,
        media_host_v2::CAP_MEDIA_READ
            | media_host_v2::CAP_DRAFT
            | media_host_v2::CAP_REASON
            | media_host_v2::CAP_SESSION
    );
    assert!(draft_context_matches(negotiated, &command));

    let unselected = Handshake {
        capabilities: media_host_v2::CAP_MEDIA_READ,
        ..welcome
    };
    assert!(!draft_context_matches(Some(unselected), &command));
    let mut stale = command.clone();
    stale.context.host_generation += 1;
    assert!(!draft_context_matches(Some(welcome), &stale));

    let mut old_client = None;
    let old_welcome = accept_handshake(
        &mut old_client,
        Handshake {
            capabilities: media_host_v2::CAP_MEDIA_READ | media_host_v2::CAP_DRAFT,
            ..hello()
        },
    )
    .unwrap();
    assert_eq!(old_welcome.capabilities, media_host_v2::CAP_MEDIA_READ);
    assert!(!draft_context_matches(old_client, &command));
}
