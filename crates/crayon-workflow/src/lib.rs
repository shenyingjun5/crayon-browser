//! Bounded workflow learning services.
//!
//! The crate consumes trusted, normalized Browser/runtime facts and emits
//! the frozen workflow domain types. It contains no browser-engine, network,
//! challenge-solving or arbitrary page-operation surface.

pub mod challenge;
pub mod checkpoint;

#[cfg(test)]
mod challenge_tests;
#[cfg(test)]
mod checkpoint_tests;
