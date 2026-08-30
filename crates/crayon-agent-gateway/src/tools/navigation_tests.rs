//! Navigation tool input tests (AGT-09, AG-008 契约侧): dangerous scheme
//! rejection, closed per-verb field sets, confirmation binding, bounded
//! scroll and deterministic receipt summaries.

use crate::tools::navigation::{
    to_caap_error, NavigationInputError, NavigationRequest, NavigationTool, ScrollDirection,
    MAX_SCROLL_PIXELS,
};
use crayon_domain::CaapError;

fn request(
    tool: NavigationTool,
    url: Option<&str>,
    tab: Option<&str>,
    scroll: Option<(ScrollDirection, u32)>,
) -> Result<NavigationRequest, NavigationInputError> {
    NavigationRequest::new(
        tool,
        7,
        url.map(std::string::ToString::to_string),
        tab.map(std::string::ToString::to_string),
        scroll,
        "conf-1",
    )
}

#[test]
fn dangerous_and_malformed_schemes_are_rejected() {
    for hostile in [
        "javascript:alert(1)",
        "file:///etc/passwd",
        "data:text/html,hi",
        "ftp://example.com",
        "https://user@example.com",
        "https://example.com/path with space",
        "not a url",
        "",
    ] {
        let parsed = request(NavigationTool::Navigate, Some(hostile), Some("tab-1"), None);
        assert!(
            matches!(
                parsed,
                Err(NavigationInputError::DangerousScheme | NavigationInputError::InvalidUrl)
            ),
            "{hostile} must be rejected"
        );
    }
    // A clean URL passes.
    assert!(request(
        NavigationTool::Navigate,
        Some("https://example.com/page"),
        Some("tab-1"),
        None
    )
    .is_ok());
}

#[test]
fn per_verb_field_sets_are_closed() {
    // Navigate and OpenTab need URLs; OpenTab must not carry a tab.
    assert_eq!(
        request(NavigationTool::Navigate, None, Some("tab-1"), None),
        Err(NavigationInputError::InvalidUrl)
    );
    assert_eq!(
        request(
            NavigationTool::OpenTab,
            Some("https://example.com"),
            Some("tab-1"),
            None
        ),
        Err(NavigationInputError::UnexpectedTab)
    );
    // Tab-scoped verbs need a tab and no URL.
    assert_eq!(
        request(NavigationTool::GoBack, None, None, None),
        Err(NavigationInputError::MissingTab)
    );
    assert_eq!(
        request(
            NavigationTool::Reload,
            Some("https://example.com"),
            Some("tab-1"),
            None
        ),
        Err(NavigationInputError::UnexpectedUrl)
    );
    // Scroll needs bounded parameters.
    assert_eq!(
        request(NavigationTool::Scroll, None, Some("tab-1"), None),
        Err(NavigationInputError::MissingScroll)
    );
    assert_eq!(
        request(
            NavigationTool::Scroll,
            None,
            Some("tab-1"),
            Some((ScrollDirection::Down, MAX_SCROLL_PIXELS + 1))
        ),
        Err(NavigationInputError::ScrollOutOfBounds)
    );
    assert!(request(
        NavigationTool::Scroll,
        None,
        Some("tab-1"),
        Some((ScrollDirection::Down, 240))
    )
    .is_ok());
}

#[test]
fn confirmation_binding_is_required_and_validated() {
    let missing = NavigationRequest::new(
        NavigationTool::GoBack,
        7,
        None,
        Some("tab-1".to_owned()),
        None,
        "",
    );
    assert_eq!(missing, Err(NavigationInputError::MissingConfirmation));
    assert_eq!(
        to_caap_error(NavigationInputError::MissingConfirmation),
        CaapError::CapabilityDenied
    );
    let malformed = NavigationRequest::new(
        NavigationTool::GoBack,
        7,
        None,
        Some("tab-1".to_owned()),
        None,
        "bad confirmation!",
    );
    assert_eq!(malformed, Err(NavigationInputError::InvalidConfirmation));
    assert_eq!(
        to_caap_error(NavigationInputError::InvalidConfirmation),
        CaapError::InvalidMessage
    );
}

#[test]
fn receipt_summaries_are_deterministic_and_data_free() {
    let navigate = request(
        NavigationTool::Navigate,
        Some("https://example.com/page?q=secret"),
        Some("tab-1"),
        None,
    )
    .expect("valid");
    let summary = navigate.receipt_summary();
    assert_eq!(summary, "nav.navigate url:example.com confirmed");
    // The query string never reaches the receipt.
    assert!(!summary.contains("secret"));
    let scroll = request(
        NavigationTool::Scroll,
        None,
        Some("tab-1"),
        Some((ScrollDirection::Top, 1)),
    )
    .expect("valid");
    assert_eq!(
        scroll.receipt_summary(),
        "nav.scroll scroll:Top:1 confirmed"
    );
}
