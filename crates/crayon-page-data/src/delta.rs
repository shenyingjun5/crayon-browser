//! Same-navigation basic revision deltas and bounded chunk delivery.

use crate::{
    index::block_payload_bytes, ContentBlock, NavigationBinding, OutputLevel, PageSnapshot,
    TruncationInfo,
};
use crayon_domain::SessionGeneration;
use std::collections::VecDeque;
use std::fmt::{Display, Formatter};

pub const MAX_DELTA_BLOCKS: usize = 512;
pub const MAX_DELTA_CHUNK_BLOCKS: usize = 64;
pub const MAX_UNACKED_DELTA_CHUNKS: usize = 4;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeltaKind {
    NoChange,
    Splice,
    ReplaceAll,
}

#[derive(Clone, Debug, PartialEq)]
pub struct DeltaMetadata {
    pub navigation: NavigationBinding,
    pub output_level: OutputLevel,
    pub revision: u64,
    pub url: String,
    pub title: String,
    pub truncation: TruncationInfo,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SnapshotDelta {
    base_revision: u64,
    metadata: DeltaMetadata,
    kind: DeltaKind,
    start: usize,
    delete_count: usize,
    inserted: Vec<ContentBlock>,
    reused_blocks: usize,
    serialized_bytes: usize,
}

impl SnapshotDelta {
    pub fn between(previous: &PageSnapshot, current: &PageSnapshot) -> Result<Self, DeltaError> {
        if previous.navigation() != current.navigation() {
            return Err(DeltaError::StaleGeneration);
        }
        if current.revision() <= previous.revision() {
            return Err(DeltaError::RevisionNotAdvanced);
        }
        let old = previous.blocks();
        let new = current.blocks();
        let prefix = old
            .iter()
            .zip(new)
            .take_while(|(left, right)| left == right)
            .count();
        let suffix = old[prefix..]
            .iter()
            .rev()
            .zip(new[prefix..].iter().rev())
            .take_while(|(left, right)| left == right)
            .count();
        let delete_count = old.len().saturating_sub(prefix).saturating_sub(suffix);
        let insert_end = new.len().saturating_sub(suffix);
        let changed = delete_count.saturating_add(insert_end.saturating_sub(prefix));
        let (kind, start, delete_count, inserted, reused_blocks) = if changed == 0 {
            (DeltaKind::NoChange, prefix, 0, Vec::new(), old.len())
        } else if changed <= MAX_DELTA_BLOCKS {
            (
                DeltaKind::Splice,
                prefix,
                delete_count,
                new[prefix..insert_end].to_vec(),
                prefix.saturating_add(suffix),
            )
        } else {
            (DeltaKind::ReplaceAll, 0, old.len(), new.to_vec(), 0)
        };
        let metadata = DeltaMetadata {
            navigation: current.navigation().clone(),
            output_level: current.output_level(),
            revision: current.revision(),
            url: current.url().to_owned(),
            title: current.title().to_owned(),
            truncation: current.truncation().clone(),
        };
        let serialized_bytes = metadata
            .url
            .len()
            .saturating_add(metadata.title.len())
            .saturating_add(inserted.iter().fold(0usize, |total, block| {
                total.saturating_add(block_payload_bytes(block))
            }));
        Ok(Self {
            base_revision: previous.revision(),
            metadata,
            kind,
            start,
            delete_count,
            inserted,
            reused_blocks,
            serialized_bytes,
        })
    }

    #[must_use]
    pub const fn base_revision(&self) -> u64 {
        self.base_revision
    }
    #[must_use]
    pub const fn revision(&self) -> u64 {
        self.metadata.revision
    }
    #[must_use]
    pub const fn kind(&self) -> DeltaKind {
        self.kind
    }
    #[must_use]
    pub const fn start(&self) -> usize {
        self.start
    }
    #[must_use]
    pub const fn delete_count(&self) -> usize {
        self.delete_count
    }
    #[must_use]
    pub fn inserted(&self) -> &[ContentBlock] {
        &self.inserted
    }
    #[must_use]
    pub const fn reused_blocks(&self) -> usize {
        self.reused_blocks
    }
    #[must_use]
    pub const fn serialized_bytes(&self) -> usize {
        self.serialized_bytes
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct DeltaChunk {
    pub sequence: u32,
    pub base_revision: u64,
    pub revision: u64,
    pub kind: DeltaKind,
    pub start: usize,
    pub delete_count: usize,
    pub metadata: Option<DeltaMetadata>,
    pub blocks: Vec<ContentBlock>,
    pub terminal: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeltaError {
    RevisionNotAdvanced,
    StaleGeneration,
    StaleRevision,
    Backpressure,
    InvalidAck,
    Cancelled,
    Complete,
}

impl Display for DeltaError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::RevisionNotAdvanced => "delta revision did not advance",
            Self::StaleGeneration => "delta generation is stale",
            Self::StaleRevision => "delta revision is stale",
            Self::Backpressure => "delta unacknowledged chunk window is full",
            Self::InvalidAck => "delta acknowledgement is out of order",
            Self::Cancelled => "delta stream is cancelled",
            Self::Complete => "delta stream is complete",
        })
    }
}

impl std::error::Error for DeltaError {}

pub struct DeltaStream {
    delta: SnapshotDelta,
    cursor: usize,
    next_sequence: u32,
    metadata_sent: bool,
    delivered_terminal: bool,
    cancelled: bool,
    unacked: VecDeque<u32>,
}

impl DeltaStream {
    #[must_use]
    pub fn new(delta: SnapshotDelta) -> Self {
        Self {
            delta,
            cursor: 0,
            next_sequence: 0,
            metadata_sent: false,
            delivered_terminal: false,
            cancelled: false,
            unacked: VecDeque::new(),
        }
    }

    pub fn next_chunk(
        &mut self,
        generation: SessionGeneration,
        revision: u64,
    ) -> Result<DeltaChunk, DeltaError> {
        if self.cancelled {
            return Err(DeltaError::Cancelled);
        }
        if generation != self.delta.metadata.navigation.generation {
            self.cancel();
            return Err(DeltaError::StaleGeneration);
        }
        if revision != self.delta.revision() {
            self.cancel();
            return Err(DeltaError::StaleRevision);
        }
        if self.delivered_terminal {
            return Err(DeltaError::Complete);
        }
        if self.unacked.len() >= MAX_UNACKED_DELTA_CHUNKS {
            return Err(DeltaError::Backpressure);
        }
        let end = self
            .cursor
            .saturating_add(MAX_DELTA_CHUNK_BLOCKS)
            .min(self.delta.inserted.len());
        let terminal = end == self.delta.inserted.len();
        let sequence = self.next_sequence;
        self.next_sequence = self.next_sequence.saturating_add(1);
        let chunk = DeltaChunk {
            sequence,
            base_revision: self.delta.base_revision,
            revision: self.delta.revision(),
            kind: self.delta.kind,
            start: self.delta.start.saturating_add(self.cursor),
            delete_count: if self.metadata_sent {
                0
            } else {
                self.delta.delete_count
            },
            metadata: if self.metadata_sent {
                None
            } else {
                Some(self.delta.metadata.clone())
            },
            blocks: self.delta.inserted[self.cursor..end].to_vec(),
            terminal,
        };
        self.metadata_sent = true;
        self.cursor = end;
        self.delivered_terminal = terminal;
        self.unacked.push_back(sequence);
        if terminal {
            self.delta.inserted.clear();
        }
        Ok(chunk)
    }

    pub fn acknowledge(&mut self, sequence: u32) -> Result<(), DeltaError> {
        if self.cancelled {
            return Err(DeltaError::Cancelled);
        }
        if self.unacked.front().copied() != Some(sequence) {
            return Err(DeltaError::InvalidAck);
        }
        self.unacked.pop_front();
        Ok(())
    }

    pub fn cancel(&mut self) -> bool {
        if self.cancelled || self.delivered_terminal {
            return false;
        }
        self.cancelled = true;
        self.delta.inserted.clear();
        self.unacked.clear();
        true
    }

    #[must_use]
    pub fn unacked_chunks(&self) -> usize {
        self.unacked.len()
    }
}

#[cfg(test)]
#[path = "delta_tests.rs"]
mod tests;
