//! MHV2 cast-draft dispatch over the unique player/planner/Cast owners.

use crate::media_cast_draft::{
    CastDraft, CastDraftError, DraftScope, MediaCastDraftOwner, MAX_CAST_DRAFTS,
};
use crate::media_host_runtime::{MediaHostRuntime, ReadyMediaHostCastStart};
use crate::media_player_registry::MediaPlayerRegistry;
use crayon_cast_adapter::DeliveryRoute;
use crayon_ipc_schema::media_host_v2::{
    DraftAction, DraftCommand, DraftError, DraftMediaRef, DraftMessage, DraftPhase, DraftReason,
    DraftRoute, DraftStateReply,
};
use crayon_ipc_schema::{MediaHostCastStartOutcome, MediaHostMessage};
use crayon_media_probe::{InspectionStatus, ProbeHttpError};

struct PreparedDraft {
    scope: DraftScope,
    draft_id: u64,
    revision: u64,
    media: DraftMediaRef,
    active_session_at_prepare: bool,
    ready: ReadyMediaHostCastStart,
}

pub struct MediaCastDraftRuntime {
    owner: MediaCastDraftOwner,
    prepared: Vec<PreparedDraft>,
}

impl Default for MediaCastDraftRuntime {
    fn default() -> Self {
        Self::new()
    }
}

impl MediaCastDraftRuntime {
    #[must_use]
    pub fn new() -> Self {
        Self {
            owner: MediaCastDraftOwner::new(),
            prepared: Vec::new(),
        }
    }

    pub async fn dispatch(
        &mut self,
        command: DraftCommand,
        players: &MediaPlayerRegistry,
        host: &mut MediaHostRuntime,
        now_ms: u64,
    ) -> DraftStateReply {
        let context = command.context.clone();
        let scope = DraftScope {
            profile_id: context.profile_id.clone(),
            tab_id: context.tab_id,
            navigation_id: context.navigation_id,
            tab_generation: context.tab_generation,
        };
        let result = match command.action {
            DraftAction::Open => {
                let result = self.owner.open(scope.clone());
                if result.is_ok() {
                    self.discard_tab(&scope.profile_id, scope.tab_id);
                }
                result
            }
            DraftAction::SelectMedia => {
                let media = command.media.expect("codec validated SelectMedia");
                let available = players
                    .fact(
                        scope.tab_id,
                        scope.navigation_id,
                        scope.tab_generation,
                        media,
                    )
                    .is_some();
                let result = self.owner.select_media(
                    &scope,
                    command.draft_id,
                    command.draft_revision,
                    media,
                    available,
                );
                if result.is_ok() {
                    self.discard_prepared(&scope, command.draft_id);
                }
                result
            }
            DraftAction::SelectDevice => {
                let available = host.draft_device_available(&command.device_id);
                let result = self.owner.select_device(
                    &scope,
                    command.draft_id,
                    command.draft_revision,
                    command.device_id.clone(),
                    available,
                );
                if result.is_ok() {
                    self.discard_prepared(&scope, command.draft_id);
                }
                result
            }
            DraftAction::Connect => {
                let effect =
                    self.owner
                        .request_connect(&scope, command.draft_id, command.draft_revision);
                match effect {
                    Ok(effect) => {
                        let connected = host.connect_draft_device(&effect.device_id).await.is_ok();
                        self.owner.complete_connect(&effect, connected)
                    }
                    Err(error) => Err(error),
                }
            }
            DraftAction::Prepare => {
                self.prepare(
                    command.draft_id,
                    command.draft_revision,
                    scope.clone(),
                    players,
                    host,
                    now_ms,
                )
                .await
            }
            DraftAction::ConfirmReplacement => {
                self.owner
                    .confirm_replacement(&scope, command.draft_id, command.draft_revision)
            }
            DraftAction::Commit => {
                self.commit(
                    command.draft_id,
                    command.draft_revision,
                    &scope,
                    players,
                    host,
                    now_ms,
                )
                .await
            }
            DraftAction::Cancel => {
                let result = self
                    .owner
                    .cancel(&scope, command.draft_id, command.draft_revision);
                if result.is_ok() {
                    self.discard_prepared(&scope, command.draft_id);
                }
                result
            }
        };
        match result {
            Ok(draft) => state(context, draft),
            Err(CastDraftError::ConfirmationRequired) => {
                if let Some(draft) = self.owner.snapshot(&scope) {
                    state(context, draft)
                } else {
                    self.error_state(context, &scope, CastDraftError::Stale)
                }
            }
            Err(error) => self.error_state(context, &scope, error),
        }
    }

    pub fn invalidate_player(
        &mut self,
        tab_id: u32,
        navigation_id: u64,
        tab_generation: u32,
        media: DraftMediaRef,
    ) -> usize {
        self.prepared.retain(|entry| {
            entry.scope.tab_id != tab_id
                || entry.scope.navigation_id != navigation_id
                || entry.scope.tab_generation != tab_generation
                || entry.media != media
        });
        self.owner
            .invalidate_player(tab_id, navigation_id, tab_generation, media)
    }

    pub fn invalidate_tab(&mut self, tab_id: u32) -> usize {
        self.prepared.retain(|entry| entry.scope.tab_id != tab_id);
        self.owner.invalidate_tab(tab_id)
    }

    async fn prepare(
        &mut self,
        draft_id: u64,
        revision: u64,
        scope: DraftScope,
        players: &MediaPlayerRegistry,
        host: &mut MediaHostRuntime,
        now_ms: u64,
    ) -> Result<CastDraft, CastDraftError> {
        let effect = self.owner.request_prepare(
            &scope,
            draft_id,
            revision,
            host.has_active_cast_session(),
        )?;
        self.discard_prepared(&scope, draft_id);
        let media = effect.media.ok_or(CastDraftError::Unavailable)?;
        let Some(player) = players.fact(
            scope.tab_id,
            scope.navigation_id,
            scope.tab_generation,
            media,
        ) else {
            return self.owner.complete_prepare(&effect, None, now_ms);
        };
        let ready = match host
            .prepare_player_cast(
                format!("draft-{}-{}", draft_id, effect.revision),
                &player,
                effect.device_id.clone(),
            )
            .await
        {
            Ok(ready) => ready,
            Err(_) => return self.owner.complete_prepare(&effect, None, now_ms),
        };
        let reason = wire_preflight(ready.preflight_status());
        let route = match host.preview_player_cast_route(&ready) {
            Ok(route) => route,
            Err(_) => {
                return self
                    .owner
                    .complete_prepare_with_reason(&effect, None, now_ms, reason)
            }
        };
        if self.prepared.len() >= MAX_CAST_DRAFTS {
            return self
                .owner
                .complete_prepare_with_reason(&effect, None, now_ms, reason);
        }
        let draft = self.owner.complete_prepare_with_reason(
            &effect,
            Some(wire_route(route)),
            now_ms,
            reason,
        )?;
        self.prepared.push(PreparedDraft {
            scope,
            draft_id,
            revision: draft.revision,
            media,
            active_session_at_prepare: host.has_active_cast_session(),
            ready,
        });
        Ok(draft)
    }

    async fn commit(
        &mut self,
        draft_id: u64,
        revision: u64,
        scope: &DraftScope,
        players: &MediaPlayerRegistry,
        host: &MediaHostRuntime,
        now_ms: u64,
    ) -> Result<CastDraft, CastDraftError> {
        let index = self
            .prepared
            .iter()
            .position(|entry| {
                entry.scope == *scope && entry.draft_id == draft_id && entry.revision == revision
            })
            .ok_or(CastDraftError::Stale)?;
        let current = players
            .fact(
                scope.tab_id,
                scope.navigation_id,
                scope.tab_generation,
                self.prepared[index].media,
            )
            .is_some();
        let device_available = self
            .owner
            .snapshot(scope)
            .is_some_and(|draft| host.draft_device_available(&draft.device_id));
        let session_unchanged =
            self.prepared[index].active_session_at_prepare == host.has_active_cast_session();
        if !current || !device_available || !session_unchanged {
            self.prepared.remove(index);
            return self.owner.cancel(scope, draft_id, revision);
        }
        let effect = self
            .owner
            .request_commit(scope, draft_id, revision, now_ms)?;
        let ready = self.prepared.remove(index).ready;
        let session_generation = match host.commit_start_cast(ready).await {
            Ok(MediaHostMessage::StartCastReply {
                outcome:
                    MediaHostCastStartOutcome::Casting {
                        session_generation, ..
                    },
                ..
            }) => Some(session_generation),
            _ => None,
        };
        self.owner.complete_commit(&effect, session_generation)
    }

    fn discard_prepared(&mut self, scope: &DraftScope, draft_id: u64) {
        self.prepared
            .retain(|entry| entry.scope != *scope || entry.draft_id != draft_id);
    }

    fn discard_tab(&mut self, profile_id: &str, tab_id: u32) {
        self.prepared
            .retain(|entry| entry.scope.profile_id != profile_id || entry.scope.tab_id != tab_id);
    }

    fn error_state(
        &self,
        context: crayon_ipc_schema::media_host_v2::DraftContext,
        scope: &DraftScope,
        error: CastDraftError,
    ) -> DraftStateReply {
        let mut draft = self.owner.snapshot(scope).unwrap_or(CastDraft {
            scope: scope.clone(),
            draft_id: u64::MAX,
            revision: 1,
            phase: DraftPhase::Failed,
            error: DraftError::Invalid,
            media: None,
            device_id: String::new(),
            device_connected: false,
            replacement_confirmation_required: false,
            route: DraftRoute::None,
            prepared_until_ms: None,
            reason: DraftReason::None,
            session_generation: None,
        });
        draft.phase = if error == CastDraftError::Expired {
            DraftPhase::Expired
        } else {
            DraftPhase::Failed
        };
        draft.error = wire_error(error);
        draft.route = DraftRoute::None;
        draft.prepared_until_ms = None;
        draft.reason = DraftReason::None;
        state(context, draft)
    }
}

fn state(
    context: crayon_ipc_schema::media_host_v2::DraftContext,
    draft: CastDraft,
) -> DraftStateReply {
    DraftStateReply {
        context,
        draft_id: draft.draft_id,
        draft_revision: draft.revision,
        phase: draft.phase,
        error: draft.error,
        media: draft.media,
        device_id: draft.device_id,
        device_connected: draft.device_connected,
        replacement_confirmation_required: draft.replacement_confirmation_required,
        route: draft.route,
        prepared_until_ms: draft.prepared_until_ms,
        reason: draft.reason,
        session_generation: draft.session_generation,
    }
}

fn wire_route(route: DeliveryRoute) -> DraftRoute {
    match route {
        DeliveryRoute::Direct => DraftRoute::Direct,
        DeliveryRoute::Relay => DraftRoute::Relay,
    }
}

fn wire_error(error: CastDraftError) -> DraftError {
    match error {
        CastDraftError::Invalid => DraftError::Invalid,
        CastDraftError::Stale => DraftError::Stale,
        CastDraftError::Unavailable => DraftError::Unavailable,
        CastDraftError::Capacity | CastDraftError::Exhausted => DraftError::Busy,
        CastDraftError::ConfirmationRequired => DraftError::Denied,
        CastDraftError::Expired => DraftError::Expired,
    }
}

pub(super) fn wire_preflight(
    status: &crate::media_planning_runtime::LocalPreflightStatus,
) -> DraftReason {
    use crate::media_planning_runtime::LocalPreflightStatus;
    match status {
        LocalPreflightStatus::SkippedCredentials => DraftReason::Credentials,
        LocalPreflightStatus::SkippedProtection => DraftReason::Protection,
        LocalPreflightStatus::Inspected(InspectionStatus::Recognized) => DraftReason::Recognized,
        LocalPreflightStatus::Inspected(InspectionStatus::Unrecognized) => {
            DraftReason::Unrecognized
        }
        LocalPreflightStatus::Inspected(InspectionStatus::RedirectRefused) => {
            DraftReason::RedirectRefused
        }
        LocalPreflightStatus::Inspected(InspectionStatus::UpstreamRejected) => {
            DraftReason::UpstreamRejected
        }
        LocalPreflightStatus::Failed(ProbeHttpError::NonPublicAddress) => {
            DraftReason::AddressRejected
        }
        LocalPreflightStatus::Failed(ProbeHttpError::Dns) => DraftReason::Dns,
        LocalPreflightStatus::Failed(ProbeHttpError::Connect) => DraftReason::Connect,
        LocalPreflightStatus::Failed(ProbeHttpError::Timeout) => DraftReason::Timeout,
        LocalPreflightStatus::Failed(ProbeHttpError::Transport) => DraftReason::Transport,
        LocalPreflightStatus::Failed(
            ProbeHttpError::UnsupportedScheme
            | ProbeHttpError::InvalidUrl
            | ProbeHttpError::InvalidRange
            | ProbeHttpError::ScopeMismatch,
        ) => DraftReason::InvalidTarget,
    }
}

#[must_use]
pub fn reply_message(state: DraftStateReply) -> DraftMessage {
    DraftMessage::State(state)
}
