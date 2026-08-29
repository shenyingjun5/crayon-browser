//! Deterministic, bounded Markdown rendering for verified PageSnapshot values.

use crayon_domain::{SessionGeneration, TabId};
use crayon_page_data::{ContentBlock, OutputLevel, PageSnapshot};
use std::fmt::{Display, Formatter, Write};

pub const STANDARD_MAX_MARKDOWN_BYTES: usize = 1536 * 1024;
pub const COMPACT_MAX_MARKDOWN_BYTES: usize = 192 * 1024;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MarkdownDocument {
    tab_id: TabId,
    generation: SessionGeneration,
    revision: u64,
    markdown: String,
}

impl MarkdownDocument {
    #[must_use]
    pub const fn tab_id(&self) -> &TabId {
        &self.tab_id
    }

    #[must_use]
    pub const fn generation(&self) -> SessionGeneration {
        self.generation
    }

    #[must_use]
    pub const fn revision(&self) -> u64 {
        self.revision
    }

    #[must_use]
    pub fn markdown(&self) -> &str {
        &self.markdown
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MarkdownError {
    OutputTooLarge,
    FormattingFailed,
}

impl Display for MarkdownError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::OutputTooLarge => "Markdown output exceeds its level budget",
            Self::FormattingFailed => "Markdown formatting failed",
        })
    }
}

impl std::error::Error for MarkdownError {}

#[must_use]
pub const fn max_markdown_bytes(level: OutputLevel) -> usize {
    match level {
        OutputLevel::Standard => STANDARD_MAX_MARKDOWN_BYTES,
        OutputLevel::Compact => COMPACT_MAX_MARKDOWN_BYTES,
    }
}

pub fn render_snapshot(snapshot: &PageSnapshot) -> Result<MarkdownDocument, MarkdownError> {
    render_with_mode(snapshot, RenderMode::Normalized)
}

pub fn render_basic_snapshot(snapshot: &PageSnapshot) -> Result<MarkdownDocument, MarkdownError> {
    render_with_mode(snapshot, RenderMode::Basic)
}

#[derive(Clone, Copy)]
enum RenderMode {
    Basic,
    Normalized,
}

fn render_with_mode(
    snapshot: &PageSnapshot,
    mode: RenderMode,
) -> Result<MarkdownDocument, MarkdownError> {
    let mut output = String::new();
    for block in snapshot.blocks() {
        let mut rendered = String::new();
        render_block(&mut rendered, block, mode)?;
        while rendered.ends_with('\n') {
            rendered.pop();
        }
        if rendered.is_empty() {
            continue;
        }
        let separator_bytes = usize::from(!output.is_empty()) * 2;
        let required = output
            .len()
            .saturating_add(separator_bytes)
            .saturating_add(rendered.len())
            .saturating_add(1);
        if required > max_markdown_bytes(snapshot.output_level()) {
            return Err(MarkdownError::OutputTooLarge);
        }
        if !output.is_empty() {
            output.push_str("\n\n");
        }
        output.push_str(&rendered);
    }
    if !output.is_empty() {
        output.push('\n');
    }
    Ok(MarkdownDocument {
        tab_id: snapshot.navigation().tab_id.clone(),
        generation: snapshot.navigation().generation,
        revision: snapshot.revision(),
        markdown: output,
    })
}

fn render_block(
    output: &mut String,
    block: &ContentBlock,
    mode: RenderMode,
) -> Result<(), MarkdownError> {
    match block {
        ContentBlock::Heading { level, text } => {
            for _ in 0..*level {
                output.push('#');
            }
            output.push(' ');
            push_escaped(output, text);
        }
        ContentBlock::Paragraph { text } => push_escaped(output, text),
        ContentBlock::ListItem {
            depth,
            ordinal,
            text,
        } => {
            if matches!(mode, RenderMode::Normalized) {
                push_list_item(output, *depth, *ordinal, text);
            } else {
                output.push_str("- ");
                push_prefixed_continuations(output, text, "  ");
            }
        }
        ContentBlock::Link { href, text } => {
            if matches!(mode, RenderMode::Normalized) {
                push_reference(output, text, href, false);
            } else {
                push_escaped(output, text);
            }
        }
        ContentBlock::Quote { text } => {
            output.push_str("> ");
            push_prefixed_continuations(output, text, "> ");
        }
        ContentBlock::CodeBlock { language, text } => {
            if matches!(mode, RenderMode::Normalized) {
                push_fenced_code(output, language.as_deref(), text);
            } else {
                push_indented_code(output, text);
            }
        }
        ContentBlock::Image { src, alt } => {
            if matches!(mode, RenderMode::Normalized) {
                if !alt.is_empty() {
                    push_reference(output, alt, src, true);
                }
            } else {
                push_escaped(output, alt);
            }
        }
        ContentBlock::Table { rows } => {
            if matches!(mode, RenderMode::Normalized) {
                push_gfm_table(output, rows);
            } else {
                push_basic_table(output, rows);
            }
        }
        ContentBlock::Divider => output.push_str("---"),
    }
    Ok(())
}

fn push_list_item(output: &mut String, depth: u8, ordinal: Option<u64>, text: &str) {
    let indent = "  ".repeat(depth.saturating_sub(1) as usize);
    let marker = ordinal.map_or_else(|| "- ".to_owned(), |value| format!("{value}. "));
    output.push_str(&indent);
    output.push_str(&marker);
    let continuation = " ".repeat(indent.len() + marker.len());
    push_prefixed_continuations(output, text, &continuation);
}

fn push_reference(output: &mut String, label: &str, url: &str, image: bool) {
    if image {
        output.push('!');
    }
    output.push('[');
    push_escaped(output, label);
    output.push_str("](");
    push_destination(output, strip_query_and_fragment(url));
    output.push(')');
}

fn strip_query_and_fragment(url: &str) -> &str {
    let query = url.find('?').unwrap_or(url.len());
    let fragment = url.find('#').unwrap_or(url.len());
    &url[..query.min(fragment)]
}

fn push_destination(output: &mut String, destination: &str) {
    for character in destination.chars() {
        if matches!(character, '\\' | '(' | ')') {
            output.push('\\');
        }
        output.push(character);
    }
}

fn push_fenced_code(output: &mut String, language: Option<&str>, text: &str) {
    let fence = "`".repeat(longest_backtick_run(text).saturating_add(1).max(3));
    output.push_str(&fence);
    if let Some(language) = language {
        output.push_str(language);
    }
    output.push('\n');
    output.push_str(text);
    if !text.ends_with('\n') {
        output.push('\n');
    }
    output.push_str(&fence);
}

fn longest_backtick_run(text: &str) -> usize {
    let mut longest = 0usize;
    let mut current = 0usize;
    for byte in text.bytes() {
        if byte == b'`' {
            current += 1;
            longest = longest.max(current);
        } else {
            current = 0;
        }
    }
    longest
}

fn push_basic_table(output: &mut String, rows: &[crayon_page_data::TableRow]) {
    for (row_index, row) in rows.iter().enumerate() {
        if row_index > 0 {
            output.push('\n');
        }
        for (cell_index, cell) in row.cells.iter().enumerate() {
            if cell_index > 0 {
                output.push_str(" | ");
            }
            push_escaped(output, cell);
        }
    }
}

fn push_gfm_table(output: &mut String, rows: &[crayon_page_data::TableRow]) {
    push_table_row(output, &rows[0].cells);
    output.push('\n');
    output.push('|');
    for _ in &rows[0].cells {
        output.push_str(" --- |");
    }
    for row in &rows[1..] {
        output.push('\n');
        push_table_row(output, &row.cells);
    }
}

fn push_table_row(output: &mut String, cells: &[String]) {
    output.push('|');
    for cell in cells {
        output.push(' ');
        for line in cell.split('\n') {
            push_escaped(output, line);
            output.push(' ');
        }
        output.push('|');
    }
}

fn push_prefixed_continuations(output: &mut String, text: &str, prefix: &str) {
    for (index, line) in text.split('\n').enumerate() {
        if index > 0 {
            output.push('\n');
            output.push_str(prefix);
        }
        push_escaped(output, line);
    }
}

fn push_indented_code(output: &mut String, text: &str) {
    for (index, line) in text.split('\n').enumerate() {
        if index > 0 {
            output.push('\n');
        }
        output.push_str("    ");
        output.push_str(line);
    }
}

fn push_escaped(output: &mut String, text: &str) {
    for character in text.chars() {
        if matches!(
            character,
            '\\' | '`'
                | '*'
                | '_'
                | '{'
                | '}'
                | '['
                | ']'
                | '<'
                | '>'
                | '('
                | ')'
                | '#'
                | '+'
                | '-'
                | '.'
                | '!'
                | '|'
        ) {
            output.push('\\');
        }
        output
            .write_char(character)
            .expect("writing to String cannot fail");
    }
}

#[cfg(test)]
#[path = "render_tests.rs"]
mod tests;
