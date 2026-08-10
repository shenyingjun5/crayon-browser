//! Candidate confidence ranking (MED-03, design §8.3).
//!
//! Pure function over browser-verified signals. The ranker never filters
//! candidates (ad-break segments, init fragments and tracking requests stay
//! as timeline evidence — filtering decisions belong to policy), it only
//! orders them. Deterministic: equal scores fall back to `CandidateId`
//! order, so the same input always yields the same output.

use super::store::CandidateId;

/// Candidate is the playing element's resolved `currentSrc`.
const W_CURRENT_SRC: u32 = 100;
/// Request observed adjacent to the media `play` event time window.
const W_NEAR_PLAY_EVENT: u32 = 80;
/// Candidate has audio activity (also covers user-chosen background audio).
const W_AUDIBLE: u32 = 40;
/// Visible-area contribution ceiling (scaled against the largest area).
const W_VISIBLE_MAX: u32 = 40;
/// Candidate belongs to the top-level frame.
const W_MAIN_FRAME: u32 = 20;

/// Browser-verified per-candidate signals. Supplied by the browser layer;
/// the ranker does not read the DOM, clocks, or the network itself.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct RankingSignals {
    pub current_src: bool,
    pub near_play_event: bool,
    pub audible: bool,
    pub main_frame: bool,
    /// Visible area in square pixels; 0 = not visible.
    pub visible_area_px: u32,
}

impl RankingSignals {
    #[must_use]
    pub const fn new(
        current_src: bool,
        near_play_event: bool,
        audible: bool,
        main_frame: bool,
        visible_area_px: u32,
    ) -> Self {
        Self {
            current_src,
            near_play_event,
            audible,
            main_frame,
            visible_area_px,
        }
    }
}

/// Scores one candidate. `max_visible_area_px` is the largest visible area in
/// the ranked set (0 when nothing is visible).
#[must_use]
fn score(signals: &RankingSignals, max_visible_area_px: u32) -> u32 {
    let mut total = 0;
    if signals.current_src {
        total += W_CURRENT_SRC;
    }
    if signals.near_play_event {
        total += W_NEAR_PLAY_EVENT;
    }
    if signals.audible {
        total += W_AUDIBLE;
    }
    if signals.main_frame {
        total += W_MAIN_FRAME;
    }
    if let Some(share) = (u64::from(signals.visible_area_px) * u64::from(W_VISIBLE_MAX))
        .checked_div(u64::from(max_visible_area_px))
    {
        total += share as u32;
    }
    total
}

/// Ranks candidates by confidence, highest first. Ties resolve by ascending
/// `CandidateId` (stable, input-order independent).
#[must_use]
pub fn rank(candidates: &[(CandidateId, RankingSignals)]) -> Vec<CandidateId> {
    let max_area = candidates
        .iter()
        .map(|(_, s)| s.visible_area_px)
        .max()
        .unwrap_or(0);
    let mut scored: Vec<(CandidateId, u32)> = candidates
        .iter()
        .map(|(id, signals)| (*id, score(signals, max_area)))
        .collect();
    scored.sort_by(|(a_id, a_score), (b_id, b_score)| {
        b_score
            .cmp(a_score)
            .then_with(|| a_id.get().cmp(&b_id.get()))
    });
    scored.into_iter().map(|(id, _)| id).collect()
}
