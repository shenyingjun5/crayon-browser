//! Candidate aggregation: observations become deduplicated candidates.

pub mod lifecycle;
pub mod ranking;
pub mod store;

pub use lifecycle::LifecyclePolicy;
pub use ranking::{rank, RankingSignals};
pub use store::{
    CandidateEntry, CandidateId, CandidateStore, Evidence, RedactedCandidate, MAX_CANDIDATES,
    MAX_EVIDENCE,
};
