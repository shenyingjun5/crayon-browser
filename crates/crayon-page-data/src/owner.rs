//! Generation-fenced, bounded ownership of verified page snapshots (CNT-03).

use crate::{ContentBlock, NavigationBinding, OutputLevel, PageSnapshot, TruncationInfo};
use crayon_domain::{SessionGeneration, TabId};
use std::collections::{BTreeMap, VecDeque};
use std::fmt::{Display, Formatter};

pub const MAX_CACHED_TABS: usize = 16;
pub const MAX_ACTIVE_READS: usize = 32;
pub const MAX_RETIRED_READS: usize = 128;
pub const MAX_PAGE_BLOCKS: usize = 256;

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct SnapshotReadId(u64);

impl SnapshotReadId {
    #[must_use]
    pub const fn get(self) -> u64 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OwnerError {
    InvalidPageSize,
    NotFound,
    StaleGeneration,
    StaleRevision,
    RevisionConflict,
    CapacityExceeded,
    ReadIdExhausted,
    Cancelled,
    ReadComplete,
    ShutDown,
}

impl Display for OwnerError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::InvalidPageSize => "snapshot page size is outside 1..=256",
            Self::NotFound => "snapshot or read not found",
            Self::StaleGeneration => "snapshot generation is stale",
            Self::StaleRevision => "snapshot revision is stale",
            Self::RevisionConflict => "same snapshot revision carries different content",
            Self::CapacityExceeded => "snapshot owner capacity exceeded",
            Self::ReadIdExhausted => "snapshot read id exhausted",
            Self::Cancelled => "snapshot read was cancelled",
            Self::ReadComplete => "snapshot read is complete",
            Self::ShutDown => "snapshot owner is shut down",
        })
    }
}

impl std::error::Error for OwnerError {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PublishResult {
    Stored,
    Replaced,
    Idempotent,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SnapshotPage {
    navigation: NavigationBinding,
    revision: u64,
    output_level: OutputLevel,
    url: String,
    title: String,
    truncation: TruncationInfo,
    blocks: Vec<ContentBlock>,
    has_more: bool,
}

impl SnapshotPage {
    #[must_use]
    pub const fn navigation(&self) -> &NavigationBinding {
        &self.navigation
    }
    #[must_use]
    pub const fn revision(&self) -> u64 {
        self.revision
    }
    #[must_use]
    pub const fn output_level(&self) -> OutputLevel {
        self.output_level
    }
    #[must_use]
    pub fn url(&self) -> &str {
        &self.url
    }
    #[must_use]
    pub fn title(&self) -> &str {
        &self.title
    }
    #[must_use]
    pub const fn truncation(&self) -> &TruncationInfo {
        &self.truncation
    }
    #[must_use]
    pub fn blocks(&self) -> &[ContentBlock] {
        &self.blocks
    }
    #[must_use]
    pub const fn has_more(&self) -> bool {
        self.has_more
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct SnapshotOwnerStats {
    pub cached_tabs: usize,
    pub active_reads: usize,
    pub retired_reads: usize,
    pub dropped_stale_results: u64,
    pub invalidated_reads: u64,
    pub evicted_tabs: u64,
}

struct TabEntry {
    tab_id: TabId,
    generation: SessionGeneration,
    snapshot: Option<PageSnapshot>,
    last_used: u64,
}

struct ReadState {
    tab_id: TabId,
    generation: SessionGeneration,
    revision: u64,
    offset: usize,
    page_size: usize,
}

#[derive(Clone, Copy)]
struct RetiredRead {
    id: SnapshotReadId,
    reason: OwnerError,
}

pub struct SnapshotOwner {
    tabs: Vec<TabEntry>,
    reads: BTreeMap<SnapshotReadId, ReadState>,
    retired: VecDeque<RetiredRead>,
    next_read_id: u64,
    clock: u64,
    shut_down: bool,
    dropped_stale_results: u64,
    invalidated_reads: u64,
    evicted_tabs: u64,
}

impl Default for SnapshotOwner {
    fn default() -> Self {
        Self {
            tabs: Vec::new(),
            reads: BTreeMap::new(),
            retired: VecDeque::new(),
            next_read_id: 1,
            clock: 0,
            shut_down: false,
            dropped_stale_results: 0,
            invalidated_reads: 0,
            evicted_tabs: 0,
        }
    }
}

impl SnapshotOwner {
    pub fn publish(&mut self, snapshot: PageSnapshot) -> Result<PublishResult, OwnerError> {
        self.require_running()?;
        let tab_id = snapshot.navigation().tab_id.clone();
        let generation = snapshot.navigation().generation;
        if let Some(index) = self.tab_index(&tab_id) {
            let current_generation = self.tabs[index].generation;
            if generation < current_generation {
                self.dropped_stale_results += 1;
                return Err(OwnerError::StaleGeneration);
            }
            if generation > current_generation {
                self.invalidate_reads_for(&tab_id, OwnerError::StaleGeneration);
                self.touch(index);
                self.tabs[index].generation = generation;
                self.tabs[index].snapshot = Some(snapshot);
                return Ok(PublishResult::Replaced);
            }
            if let Some(current) = &self.tabs[index].snapshot {
                if snapshot.revision() < current.revision() {
                    self.dropped_stale_results += 1;
                    return Err(OwnerError::StaleRevision);
                }
                if snapshot.revision() == current.revision() {
                    return if &snapshot == current {
                        self.touch(index);
                        Ok(PublishResult::Idempotent)
                    } else {
                        Err(OwnerError::RevisionConflict)
                    };
                }
                self.invalidate_reads_for(&tab_id, OwnerError::StaleRevision);
                self.touch(index);
                self.tabs[index].snapshot = Some(snapshot);
                return Ok(PublishResult::Replaced);
            }
            self.touch(index);
            self.tabs[index].snapshot = Some(snapshot);
            return Ok(PublishResult::Stored);
        }
        self.ensure_tab_capacity()?;
        self.clock = self.clock.saturating_add(1);
        self.tabs.push(TabEntry {
            tab_id,
            generation,
            snapshot: Some(snapshot),
            last_used: self.clock,
        });
        Ok(PublishResult::Stored)
    }

    pub fn advance_navigation(
        &mut self,
        tab_id: TabId,
        generation: SessionGeneration,
    ) -> Result<bool, OwnerError> {
        self.require_running()?;
        if let Some(index) = self.tab_index(&tab_id) {
            if generation < self.tabs[index].generation {
                return Err(OwnerError::StaleGeneration);
            }
            if generation == self.tabs[index].generation {
                self.touch(index);
                return Ok(false);
            }
            self.invalidate_reads_for(&tab_id, OwnerError::StaleGeneration);
            self.touch(index);
            self.tabs[index].generation = generation;
            self.tabs[index].snapshot = None;
            return Ok(true);
        }
        self.ensure_tab_capacity()?;
        self.clock = self.clock.saturating_add(1);
        self.tabs.push(TabEntry {
            tab_id,
            generation,
            snapshot: None,
            last_used: self.clock,
        });
        Ok(true)
    }

    pub fn begin_read(
        &mut self,
        tab_id: &TabId,
        generation: SessionGeneration,
        page_size: usize,
    ) -> Result<SnapshotReadId, OwnerError> {
        self.require_running()?;
        if page_size == 0 || page_size > MAX_PAGE_BLOCKS {
            return Err(OwnerError::InvalidPageSize);
        }
        if self.reads.len() >= MAX_ACTIVE_READS {
            return Err(OwnerError::CapacityExceeded);
        }
        let index = self.tab_index(tab_id).ok_or(OwnerError::NotFound)?;
        if self.tabs[index].generation != generation {
            return Err(OwnerError::StaleGeneration);
        }
        let revision = self.tabs[index]
            .snapshot
            .as_ref()
            .ok_or(OwnerError::NotFound)?
            .revision();
        let id = SnapshotReadId(self.next_read_id);
        self.next_read_id = self
            .next_read_id
            .checked_add(1)
            .ok_or(OwnerError::ReadIdExhausted)?;
        self.touch(index);
        self.reads.insert(
            id,
            ReadState {
                tab_id: tab_id.clone(),
                generation,
                revision,
                offset: 0,
                page_size,
            },
        );
        Ok(id)
    }

    pub fn next_page(&mut self, read_id: SnapshotReadId) -> Result<SnapshotPage, OwnerError> {
        self.require_running()?;
        let read = self
            .reads
            .get(&read_id)
            .ok_or_else(|| self.retired_reason(read_id).unwrap_or(OwnerError::NotFound))?;
        let index = self
            .tab_index(&read.tab_id)
            .ok_or(OwnerError::StaleGeneration)?;
        let snapshot = self.tabs[index]
            .snapshot
            .as_ref()
            .ok_or(OwnerError::StaleGeneration)?;
        if snapshot.navigation().generation != read.generation {
            return Err(OwnerError::StaleGeneration);
        }
        if snapshot.revision() != read.revision {
            return Err(OwnerError::StaleRevision);
        }
        let start = read.offset;
        let end = start
            .saturating_add(read.page_size)
            .min(snapshot.blocks().len());
        let page = SnapshotPage {
            navigation: snapshot.navigation().clone(),
            revision: snapshot.revision(),
            output_level: snapshot.output_level(),
            url: snapshot.url().to_owned(),
            title: snapshot.title().to_owned(),
            truncation: snapshot.truncation().clone(),
            blocks: snapshot.blocks()[start..end].to_vec(),
            has_more: end < snapshot.blocks().len(),
        };
        if page.has_more {
            self.reads.get_mut(&read_id).expect("read exists").offset = end;
        } else {
            self.reads.remove(&read_id);
            self.retire(read_id, OwnerError::ReadComplete);
        }
        self.touch(index);
        Ok(page)
    }

    pub fn cancel(&mut self, read_id: SnapshotReadId) -> Result<bool, OwnerError> {
        self.require_running()?;
        if self.reads.remove(&read_id).is_some() {
            self.retire(read_id, OwnerError::Cancelled);
            return Ok(true);
        }
        if self.retired_reason(read_id).is_some() {
            return Ok(false);
        }
        Err(OwnerError::NotFound)
    }

    pub fn close_tab(&mut self, tab_id: &TabId) -> Result<bool, OwnerError> {
        self.require_running()?;
        self.invalidate_reads_for(tab_id, OwnerError::Cancelled);
        if let Some(index) = self.tab_index(tab_id) {
            self.tabs.remove(index);
            return Ok(true);
        }
        Ok(false)
    }

    pub fn shut_down(&mut self) {
        self.shut_down = true;
        self.tabs.clear();
        self.reads.clear();
        self.retired.clear();
    }

    #[must_use]
    pub fn stats(&self) -> SnapshotOwnerStats {
        SnapshotOwnerStats {
            cached_tabs: self.tabs.len(),
            active_reads: self.reads.len(),
            retired_reads: self.retired.len(),
            dropped_stale_results: self.dropped_stale_results,
            invalidated_reads: self.invalidated_reads,
            evicted_tabs: self.evicted_tabs,
        }
    }

    fn require_running(&self) -> Result<(), OwnerError> {
        if self.shut_down {
            Err(OwnerError::ShutDown)
        } else {
            Ok(())
        }
    }

    fn tab_index(&self, tab_id: &TabId) -> Option<usize> {
        self.tabs.iter().position(|entry| &entry.tab_id == tab_id)
    }

    fn touch(&mut self, index: usize) {
        self.clock = self.clock.saturating_add(1);
        self.tabs[index].last_used = self.clock;
    }

    fn ensure_tab_capacity(&mut self) -> Result<(), OwnerError> {
        if self.tabs.len() < MAX_CACHED_TABS {
            return Ok(());
        }
        let candidate = self
            .tabs
            .iter()
            .enumerate()
            .filter(|(_, tab)| !self.reads.values().any(|read| read.tab_id == tab.tab_id))
            .min_by_key(|(_, tab)| tab.last_used)
            .map(|(index, _)| index)
            .ok_or(OwnerError::CapacityExceeded)?;
        self.tabs.remove(candidate);
        self.evicted_tabs += 1;
        Ok(())
    }

    fn invalidate_reads_for(&mut self, tab_id: &TabId, reason: OwnerError) {
        let ids: Vec<_> = self
            .reads
            .iter()
            .filter(|(_, read)| &read.tab_id == tab_id)
            .map(|(id, _)| *id)
            .collect();
        for id in ids {
            self.reads.remove(&id);
            self.retire(id, reason);
            self.invalidated_reads += 1;
        }
    }

    fn retire(&mut self, read_id: SnapshotReadId, reason: OwnerError) {
        if self.retired.len() >= MAX_RETIRED_READS {
            self.retired.pop_front();
        }
        self.retired.push_back(RetiredRead {
            id: read_id,
            reason,
        });
    }

    fn retired_reason(&self, read_id: SnapshotReadId) -> Option<OwnerError> {
        self.retired
            .iter()
            .find(|retired| retired.id == read_id)
            .map(|retired| retired.reason)
    }
}

#[cfg(test)]
#[path = "owner_tests.rs"]
mod owner_tests;
