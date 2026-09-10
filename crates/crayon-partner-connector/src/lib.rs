//! Outbound Partner connector interface (HUB-09).
//!
//! This crate is the *only* boundary between the product and outbound
//! partner MCP/API connectors. It is deliberately dependency-isolated from
//! the inbound agent path (`crayon-agent-gateway`, `crayon-ipc-schema`):
//! no shared session, token, network client or audit types. Implementations
//! arrive in HUB-10 (trust), HUB-11 (token vault), HUB-12 (network),
//! HUB-13 (outbound MCP namespace) and HUB-14 (runtime); this layer defines
//! the closed types and port traits only — no IO.

pub mod api;
pub mod oauth;
pub mod trust;

#[cfg(test)]
mod api_tests;
