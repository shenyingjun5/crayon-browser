//! Candidate aggregation: observations become deduplicated candidates.

pub mod store;

pub use store::{
    CandidateEntry, CandidateId, CandidateStore, Evidence, RedactedCandidate, MAX_CANDIDATES,
    MAX_EVIDENCE,
};
