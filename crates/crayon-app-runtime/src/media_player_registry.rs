use crayon_ipc_schema::media_host_v2::{
    PlayerContext, PlayerFact, PlayerListRequest, PlayerMessage, PlayerPageReply, PlayerPageStatus,
    PlayerProjection as WirePlayerProjection, PlayerSourceKind,
};
use url::Url;

pub const MAX_PLAYERS_PER_PAGE: usize = 16;
pub const MAX_PLAYERS: usize = 256;
const MAX_TABS: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlayerRegistryError {
    InvalidContext,
    StaleContext,
    CapacityExceeded,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlayerProjection {
    pub context: PlayerContext,
    pub source_kind: PlayerSourceKind,
    pub has_video: bool,
    pub has_audio: bool,
    pub visible: bool,
    pub eme_encrypted: bool,
    pub redacted_origin: String,
}

#[derive(Clone, Copy)]
struct TabContext {
    tab_id: u32,
    navigation_id: u64,
    generation: u32,
}

pub struct MediaPlayerRegistry {
    session_id: u64,
    host_generation: u64,
    tabs: Vec<TabContext>,
    players: Vec<PlayerFact>,
    revision: u64,
    dropped_capacity_total: u64,
}

impl MediaPlayerRegistry {
    pub fn new(session_id: u64, host_generation: u64) -> Result<Self, PlayerRegistryError> {
        if session_id == 0 || host_generation == 0 {
            return Err(PlayerRegistryError::InvalidContext);
        }
        Ok(Self {
            session_id,
            host_generation,
            tabs: Vec::new(),
            players: Vec::new(),
            revision: 1,
            dropped_capacity_total: 0,
        })
    }

    pub fn apply(&mut self, message: PlayerMessage) -> Result<(), PlayerRegistryError> {
        if self.revision == u64::MAX {
            self.note_capacity_drop();
            return Err(PlayerRegistryError::CapacityExceeded);
        }
        let result = match message {
            PlayerMessage::Upsert(fact) => self.upsert(fact),
            PlayerMessage::Remove(context) => self.remove(context),
        };
        if result.is_ok() {
            self.revision += 1;
        }
        result
    }

    #[must_use]
    pub fn snapshot(&self) -> Vec<PlayerProjection> {
        self.players
            .iter()
            .map(|fact| PlayerProjection {
                context: fact.context,
                source_kind: fact.source_kind,
                has_video: fact.has_video,
                has_audio: fact.has_audio,
                visible: fact.visible,
                eme_encrypted: fact.eme_encrypted,
                redacted_origin: redacted_origin(&fact.media_url),
            })
            .collect()
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.players.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.players.is_empty()
    }

    #[must_use]
    pub fn dropped_capacity_total(&self) -> u64 {
        self.dropped_capacity_total
    }

    pub fn page(&self, request: PlayerListRequest) -> Result<PlayerPageReply, PlayerRegistryError> {
        let context = request.context;
        if context.request_id == 0
            || context.tab_id == 0
            || context.navigation_id == 0
            || context.tab_generation == 0
            || request.max_items == 0
            || request.max_items > crayon_ipc_schema::media_host_v2::MAX_PAGE_ITEMS
        {
            return Err(PlayerRegistryError::InvalidContext);
        }
        if context.session_id != self.session_id || context.host_generation != self.host_generation
        {
            return Err(PlayerRegistryError::StaleContext);
        }
        let Some(tab) = self.tabs.iter().find(|tab| tab.tab_id == context.tab_id) else {
            return Err(PlayerRegistryError::StaleContext);
        };
        if tab.navigation_id != context.navigation_id || tab.generation != context.tab_generation {
            return Err(PlayerRegistryError::StaleContext);
        }
        if request.snapshot_revision != 0 && request.snapshot_revision != self.revision {
            return Ok(PlayerPageReply {
                context,
                snapshot_revision: self.revision,
                status: PlayerPageStatus::Stale,
                offset: request.offset,
                next_offset: None,
                players: Vec::new(),
            });
        }
        let mut matching: Vec<_> = self
            .players
            .iter()
            .filter(|fact| {
                fact.context.tab_id == context.tab_id
                    && fact.context.navigation_id == context.navigation_id
                    && fact.context.tab_generation == context.tab_generation
            })
            .collect();
        matching.sort_by_key(|fact| (fact.context.instance_id, fact.context.source_revision));
        let begin = usize::from(request.offset).min(matching.len());
        let end = begin
            .saturating_add(usize::from(request.max_items))
            .min(matching.len());
        let players = matching[begin..end]
            .iter()
            .map(|fact| WirePlayerProjection {
                instance_id: fact.context.instance_id,
                source_revision: fact.context.source_revision,
                source_kind: fact.source_kind,
                has_video: fact.has_video,
                has_audio: fact.has_audio,
                visible: fact.visible,
                eme_encrypted: fact.eme_encrypted,
                redacted_origin: redacted_origin(&fact.media_url),
            })
            .collect();
        let next_offset = (end < matching.len()).then_some(end as u16);
        Ok(PlayerPageReply {
            context,
            snapshot_revision: self.revision,
            status: PlayerPageStatus::Ok,
            offset: request.offset,
            next_offset,
            players,
        })
    }

    #[must_use]
    pub fn fact(
        &self,
        tab_id: u32,
        navigation_id: u64,
        tab_generation: u32,
        media: crayon_ipc_schema::media_host_v2::DraftMediaRef,
    ) -> Option<PlayerFact> {
        self.players
            .iter()
            .find(|fact| {
                fact.context.tab_id == tab_id
                    && fact.context.navigation_id == navigation_id
                    && fact.context.tab_generation == tab_generation
                    && fact.context.instance_id == media.instance_id
                    && fact.context.source_revision == media.source_revision
            })
            .cloned()
    }

    fn valid_session(&self, context: PlayerContext) -> bool {
        context.session_id == self.session_id && context.host_generation == self.host_generation
    }

    fn advance_tab(&mut self, context: PlayerContext) -> Result<(), PlayerRegistryError> {
        if let Some(tab) = self
            .tabs
            .iter_mut()
            .find(|tab| tab.tab_id == context.tab_id)
        {
            if context.tab_generation < tab.generation
                || (context.tab_generation == tab.generation
                    && context.navigation_id != tab.navigation_id)
            {
                return Err(PlayerRegistryError::StaleContext);
            }
            if context.tab_generation > tab.generation {
                self.players
                    .retain(|fact| fact.context.tab_id != context.tab_id);
                tab.navigation_id = context.navigation_id;
                tab.generation = context.tab_generation;
            }
            return Ok(());
        }
        if self.tabs.len() >= MAX_TABS {
            self.note_capacity_drop();
            return Err(PlayerRegistryError::CapacityExceeded);
        }
        self.tabs.push(TabContext {
            tab_id: context.tab_id,
            navigation_id: context.navigation_id,
            generation: context.tab_generation,
        });
        Ok(())
    }

    fn upsert(&mut self, fact: PlayerFact) -> Result<(), PlayerRegistryError> {
        if !self.valid_session(fact.context) {
            return Err(PlayerRegistryError::StaleContext);
        }
        self.advance_tab(fact.context)?;
        if let Some(existing) = self.players.iter_mut().find(|existing| {
            existing.context.tab_id == fact.context.tab_id
                && existing.context.instance_id == fact.context.instance_id
        }) {
            if fact.context.source_revision < existing.context.source_revision {
                return Err(PlayerRegistryError::StaleContext);
            }
            if fact.context.source_revision == existing.context.source_revision
                && (fact.observed_at_ms < existing.observed_at_ms
                    || fact.source_kind != existing.source_kind
                    || fact.media_url != existing.media_url
                    || fact.page_url != existing.page_url)
            {
                return Err(PlayerRegistryError::StaleContext);
            }
            *existing = fact;
            return Ok(());
        }
        let page_count = self
            .players
            .iter()
            .filter(|existing| {
                existing.context.tab_id == fact.context.tab_id
                    && existing.context.navigation_id == fact.context.navigation_id
                    && existing.context.tab_generation == fact.context.tab_generation
            })
            .count();
        if self.players.len() >= MAX_PLAYERS || page_count >= MAX_PLAYERS_PER_PAGE {
            self.note_capacity_drop();
            return Err(PlayerRegistryError::CapacityExceeded);
        }
        self.players.push(fact);
        Ok(())
    }

    fn remove(&mut self, context: PlayerContext) -> Result<(), PlayerRegistryError> {
        if !self.valid_session(context) {
            return Err(PlayerRegistryError::StaleContext);
        }
        let Some(tab) = self.tabs.iter().find(|tab| tab.tab_id == context.tab_id) else {
            return Err(PlayerRegistryError::StaleContext);
        };
        if tab.navigation_id != context.navigation_id || tab.generation != context.tab_generation {
            return Err(PlayerRegistryError::StaleContext);
        }
        let Some(index) = self.players.iter().position(|existing| {
            existing.context.tab_id == context.tab_id
                && existing.context.navigation_id == context.navigation_id
                && existing.context.tab_generation == context.tab_generation
                && existing.context.instance_id == context.instance_id
        }) else {
            return Err(PlayerRegistryError::StaleContext);
        };
        if self.players[index].context.source_revision != context.source_revision {
            return Err(PlayerRegistryError::StaleContext);
        }
        self.players.remove(index);
        Ok(())
    }

    fn note_capacity_drop(&mut self) {
        self.dropped_capacity_total = self.dropped_capacity_total.saturating_add(1);
    }
}

fn redacted_origin(value: &str) -> String {
    if value.is_empty() {
        return String::new();
    }
    Url::parse(value)
        .ok()
        .map(|url| url.origin().ascii_serialization())
        .unwrap_or_default()
}
