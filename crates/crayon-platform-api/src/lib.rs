//! Platform adapter interface contracts (PLT-01).
//!
//! This crate defines the interface surfaces that Windows and macOS
//! adapters implement: secure storage, local network observation, power
//! lifecycle, update, current-user local agent IPC and external client
//! handoff.
//!
//! Rules:
//! - Interfaces carry no cookies, authorization data, browsing history or
//!   arbitrary URLs, and no OS-specific types appear here.
//! - All errors are closed enums whose `Display` strings are stable and
//!   carry no paths, URLs or user data.
//! - Linux is out of scope; no Linux-specific surface exists.

pub mod external_client_handoff;
pub mod lifecycle;
pub mod local_agent_ipc;
pub mod local_network;
pub mod secure_store;
pub mod token;
pub mod update;
