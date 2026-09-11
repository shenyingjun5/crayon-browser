//! Bounded workflow learning services.
//!
//! The crate consumes trusted, normalized Browser/runtime facts and emits
//! the frozen workflow domain types. It contains no browser-engine, network,
//! challenge-solving or arbitrary page-operation surface.

pub mod challenge;
pub mod checkpoint;
pub mod drift;
pub mod handoff;
pub mod health;
pub mod preview;
pub mod recipe;
pub mod redaction;
pub mod resume;
pub mod runner;
pub mod store;
pub mod trace;
pub mod validation;
pub mod version;

#[cfg(test)]
mod challenge_tests;
#[cfg(test)]
mod checkpoint_tests;
#[cfg(test)]
mod handoff_tests;
#[cfg(test)]
mod redaction_tests;
#[cfg(test)]
mod trace_tests;
