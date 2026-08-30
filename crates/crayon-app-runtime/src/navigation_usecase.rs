//! Navigation use case (AGT-09, AG-008).
//!
//! Implements the gateway `NavigationPort` on top of an injected engine
//! port — the only path toward real navigation. The use case owns the
//! bounded open-tab table and the per-tab generation fence: a request
//! built before the tab's generation advanced is rejected as stale and
//! reaches no engine. Every dispatched request already carries the
//! user-confirmation reference validated by the tool layer.

use crayon_agent_gateway::tools::navigation::{
    NavigationOutcome, NavigationPort, NavigationRejection, NavigationRequest, NavigationTool,
};
use std::collections::BTreeMap;

/// Maximum tabs one use case tracks; the engine may open fewer.
pub const MAX_OPEN_TABS: usize = 64;

/// Engine port: the minimal navigation surface of the browser engine.
/// Implementations wrap the engine adapter; a `false`/`None` return is a
/// terminal engine rejection (dangerous redirect, download, blocked
/// target, engine refusal) and never a retryable state.
pub trait NavigationEngine {
    fn navigate(&mut self, tab: &str, url: &str) -> bool;
    fn open_tab(&mut self) -> Option<String>;
    fn close_tab(&mut self, tab: &str) -> bool;
    fn switch_tab(&mut self, tab: &str) -> bool;
    fn go_back(&mut self, tab: &str) -> bool;
    fn reload(&mut self, tab: &str) -> bool;
    fn scroll(&mut self, tab: &str, direction: &str, pixels: u32) -> bool;
}

/// Single owner of navigation dispatch state.
#[derive(Default)]
pub struct NavigationUseCase {
    tabs: BTreeMap<String, u64>,
    engine: Option<Box<dyn NavigationEngine>>,
    opened_count: u64,
}

impl NavigationUseCase {
    /// Creates the use case over one engine adapter; `None` models a
    /// stopped engine that rejects everything.
    pub fn new(engine: Option<Box<dyn NavigationEngine>>) -> Self {
        Self {
            tabs: BTreeMap::new(),
            engine,
            opened_count: 0,
        }
    }

    /// Registers one engine-created tab at its current generation.
    pub fn register_tab(&mut self, tab: &str, generation: u64) -> Result<(), NavigationRejection> {
        if self.tabs.len() >= MAX_OPEN_TABS {
            return Err(NavigationRejection::TabCapacityExceeded);
        }
        self.tabs.insert(tab.to_owned(), generation);
        Ok(())
    }

    /// Updates a tab's generation after an engine-side navigation event;
    /// unknown tabs report `false`.
    pub fn advance_generation(&mut self, tab: &str, generation: u64) -> bool {
        match self.tabs.get_mut(tab) {
            Some(bound) if generation > *bound => {
                *bound = generation;
                true
            }
            _ => false,
        }
    }

    /// Diagnostics: tabs opened through `OpenTab`.
    #[must_use]
    pub const fn opened_count(&self) -> u64 {
        self.opened_count
    }

    fn fenced(&self, request: &NavigationRequest) -> Result<String, NavigationRejection> {
        let Some(tab) = &request.tab_id else {
            // OpenTab: capacity is fenced here before the engine runs.
            if self.tabs.len() >= MAX_OPEN_TABS {
                return Err(NavigationRejection::TabCapacityExceeded);
            }
            return Ok(String::new());
        };
        match self.tabs.get(tab) {
            None => Err(NavigationRejection::UnknownTab),
            Some(&bound) if bound > request.generation => Err(NavigationRejection::StaleGeneration),
            Some(_) => Ok(tab.clone()),
        }
    }
}

impl NavigationPort for NavigationUseCase {
    fn execute(&mut self, request: &NavigationRequest) -> NavigationOutcome {
        let tab = match self.fenced(request) {
            Ok(tab) => tab,
            Err(rejection) => return NavigationOutcome::Rejected(rejection),
        };
        let Some(engine) = &mut self.engine else {
            return NavigationOutcome::Rejected(NavigationRejection::PortUnavailable);
        };
        let accepted = match request.tool {
            NavigationTool::Navigate => {
                let Some(url) = &request.url else {
                    return NavigationOutcome::Rejected(NavigationRejection::EngineRejected);
                };
                engine.navigate(&tab, url)
            }
            NavigationTool::OpenTab => {
                let Some(new_tab) = engine.open_tab() else {
                    return NavigationOutcome::Rejected(NavigationRejection::EngineRejected);
                };
                if self.tabs.len() >= MAX_OPEN_TABS {
                    return NavigationOutcome::Rejected(NavigationRejection::TabCapacityExceeded);
                }
                self.tabs.insert(new_tab, request.generation);
                self.opened_count += 1;
                true
            }
            NavigationTool::CloseTab => {
                let closed = engine.close_tab(&tab);
                if closed {
                    self.tabs.remove(&tab);
                }
                closed
            }
            NavigationTool::SwitchTab => engine.switch_tab(&tab),
            NavigationTool::GoBack => engine.go_back(&tab),
            NavigationTool::Reload => engine.reload(&tab),
            NavigationTool::Scroll => {
                let Some((direction, pixels)) = request.scroll else {
                    return NavigationOutcome::Rejected(NavigationRejection::EngineRejected);
                };
                let wire = match direction {
                    crayon_agent_gateway::tools::navigation::ScrollDirection::Up => "up",
                    crayon_agent_gateway::tools::navigation::ScrollDirection::Down => "down",
                    crayon_agent_gateway::tools::navigation::ScrollDirection::Top => "top",
                    crayon_agent_gateway::tools::navigation::ScrollDirection::Bottom => "bottom",
                };
                engine.scroll(&tab, wire, pixels)
            }
        };
        if accepted {
            NavigationOutcome::Accepted
        } else {
            NavigationOutcome::Rejected(NavigationRejection::EngineRejected)
        }
    }
}
