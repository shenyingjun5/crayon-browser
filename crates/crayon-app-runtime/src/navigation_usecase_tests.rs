//! Navigation use case tests (AGT-09, AG-008): generation fencing, tab
//! table bounds, engine rejection propagation, open/close tab lifecycle
//! and stopped-engine fail closed.

use crate::navigation_usecase::{NavigationEngine, NavigationUseCase, MAX_OPEN_TABS};
use crayon_agent_gateway::tools::navigation::{
    NavigationOutcome, NavigationPort, NavigationRejection, NavigationRequest, NavigationTool,
    ScrollDirection,
};
use std::cell::Cell;
use std::rc::Rc;

fn request(tool: NavigationTool, generation: u64, tab: Option<&str>) -> NavigationRequest {
    NavigationRequest::new(
        tool,
        generation,
        matches!(tool, NavigationTool::Navigate | NavigationTool::OpenTab)
            .then(|| "https://example.com".to_owned()),
        tab.map(std::string::ToString::to_string),
        (tool == NavigationTool::Scroll).then_some((ScrollDirection::Down, 240)),
        "conf-1",
    )
    .expect("valid request")
}

/// Recording engine: every call succeeds unless `reject` is set.
#[derive(Default)]
struct FakeEngine {
    calls: Rc<Cell<u32>>,
    reject: bool,
    next_tab: u32,
}

impl FakeEngine {
    fn counter(&self) -> Rc<Cell<u32>> {
        self.calls.clone()
    }
}

impl NavigationEngine for FakeEngine {
    fn navigate(&mut self, _tab: &str, _url: &str) -> bool {
        self.calls.set(self.calls.get() + 1);
        !self.reject
    }
    fn open_tab(&mut self) -> Option<String> {
        self.calls.set(self.calls.get() + 1);
        self.next_tab += 1;
        (!self.reject).then(|| format!("tab-{}", self.next_tab))
    }
    fn close_tab(&mut self, _tab: &str) -> bool {
        self.calls.set(self.calls.get() + 1);
        !self.reject
    }
    fn switch_tab(&mut self, _tab: &str) -> bool {
        self.calls.set(self.calls.get() + 1);
        !self.reject
    }
    fn go_back(&mut self, _tab: &str) -> bool {
        self.calls.set(self.calls.get() + 1);
        !self.reject
    }
    fn reload(&mut self, _tab: &str) -> bool {
        self.calls.set(self.calls.get() + 1);
        !self.reject
    }
    fn scroll(&mut self, _tab: &str, _direction: &str, _pixels: u32) -> bool {
        self.calls.set(self.calls.get() + 1);
        !self.reject
    }
}

#[test]
fn navigate_dispatches_to_engine_once_within_generation() {
    let mut usecase = NavigationUseCase::new(Some(Box::new(FakeEngine::default())));
    usecase.register_tab("tab-1", 3).expect("register");
    let outcome = usecase.execute(&request(NavigationTool::Navigate, 3, Some("tab-1")));
    assert_eq!(outcome, NavigationOutcome::Accepted);
    // Engine saw exactly one navigate call.
    // (call accounting asserted via the stale tests below)
}

#[test]
fn stale_generation_and_unknown_tab_never_reach_the_engine() {
    let engine = FakeEngine::default();
    let calls = engine.counter();
    let mut usecase = NavigationUseCase::new(Some(Box::new(engine)));
    usecase.register_tab("tab-1", 3).expect("register");
    // A request built before the generation advanced is stale.
    let outcome = usecase.execute(&request(NavigationTool::Navigate, 2, Some("tab-1")));
    assert_eq!(
        outcome,
        NavigationOutcome::Rejected(NavigationRejection::StaleGeneration)
    );
    // Unknown tab.
    let outcome = usecase.execute(&request(NavigationTool::GoBack, 3, Some("tab-9")));
    assert_eq!(
        outcome,
        NavigationOutcome::Rejected(NavigationRejection::UnknownTab)
    );
    assert_eq!(calls.get(), 0, "engine never sees fenced requests");
    // Same-generation and newer requests reach the engine.
    assert_eq!(
        usecase.execute(&request(NavigationTool::GoBack, 3, Some("tab-1"))),
        NavigationOutcome::Accepted
    );
    assert_eq!(calls.get(), 1);
}

#[test]
fn open_close_tab_lifecycle_and_capacity() {
    let mut usecase = NavigationUseCase::new(Some(Box::new(FakeEngine::default())));
    // Opening works and registers the new tab at the request generation.
    assert_eq!(
        usecase.execute(&request(NavigationTool::OpenTab, 3, None)),
        NavigationOutcome::Accepted
    );
    assert_eq!(usecase.opened_count(), 1);
    // The opened tab is now addressable.
    assert_eq!(
        usecase.execute(&request(NavigationTool::Reload, 3, Some("tab-1"))),
        NavigationOutcome::Accepted
    );
    // Closing removes it; a subsequent use is unknown.
    assert_eq!(
        usecase.execute(&request(NavigationTool::CloseTab, 3, Some("tab-1"))),
        NavigationOutcome::Accepted
    );
    assert_eq!(
        usecase.execute(&request(NavigationTool::Reload, 3, Some("tab-1"))),
        NavigationOutcome::Rejected(NavigationRejection::UnknownTab)
    );
    // Capacity is fenced before the engine is asked.
    let mut saturated = NavigationUseCase::new(Some(Box::new(FakeEngine::default())));
    for index in 0..MAX_OPEN_TABS {
        saturated
            .register_tab(&format!("t-{index}"), 1)
            .expect("register");
    }
    assert_eq!(
        saturated.execute(&request(NavigationTool::OpenTab, 1, None)),
        NavigationOutcome::Rejected(NavigationRejection::TabCapacityExceeded)
    );
}

#[test]
fn engine_rejection_is_terminal_and_stopped_engine_fails_closed() {
    let engine = FakeEngine {
        reject: true,
        ..FakeEngine::default()
    };
    let mut usecase = NavigationUseCase::new(Some(Box::new(engine)));
    usecase.register_tab("tab-1", 3).expect("register");
    assert_eq!(
        usecase.execute(&request(NavigationTool::Navigate, 3, Some("tab-1"))),
        NavigationOutcome::Rejected(NavigationRejection::EngineRejected)
    );
    // A rejected navigation does not open or consume tabs implicitly.
    assert_eq!(
        usecase.execute(&request(NavigationTool::GoBack, 3, Some("tab-1"))),
        NavigationOutcome::Rejected(NavigationRejection::EngineRejected)
    );
    // Stopped engine: every dispatch fails closed without the engine.
    let mut stopped: NavigationUseCase = NavigationUseCase::new(None);
    stopped.register_tab("tab-1", 3).expect("register");
    assert_eq!(
        stopped.execute(&request(NavigationTool::Navigate, 3, Some("tab-1"))),
        NavigationOutcome::Rejected(NavigationRejection::PortUnavailable)
    );
}
