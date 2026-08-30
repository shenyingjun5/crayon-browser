//! Bounded field positions for one verified PageSnapshot revision.

use crate::{ContentBlock, PageSnapshot};

const BLOCK_KIND_COUNT: usize = 9;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BlockKind {
    Heading,
    Paragraph,
    ListItem,
    Link,
    Quote,
    CodeBlock,
    Image,
    Table,
    Divider,
}

impl BlockKind {
    const fn index(self) -> usize {
        match self {
            Self::Heading => 0,
            Self::Paragraph => 1,
            Self::ListItem => 2,
            Self::Link => 3,
            Self::Quote => 4,
            Self::CodeBlock => 5,
            Self::Image => 6,
            Self::Table => 7,
            Self::Divider => 8,
        }
    }

    #[must_use]
    pub const fn of(block: &ContentBlock) -> Self {
        match block {
            ContentBlock::Heading { .. } => Self::Heading,
            ContentBlock::Paragraph { .. } => Self::Paragraph,
            ContentBlock::ListItem { .. } => Self::ListItem,
            ContentBlock::Link { .. } => Self::Link,
            ContentBlock::Quote { .. } => Self::Quote,
            ContentBlock::CodeBlock { .. } => Self::CodeBlock,
            ContentBlock::Image { .. } => Self::Image,
            ContentBlock::Table { .. } => Self::Table,
            ContentBlock::Divider => Self::Divider,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SnapshotIndex {
    revision: u64,
    positions: [Vec<usize>; BLOCK_KIND_COUNT],
    total_positions: usize,
    payload_bytes: usize,
}

impl SnapshotIndex {
    #[must_use]
    pub fn build(snapshot: &PageSnapshot) -> Self {
        let mut positions: [Vec<usize>; BLOCK_KIND_COUNT] = Default::default();
        let mut payload_bytes = snapshot.url().len().saturating_add(snapshot.title().len());
        for (position, block) in snapshot.blocks().iter().enumerate() {
            positions[BlockKind::of(block).index()].push(position);
            payload_bytes = payload_bytes.saturating_add(block_payload_bytes(block));
        }
        Self {
            revision: snapshot.revision(),
            positions,
            total_positions: snapshot.blocks().len(),
            payload_bytes,
        }
    }

    #[must_use]
    pub const fn revision(&self) -> u64 {
        self.revision
    }

    #[must_use]
    pub fn positions(&self, kind: BlockKind) -> &[usize] {
        &self.positions[kind.index()]
    }

    #[must_use]
    pub const fn total_positions(&self) -> usize {
        self.total_positions
    }

    #[must_use]
    pub const fn payload_bytes(&self) -> usize {
        self.payload_bytes
    }
}

pub(crate) fn block_payload_bytes(block: &ContentBlock) -> usize {
    match block {
        ContentBlock::Heading { text, .. }
        | ContentBlock::Paragraph { text }
        | ContentBlock::ListItem { text, .. }
        | ContentBlock::Quote { text } => text.len(),
        ContentBlock::Link { href, text } => href.len().saturating_add(text.len()),
        ContentBlock::CodeBlock { language, text } => language
            .as_ref()
            .map_or(0, String::len)
            .saturating_add(text.len()),
        ContentBlock::Image { src, alt } => src.len().saturating_add(alt.len()),
        ContentBlock::Table { rows } => rows
            .iter()
            .flat_map(|row| &row.cells)
            .fold(0usize, |total, cell| total.saturating_add(cell.len())),
        ContentBlock::Divider => 0,
    }
}

#[cfg(test)]
#[path = "index_tests.rs"]
mod tests;
