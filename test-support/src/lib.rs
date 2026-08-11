//! Deterministic test doubles for Crayon product tests (testing-standard §4).
//!
//! Dev/test targets only: production dependency graphs must never include this
//! crate (RG-001/RG-006). Rules enforced here:
//!
//! - no fixed-length `sleep` to wait for async results — wakers, gates and
//!   explicit deadlines only;
//! - no public network — servers bind loopback on system-assigned ports;
//! - no real secrets — fixtures use documented example values only.

pub mod browser_fixture;
pub mod cast_facade;
pub mod clock;
pub mod leak_scanner;
pub mod platform;
pub mod receiver;
mod server;
pub mod upstream;
