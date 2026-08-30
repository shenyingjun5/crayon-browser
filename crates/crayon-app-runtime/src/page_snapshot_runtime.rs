//! Thread-safe app-runtime ownership seam for verified current-page snapshots.

use crayon_agent_gateway::grant::ProfileScope;
use crayon_agent_gateway::tools::content::{
    validate_selection, ContentReadPort, ContentReadRejection, ContentTarget, PageSelection,
    PageTitle,
};
use crayon_domain::{AgentTarget, SessionGeneration, TabId};
use crayon_page_data::{
    OwnerError, PageSnapshot, PublishResult, SnapshotOwner, SnapshotOwnerStats, SnapshotPage,
    SnapshotReadId, MAX_PAGE_BLOCKS,
};
use std::sync::{Mutex, MutexGuard};

#[derive(Clone)]
struct ContentTargetState {
    tab_id: TabId,
    generation: SessionGeneration,
    profile: ProfileScope,
    title: String,
    selection: String,
    active: bool,
    ready: bool,
}

#[derive(Default)]
struct RuntimeState {
    owner: SnapshotOwner,
    targets: Vec<ContentTargetState>,
}

/// Atomic publish failure for an Agent-visible page.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContentPublishError {
    Owner(OwnerError),
    Rejected(ContentReadRejection),
}

/// The sole app-runtime writer for page snapshot state. The mutex protects
/// only in-memory state; methods perform no callbacks, network or file IO.
#[derive(Default)]
pub struct PageSnapshotRuntime {
    state: Mutex<RuntimeState>,
}

impl PageSnapshotRuntime {
    pub fn publish(&self, snapshot: PageSnapshot) -> Result<PublishResult, OwnerError> {
        let mut state = self.lock();
        let result = state.owner.publish(snapshot)?;
        // This compatibility entry point carries no Profile/foreground
        // attestation. Invalidate the Agent view instead of risking stale
        // metadata after owner replacement/eviction.
        state.targets.clear();
        Ok(result)
    }

    /// Atomically publishes a verified snapshot and its Browser-owned R1
    /// metadata. A tab cannot migrate across profiles without first being
    /// closed, and at most one target per profile is active.
    pub fn publish_content(
        &self,
        profile: ProfileScope,
        active: bool,
        selection: String,
        snapshot: PageSnapshot,
    ) -> Result<PublishResult, ContentPublishError> {
        validate_selection(&selection).map_err(ContentPublishError::Rejected)?;
        let tab_id = snapshot.navigation().tab_id.clone();
        let generation = snapshot.navigation().generation;
        let title = snapshot.title().to_owned();
        let mut state = self.lock();
        if let Some(existing) = state.targets.iter().find(|item| item.tab_id == tab_id) {
            if existing.profile != profile {
                return Err(ContentPublishError::Rejected(
                    ContentReadRejection::TargetInvalid,
                ));
            }
            if generation < existing.generation {
                return Err(ContentPublishError::Rejected(
                    ContentReadRejection::StaleGeneration,
                ));
            }
        } else if state.targets.len() >= crayon_page_data::MAX_CACHED_TABS {
            return Err(ContentPublishError::Rejected(
                ContentReadRejection::CapacityExceeded,
            ));
        }
        let result = state
            .owner
            .publish(snapshot)
            .map_err(ContentPublishError::Owner)?;
        if active {
            for item in &mut state.targets {
                if item.profile == profile {
                    item.active = false;
                }
            }
        }
        match state.targets.iter_mut().find(|item| item.tab_id == tab_id) {
            Some(item) => {
                item.generation = generation;
                item.title = title;
                item.selection = selection;
                item.active = active;
                item.ready = true;
            }
            None => state.targets.push(ContentTargetState {
                tab_id,
                generation,
                profile,
                title,
                selection,
                active,
                ready: true,
            }),
        }
        Ok(result)
    }

    pub fn advance_navigation(
        &self,
        tab_id: TabId,
        generation: SessionGeneration,
    ) -> Result<bool, OwnerError> {
        let mut state = self.lock();
        let changed = state.owner.advance_navigation(tab_id.clone(), generation)?;
        if changed {
            if let Some(target) = state.targets.iter_mut().find(|item| item.tab_id == tab_id) {
                target.generation = generation;
                target.selection.clear();
                target.ready = false;
            }
        }
        Ok(changed)
    }

    pub fn begin_read(
        &self,
        tab_id: &TabId,
        generation: SessionGeneration,
        page_size: usize,
    ) -> Result<SnapshotReadId, OwnerError> {
        self.lock().owner.begin_read(tab_id, generation, page_size)
    }

    pub fn next_page(&self, read_id: SnapshotReadId) -> Result<SnapshotPage, OwnerError> {
        self.lock().owner.next_page(read_id)
    }

    pub fn cancel(&self, read_id: SnapshotReadId) -> Result<bool, OwnerError> {
        self.lock().owner.cancel(read_id)
    }

    pub fn close_tab(&self, tab_id: &TabId) -> Result<bool, OwnerError> {
        let mut state = self.lock();
        let closed = state.owner.close_tab(tab_id)?;
        state.targets.retain(|target| &target.tab_id != tab_id);
        Ok(closed)
    }

    pub fn shut_down(&self) {
        let mut state = self.lock();
        state.owner.shut_down();
        state.targets.clear();
    }

    #[must_use]
    pub fn stats(&self) -> SnapshotOwnerStats {
        self.lock().owner.stats()
    }

    fn lock(&self) -> MutexGuard<'_, RuntimeState> {
        self.state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }
}

fn owner_rejection(error: OwnerError) -> ContentReadRejection {
    match error {
        OwnerError::StaleGeneration | OwnerError::StaleRevision => {
            ContentReadRejection::StaleGeneration
        }
        OwnerError::NotFound => ContentReadRejection::TargetInvalid,
        OwnerError::CapacityExceeded | OwnerError::InvalidPageSize => {
            ContentReadRejection::CapacityExceeded
        }
        OwnerError::Cancelled | OwnerError::ReadComplete => ContentReadRejection::Cancelled,
        OwnerError::ShutDown | OwnerError::ReadIdExhausted | OwnerError::RevisionConflict => {
            ContentReadRejection::SourceUnavailable
        }
    }
}

fn resolve_target<'a>(
    targets: &'a [ContentTargetState],
    profile: &ProfileScope,
    target: &AgentTarget,
    generation: SessionGeneration,
) -> Result<&'a ContentTargetState, ContentReadRejection> {
    let resolved = match target {
        AgentTarget::Tab { tab } => targets.iter().find(|item| item.tab_id == *tab),
        AgentTarget::ActiveTab => targets
            .iter()
            .find(|item| item.profile == *profile && item.active),
    }
    .ok_or(ContentReadRejection::TargetInvalid)?;
    if resolved.profile != *profile {
        return Err(ContentReadRejection::TargetInvalid);
    }
    if !resolved.ready {
        return Err(ContentReadRejection::SourceUnavailable);
    }
    if !resolved.active {
        return Err(ContentReadRejection::BackgroundTarget);
    }
    if resolved.generation != generation {
        return Err(ContentReadRejection::StaleGeneration);
    }
    Ok(resolved)
}

fn collect_snapshot(
    owner: &mut SnapshotOwner,
    tab_id: &TabId,
    generation: SessionGeneration,
) -> Result<PageSnapshot, ContentReadRejection> {
    let read = owner
        .begin_read(tab_id, generation, MAX_PAGE_BLOCKS)
        .map_err(owner_rejection)?;
    let first = owner.next_page(read).map_err(owner_rejection)?;
    let mut blocks = first.blocks().to_vec();
    let mut has_more = first.has_more();
    while has_more {
        let page = owner.next_page(read).map_err(owner_rejection)?;
        blocks.extend_from_slice(page.blocks());
        has_more = page.has_more();
    }
    PageSnapshot::new(
        first.output_level(),
        first.navigation().clone(),
        first.url().to_owned(),
        first.title().to_owned(),
        first.revision(),
        first.truncation().clone(),
        blocks,
    )
    .map_err(|_| ContentReadRejection::SourceUnavailable)
}

impl ContentReadPort for PageSnapshotRuntime {
    fn list_targets(
        &self,
        profile: &ProfileScope,
    ) -> Result<Vec<ContentTarget>, ContentReadRejection> {
        let state = self.lock();
        let mut targets: Vec<_> = state
            .targets
            .iter()
            .filter(|item| item.profile == *profile && item.ready)
            .map(|item| ContentTarget {
                tab_id: item.tab_id.clone(),
                generation: item.generation,
                title: item.title.clone(),
                active: item.active,
            })
            .collect();
        targets.sort_by(|left, right| left.tab_id.as_str().cmp(right.tab_id.as_str()));
        Ok(targets)
    }

    fn get_title(
        &self,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
    ) -> Result<PageTitle, ContentReadRejection> {
        let state = self.lock();
        let resolved = resolve_target(&state.targets, profile, target, generation)?;
        Ok(PageTitle {
            tab_id: resolved.tab_id.clone(),
            generation: resolved.generation,
            title: resolved.title.clone(),
        })
    }

    fn get_selection(
        &self,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
    ) -> Result<PageSelection, ContentReadRejection> {
        let state = self.lock();
        let resolved = resolve_target(&state.targets, profile, target, generation)?;
        Ok(PageSelection {
            tab_id: resolved.tab_id.clone(),
            generation: resolved.generation,
            text: resolved.selection.clone(),
        })
    }

    fn get_snapshot(
        &self,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
    ) -> Result<PageSnapshot, ContentReadRejection> {
        let mut state = self.lock();
        let tab_id = resolve_target(&state.targets, profile, target, generation)?
            .tab_id
            .clone();
        collect_snapshot(&mut state.owner, &tab_id, generation)
    }
}

#[cfg(test)]
#[path = "page_snapshot_runtime_tests.rs"]
mod tests;
