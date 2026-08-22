//! Local network observation: interface enumeration and change events.
//!
//! Observations carry capability flags only — never IP or MAC addresses —
//! so routing and policy decisions do not depend on address parsing.

use crate::token::validate_token;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum number of reported network interfaces.
pub const MAX_INTERFACES: usize = 64;

/// Maximum length of an interface name, in bytes.
const MAX_INTERFACE_NAME_LEN: usize = 64;

/// Local-network failure.  Variants are stable and carry no addresses or
/// user data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LocalNetworkError {
    /// The platform cannot observe the local network right now.
    Unavailable,
    /// The OS denied local-network observation (privacy permission).
    AccessDenied,
    /// More interfaces than the bounded report can hold.
    TooManyInterfaces,
    /// The interface name violates shape or bounds.
    InvalidInterfaceName,
}

impl Display for LocalNetworkError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::Unavailable => "local network observation unavailable",
            Self::AccessDenied => "local network permission denied",
            Self::TooManyInterfaces => "interface count exceeds limit",
            Self::InvalidInterfaceName => "network interface name rejected",
        };
        formatter.write_str(message)
    }
}

impl Error for LocalNetworkError {}

/// Validated network interface name (closed token charset, bounded).
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct InterfaceName(String);

impl InterfaceName {
    /// Creates a validated interface name.
    pub fn new(value: &str) -> Result<Self, LocalNetworkError> {
        validate_token(value, MAX_INTERFACE_NAME_LEN)
            .map_err(|_| LocalNetworkError::InvalidInterfaceName)?;
        Ok(Self(value.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Display for InterfaceName {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Enforces the bounded interface report size.
pub fn validate_interface_count(count: usize) -> Result<(), LocalNetworkError> {
    if count > MAX_INTERFACES {
        return Err(LocalNetworkError::TooManyInterfaces);
    }
    Ok(())
}

/// One observed network interface.  Capability flags only; no addresses.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NetworkInterface {
    pub name: InterfaceName,
    pub is_loopback: bool,
    pub is_up: bool,
}

/// Closed network change events.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum NetworkChangeEvent {
    /// An interface came up.
    InterfaceUp(InterfaceName),
    /// An interface went down.
    InterfaceDown(InterfaceName),
    /// The default route changed (network switch, VPN toggle).
    DefaultRouteChanged,
}

/// Local network observation facility.
pub trait LocalNetworkMonitor: Send {
    /// Enumerates interfaces, bounded to `MAX_INTERFACES`.
    fn interfaces(&self) -> Result<Vec<NetworkInterface>, LocalNetworkError>;

    /// Registers or replaces the change listener; `None` unregisters.
    /// Delivery stops when the adapter shuts down.
    fn set_listener(
        &mut self,
        listener: Option<Box<dyn FnMut(NetworkChangeEvent) + Send>>,
    ) -> Result<(), LocalNetworkError>;
}

#[cfg(test)]
#[path = "local_network_tests.rs"]
mod tests;
