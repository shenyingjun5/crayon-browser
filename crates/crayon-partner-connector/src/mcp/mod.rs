//! Outbound Partner MCP namespace (HUB-13).
//!
//! Partner-provided tool metadata is untrusted data: descriptions cannot
//! widen authority, capabilities and risk stay host-owned, and responses
//! are opaque payload text. Everything fails closed.

use crate::api::ConnectorId;

/// Maximum bytes of a partner tool name.
pub const MAX_TOOL_NAME_BYTES: usize = 64;
/// Maximum bytes of a partner tool description (bounded metadata).
pub const MAX_TOOL_DESCRIPTION_BYTES: usize = 2048;

/// Why a partner tool registration was rejected. Content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ToolFilterError {
    /// Empty, overlong or charset-violating tool name.
    InvalidName,
    /// The description exceeds its bound.
    DescriptionTooLong,
    /// The description contains control characters (injection carrier).
    DescriptionControlChars,
    /// The tool name would collide with the host-reserved namespace.
    ReservedNamespace,
}

/// Host-owned authority assignment for one partner tool. Partner metadata
/// can never change these fields.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ToolAuthority {
    /// Risk level digit (0..=4) assigned by the host.
    pub risk: u8,
    /// Whether calls require explicit user confirmation.
    pub requires_confirmation: bool,
}

/// One accepted partner tool as seen by the product: namespaced name,
/// sanitized bounded description and host-owned authority.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NamespacedTool {
    /// `<connector-namespace>.<connector-name>.<tool>` — collides with
    /// nothing in the inbound registry namespace.
    pub qualified_name: String,
    /// Sanitized description (control characters already rejected at
    /// registration).
    pub description: String,
    pub authority: ToolAuthority,
}

/// Registry of partner tools for one connector, namespaced and filtered.
pub struct OutboundMcpRegistry {
    connector: ConnectorId,
    tools: Vec<NamespacedTool>,
}

impl OutboundMcpRegistry {
    #[must_use]
    pub fn new(connector: ConnectorId) -> Self {
        Self {
            connector,
            tools: Vec::new(),
        }
    }

    /// Registers one partner tool. `description` is untrusted partner
    /// metadata: bounded, control-character free, never interpreted.
    /// `authority` is host-owned and cannot be influenced by the partner.
    pub fn register(
        &mut self,
        tool: &str,
        description: &str,
        authority: ToolAuthority,
    ) -> Result<&NamespacedTool, ToolFilterError> {
        if tool.is_empty() || tool.len() > MAX_TOOL_NAME_BYTES {
            return Err(ToolFilterError::InvalidName);
        }
        if !tool
            .bytes()
            .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'_' || b == b'-')
        {
            return Err(ToolFilterError::InvalidName);
        }
        if description.len() > MAX_TOOL_DESCRIPTION_BYTES {
            return Err(ToolFilterError::DescriptionTooLong);
        }
        if description.bytes().any(|b| b < 0x20 || b == 0x7f) {
            return Err(ToolFilterError::DescriptionControlChars);
        }
        let qualified_name = format!(
            "{}.{}.{}",
            self.connector.namespace(),
            self.connector.name(),
            tool
        );
        if self
            .tools
            .iter()
            .any(|t| t.qualified_name == qualified_name)
        {
            return Err(ToolFilterError::ReservedNamespace);
        }
        let entry = NamespacedTool {
            qualified_name,
            description: description.to_owned(),
            authority,
        };
        self.tools.push(entry);
        Ok(self.tools.last().expect("just pushed"))
    }

    /// All accepted tools.
    #[must_use]
    pub fn tools(&self) -> &[NamespacedTool] {
        &self.tools
    }
}

/// Untrusted MCP response: pure payload text. It can never trigger new
/// capabilities, never enters error surfaces and is budget-clipped.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct McpResponse {
    text: String,
    truncated: bool,
}

impl McpResponse {
    /// Wraps an untrusted response with the bounded budget. The response
    /// never grants authority nor is parsed as instructions.
    #[must_use]
    pub fn from_untrusted(text: &str, max_bytes: usize) -> Self {
        if text.len() <= max_bytes {
            Self {
                text: text.to_owned(),
                truncated: false,
            }
        } else {
            let mut end = max_bytes;
            while !text.is_char_boundary(end) {
                end -= 1;
            }
            Self {
                text: text[..end].to_owned(),
                truncated: true,
            }
        }
    }

    #[must_use]
    pub fn text(&self) -> &str {
        &self.text
    }

    #[must_use]
    pub const fn truncated(&self) -> bool {
        self.truncated
    }
}

#[cfg(test)]
mod mcp_tests;
