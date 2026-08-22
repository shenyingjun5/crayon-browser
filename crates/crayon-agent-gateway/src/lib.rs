//! Agent access gateway (AGT): protocol orchestration, tool registry,
//! grants and receipts.
//!
//! The gateway only orchestrates; real behavior is invoked through
//! app-runtime use cases.  This crate currently hosts the A0 wave tool
//! registry (AGT-02).

pub mod registry;
pub mod session;
