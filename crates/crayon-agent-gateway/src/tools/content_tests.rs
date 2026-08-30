use super::content::*;
use crate::grant::{GrantKind, GrantManager, GrantRequest, ProfileScope};
use crayon_domain::{AgentCapability, AgentTarget, SessionGeneration, TabId};
use crayon_page_data::{
    ContentBlock, NavigationBinding, OutputLevel, PageSnapshot, TruncationInfo,
};
use std::cell::Cell;

const SESSION: &str = "content-client";

fn profile(raw: &str) -> ProfileScope {
    ProfileScope::new(raw).unwrap()
}

fn tab() -> TabId {
    TabId::new("content-tab").unwrap()
}

fn target() -> AgentTarget {
    AgentTarget::Tab { tab: tab() }
}

fn snapshot() -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(tab(), SessionGeneration::from_raw(7)),
        "https://example.test/article".to_owned(),
        "Article title".to_owned(),
        3,
        TruncationInfo::default(),
        vec![ContentBlock::Paragraph {
            text: "Visible body".to_owned(),
        }],
    )
    .unwrap()
}

fn grant(manager: &mut GrantManager, profile: ProfileScope, target: Option<AgentTarget>) {
    manager
        .issue(
            GrantRequest {
                kind: GrantKind::AppSession,
                session: SESSION.to_owned(),
                profile,
                capability: AgentCapability::PageRead,
                target,
                task: None,
                ttl_ms: 10_000,
            },
            1,
        )
        .unwrap();
}

struct FakePort {
    calls: Cell<u32>,
    rejection: Option<ContentReadRejection>,
    target_count: usize,
}

impl FakePort {
    fn ok() -> Self {
        Self {
            calls: Cell::new(0),
            rejection: None,
            target_count: 1,
        }
    }

    fn enter(&self) -> Result<(), ContentReadRejection> {
        self.calls.set(self.calls.get() + 1);
        self.rejection.map_or(Ok(()), Err)
    }
}

impl ContentReadPort for FakePort {
    fn list_targets(
        &self,
        _profile: &ProfileScope,
    ) -> Result<Vec<ContentTarget>, ContentReadRejection> {
        self.enter()?;
        (0..self.target_count)
            .map(|index| {
                Ok(ContentTarget {
                    tab_id: TabId::new(&format!("content-tab-{index}")).unwrap(),
                    generation: SessionGeneration::from_raw(7),
                    title: "Article title".to_owned(),
                    active: index == 0,
                })
            })
            .collect()
    }

    fn get_title(
        &self,
        _profile: &ProfileScope,
        _target: &AgentTarget,
        _generation: SessionGeneration,
    ) -> Result<PageTitle, ContentReadRejection> {
        self.enter()?;
        Ok(PageTitle {
            tab_id: tab(),
            generation: SessionGeneration::from_raw(7),
            title: "Article title".to_owned(),
        })
    }

    fn get_selection(
        &self,
        _profile: &ProfileScope,
        _target: &AgentTarget,
        _generation: SessionGeneration,
    ) -> Result<PageSelection, ContentReadRejection> {
        self.enter()?;
        Ok(PageSelection {
            tab_id: tab(),
            generation: SessionGeneration::from_raw(7),
            text: "Visible selection".to_owned(),
        })
    }

    fn get_snapshot(
        &self,
        _profile: &ProfileScope,
        _target: &AgentTarget,
        _generation: SessionGeneration,
    ) -> Result<PageSnapshot, ContentReadRejection> {
        self.enter()?;
        Ok(snapshot())
    }
}

#[test]
fn five_frozen_r1_tools_share_grant_and_port() {
    let scope = profile("profile-a");
    let mut grants = GrantManager::new();
    grant(&mut grants, scope.clone(), None);
    let port = FakePort::ok();
    let mut reader = ContentReader::new(&mut grants, &port);

    assert_eq!(reader.list_targets(SESSION, &scope, 2).unwrap().len(), 1);
    assert_eq!(
        reader
            .get_title(
                SESSION,
                &scope,
                &target(),
                SessionGeneration::from_raw(7),
                2
            )
            .unwrap()
            .title,
        "Article title"
    );
    assert_eq!(
        reader
            .get_selection(
                SESSION,
                &scope,
                &target(),
                SessionGeneration::from_raw(7),
                2
            )
            .unwrap()
            .text,
        "Visible selection"
    );
    assert_eq!(
        reader
            .get_snapshot(
                SESSION,
                &scope,
                &target(),
                SessionGeneration::from_raw(7),
                MAX_SNAPSHOT_OUTPUT_BYTES,
                2,
            )
            .unwrap()
            .blocks()
            .len(),
        1
    );
    assert_eq!(
        reader
            .get_markdown(
                SESSION,
                &scope,
                &target(),
                SessionGeneration::from_raw(7),
                1024,
                2,
            )
            .unwrap()
            .markdown(),
        "Visible body\n"
    );
    assert_eq!(port.calls.get(), 5);
}

#[test]
fn unauthorized_and_wrong_target_never_reach_source() {
    let scope = profile("profile-a");
    let port = FakePort::ok();
    let mut empty = GrantManager::new();
    let mut reader = ContentReader::new(&mut empty, &port);
    assert_eq!(
        reader.get_title(
            SESSION,
            &scope,
            &target(),
            SessionGeneration::from_raw(7),
            2
        ),
        Err(ContentReadError::Grant(crate::grant::GrantError::Denied))
    );
    assert_eq!(port.calls.get(), 0);

    let mut scoped = GrantManager::new();
    grant(
        &mut scoped,
        scope.clone(),
        Some(AgentTarget::Tab {
            tab: TabId::new("another-tab").unwrap(),
        }),
    );
    let mut reader = ContentReader::new(&mut scoped, &port);
    assert!(matches!(
        reader.get_title(
            SESSION,
            &scope,
            &target(),
            SessionGeneration::from_raw(7),
            2
        ),
        Err(ContentReadError::Grant(crate::grant::GrantError::Denied))
    ));
    assert_eq!(port.calls.get(), 0);
}

#[test]
fn invalid_limits_and_oversized_selection_fail_closed() {
    assert_eq!(
        validate_selection(&"x".repeat(MAX_SELECTION_BYTES + 1)),
        Err(ContentReadRejection::SelectionTooLarge)
    );
    let scope = profile("profile-a");
    let mut grants = GrantManager::new();
    grant(&mut grants, scope.clone(), None);
    let port = FakePort::ok();
    let mut reader = ContentReader::new(&mut grants, &port);
    assert_eq!(
        reader.get_snapshot(
            SESSION,
            &scope,
            &target(),
            SessionGeneration::from_raw(7),
            0,
            2
        ),
        Err(ContentReadError::InvalidLimit)
    );
    assert_eq!(port.calls.get(), 0);

    let oversized = FakePort {
        target_count: MAX_CONTENT_TARGETS + 1,
        ..FakePort::ok()
    };
    let mut reader = ContentReader::new(&mut grants, &oversized);
    assert_eq!(
        reader.list_targets(SESSION, &scope, 2),
        Err(ContentReadError::Rejected(
            ContentReadRejection::OutputTooLarge
        ))
    );
}

#[test]
fn source_rejections_map_without_content() {
    for (rejection, expected) in [
        (
            ContentReadRejection::TargetInvalid,
            crayon_domain::CaapError::TargetInvalid,
        ),
        (
            ContentReadRejection::BackgroundTarget,
            crayon_domain::CaapError::CapabilityDenied,
        ),
        (
            ContentReadRejection::StaleGeneration,
            crayon_domain::CaapError::TargetStale,
        ),
        (
            ContentReadRejection::OutputTooLarge,
            crayon_domain::CaapError::QueueFull,
        ),
        (
            ContentReadRejection::CapacityExceeded,
            crayon_domain::CaapError::QueueFull,
        ),
        (
            ContentReadRejection::Cancelled,
            crayon_domain::CaapError::Cancelled,
        ),
    ] {
        assert_eq!(
            ContentReadError::Rejected(rejection).to_caap_error(),
            expected
        );
    }
}
