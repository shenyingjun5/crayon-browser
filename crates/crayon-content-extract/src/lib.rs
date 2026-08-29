//! Deterministic main-content selection over Browser-normalized facts (CNT-04).
//!
//! This crate never receives DOM, HTML, selectors, form values or engine handles.
//! It is a pure, bounded transformation into the closed `ContentBlock` contract.

use crayon_page_data::{is_safe_url, limits, ContentBlock, OutputLevel, TableRow};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::{Display, Formatter};

pub const STANDARD_MAX_SOURCE_FACTS: usize = 4096;
pub const COMPACT_MAX_SOURCE_FACTS: usize = 512;

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum RegionKind {
    Main,
    Article,
    Unknown,
    Navigation,
    Complementary,
    Header,
    Footer,
}

impl RegionKind {
    const fn score(self) -> Option<u64> {
        match self {
            Self::Main => Some(4_000_000),
            Self::Article => Some(3_000_000),
            Self::Unknown => Some(0),
            Self::Navigation | Self::Complementary | Self::Header | Self::Footer => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PrivacyClass {
    Public,
    SensitiveControl,
}

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct ReadingKey {
    pub section: u16,
    pub column: u16,
    pub row: u32,
    pub source_order: u32,
}

#[derive(Clone, Debug, PartialEq)]
pub enum SourceContent {
    Heading {
        level: u8,
        text: String,
    },
    Paragraph {
        text: String,
    },
    ListItem {
        depth: u8,
        ordinal: Option<u64>,
        text: String,
    },
    Link {
        href: String,
        text: String,
    },
    Quote {
        text: String,
    },
    CodeBlock {
        language: Option<String>,
        text: String,
    },
    Image {
        src: String,
        alt: String,
    },
    Table {
        rows: Vec<TableRow>,
    },
    Divider,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SourceFact {
    pub node_id: u64,
    pub region_id: u32,
    pub region_kind: RegionKind,
    pub reading_key: ReadingKey,
    pub visible: bool,
    pub same_origin: bool,
    pub privacy: PrivacyClass,
    pub content: SourceContent,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ExclusionCounts {
    pub hidden: u32,
    pub cross_origin: u32,
    pub sensitive: u32,
    pub non_content_region: u32,
    pub duplicate: u32,
    pub unsafe_or_invalid: u32,
    pub over_budget: u32,
    pub omitted_bytes: u64,
}

impl ExclusionCounts {
    #[must_use]
    pub const fn total(self) -> u64 {
        self.hidden as u64
            + self.cross_origin as u64
            + self.sensitive as u64
            + self.non_content_region as u64
            + self.duplicate as u64
            + self.unsafe_or_invalid as u64
            + self.over_budget as u64
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct ExtractedContent {
    pub selected_region_id: Option<u32>,
    pub blocks: Vec<ContentBlock>,
    pub exclusions: ExclusionCounts,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExtractError {
    CounterOverflow,
}

impl Display for ExtractError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("content extraction counter overflow")
    }
}

impl std::error::Error for ExtractError {}

struct Candidate {
    kind: RegionKind,
    text_bytes: u64,
    link_bytes: u64,
    block_count: u64,
}

impl Candidate {
    fn new(kind: RegionKind) -> Self {
        Self {
            kind,
            text_bytes: 0,
            link_bytes: 0,
            block_count: 0,
        }
    }

    fn add(&mut self, content: &SourceContent) {
        let bytes = content_text_bytes(content) as u64;
        self.text_bytes = self.text_bytes.saturating_add(bytes);
        self.block_count = self.block_count.saturating_add(1);
        if matches!(content, SourceContent::Link { .. }) {
            self.link_bytes = self.link_bytes.saturating_add(bytes);
        }
    }

    fn score(&self) -> Option<u64> {
        let base = self.kind.score()?;
        if self.block_count == 0 || self.text_bytes == 0 {
            return None;
        }
        if self.kind == RegionKind::Unknown && self.link_bytes == self.text_bytes {
            return None;
        }
        let density_penalty = self
            .link_bytes
            .saturating_mul(1000)
            .checked_div(self.text_bytes.max(1))
            .unwrap_or(1000)
            .saturating_mul(1000);
        Some(
            base.saturating_add(self.text_bytes.min(1_000_000))
                .saturating_add(self.block_count.min(4096).saturating_mul(100))
                .saturating_sub(density_penalty),
        )
    }
}

#[must_use]
pub fn max_source_facts(level: OutputLevel) -> usize {
    match level {
        OutputLevel::Standard => STANDARD_MAX_SOURCE_FACTS,
        OutputLevel::Compact => COMPACT_MAX_SOURCE_FACTS,
    }
}

pub fn extract_main_content(
    level: OutputLevel,
    facts: Vec<SourceFact>,
) -> Result<ExtractedContent, ExtractError> {
    let mut exclusions = ExclusionCounts::default();
    let limit = max_source_facts(level);
    let mut eligible = Vec::new();
    for (index, fact) in facts.into_iter().enumerate() {
        if index >= limit {
            increment(&mut exclusions.over_budget)?;
            exclusions.omitted_bytes = exclusions
                .omitted_bytes
                .saturating_add(content_text_bytes(&fact.content) as u64);
            continue;
        }
        if !fact.visible {
            increment(&mut exclusions.hidden)?;
        } else if !fact.same_origin {
            increment(&mut exclusions.cross_origin)?;
        } else if fact.privacy != PrivacyClass::Public {
            increment(&mut exclusions.sensitive)?;
        } else if fact.region_kind.score().is_none() {
            increment(&mut exclusions.non_content_region)?;
        } else if !valid_content(level, &fact.content) {
            increment(&mut exclusions.unsafe_or_invalid)?;
        } else {
            eligible.push(fact);
        }
    }

    eligible.sort_by_key(|fact| (fact.reading_key, fact.node_id));
    let mut seen = BTreeSet::new();
    eligible.retain(|fact| {
        if seen.insert(fact.node_id) {
            true
        } else {
            exclusions.duplicate = exclusions.duplicate.saturating_add(1);
            false
        }
    });

    let mut regions = BTreeMap::<u32, Candidate>::new();
    for fact in &eligible {
        let candidate = regions
            .entry(fact.region_id)
            .or_insert_with(|| Candidate::new(fact.region_kind));
        candidate.add(&fact.content);
    }
    let selected_region_id = regions
        .iter()
        .filter_map(|(id, candidate)| candidate.score().map(|score| (*id, score)))
        .max_by(|left, right| left.1.cmp(&right.1).then_with(|| right.0.cmp(&left.0)))
        .map(|(id, _)| id);

    let mut blocks = Vec::new();
    let mut retained_bytes = 0usize;
    for fact in eligible {
        if Some(fact.region_id) == selected_region_id {
            let bytes = content_text_bytes(&fact.content);
            let next_bytes = retained_bytes.saturating_add(bytes);
            if blocks.len() >= level.max_blocks() || next_bytes > level.max_total_text_bytes() {
                increment(&mut exclusions.over_budget)?;
                exclusions.omitted_bytes = exclusions.omitted_bytes.saturating_add(bytes as u64);
            } else {
                retained_bytes = next_bytes;
                blocks.push(into_block(fact.content));
            }
        } else {
            increment(&mut exclusions.non_content_region)?;
        }
    }
    Ok(ExtractedContent {
        selected_region_id,
        blocks,
        exclusions,
    })
}

fn valid_content(level: OutputLevel, content: &SourceContent) -> bool {
    let max_text = level.max_block_text_bytes();
    match content {
        SourceContent::Heading { level, text } => {
            (1..=6).contains(level) && valid_text(text, max_text)
        }
        SourceContent::Paragraph { text } | SourceContent::Quote { text } => {
            valid_text(text, max_text)
        }
        SourceContent::ListItem {
            depth,
            ordinal,
            text,
        } => {
            (1..=limits::MAX_LIST_DEPTH).contains(depth)
                && ordinal.is_none_or(|value| value > 0)
                && valid_text(text, max_text)
        }
        SourceContent::Link { href, text } => is_safe_url(href) && valid_text(text, max_text),
        SourceContent::CodeBlock { language, text } => {
            !text.is_empty()
                && text.len() <= limits::MAX_CODE_BYTES.min(max_text)
                && language.as_ref().is_none_or(|value| valid_token(value, 64))
        }
        SourceContent::Image { src, alt } => {
            is_safe_url(src) && alt.len() <= max_text && !has_forbidden_text(alt)
        }
        SourceContent::Table { rows } => valid_table(rows),
        SourceContent::Divider => true,
    }
}

fn valid_text(text: &str, max_bytes: usize) -> bool {
    !text.trim().is_empty() && text.len() <= max_bytes && !has_forbidden_text(text)
}

fn has_forbidden_text(text: &str) -> bool {
    text.chars()
        .any(|character| character.is_control() && !matches!(character, '\n' | '\t'))
}

fn valid_token(value: &str, max_bytes: usize) -> bool {
    !value.is_empty()
        && value.len() <= max_bytes
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'+' | b'-' | b'_' | b'.'))
}

fn valid_table(rows: &[TableRow]) -> bool {
    let Some(first) = rows.first() else {
        return false;
    };
    let columns = first.cells.len();
    columns > 0
        && columns <= limits::MAX_TABLE_COLS
        && rows.len() <= limits::MAX_TABLE_ROWS
        && rows.iter().all(|row| {
            row.cells.len() == columns
                && row.cells.iter().all(|cell| {
                    cell.len() <= limits::MAX_TABLE_CELL_BYTES && !has_forbidden_text(cell)
                })
        })
}

fn content_text_bytes(content: &SourceContent) -> usize {
    match content {
        SourceContent::Heading { text, .. }
        | SourceContent::Paragraph { text }
        | SourceContent::ListItem { text, .. }
        | SourceContent::Quote { text } => text.len(),
        SourceContent::Link { text, .. } => text.len(),
        SourceContent::CodeBlock { text, .. } => text.len(),
        SourceContent::Image { alt, .. } => alt.len(),
        SourceContent::Table { rows } => rows
            .iter()
            .flat_map(|row| &row.cells)
            .fold(0usize, |total, cell| total.saturating_add(cell.len())),
        SourceContent::Divider => 0,
    }
}

fn into_block(content: SourceContent) -> ContentBlock {
    match content {
        SourceContent::Heading { level, text } => ContentBlock::Heading { level, text },
        SourceContent::Paragraph { text } => ContentBlock::Paragraph { text },
        SourceContent::ListItem {
            depth,
            ordinal,
            text,
        } => ContentBlock::ListItem {
            depth,
            ordinal,
            text,
        },
        SourceContent::Link { href, text } => ContentBlock::Link { href, text },
        SourceContent::Quote { text } => ContentBlock::Quote { text },
        SourceContent::CodeBlock { language, text } => ContentBlock::CodeBlock { language, text },
        SourceContent::Image { src, alt } => ContentBlock::Image { src, alt },
        SourceContent::Table { rows } => ContentBlock::Table { rows },
        SourceContent::Divider => ContentBlock::Divider,
    }
}

fn increment(value: &mut u32) -> Result<(), ExtractError> {
    *value = value.checked_add(1).ok_or(ExtractError::CounterOverflow)?;
    Ok(())
}

#[cfg(test)]
#[path = "extract_tests.rs"]
mod tests;
