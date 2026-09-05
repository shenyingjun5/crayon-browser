use crayon_ipc_schema::media_host_v2::{
    DraftError, DraftMediaRef, DraftPhase, DraftReason, DraftRoute,
};

pub const MAX_CAST_DRAFTS: usize = 64;
pub const PREPARED_LIFETIME_MS: u64 = 15_000;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DraftScope {
    pub profile_id: String,
    pub tab_id: u32,
    pub navigation_id: u64,
    pub tab_generation: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CastDraft {
    pub scope: DraftScope,
    pub draft_id: u64,
    pub revision: u64,
    pub phase: DraftPhase,
    pub error: DraftError,
    pub media: Option<DraftMediaRef>,
    pub device_id: String,
    pub device_connected: bool,
    pub replacement_confirmation_required: bool,
    pub route: DraftRoute,
    pub prepared_until_ms: Option<u64>,
    pub reason: DraftReason,
    pub session_generation: Option<u64>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DraftEffectKind {
    Connect,
    Prepare,
    Commit,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DraftEffect {
    pub kind: DraftEffectKind,
    pub scope: DraftScope,
    pub draft_id: u64,
    pub revision: u64,
    pub media: Option<DraftMediaRef>,
    pub device_id: String,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CastDraftError {
    Invalid,
    Stale,
    Unavailable,
    Capacity,
    Exhausted,
    ConfirmationRequired,
    Expired,
}

struct Entry {
    draft: CastDraft,
    pending: Option<DraftEffectKind>,
    replacement_confirmed: bool,
}

pub struct MediaCastDraftOwner {
    drafts: Vec<Entry>,
    next_id: u64,
    dropped_capacity_total: u64,
}

impl Default for MediaCastDraftOwner {
    fn default() -> Self {
        Self::new()
    }
}

impl MediaCastDraftOwner {
    #[must_use]
    pub fn new() -> Self {
        Self {
            drafts: Vec::new(),
            next_id: 1,
            dropped_capacity_total: 0,
        }
    }

    pub fn open(&mut self, scope: DraftScope) -> Result<CastDraft, CastDraftError> {
        if !valid_scope(&scope) {
            return Err(CastDraftError::Invalid);
        }
        let replacement = self.same_tab(&scope);
        if replacement.is_none() && self.drafts.len() >= MAX_CAST_DRAFTS {
            self.dropped_capacity_total = self.dropped_capacity_total.saturating_add(1);
            return Err(CastDraftError::Capacity);
        }
        let draft_id = self.allocate_id()?;
        if let Some(index) = replacement {
            self.drafts.remove(index);
        }
        let draft = CastDraft {
            scope,
            draft_id,
            revision: 1,
            phase: DraftPhase::Choosing,
            error: DraftError::None,
            media: None,
            device_id: String::new(),
            device_connected: false,
            replacement_confirmation_required: false,
            route: DraftRoute::None,
            prepared_until_ms: None,
            reason: DraftReason::None,
            session_generation: None,
        };
        self.drafts.push(Entry {
            draft: draft.clone(),
            pending: None,
            replacement_confirmed: false,
        });
        Ok(draft)
    }

    pub fn select_media(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
        media: DraftMediaRef,
        available: bool,
    ) -> Result<CastDraft, CastDraftError> {
        if !available || media.instance_id == 0 || media.source_revision == 0 {
            return Err(CastDraftError::Unavailable);
        }
        let entry = self.current_mut(scope, draft_id, revision)?;
        can_bump(entry)?;
        entry.draft.media = Some(media);
        reset_selection(entry, false)?;
        Ok(entry.draft.clone())
    }

    pub fn select_device(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
        device_id: String,
        available: bool,
    ) -> Result<CastDraft, CastDraftError> {
        if !available || !valid_id(&device_id) {
            return Err(CastDraftError::Unavailable);
        }
        let entry = self.current_mut(scope, draft_id, revision)?;
        can_bump(entry)?;
        let changed = entry.draft.device_id != device_id;
        entry.draft.device_id = device_id;
        reset_selection(entry, changed)?;
        Ok(entry.draft.clone())
    }

    pub fn request_connect(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
    ) -> Result<DraftEffect, CastDraftError> {
        let entry = self.current_mut(scope, draft_id, revision)?;
        if entry.draft.device_id.is_empty() || entry.pending.is_some() {
            return Err(CastDraftError::Unavailable);
        }
        transition(entry, DraftPhase::Connecting)?;
        entry.pending = Some(DraftEffectKind::Connect);
        Ok(effect(entry, DraftEffectKind::Connect, entry.draft.media))
    }

    pub fn complete_connect(
        &mut self,
        effect: &DraftEffect,
        connected: bool,
    ) -> Result<CastDraft, CastDraftError> {
        let entry = self.effect_mut(effect)?;
        can_bump(entry)?;
        entry.pending = None;
        entry.draft.device_connected = connected;
        transition(
            entry,
            if connected {
                DraftPhase::Choosing
            } else {
                DraftPhase::Failed
            },
        )?;
        entry.draft.error = if connected {
            DraftError::None
        } else {
            DraftError::Unavailable
        };
        Ok(entry.draft.clone())
    }

    pub fn request_prepare(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
        replacement_required: bool,
    ) -> Result<DraftEffect, CastDraftError> {
        let entry = self.current_mut(scope, draft_id, revision)?;
        let media = entry.draft.media.ok_or(CastDraftError::Unavailable)?;
        if entry.draft.device_id.is_empty()
            || !entry.draft.device_connected
            || entry.pending.is_some()
        {
            return Err(CastDraftError::Unavailable);
        }
        if replacement_required && !entry.replacement_confirmed {
            can_bump(entry)?;
            entry.draft.replacement_confirmation_required = true;
            transition(entry, DraftPhase::Choosing)?;
            return Err(CastDraftError::ConfirmationRequired);
        }
        if entry.draft.replacement_confirmation_required {
            return Err(CastDraftError::ConfirmationRequired);
        }
        transition(entry, DraftPhase::Preparing)?;
        entry.pending = Some(DraftEffectKind::Prepare);
        entry.replacement_confirmed = false;
        Ok(effect(entry, DraftEffectKind::Prepare, Some(media)))
    }

    pub fn confirm_replacement(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
    ) -> Result<CastDraft, CastDraftError> {
        let entry = self.current_mut(scope, draft_id, revision)?;
        if !entry.draft.replacement_confirmation_required {
            return Err(CastDraftError::Invalid);
        }
        bump(entry)?;
        entry.draft.replacement_confirmation_required = false;
        entry.replacement_confirmed = true;
        Ok(entry.draft.clone())
    }

    pub fn complete_prepare(
        &mut self,
        effect: &DraftEffect,
        route: Option<DraftRoute>,
        now_ms: u64,
    ) -> Result<CastDraft, CastDraftError> {
        self.complete_prepare_with_reason(effect, route, now_ms, DraftReason::None)
    }

    pub fn complete_prepare_with_reason(
        &mut self,
        effect: &DraftEffect,
        route: Option<DraftRoute>,
        now_ms: u64,
        reason: DraftReason,
    ) -> Result<CastDraft, CastDraftError> {
        let deadline = match route.filter(|route| *route != DraftRoute::None) {
            Some(_) => Some(
                now_ms
                    .checked_add(PREPARED_LIFETIME_MS)
                    .ok_or(CastDraftError::Exhausted)?,
            ),
            None => None,
        };
        let entry = self.effect_mut(effect)?;
        can_bump(entry)?;
        entry.pending = None;
        let Some(route) = route.filter(|route| *route != DraftRoute::None) else {
            transition(entry, DraftPhase::Failed)?;
            entry.draft.error = DraftError::Unavailable;
            entry.draft.reason = reason;
            return Ok(entry.draft.clone());
        };
        entry.draft.route = route;
        entry.draft.prepared_until_ms = deadline;
        transition(entry, DraftPhase::Prepared)?;
        entry.draft.reason = reason;
        Ok(entry.draft.clone())
    }

    pub fn request_commit(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
        now_ms: u64,
    ) -> Result<DraftEffect, CastDraftError> {
        let entry = self.current_mut(scope, draft_id, revision)?;
        let deadline = entry.draft.prepared_until_ms.ok_or(CastDraftError::Stale)?;
        if now_ms >= deadline {
            expire(entry)?;
            return Err(CastDraftError::Expired);
        }
        let media = entry.draft.media.ok_or(CastDraftError::Unavailable)?;
        if entry.draft.phase != DraftPhase::Prepared
            || !entry.draft.device_connected
            || entry.draft.device_id.is_empty()
            || entry.draft.route == DraftRoute::None
            || entry.draft.replacement_confirmation_required
            || entry.pending.is_some()
        {
            return Err(CastDraftError::Stale);
        }
        can_bump(entry)?;
        entry.draft.prepared_until_ms = None;
        transition(entry, DraftPhase::Committing)?;
        entry.pending = Some(DraftEffectKind::Commit);
        Ok(effect(entry, DraftEffectKind::Commit, Some(media)))
    }

    pub fn complete_commit(
        &mut self,
        effect: &DraftEffect,
        session_generation: Option<u64>,
    ) -> Result<CastDraft, CastDraftError> {
        let entry = self.effect_mut(effect)?;
        can_bump(entry)?;
        entry.pending = None;
        entry.draft.route = DraftRoute::None;
        transition(
            entry,
            if session_generation.is_some() {
                DraftPhase::Committed
            } else {
                DraftPhase::Failed
            },
        )?;
        entry.draft.error = if session_generation.is_some() {
            DraftError::None
        } else {
            DraftError::Unavailable
        };
        entry.draft.session_generation = session_generation;
        Ok(entry.draft.clone())
    }

    pub fn cancel(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
    ) -> Result<CastDraft, CastDraftError> {
        let entry = self.current_mut(scope, draft_id, revision)?;
        can_bump(entry)?;
        entry.pending = None;
        entry.draft.prepared_until_ms = None;
        entry.draft.route = DraftRoute::None;
        transition(entry, DraftPhase::Cancelled)?;
        Ok(entry.draft.clone())
    }

    pub fn invalidate_scope(&mut self, profile_id: &str, tab_id: u32) -> usize {
        let before = self.drafts.len();
        self.drafts.retain(|entry| {
            entry.draft.scope.profile_id != profile_id || entry.draft.scope.tab_id != tab_id
        });
        before - self.drafts.len()
    }

    pub fn invalidate_media(&mut self, media: DraftMediaRef) -> usize {
        let before = self.drafts.len();
        self.drafts.retain(|entry| entry.draft.media != Some(media));
        before - self.drafts.len()
    }

    pub fn invalidate_player(
        &mut self,
        tab_id: u32,
        navigation_id: u64,
        tab_generation: u32,
        media: DraftMediaRef,
    ) -> usize {
        let before = self.drafts.len();
        self.drafts.retain(|entry| {
            entry.draft.scope.tab_id != tab_id
                || entry.draft.scope.navigation_id != navigation_id
                || entry.draft.scope.tab_generation != tab_generation
                || entry.draft.media != Some(media)
        });
        before - self.drafts.len()
    }

    pub fn invalidate_tab(&mut self, tab_id: u32) -> usize {
        let before = self.drafts.len();
        self.drafts
            .retain(|entry| entry.draft.scope.tab_id != tab_id);
        before - self.drafts.len()
    }

    pub fn invalidate_device(&mut self, device_id: &str) -> usize {
        let before = self.drafts.len();
        self.drafts
            .retain(|entry| entry.draft.device_id != device_id);
        before - self.drafts.len()
    }

    #[must_use]
    pub fn snapshot(&self, scope: &DraftScope) -> Option<CastDraft> {
        self.drafts
            .iter()
            .find(|entry| entry.draft.scope == *scope)
            .map(|entry| entry.draft.clone())
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.drafts.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.drafts.is_empty()
    }

    #[must_use]
    pub fn dropped_capacity_total(&self) -> u64 {
        self.dropped_capacity_total
    }

    fn same_tab(&self, scope: &DraftScope) -> Option<usize> {
        self.drafts.iter().position(|entry| {
            entry.draft.scope.profile_id == scope.profile_id
                && entry.draft.scope.tab_id == scope.tab_id
        })
    }

    fn current_mut(
        &mut self,
        scope: &DraftScope,
        draft_id: u64,
        revision: u64,
    ) -> Result<&mut Entry, CastDraftError> {
        self.drafts
            .iter_mut()
            .find(|entry| entry.draft.scope == *scope && entry.draft.draft_id == draft_id)
            .filter(|entry| entry.draft.revision == revision)
            .ok_or(CastDraftError::Stale)
    }

    fn effect_mut(&mut self, effect: &DraftEffect) -> Result<&mut Entry, CastDraftError> {
        let entry = self.current_mut(&effect.scope, effect.draft_id, effect.revision)?;
        if entry.pending != Some(effect.kind)
            || entry.draft.media != effect.media
            || entry.draft.device_id != effect.device_id
        {
            return Err(CastDraftError::Stale);
        }
        Ok(entry)
    }

    fn allocate_id(&mut self) -> Result<u64, CastDraftError> {
        if self.next_id == 0 || self.next_id == u64::MAX {
            return Err(CastDraftError::Exhausted);
        }
        let id = self.next_id;
        self.next_id += 1;
        Ok(id)
    }
}

fn valid_scope(scope: &DraftScope) -> bool {
    valid_id(&scope.profile_id)
        && scope.tab_id != 0
        && scope.navigation_id != 0
        && scope.tab_generation != 0
}

fn valid_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 128
        && !value.chars().any(|c| {
            c.is_control() || matches!(c, '\u{202a}'..='\u{202e}' | '\u{2066}'..='\u{2069}')
        })
}

fn bump(entry: &mut Entry) -> Result<(), CastDraftError> {
    entry.draft.revision = entry
        .draft
        .revision
        .checked_add(1)
        .ok_or(CastDraftError::Exhausted)?;
    Ok(())
}

fn can_bump(entry: &Entry) -> Result<(), CastDraftError> {
    (entry.draft.revision < u64::MAX)
        .then_some(())
        .ok_or(CastDraftError::Exhausted)
}

fn transition(entry: &mut Entry, phase: DraftPhase) -> Result<(), CastDraftError> {
    bump(entry)?;
    entry.draft.phase = phase;
    entry.draft.error = DraftError::None;
    entry.draft.reason = DraftReason::None;
    entry.draft.session_generation = None;
    Ok(())
}

fn reset_selection(entry: &mut Entry, device_changed: bool) -> Result<(), CastDraftError> {
    bump(entry)?;
    entry.pending = None;
    entry.draft.phase = DraftPhase::Choosing;
    entry.draft.error = DraftError::None;
    entry.draft.reason = DraftReason::None;
    entry.draft.session_generation = None;
    entry.draft.route = DraftRoute::None;
    entry.draft.prepared_until_ms = None;
    entry.draft.replacement_confirmation_required = false;
    entry.replacement_confirmed = false;
    if device_changed {
        entry.draft.device_connected = false;
    }
    Ok(())
}

fn expire(entry: &mut Entry) -> Result<(), CastDraftError> {
    bump(entry)?;
    entry.pending = None;
    entry.draft.phase = DraftPhase::Expired;
    entry.draft.error = DraftError::Expired;
    entry.draft.reason = DraftReason::None;
    entry.draft.session_generation = None;
    entry.draft.route = DraftRoute::None;
    entry.draft.prepared_until_ms = None;
    Ok(())
}

fn effect(entry: &Entry, kind: DraftEffectKind, media: Option<DraftMediaRef>) -> DraftEffect {
    DraftEffect {
        kind,
        scope: entry.draft.scope.clone(),
        draft_id: entry.draft.draft_id,
        revision: entry.draft.revision,
        media,
        device_id: entry.draft.device_id.clone(),
    }
}
