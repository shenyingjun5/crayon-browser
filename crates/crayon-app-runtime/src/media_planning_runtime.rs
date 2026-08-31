//! Browser-verified media facts to the single MED candidate/probe/policy path.
//!
//! Full page/media URLs stay in this trusted in-memory owner. Public results
//! contain only opaque candidate ids, redacted origins and closed decisions.

use crayon_cast_policy::{decide, HandoffAvailability, PolicyContext};
use crayon_domain::{ReceiverCapabilities, TabId};
use crayon_ipc_schema::{
    AdContinuity, AudioCodecKind, CastPolicyDecision, CastPolicyInput, HeadersClass,
    MediaCandidate, PageContext, PlaybackState, ProtocolKind, VideoCodecKind,
};
use crayon_media_observer::candidate::{
    rank, CandidateId, CandidateStore, LifecyclePolicy, RankingSignals,
};
use crayon_media_observer::{
    FrameContext, NavigationId, ObservationOrigin, ObservationSource, PlaybackObservation,
    PlaybackProgress, SourceObservation, UserActivation,
};
use crayon_media_probe::{
    assess_protection, HlsPlaylist, Inspection, MediaInspector, Protection, ProtectionEvidence,
};

/// Browser-verified playback attached to an eligible currentSrc fact.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct VerifiedPlayback {
    pub state: PlaybackState,
    pub ad_continuity: AdContinuity,
    pub ranking: RankingSignals,
}

/// One URL-bearing Browser fact. Intentionally no `Debug`: URLs may contain
/// short-lived signatures and must not enter diagnostics.
pub struct VerifiedUrlFact {
    pub tab_id: TabId,
    pub navigation_id: u64,
    pub page_url: String,
    pub media_url: String,
    pub source: ObservationSource,
    pub observed_at_ms: u64,
    pub headers_class: HeadersClass,
    pub playback: Option<VerifiedPlayback>,
    pub eme_encrypted: bool,
}

/// Log/UI-safe candidate view.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlanningCandidate {
    pub id: CandidateId,
    pub redacted_origin: String,
}

/// Closed result of one receiver-specific policy evaluation. No upstream URL
/// is exposed; Direct/Relay execution remains owned by later runtime slices.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlanningDecision {
    pub candidate_id: CandidateId,
    pub protocol: ProtocolKind,
    pub decision: CastPolicyDecision,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MediaPlanningError {
    InvalidFact,
    CandidateUnavailable,
}

struct CandidateFacts {
    id: CandidateId,
    page_url: String,
    headers_class: HeadersClass,
    playback: Option<VerifiedPlayback>,
    eme_encrypted: bool,
}

/// Single platform-neutral owner of candidate lifecycle, bounded inspection
/// and cast policy evaluation for the product media path.
pub struct MediaPlanningRuntime {
    candidates: CandidateStore,
    facts: Vec<CandidateFacts>,
    inspector: MediaInspector,
}

impl MediaPlanningRuntime {
    #[must_use]
    pub fn new(inspector: MediaInspector) -> Self {
        Self {
            candidates: CandidateStore::new(),
            facts: Vec::new(),
            inspector,
        }
    }

    /// Ingests a current-navigation URL fact. Only facts carrying a verified
    /// playback proof become user-visible candidates; network facts enrich
    /// the same candidate without granting eligibility.
    pub fn ingest_url(
        &mut self,
        fact: VerifiedUrlFact,
    ) -> Result<Option<PlanningCandidate>, MediaPlanningError> {
        if fact.navigation_id == 0 || !valid_fact_shape(&fact) || self.page_context_conflicts(&fact)
        {
            return Err(MediaPlanningError::InvalidFact);
        }
        let observation = SourceObservation::new(
            fact.tab_id,
            NavigationId::new(fact.navigation_id),
            FrameContext::Main,
            fact.source,
            fact.media_url,
            fact.page_url.clone(),
            fact.observed_at_ms,
        )
        .map_err(|_| MediaPlanningError::InvalidFact)?;
        let id = self
            .candidates
            .ingest(&observation)
            .ok_or(MediaPlanningError::CandidateUnavailable)?;
        self.retain_live_facts();
        let record = if let Some(record) = self.facts.iter_mut().find(|item| item.id == id) {
            record
        } else {
            self.facts.push(CandidateFacts {
                id,
                page_url: fact.page_url,
                headers_class: HeadersClass::None,
                playback: None,
                eme_encrypted: false,
            });
            let index = self.facts.len() - 1;
            &mut self.facts[index]
        };
        record.headers_class = merge_headers(record.headers_class, fact.headers_class);
        record.eme_encrypted |= fact.eme_encrypted;
        if let Some(playback) = fact.playback {
            record.playback = Some(playback);
        }
        Ok(record
            .playback
            .and_then(|_| self.candidates.redacted(id))
            .map(|candidate| PlanningCandidate {
                id: candidate.id,
                redacted_origin: candidate.origin,
            }))
    }

    /// Conservatively upgrades every candidate on the current navigation
    /// when CEF reports an EME encrypted event without a URL-bearing request.
    pub fn mark_eme_encrypted(&mut self, tab_id: &TabId, navigation_id: u64) {
        for record in &mut self.facts {
            let Some(entry) = self.candidates.get(record.id) else {
                continue;
            };
            if entry.tab_id() == tab_id && entry.navigation().get() == navigation_id {
                record.eme_encrypted = true;
            }
        }
    }

    pub fn on_navigation(&mut self, tab_id: &TabId, navigation_id: u64) -> usize {
        let dropped = self
            .candidates
            .on_navigation(tab_id, NavigationId::new(navigation_id));
        self.retain_live_facts();
        dropped
    }

    pub fn on_tab_close(&mut self, tab_id: &TabId) -> usize {
        let dropped = self.candidates.on_tab_close(tab_id);
        self.retain_live_facts();
        dropped
    }

    pub fn expire_stale(&mut self, now_ms: u64, policy: LifecyclePolicy) -> usize {
        let dropped = self.candidates.expire_stale(now_ms, policy);
        self.retain_live_facts();
        dropped
    }

    #[must_use]
    pub fn candidates(&self) -> Vec<PlanningCandidate> {
        let signals: Vec<_> = self
            .facts
            .iter()
            .filter_map(|record| {
                record
                    .playback
                    .map(|playback| (record.id, playback.ranking))
            })
            .collect();
        rank(&signals)
            .into_iter()
            .filter_map(|id| self.candidates.redacted(id))
            .map(|candidate| PlanningCandidate {
                id: candidate.id,
                redacted_origin: candidate.origin,
            })
            .collect()
    }

    #[must_use]
    pub fn retained_count(&self) -> usize {
        self.facts.len()
    }

    /// Runs one bounded, credential-free inspection and the unique policy
    /// decision for explicitly supplied receiver capabilities. Ordinary
    /// probe failures become `Protection::Unknown`; they never retry or
    /// upgrade to Direct/Relay.
    pub async fn decide_for_receiver(
        &self,
        candidate_id: CandidateId,
        receiver: ReceiverCapabilities,
        handoff: HandoffAvailability,
    ) -> Result<PlanningDecision, MediaPlanningError> {
        let entry = self
            .candidates
            .get(candidate_id)
            .ok_or(MediaPlanningError::CandidateUnavailable)?;
        let facts = self
            .facts
            .iter()
            .find(|record| record.id == candidate_id)
            .ok_or(MediaPlanningError::CandidateUnavailable)?;
        let playback = facts
            .playback
            .ok_or(MediaPlanningError::CandidateUnavailable)?;

        // Credential-bound and EME candidates need no network request to
        // reach their fail-closed policy branch.
        let inspection =
            if facts.headers_class == HeadersClass::CredentialBound || facts.eme_encrypted {
                None
            } else {
                Some(
                    self.inspector
                        .inspect(entry.url())
                        .await
                        .unwrap_or(Inspection::Unknown),
                )
            };
        let protocol = protocol_for(inspection.as_ref(), entry.url());
        let (video_codec, audio_codec) = codecs_for(inspection.as_ref());
        let mut evidence = Vec::with_capacity(2);
        if let Some(inspection) = inspection {
            evidence.push(ProtectionEvidence::Inspection(inspection));
        }
        if facts.eme_encrypted {
            evidence.push(ProtectionEvidence::EmeEncryptedSignal);
        }
        let assessment = assess_protection(&evidence, None);
        let candidate = MediaCandidate::new(
            entry.url().to_owned(),
            protocol,
            assessment.protection == Protection::DrmProtected,
            facts.headers_class,
            video_codec,
            audio_codec,
            playback.ad_continuity,
        );
        let input = CastPolicyInput::new(
            PageContext::new(entry.tab_id().clone(), facts.page_url.clone()),
            playback.state,
            candidate,
            receiver,
        );
        let decision = decide(
            &input,
            &PolicyContext {
                observation: verified_playback_observation(),
                protection: assessment.protection,
                external_client_handoff: handoff,
            },
        );
        Ok(PlanningDecision {
            candidate_id,
            protocol,
            decision,
        })
    }

    /// URL-less blob/MediaStream evidence cannot enter `CandidateStore` and
    /// must never be assigned a fabricated URL. It resolves immediately to
    /// DRM reject or explicit NoDirectUrl handoff/reject.
    pub fn decide_url_less(
        tab_id: TabId,
        page_url: String,
        playback: PlaybackState,
        eme_encrypted: bool,
        handoff: HandoffAvailability,
    ) -> Result<CastPolicyDecision, MediaPlanningError> {
        if !valid_page_url(&page_url) || !valid_playback(playback) {
            return Err(MediaPlanningError::InvalidFact);
        }
        let input = CastPolicyInput::new(
            PageContext::new(tab_id, page_url),
            playback,
            MediaCandidate::new(
                String::new(),
                ProtocolKind::Mp4,
                eme_encrypted,
                HeadersClass::None,
                None,
                None,
                AdContinuity::Unknown,
            ),
            ReceiverCapabilities::new(false, false, false, false, false, false, 0),
        );
        Ok(decide(
            &input,
            &PolicyContext {
                observation: verified_playback_observation(),
                protection: if eme_encrypted {
                    Protection::DrmProtected
                } else {
                    Protection::NoDirectUrl
                },
                external_client_handoff: handoff,
            },
        ))
    }

    fn retain_live_facts(&mut self) {
        self.facts
            .retain(|record| self.candidates.get(record.id).is_some());
    }

    fn page_context_conflicts(&self, fact: &VerifiedUrlFact) -> bool {
        self.facts.iter().any(|record| {
            self.candidates.get(record.id).is_some_and(|entry| {
                entry.tab_id() == &fact.tab_id
                    && entry.navigation().get() == fact.navigation_id
                    && record.page_url != fact.page_url
            })
        })
    }
}

fn verified_playback_observation() -> PlaybackObservation {
    PlaybackObservation::new(
        ObservationOrigin::BrowserVerified,
        UserActivation::BrowserVerified,
        PlaybackProgress::Advanced,
    )
}

fn valid_fact_shape(fact: &VerifiedUrlFact) -> bool {
    if fact.playback.is_some() && fact.source != ObservationSource::CurrentSrc {
        return false;
    }
    if fact.headers_class != HeadersClass::None && fact.source != ObservationSource::NetworkRequest
    {
        return false;
    }
    if fact.eme_encrypted && fact.source != ObservationSource::CurrentSrc {
        return false;
    }
    fact.playback
        .is_none_or(|playback| valid_playback(playback.state))
}

fn valid_playback(playback: PlaybackState) -> bool {
    playback.position_seconds().is_finite()
        && playback.position_seconds() >= 0.0
        && playback
            .duration_seconds()
            .is_none_or(|duration| duration.is_finite() && duration >= 0.0)
}

fn valid_page_url(value: &str) -> bool {
    value.len() <= 2_048
        && url::Url::parse(value).is_ok_and(|parsed| matches!(parsed.scheme(), "http" | "https"))
}

fn merge_headers(current: HeadersClass, incoming: HeadersClass) -> HeadersClass {
    use HeadersClass::{CredentialBound, None, RefererAndUa, RefererOnly};
    match (current, incoming) {
        (CredentialBound, _) | (_, CredentialBound) => CredentialBound,
        (RefererAndUa, _) | (_, RefererAndUa) => RefererAndUa,
        (RefererOnly, _) | (_, RefererOnly) => RefererOnly,
        (None, None) => None,
    }
}

fn protocol_for(inspection: Option<&Inspection>, url: &str) -> ProtocolKind {
    match inspection {
        Some(Inspection::Hls(_)) => ProtocolKind::Hls,
        Some(Inspection::Dash(_)) => ProtocolKind::Dash,
        Some(Inspection::Mp4(_) | Inspection::Unknown) | None => {
            if url
                .split('?')
                .next()
                .is_some_and(|path| path.ends_with(".m3u8"))
            {
                ProtocolKind::Hls
            } else if url
                .split('?')
                .next()
                .is_some_and(|path| path.ends_with(".mpd"))
            {
                ProtocolKind::Dash
            } else {
                ProtocolKind::Mp4
            }
        }
    }
}

fn codecs_for(inspection: Option<&Inspection>) -> (Option<VideoCodecKind>, Option<AudioCodecKind>) {
    let Some(Inspection::Hls(HlsPlaylist::Master { variants, .. })) = inspection else {
        return (None, None);
    };
    let mut video = None;
    let mut audio = None;
    for token in variants.iter().flat_map(|variant| &variant.codecs) {
        let base = token.to_ascii_lowercase();
        let base = base.split('.').next().unwrap_or("");
        video = video.or(match base {
            "avc1" | "avc2" | "avc3" | "avc4" => Some(VideoCodecKind::H264),
            "hvc1" | "hev1" => Some(VideoCodecKind::Hevc),
            "av01" => Some(VideoCodecKind::Av1),
            "vp09" | "vp9" => Some(VideoCodecKind::Vp9),
            "vp08" | "vp8" => Some(VideoCodecKind::Vp8),
            _ => None,
        });
        audio = audio.or(match base {
            "mp4a" => Some(AudioCodecKind::Aac),
            "opus" => Some(AudioCodecKind::Opus),
            "ec-3" | "ec3" => Some(AudioCodecKind::Eac3),
            _ => None,
        });
    }
    (video, audio)
}
