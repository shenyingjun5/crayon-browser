//! Bounded MHV1 request owner around the single media planning runtime.

use crate::media_planning_runtime::{
    MediaPlanningError, MediaPlanningRuntime, VerifiedPlayback, VerifiedUrlFact,
};
use crayon_cast_policy::HandoffAvailability;
use crayon_domain::TabId;
use crayon_ipc_schema::{
    MediaHostErrorCode, MediaHostMessage, MediaHostPlayback, MediaHostSource, MediaHostUrlFact,
    PlaybackState,
};
use crayon_media_observer::candidate::{CandidateId, LifecyclePolicy, RankingSignals};
use crayon_media_observer::ObservationSource;
use crayon_media_probe::MediaInspector;
use std::collections::VecDeque;

pub const MAX_MEDIA_HOST_TABS: usize = 64;
pub const MAX_MEDIA_HOST_RECENT_REQUESTS: usize = 256;
pub const MAX_MEDIA_HOST_PENDING_MESSAGES: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MediaHostRuntimeError {
    InvalidMessage,
    InvalidState,
    StaleContext,
    CapacityExceeded,
    Cancelled,
    CandidateUnavailable,
    HostUnavailable,
}

impl MediaHostRuntimeError {
    #[must_use]
    pub const fn code(self) -> MediaHostErrorCode {
        match self {
            Self::InvalidMessage => MediaHostErrorCode::InvalidMessage,
            Self::InvalidState => MediaHostErrorCode::InvalidState,
            Self::StaleContext => MediaHostErrorCode::StaleContext,
            Self::CapacityExceeded => MediaHostErrorCode::CapacityExceeded,
            Self::Cancelled => MediaHostErrorCode::Cancelled,
            Self::CandidateUnavailable => MediaHostErrorCode::CandidateUnavailable,
            Self::HostUnavailable => MediaHostErrorCode::HostUnavailable,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MediaHostInterruptAction {
    Continue,
    Cancel,
    Shutdown,
}

/// Bounded queue for messages arriving while a probe decision is active.
pub struct MediaHostPendingQueue {
    messages: VecDeque<MediaHostMessage>,
}

impl Default for MediaHostPendingQueue {
    fn default() -> Self {
        Self {
            messages: VecDeque::with_capacity(MAX_MEDIA_HOST_PENDING_MESSAGES),
        }
    }
}

impl MediaHostPendingQueue {
    pub fn accept_during_decision(
        &mut self,
        active_request: &str,
        message: MediaHostMessage,
    ) -> Result<(MediaHostInterruptAction, Option<MediaHostMessage>), MediaHostRuntimeError> {
        match message {
            MediaHostMessage::Cancel { request_id } if request_id == active_request => {
                Ok((MediaHostInterruptAction::Cancel, None))
            }
            MediaHostMessage::Shutdown => Ok((MediaHostInterruptAction::Shutdown, None)),
            message @ (MediaHostMessage::Navigation { .. } | MediaHostMessage::CloseTab { .. }) => {
                let reply = if self.messages.len() >= MAX_MEDIA_HOST_PENDING_MESSAGES {
                    self.messages.pop_back().and_then(|evicted| {
                        message_request_id(&evicted).map(|request_id| {
                            error_reply(
                                request_id.to_owned(),
                                MediaHostRuntimeError::CapacityExceeded,
                            )
                        })
                    })
                } else {
                    None
                };
                self.messages.push_front(message);
                Ok((MediaHostInterruptAction::Cancel, reply))
            }
            MediaHostMessage::Cancel { request_id } => Ok((
                MediaHostInterruptAction::Continue,
                Some(error_reply(
                    request_id,
                    MediaHostRuntimeError::CandidateUnavailable,
                )),
            )),
            message => {
                if self.messages.len() >= MAX_MEDIA_HOST_PENDING_MESSAGES {
                    let request_id = message_request_id(&message)
                        .ok_or(MediaHostRuntimeError::InvalidState)?
                        .to_owned();
                    Ok((
                        MediaHostInterruptAction::Continue,
                        Some(error_reply(
                            request_id,
                            MediaHostRuntimeError::CapacityExceeded,
                        )),
                    ))
                } else {
                    self.messages.push_back(message);
                    Ok((MediaHostInterruptAction::Continue, None))
                }
            }
        }
    }

    pub fn pop_front(&mut self) -> Option<MediaHostMessage> {
        self.messages.pop_front()
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.messages.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.messages.is_empty()
    }
}

struct TabContext {
    tab_id: TabId,
    navigation_id: u64,
    generation: u64,
    closed: bool,
}

struct WireCandidate {
    wire_id: u64,
    candidate_id: CandidateId,
    tab_id: TabId,
    navigation_id: u64,
    generation: u64,
}

enum PreparedDecisionKind {
    Candidate {
        candidate_id: CandidateId,
        wire_id: u64,
        receiver: crayon_domain::ReceiverCapabilities,
        handoff: HandoffAvailability,
    },
    UrlLess {
        tab_id: TabId,
        page_url: String,
        playback: PlaybackState,
        eme_encrypted: bool,
        handoff: HandoffAvailability,
    },
}

/// Validated decision request. It deliberately has no `Debug` because the
/// URL-less variant carries a private page URL.
pub struct PreparedMediaHostDecision {
    request_id: String,
    kind: PreparedDecisionKind,
}

impl PreparedMediaHostDecision {
    #[must_use]
    pub fn request_id(&self) -> &str {
        &self.request_id
    }
}

pub struct MediaHostRuntime {
    planner: MediaPlanningRuntime,
    tabs: Vec<TabContext>,
    candidates: Vec<WireCandidate>,
    recent_requests: Vec<String>,
    shutdown: bool,
}

impl MediaHostRuntime {
    #[must_use]
    pub fn new(inspector: MediaInspector) -> Self {
        Self {
            planner: MediaPlanningRuntime::new(inspector),
            tabs: Vec::new(),
            candidates: Vec::new(),
            recent_requests: Vec::new(),
            shutdown: false,
        }
    }

    #[must_use]
    pub const fn is_shutdown(&self) -> bool {
        self.shutdown
    }

    pub fn handle_immediate(
        &mut self,
        message: MediaHostMessage,
    ) -> Result<Option<MediaHostMessage>, MediaHostRuntimeError> {
        if self.shutdown {
            return Err(MediaHostRuntimeError::HostUnavailable);
        }
        match message {
            MediaHostMessage::IngestUrl(fact) => self.ingest(fact).map(Some),
            MediaHostMessage::MarkEme {
                request_id,
                tab_id,
                navigation_id,
                generation,
            } => {
                self.claim(&request_id)?;
                let tab_id =
                    TabId::new(&tab_id).map_err(|_| MediaHostRuntimeError::InvalidMessage)?;
                self.require_context(&tab_id, navigation_id, generation)?;
                self.planner.mark_eme_encrypted(&tab_id, navigation_id);
                Ok(Some(MediaHostMessage::Ack { request_id }))
            }
            MediaHostMessage::Navigation {
                request_id,
                tab_id,
                navigation_id,
                generation,
            } => self
                .navigate(request_id, tab_id, navigation_id, generation)
                .map(Some),
            MediaHostMessage::CloseTab {
                request_id,
                tab_id,
                generation,
            } => self.close_tab(request_id, tab_id, generation).map(Some),
            MediaHostMessage::Shutdown => {
                self.shutdown = true;
                for context in &self.tabs {
                    self.planner.on_tab_close(&context.tab_id);
                }
                self.tabs.clear();
                self.candidates.clear();
                self.recent_requests.clear();
                Ok(None)
            }
            MediaHostMessage::Cancel { .. } => Err(MediaHostRuntimeError::Cancelled),
            MediaHostMessage::Decide { .. } | MediaHostMessage::DecideUrlLess { .. } => {
                Err(MediaHostRuntimeError::InvalidState)
            }
            MediaHostMessage::CandidateReply { .. }
            | MediaHostMessage::DecisionReply { .. }
            | MediaHostMessage::Ack { .. }
            | MediaHostMessage::ErrorReply { .. } => Err(MediaHostRuntimeError::InvalidMessage),
        }
    }

    pub fn prepare_decision(
        &mut self,
        message: MediaHostMessage,
    ) -> Result<PreparedMediaHostDecision, MediaHostRuntimeError> {
        if self.shutdown {
            return Err(MediaHostRuntimeError::HostUnavailable);
        }
        match message {
            MediaHostMessage::Decide {
                request_id,
                candidate_id,
                now_ms,
                receiver,
                handoff_available,
            } => {
                self.claim(&request_id)?;
                self.planner
                    .expire_stale(now_ms, LifecyclePolicy::default());
                self.retain_live_candidates();
                let candidate = self
                    .candidates
                    .iter()
                    .find(|candidate| candidate.wire_id == candidate_id)
                    .ok_or(MediaHostRuntimeError::CandidateUnavailable)?;
                self.require_context(
                    &candidate.tab_id,
                    candidate.navigation_id,
                    candidate.generation,
                )?;
                Ok(PreparedMediaHostDecision {
                    request_id,
                    kind: PreparedDecisionKind::Candidate {
                        candidate_id: candidate.candidate_id,
                        wire_id: candidate.wire_id,
                        receiver,
                        handoff: handoff(handoff_available),
                    },
                })
            }
            MediaHostMessage::DecideUrlLess {
                request_id,
                tab_id,
                navigation_id,
                generation,
                page_url,
                playback,
                eme_encrypted,
                handoff_available,
            } => {
                self.claim(&request_id)?;
                let tab_id =
                    TabId::new(&tab_id).map_err(|_| MediaHostRuntimeError::InvalidMessage)?;
                self.require_context(&tab_id, navigation_id, generation)?;
                Ok(PreparedMediaHostDecision {
                    request_id,
                    kind: PreparedDecisionKind::UrlLess {
                        tab_id,
                        page_url,
                        playback: playback_state(playback),
                        eme_encrypted,
                        handoff: handoff(handoff_available),
                    },
                })
            }
            _ => Err(MediaHostRuntimeError::InvalidState),
        }
    }

    pub async fn execute_decision(
        &self,
        prepared: PreparedMediaHostDecision,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        match prepared.kind {
            PreparedDecisionKind::Candidate {
                candidate_id,
                wire_id,
                receiver,
                handoff,
            } => {
                let decision = self
                    .planner
                    .decide_for_receiver(candidate_id, receiver, handoff)
                    .await
                    .map_err(map_planning_error)?;
                Ok(MediaHostMessage::DecisionReply {
                    request_id: prepared.request_id,
                    candidate_id: Some(wire_id),
                    protocol: Some(decision.protocol),
                    decision: decision.decision,
                })
            }
            PreparedDecisionKind::UrlLess {
                tab_id,
                page_url,
                playback,
                eme_encrypted,
                handoff,
            } => Ok(MediaHostMessage::DecisionReply {
                request_id: prepared.request_id,
                candidate_id: None,
                protocol: None,
                decision: MediaPlanningRuntime::decide_url_less(
                    tab_id,
                    page_url,
                    playback,
                    eme_encrypted,
                    handoff,
                )
                .map_err(map_planning_error)?,
            }),
        }
    }

    fn ingest(
        &mut self,
        fact: MediaHostUrlFact,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        self.claim(&fact.request_id)?;
        let tab_id = TabId::new(&fact.tab_id).map_err(|_| MediaHostRuntimeError::InvalidMessage)?;
        self.admit_or_establish(&tab_id, fact.navigation_id, fact.generation)?;
        self.planner
            .expire_stale(fact.observed_at_ms, LifecyclePolicy::default());
        self.retain_live_candidates();
        let request_id = fact.request_id.clone();
        let planning = self
            .planner
            .ingest_url(VerifiedUrlFact {
                tab_id: tab_id.clone(),
                navigation_id: fact.navigation_id,
                page_url: fact.page_url,
                media_url: fact.media_url,
                source: match fact.source {
                    MediaHostSource::CurrentSrc => ObservationSource::CurrentSrc,
                    MediaHostSource::NetworkRequest => ObservationSource::NetworkRequest,
                },
                observed_at_ms: fact.observed_at_ms,
                headers_class: fact.headers_class,
                playback: fact.playback.map(verified_playback),
                eme_encrypted: fact.eme_encrypted,
            })
            .map_err(map_planning_error)?;
        self.retain_live_candidates();
        if let Some(candidate) = planning {
            if !self
                .candidates
                .iter()
                .any(|mapped| mapped.candidate_id == candidate.id)
            {
                let wire_id = candidate
                    .id
                    .get()
                    .checked_add(1)
                    .ok_or(MediaHostRuntimeError::CapacityExceeded)?;
                self.candidates.push(WireCandidate {
                    wire_id,
                    candidate_id: candidate.id,
                    tab_id,
                    navigation_id: fact.navigation_id,
                    generation: fact.generation,
                });
            }
            let mapped = self
                .candidates
                .iter()
                .find(|mapped| mapped.candidate_id == candidate.id)
                .ok_or(MediaHostRuntimeError::CandidateUnavailable)?;
            Ok(MediaHostMessage::CandidateReply {
                request_id,
                candidate_id: Some(mapped.wire_id),
                redacted_origin: candidate.redacted_origin,
            })
        } else {
            Ok(MediaHostMessage::CandidateReply {
                request_id,
                candidate_id: None,
                redacted_origin: String::new(),
            })
        }
    }

    fn navigate(
        &mut self,
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        self.claim(&request_id)?;
        if navigation_id == 0 || generation == 0 {
            return Err(MediaHostRuntimeError::InvalidMessage);
        }
        let tab_id = TabId::new(&tab_id).map_err(|_| MediaHostRuntimeError::InvalidMessage)?;
        if let Some(context) = self
            .tabs
            .iter_mut()
            .find(|context| context.tab_id == tab_id)
        {
            if (context.closed && generation <= context.generation)
                || generation < context.generation
                || (generation == context.generation && navigation_id != context.navigation_id)
                || navigation_id < context.navigation_id
            {
                return Err(MediaHostRuntimeError::StaleContext);
            }
            context.navigation_id = navigation_id;
            context.generation = generation;
            context.closed = false;
        } else {
            if self.tabs.len() >= MAX_MEDIA_HOST_TABS {
                return Err(MediaHostRuntimeError::CapacityExceeded);
            }
            self.tabs.push(TabContext {
                tab_id: tab_id.clone(),
                navigation_id,
                generation,
                closed: false,
            });
        }
        self.planner.on_navigation(&tab_id, navigation_id);
        self.retain_live_candidates();
        Ok(MediaHostMessage::Ack { request_id })
    }

    fn close_tab(
        &mut self,
        request_id: String,
        tab_id: String,
        generation: u64,
    ) -> Result<MediaHostMessage, MediaHostRuntimeError> {
        self.claim(&request_id)?;
        if generation == 0 {
            return Err(MediaHostRuntimeError::InvalidMessage);
        }
        let tab_id = TabId::new(&tab_id).map_err(|_| MediaHostRuntimeError::InvalidMessage)?;
        let context = self
            .tabs
            .iter_mut()
            .find(|context| {
                context.tab_id == tab_id && context.generation == generation && !context.closed
            })
            .ok_or(MediaHostRuntimeError::StaleContext)?;
        context.closed = true;
        self.planner.on_tab_close(&tab_id);
        self.retain_live_candidates();
        Ok(MediaHostMessage::Ack { request_id })
    }

    fn admit_or_establish(
        &mut self,
        tab_id: &TabId,
        navigation_id: u64,
        generation: u64,
    ) -> Result<(), MediaHostRuntimeError> {
        if navigation_id == 0 || generation == 0 {
            return Err(MediaHostRuntimeError::InvalidMessage);
        }
        if self.tabs.iter().any(|context| &context.tab_id == tab_id) {
            return self.require_context(tab_id, navigation_id, generation);
        }
        if self.tabs.len() >= MAX_MEDIA_HOST_TABS {
            return Err(MediaHostRuntimeError::CapacityExceeded);
        }
        self.tabs.push(TabContext {
            tab_id: tab_id.clone(),
            navigation_id,
            generation,
            closed: false,
        });
        Ok(())
    }

    fn require_context(
        &self,
        tab_id: &TabId,
        navigation_id: u64,
        generation: u64,
    ) -> Result<(), MediaHostRuntimeError> {
        if navigation_id == 0 || generation == 0 {
            return Err(MediaHostRuntimeError::InvalidMessage);
        }
        self.tabs
            .iter()
            .any(|context| {
                &context.tab_id == tab_id
                    && context.navigation_id == navigation_id
                    && context.generation == generation
                    && !context.closed
            })
            .then_some(())
            .ok_or(MediaHostRuntimeError::StaleContext)
    }

    fn claim(&mut self, request_id: &str) -> Result<(), MediaHostRuntimeError> {
        if self.recent_requests.iter().any(|known| known == request_id) {
            return Err(MediaHostRuntimeError::InvalidState);
        }
        if self.recent_requests.len() >= MAX_MEDIA_HOST_RECENT_REQUESTS {
            self.recent_requests.remove(0);
        }
        self.recent_requests.push(request_id.to_owned());
        Ok(())
    }

    fn retain_live_candidates(&mut self) {
        let live = self.planner.candidates();
        self.candidates.retain(|mapped| {
            live.iter()
                .any(|candidate| candidate.id == mapped.candidate_id)
        });
    }
}

fn verified_playback(playback: MediaHostPlayback) -> VerifiedPlayback {
    VerifiedPlayback {
        state: playback_state(playback),
        ad_continuity: playback.ad_continuity,
        ranking: RankingSignals::new(
            playback.current_src,
            playback.near_play_event,
            playback.audible,
            playback.main_frame,
            playback.visible_area_px,
        ),
    }
}

fn playback_state(playback: MediaHostPlayback) -> PlaybackState {
    PlaybackState::new(
        playback.position_ms as f64 / 1_000.0,
        playback.duration_ms.map(|value| value as f64 / 1_000.0),
        playback.is_live,
    )
}

fn handoff(available: bool) -> HandoffAvailability {
    if available {
        HandoffAvailability::Available
    } else {
        HandoffAvailability::Unavailable
    }
}

fn map_planning_error(error: MediaPlanningError) -> MediaHostRuntimeError {
    match error {
        MediaPlanningError::InvalidFact => MediaHostRuntimeError::InvalidMessage,
        MediaPlanningError::CandidateUnavailable => MediaHostRuntimeError::CandidateUnavailable,
    }
}

#[must_use]
pub fn error_reply(request_id: String, error: MediaHostRuntimeError) -> MediaHostMessage {
    MediaHostMessage::ErrorReply {
        request_id,
        code: error.code(),
    }
}

#[must_use]
pub fn message_request_id(message: &MediaHostMessage) -> Option<&str> {
    match message {
        MediaHostMessage::IngestUrl(fact) => Some(&fact.request_id),
        MediaHostMessage::MarkEme { request_id, .. }
        | MediaHostMessage::Decide { request_id, .. }
        | MediaHostMessage::DecideUrlLess { request_id, .. }
        | MediaHostMessage::Cancel { request_id }
        | MediaHostMessage::Navigation { request_id, .. }
        | MediaHostMessage::CloseTab { request_id, .. }
        | MediaHostMessage::CandidateReply { request_id, .. }
        | MediaHostMessage::DecisionReply { request_id, .. }
        | MediaHostMessage::Ack { request_id }
        | MediaHostMessage::ErrorReply { request_id, .. } => Some(request_id),
        MediaHostMessage::Shutdown => None,
    }
}
