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
use serde::{de::Error as _, Deserialize, Deserializer, Serialize};
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

/// Reports whether `url` fits the closed URL rule: allowed scheme,
/// bounded length, no control characters or whitespace.
#[must_use]
pub fn is_safe_url(url: &str) -> bool {
    if url.len() > limits::MAX_URL_BYTES
        || url.contains('\\')
        || url
            .chars()
            .any(|character| character.is_control() || character.is_whitespace())
    {
        return false;
    }
    let Some((scheme, remainder)) = url.split_once("://") else {
        return false;
    };
    if !scheme.eq_ignore_ascii_case("http") && !scheme.eq_ignore_ascii_case("https") {
        return false;
    }
    let authority = remainder.split(['/', '?', '#']).next().unwrap_or_default();
    !authority.is_empty() && !authority.contains('@')
}

/// Closed content block kinds.  Inline formatting is not modelled as
/// separate blocks; link/image spans appear inside paragraph text as
/// recorded reference metadata instead of DOM equivalents.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum ContentBlock {
    /// Section heading, level 1..=6.
    Heading { level: u8, text: String },
    /// Plain paragraph text.
    Paragraph { text: String },
    /// One list item; `ordinal` is set for ordered lists (starts at 1).
    ListItem {
        depth: u8,
        ordinal: Option<u64>,
        text: String,
    },
    /// Visible link label and a Browser-validated absolute destination.
    Link { href: String, text: String },
    /// Quoted passage.
    Quote { text: String },
    /// Fenced or indented code with an optional language token.
    CodeBlock {
        language: Option<String>,
        text: String,
    },
    /// Reference-only image metadata; images are never loaded inline.
    Image { src: String, alt: String },
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
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case", deny_unknown_fields)]
pub enum TruncationReason {
    LimitBlockCount,
    LimitTotalBytes,
    LimitDepth,
}

/// Explicit truncation declaration.  Zero-values mean "nothing omitted".
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TruncationInfo {
    pub truncated: bool,
    pub omitted_blocks: u32,
    pub omitted_bytes: u32,
    pub reasons: Vec<TruncationReason>,
}

/// Provenance attestation.  `verified_by` must equal
/// [`VERIFIED_BY_BROWSER_PROCESS`]; anything else — including forged
/// values from page-controlled surfaces — fails validation.
pub const VERIFIED_BY_BROWSER_PROCESS: &str = "browser_process";

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Provenance {
    verified_by: String,
}

impl Provenance {
    #[must_use]
    fn browser_verified() -> Self {
        Self {
            verified_by: VERIFIED_BY_BROWSER_PROCESS.to_owned(),
        }
    }

    #[must_use]
    pub fn verified_by(&self) -> &str {
        &self.verified_by
    }
}

/// Navigation binding used by consumers to fence stale snapshots.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NavigationBinding {
    pub tab_id: TabId,
    pub generation: SessionGeneration,
}

impl NavigationBinding {
    #[must_use]
    pub const fn new(tab_id: TabId, generation: SessionGeneration) -> Self {
        Self { tab_id, generation }
    }
}

/// The frozen PageSnapshot envelope.
#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct PageSnapshot {
    schema_version: SchemaVersion,
    output_level: OutputLevel,
    navigation: NavigationBinding,
    url: String,
    title: String,
    revision: u64,
    provenance: Provenance,
    truncation: TruncationInfo,
    blocks: Vec<ContentBlock>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct PageSnapshotWire {
    schema_version: SchemaVersion,
    output_level: OutputLevel,
    navigation: NavigationBinding,
    url: String,
    title: String,
    revision: u64,
    provenance: Provenance,
    truncation: TruncationInfo,
    blocks: Vec<ContentBlock>,
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
    TruncationInconsistent,
    DuplicateTruncationReason,
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
            Self::TruncationInconsistent => "truncation fields are inconsistent",
            Self::DuplicateTruncationReason => "truncation reasons contain duplicates",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for SnapshotError {}

fn valid_text(text: &str, max_len: usize) -> bool {
    !text.is_empty()
        && text.len() <= max_len
        && !text
            .chars()
            .any(|character| character.is_control() && !matches!(character, '\n' | '\t'))
}

fn valid_optional_text(text: &str, max_len: usize) -> bool {
    text.len() <= max_len
        && !text
            .chars()
            .any(|character| character.is_control() && !matches!(character, '\n' | '\t'))
}

fn validate_block(block: &ContentBlock, level: OutputLevel) -> Result<usize, SnapshotError> {
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
        ContentBlock::Link { href, text } => {
            if !is_safe_url(href) {
                return Err(SnapshotError::InvalidUrl);
            }
            if !valid_text(text, max_text) {
                return Err(SnapshotError::BlockOutOfBounds);
            }
            href.len() + text.len()
        }
        ContentBlock::CodeBlock { language, text } => {
            if !valid_text(text, limits::MAX_CODE_BYTES) {
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
        ContentBlock::Image { src, alt } => {
            if !is_safe_url(src) {
                return Err(SnapshotError::InvalidUrl);
            }
            if !valid_optional_text(alt, max_text) {
                return Err(SnapshotError::BlockOutOfBounds);
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
                    if !valid_optional_text(cell, limits::MAX_TABLE_CELL_BYTES) {
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
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        output_level: OutputLevel,
        navigation: NavigationBinding,
        url: String,
        title: String,
        revision: u64,
        truncation: TruncationInfo,
        blocks: Vec<ContentBlock>,
    ) -> Result<Self, SnapshotError> {
        let snapshot = Self {
            schema_version: SchemaVersion::CURRENT,
            output_level,
            navigation,
            url,
            title,
            revision,
            provenance: Provenance::browser_verified(),
            truncation,
            blocks,
        };
        snapshot.validate()?;
        Ok(snapshot)
    }

    #[must_use]
    pub const fn schema_version(&self) -> SchemaVersion {
        self.schema_version
    }

    #[must_use]
    pub const fn output_level(&self) -> OutputLevel {
        self.output_level
    }

    #[must_use]
    pub const fn navigation(&self) -> &NavigationBinding {
        &self.navigation
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
    pub const fn revision(&self) -> u64 {
        self.revision
    }

    #[must_use]
    pub const fn provenance(&self) -> &Provenance {
        &self.provenance
    }

    #[must_use]
    pub const fn truncation(&self) -> &TruncationInfo {
        &self.truncation
    }

    #[must_use]
    pub fn blocks(&self) -> &[ContentBlock] {
        &self.blocks
    }

    /// Validates against the closed schema rules and the level-specific
    /// resource limits.  Decoded snapshots MUST pass through here.
    pub fn validate(&self) -> Result<(), SnapshotError> {
        if self.schema_version != SchemaVersion::CURRENT {
            return Err(SnapshotError::InvalidSchemaVersion);
        }
        if !is_safe_url(&self.url) {
            return Err(SnapshotError::InvalidUrl);
        }
        if !valid_text(&self.title, limits::MAX_TITLE_BYTES)
            || self
                .title
                .bytes()
                .any(|byte| matches!(byte, b'\n' | b'\r' | b'\t'))
        {
            return Err(SnapshotError::InvalidTitle);
        }
        if self.provenance.verified_by != VERIFIED_BY_BROWSER_PROCESS {
            return Err(SnapshotError::BadProvenance);
        }
        if self.blocks.len() > self.output_level.max_blocks() {
            return Err(SnapshotError::BlockCountExceeded);
        }
        let mut total = 0_usize;
        for block in &self.blocks {
            total = total.saturating_add(validate_block(block, self.output_level)?);
            if total > self.output_level.max_total_text_bytes() {
                return Err(SnapshotError::TotalBytesExceeded);
            }
        }
        // Truncation bookkeeping must be coherent.
        let has_omissions =
            self.truncation.omitted_blocks != 0 || self.truncation.omitted_bytes != 0;
        if self.truncation.truncated && !has_omissions {
            return Err(SnapshotError::TruncatedButNothingOmitted);
        }
        if self.truncation.truncated != has_omissions
            || self.truncation.truncated != !self.truncation.reasons.is_empty()
        {
            return Err(SnapshotError::TruncationInconsistent);
        }
        for (index, reason) in self.truncation.reasons.iter().enumerate() {
            if self.truncation.reasons[index + 1..].contains(reason) {
                return Err(SnapshotError::DuplicateTruncationReason);
            }
        }
        Ok(())
    }
}

impl<'de> Deserialize<'de> for PageSnapshot {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let wire = PageSnapshotWire::deserialize(deserializer)?;
        let snapshot = Self {
            schema_version: wire.schema_version,
            output_level: wire.output_level,
            navigation: wire.navigation,
            url: wire.url,
            title: wire.title,
            revision: wire.revision,
            provenance: wire.provenance,
            truncation: wire.truncation,
            blocks: wire.blocks,
        };
        snapshot.validate().map_err(D::Error::custom)?;
        Ok(snapshot)
    }
}

#[cfg(test)]
#[path = "snapshot_tests.rs"]
mod tests;
