//! CNT-18b: pure Rust owner for one Browser-verified content-host stream.
//! Transport, CEF and process lifecycle belong to CNT-18c.

use crate::page_snapshot_runtime::PageSnapshotRuntime;
use crayon_content_extract::{
    extract_main_content, PrivacyClass, ReadingKey, RegionKind, SourceContent, SourceFact,
};
use crayon_content_markdown::render_snapshot;
use crayon_domain::{SessionGeneration, TabId};
use crayon_ipc_schema::{
    ContentHostErrorCode, ContentHostFact, ContentHostFactKind, ContentHostMessage,
    ContentHostMode, ContentHostTerminalStatus, MAX_CONTENT_HOST_FACTS,
    MAX_CONTENT_HOST_MARKDOWN_BYTES,
};
use crayon_page_data::{NavigationBinding, OutputLevel, PageSnapshot, TableRow, TruncationInfo};

pub const MAX_ACTIVE_CONTENT_STREAMS: usize = 8;
pub const MAX_CONTENT_STREAM_CHUNKS: u32 = 64;
const MAX_TRACKED_TABS: usize = 16;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContentHostRuntimeError {
    ShutDown,
    InvalidMessage,
    DuplicateRequest,
    DuplicateTab,
    NotFound,
    StaleNavigation,
    SequenceMismatch,
    CapacityExceeded,
    ExtractionFailed,
    SnapshotFailed,
    MarkdownFailed,
}

impl ContentHostRuntimeError {
    #[must_use]
    pub const fn reply_code(self) -> ContentHostErrorCode {
        match self {
            Self::ShutDown => ContentHostErrorCode::HostUnavailable,
            Self::InvalidMessage | Self::DuplicateRequest | Self::DuplicateTab => {
                ContentHostErrorCode::InvalidMessage
            }
            Self::NotFound => ContentHostErrorCode::InvalidState,
            Self::StaleNavigation | Self::SequenceMismatch => ContentHostErrorCode::StaleNavigation,
            Self::CapacityExceeded => ContentHostErrorCode::CapacityExceeded,
            Self::ExtractionFailed => ContentHostErrorCode::ExtractionFailed,
            Self::SnapshotFailed => ContentHostErrorCode::InvalidState,
            Self::MarkdownFailed => ContentHostErrorCode::MarkdownFailed,
        }
    }
}

struct ActiveStream {
    request_id: String,
    tab_id: String,
    navigation_id: u64,
    generation: u64,
    mode: ContentHostMode,
    url: String,
    title: String,
    next_sequence: u32,
    fact_bytes: usize,
    facts: Vec<ContentHostFact>,
}

struct TabState {
    tab_id: TabId,
    navigation_id: u64,
    generation: u64,
    revision: u64,
}

#[derive(Default)]
pub struct ContentHostRuntime {
    snapshots: PageSnapshotRuntime,
    streams: Vec<ActiveStream>,
    tabs: Vec<TabState>,
    shut_down: bool,
}

impl ContentHostRuntime {
    pub fn handle(
        &mut self,
        message: ContentHostMessage,
    ) -> Result<Vec<ContentHostMessage>, ContentHostRuntimeError> {
        if self.shut_down {
            return if matches!(message, ContentHostMessage::Shutdown) {
                Ok(Vec::new())
            } else {
                Err(ContentHostRuntimeError::ShutDown)
            };
        }
        match message {
            ContentHostMessage::Begin {
                request_id,
                tab_id,
                navigation_id,
                generation,
                mode,
                url,
                title,
            } => {
                self.begin(ActiveStream {
                    request_id,
                    tab_id,
                    navigation_id,
                    generation,
                    mode,
                    url,
                    title,
                    next_sequence: 0,
                    fact_bytes: 0,
                    facts: Vec::new(),
                })?;
                Ok(Vec::new())
            }
            ContentHostMessage::FactBatch {
                request_id,
                tab_id,
                navigation_id,
                generation,
                sequence,
                facts,
            } => {
                self.append_facts(
                    &request_id,
                    &tab_id,
                    navigation_id,
                    generation,
                    sequence,
                    facts,
                )?;
                Ok(Vec::new())
            }
            ContentHostMessage::Terminal {
                request_id,
                tab_id,
                navigation_id,
                generation,
                status,
                ..
            } => self.finish(request_id, tab_id, navigation_id, generation, status),
            ContentHostMessage::Cancel { request_id } => self.cancel(&request_id),
            ContentHostMessage::Navigation {
                tab_id,
                navigation_id,
                generation,
            } => self.navigate(tab_id, navigation_id, generation),
            ContentHostMessage::CloseTab { tab_id } => self.close_tab(&tab_id),
            ContentHostMessage::Shutdown => Ok(self.shut_down()),
            ContentHostMessage::MarkdownChunk { .. } | ContentHostMessage::ErrorReply { .. } => {
                Err(ContentHostRuntimeError::InvalidMessage)
            }
        }
    }

    fn begin(&mut self, stream: ActiveStream) -> Result<(), ContentHostRuntimeError> {
        if self
            .streams
            .iter()
            .any(|active| active.request_id == stream.request_id)
        {
            return Err(ContentHostRuntimeError::DuplicateRequest);
        }
        if self
            .streams
            .iter()
            .any(|active| active.tab_id == stream.tab_id)
        {
            return Err(ContentHostRuntimeError::DuplicateTab);
        }
        if self.streams.len() >= MAX_ACTIVE_CONTENT_STREAMS {
            return Err(ContentHostRuntimeError::CapacityExceeded);
        }
        let typed_tab =
            TabId::new(&stream.tab_id).map_err(|_| ContentHostRuntimeError::InvalidMessage)?;
        self.align_navigation(typed_tab, stream.navigation_id, stream.generation)?;
        self.streams.push(stream);
        Ok(())
    }

    fn append_facts(
        &mut self,
        request_id: &str,
        tab_id: &str,
        navigation_id: u64,
        generation: u64,
        sequence: u32,
        facts: Vec<ContentHostFact>,
    ) -> Result<(), ContentHostRuntimeError> {
        let index = self
            .stream_index(request_id)
            .ok_or(ContentHostRuntimeError::NotFound)?;
        let stream = &self.streams[index];
        let expected_sequence = stream.next_sequence;
        let bytes = facts
            .iter()
            .try_fold(0usize, |total, fact| total.checked_add(fact_bytes(fact)));
        let identity_matches = stream.tab_id == tab_id
            && stream.navigation_id == navigation_id
            && stream.generation == generation;
        let sequence_matches = sequence == stream.next_sequence;
        let within_budget = sequence < MAX_CONTENT_STREAM_CHUNKS
            && !facts.is_empty()
            && facts.len() <= MAX_CONTENT_HOST_FACTS
            && stream.facts.len().saturating_add(facts.len()) <= max_facts(stream.mode)
            && bytes
                .and_then(|value| stream.fact_bytes.checked_add(value))
                .is_some_and(|value| value <= max_fact_bytes(stream.mode));
        if !identity_matches || !sequence_matches || !within_budget {
            self.streams.remove(index);
            return Err(if !identity_matches {
                ContentHostRuntimeError::StaleNavigation
            } else if sequence != expected_sequence {
                ContentHostRuntimeError::SequenceMismatch
            } else {
                ContentHostRuntimeError::CapacityExceeded
            });
        }
        let stream = &mut self.streams[index];
        stream.fact_bytes += bytes.expect("validated Some above");
        stream.facts.extend(facts);
        stream.next_sequence += 1;
        Ok(())
    }

    fn finish(
        &mut self,
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
        status: ContentHostTerminalStatus,
    ) -> Result<Vec<ContentHostMessage>, ContentHostRuntimeError> {
        let index = self
            .stream_index(&request_id)
            .ok_or(ContentHostRuntimeError::NotFound)?;
        let stream = self.streams.remove(index);
        if stream.tab_id != tab_id
            || stream.navigation_id != navigation_id
            || stream.generation != generation
        {
            return Err(ContentHostRuntimeError::StaleNavigation);
        }
        if status != ContentHostTerminalStatus::Completed {
            return Ok(vec![ContentHostMessage::ErrorReply {
                request_id,
                code: terminal_reply(status),
            }]);
        }
        self.complete(stream)
    }

    fn complete(
        &mut self,
        stream: ActiveStream,
    ) -> Result<Vec<ContentHostMessage>, ContentHostRuntimeError> {
        let output_level = output_level(stream.mode);
        let source = stream
            .facts
            .into_iter()
            .enumerate()
            .map(|(index, fact)| into_source_fact(index, fact))
            .collect::<Result<Vec<_>, _>>()?;
        let extracted = extract_main_content(output_level, source)
            .map_err(|_| ContentHostRuntimeError::ExtractionFailed)?;
        let tab_index = self
            .tabs
            .iter()
            .position(|tab| tab.tab_id.as_str() == stream.tab_id)
            .ok_or(ContentHostRuntimeError::StaleNavigation)?;
        let revision = self.tabs[tab_index]
            .revision
            .checked_add(1)
            .ok_or(ContentHostRuntimeError::SnapshotFailed)?;
        let snapshot = PageSnapshot::new(
            output_level,
            NavigationBinding::new(
                self.tabs[tab_index].tab_id.clone(),
                SessionGeneration::from_raw(stream.generation),
            ),
            stream.url,
            stream.title,
            revision,
            TruncationInfo::default(),
            extracted.blocks,
        )
        .map_err(|_| ContentHostRuntimeError::SnapshotFailed)?;
        self.snapshots
            .publish(snapshot.clone())
            .map_err(|_| ContentHostRuntimeError::SnapshotFailed)?;
        self.tabs[tab_index].revision = revision;
        let markdown = render_snapshot(&snapshot)
            .map_err(|_| ContentHostRuntimeError::MarkdownFailed)?
            .markdown()
            .to_owned();
        Ok(markdown_chunks(
            stream.request_id,
            stream.tab_id,
            stream.navigation_id,
            stream.generation,
            markdown,
        ))
    }

    fn cancel(
        &mut self,
        request_id: &str,
    ) -> Result<Vec<ContentHostMessage>, ContentHostRuntimeError> {
        let index = self
            .stream_index(request_id)
            .ok_or(ContentHostRuntimeError::NotFound)?;
        let stream = self.streams.remove(index);
        Ok(vec![ContentHostMessage::ErrorReply {
            request_id: stream.request_id,
            code: ContentHostErrorCode::Cancelled,
        }])
    }

    fn navigate(
        &mut self,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
    ) -> Result<Vec<ContentHostMessage>, ContentHostRuntimeError> {
        let typed_tab = TabId::new(&tab_id).map_err(|_| ContentHostRuntimeError::InvalidMessage)?;
        if self.align_navigation(typed_tab, navigation_id, generation)? {
            Ok(self.cancel_tab_streams(&tab_id, ContentHostErrorCode::StaleNavigation))
        } else {
            Ok(Vec::new())
        }
    }

    fn close_tab(
        &mut self,
        tab_id: &str,
    ) -> Result<Vec<ContentHostMessage>, ContentHostRuntimeError> {
        let typed_tab = TabId::new(tab_id).map_err(|_| ContentHostRuntimeError::InvalidMessage)?;
        let replies = self.cancel_tab_streams(tab_id, ContentHostErrorCode::Cancelled);
        self.snapshots
            .close_tab(&typed_tab)
            .map_err(|_| ContentHostRuntimeError::SnapshotFailed)?;
        self.tabs.retain(|tab| tab.tab_id != typed_tab);
        Ok(replies)
    }

    fn shut_down(&mut self) -> Vec<ContentHostMessage> {
        let replies = self
            .streams
            .drain(..)
            .map(|stream| ContentHostMessage::ErrorReply {
                request_id: stream.request_id,
                code: ContentHostErrorCode::HostUnavailable,
            })
            .collect();
        self.tabs.clear();
        self.snapshots.shut_down();
        self.shut_down = true;
        replies
    }

    fn align_navigation(
        &mut self,
        tab_id: TabId,
        navigation_id: u64,
        generation: u64,
    ) -> Result<bool, ContentHostRuntimeError> {
        if let Some(index) = self.tabs.iter().position(|tab| tab.tab_id == tab_id) {
            let tab = &self.tabs[index];
            if generation < tab.generation
                || (generation == tab.generation && navigation_id != tab.navigation_id)
            {
                return Err(ContentHostRuntimeError::StaleNavigation);
            }
            let changed = generation > tab.generation;
            self.snapshots
                .advance_navigation(tab_id, SessionGeneration::from_raw(generation))
                .map_err(|_| ContentHostRuntimeError::StaleNavigation)?;
            if changed {
                let tab = &mut self.tabs[index];
                tab.generation = generation;
                tab.navigation_id = navigation_id;
                tab.revision = 0;
            }
            return Ok(changed);
        }
        if self.tabs.len() >= MAX_TRACKED_TABS {
            return Err(ContentHostRuntimeError::CapacityExceeded);
        }
        self.snapshots
            .advance_navigation(tab_id.clone(), SessionGeneration::from_raw(generation))
            .map_err(|_| ContentHostRuntimeError::CapacityExceeded)?;
        self.tabs.push(TabState {
            tab_id,
            navigation_id,
            generation,
            revision: 0,
        });
        Ok(true)
    }

    fn cancel_tab_streams(
        &mut self,
        tab_id: &str,
        code: ContentHostErrorCode,
    ) -> Vec<ContentHostMessage> {
        let mut replies = Vec::new();
        self.streams.retain(|stream| {
            if stream.tab_id == tab_id {
                replies.push(ContentHostMessage::ErrorReply {
                    request_id: stream.request_id.clone(),
                    code,
                });
                false
            } else {
                true
            }
        });
        replies
    }

    fn stream_index(&self, request_id: &str) -> Option<usize> {
        self.streams
            .iter()
            .position(|stream| stream.request_id == request_id)
    }

    #[must_use]
    pub fn active_streams(&self) -> usize {
        self.streams.len()
    }
}

fn output_level(mode: ContentHostMode) -> OutputLevel {
    match mode {
        ContentHostMode::Standard => OutputLevel::Standard,
        ContentHostMode::Compact => OutputLevel::Compact,
    }
}

fn max_facts(mode: ContentHostMode) -> usize {
    output_level(mode).max_blocks()
}

fn max_fact_bytes(mode: ContentHostMode) -> usize {
    output_level(mode).max_total_text_bytes()
}

fn fact_bytes(fact: &ContentHostFact) -> usize {
    fact.text
        .len()
        .saturating_add(fact.url.as_ref().map_or(0, String::len))
        .saturating_add(fact.language.as_ref().map_or(0, String::len))
        .saturating_add(
            fact.table_cells
                .iter()
                .fold(0usize, |total, cell| total.saturating_add(cell.len())),
        )
}

fn into_source_fact(
    index: usize,
    fact: ContentHostFact,
) -> Result<SourceFact, ContentHostRuntimeError> {
    let source_order =
        u32::try_from(index).map_err(|_| ContentHostRuntimeError::CapacityExceeded)?;
    let node_id = u64::try_from(index)
        .ok()
        .and_then(|value| value.checked_add(1))
        .ok_or(ContentHostRuntimeError::CapacityExceeded)?;
    let content = match fact.kind {
        ContentHostFactKind::Heading => SourceContent::Heading {
            level: fact.level,
            text: fact.text,
        },
        ContentHostFactKind::Paragraph => SourceContent::Paragraph { text: fact.text },
        ContentHostFactKind::ListItem => SourceContent::ListItem {
            depth: fact.depth,
            ordinal: fact.ordinal.map(u64::from),
            text: fact.text,
        },
        ContentHostFactKind::Link => SourceContent::Link {
            href: fact.url.ok_or(ContentHostRuntimeError::InvalidMessage)?,
            text: fact.text,
        },
        ContentHostFactKind::Image => SourceContent::Image {
            src: fact.url.ok_or(ContentHostRuntimeError::InvalidMessage)?,
            alt: fact.text,
        },
        ContentHostFactKind::Table => SourceContent::Table {
            rows: table_rows(fact.table_columns, fact.table_cells)?,
        },
        ContentHostFactKind::CodeBlock => SourceContent::CodeBlock {
            language: fact.language,
            text: fact.text,
        },
        ContentHostFactKind::Divider => SourceContent::Divider,
        ContentHostFactKind::Quote => SourceContent::Quote { text: fact.text },
    };
    Ok(SourceFact {
        node_id,
        region_id: 1,
        region_kind: RegionKind::Unknown,
        reading_key: ReadingKey {
            section: 0,
            column: 0,
            row: source_order,
            source_order,
        },
        visible: true,
        same_origin: true,
        privacy: PrivacyClass::Public,
        content,
    })
}

fn table_rows(columns: u16, cells: Vec<String>) -> Result<Vec<TableRow>, ContentHostRuntimeError> {
    let columns = usize::from(columns);
    if columns == 0 || cells.is_empty() || cells.len() % columns != 0 {
        return Err(ContentHostRuntimeError::InvalidMessage);
    }
    Ok(cells
        .chunks(columns)
        .map(|row| TableRow {
            cells: row.to_vec(),
        })
        .collect())
}

fn markdown_chunks(
    request_id: String,
    tab_id: String,
    navigation_id: u64,
    generation: u64,
    markdown: String,
) -> Vec<ContentHostMessage> {
    if markdown.is_empty() {
        return vec![ContentHostMessage::MarkdownChunk {
            request_id,
            tab_id,
            navigation_id,
            generation,
            sequence: 0,
            completed: true,
            markdown,
        }];
    }
    let mut chunks = Vec::new();
    let mut offset = 0;
    while offset < markdown.len() {
        let mut end = offset
            .saturating_add(MAX_CONTENT_HOST_MARKDOWN_BYTES)
            .min(markdown.len());
        while !markdown.is_char_boundary(end) {
            end -= 1;
        }
        chunks.push(ContentHostMessage::MarkdownChunk {
            request_id: request_id.clone(),
            tab_id: tab_id.clone(),
            navigation_id,
            generation,
            sequence: chunks.len() as u32,
            completed: end == markdown.len(),
            markdown: markdown[offset..end].to_owned(),
        });
        offset = end;
    }
    chunks
}

fn terminal_reply(status: ContentHostTerminalStatus) -> ContentHostErrorCode {
    match status {
        ContentHostTerminalStatus::Completed => ContentHostErrorCode::InvalidState,
        ContentHostTerminalStatus::Cancelled => ContentHostErrorCode::Cancelled,
        ContentHostTerminalStatus::StaleNavigation => ContentHostErrorCode::StaleNavigation,
        ContentHostTerminalStatus::Rejected => ContentHostErrorCode::InvalidMessage,
    }
}
