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
    let mut output = String::new();
    for block in snapshot.blocks() {
        let mut rendered = String::new();
        render_block(&mut rendered, block)?;
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

fn render_block(output: &mut String, block: &ContentBlock) -> Result<(), MarkdownError> {
    match block {
        ContentBlock::Heading { level, text } => {
            for _ in 0..*level {
                output.push('#');
            }
            output.push(' ');
            push_escaped(output, text);
        }
        ContentBlock::Paragraph { text } => push_escaped(output, text),
        ContentBlock::ListItem { text, .. } => {
            output.push_str("- ");
            push_prefixed_continuations(output, text, "  ");
        }
        ContentBlock::Link { text, .. } => push_escaped(output, text),
        ContentBlock::Quote { text } => {
            output.push_str("> ");
            push_prefixed_continuations(output, text, "> ");
        }
        ContentBlock::CodeBlock { text, .. } => push_indented_code(output, text),
        ContentBlock::Image { alt, .. } => push_escaped(output, alt),
        ContentBlock::Table { rows } => {
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
        ContentBlock::Divider => output.push_str("---"),
    }
    Ok(())
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
