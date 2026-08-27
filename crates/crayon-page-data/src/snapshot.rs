//! PageSnapshot wire contract (CNT-01): the verified current-page data
//! plane shared by the user surfaces and Agent R1.
//!
//! Everything in a snapshot is a fact attested by the Browser process —
//! `verified_by` is a constant, page-forged provenance cannot be
//! represented.  URLs are whitelist-only (`http`/`https`); scripting and
//! data URLs are rejected at validation time and can never be stored.
//! Resource limits come in two closed levels (`standard`, `compact`)
//! and every limit breach — like every other contract violation — is a
//! stable rejection, never silent truncation: truncation performed by
//! the collector must be declared explicitly in `TruncationInfo`.
//!
//! Schema rules mirror the FND-08 conventions: `deny_unknown_fields`
//! everywhere, constructor validation plus post-decode `validate()`,
//! non-zero `SchemaVersion`, golden vectors under `schemas/current`
//! mirrored into `schemas/previous`.
//!
//! Collection (CNT-02), generation-scoped ownership (CNT-03), main
//! content extraction (CNT-04) and Markdown rendering (CNT-05/06) live
//! in their own tasks; this module is pure schema.

use crayon_domain::{SessionGeneration, TabId};
use crayon_ipc_schema::SchemaVersion;
use serde::{Deserialize, Serialize};
use std::fmt::{Display, Formatter};

/// Output detail level; caps differ per level (see the limit constants).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum OutputLevel {
    /// Full standard surface for on-screen reading and R1 tools.
    #[default]
    Standard,
    /// Compact summaries for tight budgets (pickers, previews).
    Compact,
}

impl OutputLevel {
    /// Maximum number of blocks retained at this level.
    #[must_use]
    pub const fn max_blocks(self) -> usize {
        match self {
            Self::Standard => 4096,
            Self::Compact => 512,
        }
    }

    /// Maximum bytes of one block's text payload at this level.
    #[must_use]
    pub const fn max_block_text_bytes(self) -> usize {
        match self {
            Self::Standard => 16_384,
            Self::Compact => 2_048,
        }
    }

    /// Maximum total bytes across all block payloads at this level.
    #[must_use]
    pub const fn max_total_text_bytes(self) -> usize {
        match self {
            Self::Standard => 1_048_576,
            Self::Compact => 131_072,
        }
    }

    /// Stable wire name.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Standard => "standard",
            Self::Compact => "compact",
        }
    }
}

/// Hard per-shape limits independent of level.
pub mod limits {
    /// Maximum URL length in bytes.
    pub const MAX_URL_BYTES: usize = 2048;
    /// Maximum title length in bytes.
    pub const MAX_TITLE_BYTES: usize = 512;
    /// Maximum table rows.
    pub const MAX_TABLE_ROWS: usize = 256;
    /// Maximum table columns.
    pub const MAX_TABLE_COLS: usize = 32;
    /// Maximum table cell length in bytes.
    pub const MAX_TABLE_CELL_BYTES: usize = 1024;
    /// Maximum code block length in bytes.
    pub const MAX_CODE_BYTES: usize = 32_768;
    /// Maximum list nesting depth.
    pub const MAX_LIST_DEPTH: u8 = 8;
}

/// The only accepted URL schemes for links and images.
const ALLOWED_SCHEMES: [&str; 2] = ["http://", "https://"];

/// Reports whether `url` fits the closed URL rule: allowed scheme,
/// bounded length, no control characters or whitespace.
#[must_use]
pub fn is_safe_url(url: &str) -> bool {
    if url.len() > limits::MAX_URL_BYTES || url.bytes().any(|b| b < 0x21 || b == 0x7F) {
        return false;
    }
    let lowered = url.to_ascii_lowercase();
    ALLOWED_SCHEMES.iter().any(|scheme| lowered.starts_with(scheme))
}

/// Closed content block kinds.  Inline formatting is not modelled as
/// separate blocks; link/image spans appear inside paragraph text as
/// recorded reference metadata instead of DOM equivalents.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum ContentBlock {
    /// Section heading, level 1..=6.
    Heading {
        level: u8,
        text: String,
    },
    /// Plain paragraph text.
    Paragraph { text: String },
    /// One list item; `ordinal` is set for ordered lists (starts at 1).
    ListItem {
        depth: u8,
        ordinal: Option<u64>,
        text: String,
    },
    /// Quoted passage.
    Quote { text: String },
    /// Fenced or indented code with an optional language token.
    CodeBlock {
        language: Option<String>,
        text: String,
    },
    /// Reference-only image metadata; images are never loaded inline.
    ImageRef {
        src: String,
        alt: String,
    },
    /// Table with rectangular row data (header first when present).
    Table { rows: Vec<TableRow> },
    /// Horizontal rule / section separator.
    Divider,
}

/// One rectangular table row.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TableRow {
    pub cells: Vec<String>,
}

/// Why records were omitted by the collector.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case", deny_unknown_fields)]
pub enum TruncationReason {
    LimitBlockCount,
    LimitTotalBytes,
    LimitDepth,
}

/// Explicit truncation declaration.  Zero-values mean "nothing omitted".
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TruncationInfo {
    pub truncated: bool,
    pub omitted_blocks: u32,
    pub omitted_bytes: u32,
    pub reasons: u32, // bitmask over TruncationReason bit positions 0..2
}

/// Provenance attestation.  `verified_by` must equal
/// [`VERIFIED_BY_BROWSER_PROCESS`]; anything else — including forged
/// values from page-controlled surfaces — fails validation.
pub const VERIFIED_BY_BROWSER_PROCESS: &str = "browser_process";

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Provenance {
    pub verified_by: String,
}

/// Navigation binding used by consumers to fence stale snapshots.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NavigationBinding {
    pub tab_id: TabId,
    pub generation: SessionGeneration,
}

/// The frozen PageSnapshot envelope.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PageSnapshot {
    pub schema: SchemaVersion,
    pub level: OutputLevel,
    pub navigation: NavigationBinding,
    pub url: String,
    pub title: String,
    pub captured_at_ms: u64,
    pub revision: u64,
    pub provenance: Provenance,
    pub truncation: TruncationInfo,
    pub blocks: Vec<ContentBlock>,
}

/// Snapshot schema failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SnapshotError {
    InvalidSchemaVersion,
    InvalidUrl,
    InvalidTitle,
    BadProvenance,
    BlockOutOfBounds,
    ShapeInvalid,
    TotalBytesExceeded,
    BlockCountExceeded,
    TruncatedButNothingOmitted,
    TruncationFlagsUnknown,
}

impl Display for SnapshotError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidSchemaVersion => "snapshot schema version invalid",
            Self::InvalidUrl => "page url is outside the http/https whitelist",
            Self::InvalidTitle => "title missing or out of bounds",
            Self::BadProvenance => "provenance does not attest browser-process verification",
            Self::BlockOutOfBounds => "a block exceeds its shape or byte bounds",
            Self::ShapeInvalid => "block shape is inconsistent",
            Self::TotalBytesExceeded => "total text bytes exceed the level cap",
            Self::BlockCountExceeded => "block count exceeds the level cap",
            Self::TruncatedButNothingOmitted => "truncation flag without omissions",
            Self::TruncationFlagsUnknown => "truncation reason bits unknown",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for SnapshotError {}

const TRUNCATION_REASON_BITS: u32 = 0b111;

fn valid_text(text: &str, max_len: usize) -> bool {
    !text.is_empty()
        && text.len() <= max_len
        && !text.bytes().any(|byte| byte < 0x20 && byte != b'\n' && byte != b'\t')
}

fn validate_block(
    block: &ContentBlock,
    level: OutputLevel,
) -> Result<usize, SnapshotError> {
    let max_text = level.max_block_text_bytes();
    let bytes = match block {
        ContentBlock::Heading { level, text } => {
            if !(1..=6).contains(level) {
                return Err(SnapshotError::ShapeInvalid);
            }
            if !valid_text(text, max_text) {
                return Err(SnapshotError::BlockOutOfBounds);
            }
            text.len()
        }
        ContentBlock::Paragraph { text } | ContentBlock::Quote { text } => {
            if !valid_text(text, max_text) {
                return Err(SnapshotError::BlockOutOfBounds);
            }
            text.len()
        }
        ContentBlock::ListItem {
            depth,
            ordinal,
            text,
        } => {
            if *depth == 0 || *depth > limits::MAX_LIST_DEPTH {
                return Err(SnapshotError::ShapeInvalid);
            }
            if let Some(ordinal) = ordinal {
                if *ordinal == 0 {
                    return Err(SnapshotError::ShapeInvalid);
                }
            }
            if !valid_text(text, max_text) {
                return Err(SnapshotError::BlockOutOfBounds);
            }
            text.len()
        }
        ContentBlock::CodeBlock { language, text } => {
            if text.is_empty() || text.len() > limits::MAX_CODE_BYTES {
                return Err(SnapshotError::BlockOutOfBounds);
            }
            if let Some(language) = language {
                let ok = !language.is_empty()
                    && language.len() <= 32
                    && language.bytes().all(|byte| {
                        byte.is_ascii_lowercase()
                            || byte.is_ascii_digit()
                            || matches!(byte, b'_' | b'+' | b'-')
                    });
                if !ok {
                    return Err(SnapshotError::ShapeInvalid);
                }
            }
            text.len()
        }
        ContentBlock::ImageRef { src, alt } => {
            if !is_safe_url(src) || alt.len() > limits::MAX_TABLE_CELL_BYTES {
                return Err(SnapshotError::InvalidUrl);
            }
            src.len() + alt.len()
        }
        ContentBlock::Table { rows } => {
            if rows.is_empty() || rows.len() > limits::MAX_TABLE_ROWS {
                return Err(SnapshotError::ShapeInvalid);
            }
            let mut total = 0_usize;
            let mut columns = 0_usize;
            for row in rows {
                if columns == 0 {
                    columns = row.cells.len();
                }
                if row.cells.len() != columns
                    || row.cells.is_empty()
                    || row.cells.len() > limits::MAX_TABLE_COLS
                {
                    return Err(SnapshotError::ShapeInvalid);
                }
                for cell in &row.cells {
                    if cell.len() > limits::MAX_TABLE_CELL_BYTES {
                        return Err(SnapshotError::BlockOutOfBounds);
                    }
                    total += cell.len();
                }
            }
            total
        }
        ContentBlock::Divider => 0,
    };
    Ok(bytes)
}

impl PageSnapshot {
    /// Validates against the closed schema rules and the level-specific
    /// resource limits.  Decoded snapshots MUST pass through here.
    pub fn validate(&self) -> Result<(), SnapshotError> {
        if self.schema.get() != 1 {
            return Err(SnapshotError::InvalidSchemaVersion);
        }
        if !is_safe_url(&self.url) {
            return Err(SnapshotError::InvalidUrl);
        }
        if !valid_text(&self.title, limits::MAX_TITLE_BYTES) {
            return Err(SnapshotError::InvalidTitle);
        }
        if self.provenance.verified_by != VERIFIED_BY_BROWSER_PROCESS {
            return Err(SnapshotError::BadProvenance);
        }
        if self.blocks.len() > self.level.max_blocks() {
            return Err(SnapshotError::BlockCountExceeded);
        }
        let mut total = 0_usize;
        for block in &self.blocks {
            total = total.saturating_add(validate_block(block, self.level)?);
            if total > self.level.max_total_text_bytes() {
                return Err(SnapshotError::TotalBytesExceeded);
            }
        }
        // Truncation bookkeeping must be coherent.
        if self.truncation.truncated
            && self.truncation.omitted_blocks == 0
            && self.truncation.omitted_bytes == 0
        {
            return Err(SnapshotError::TruncatedButNothingOmitted);
        }
        if self.truncation.reasons & !TRUNCATION_REASON_BITS != 0 {
            return Err(SnapshotError::TruncationFlagsUnknown);
        }
        Ok(())
    }
}

#[cfg(test)]
#[path = "snapshot_tests.rs"]
mod tests;
