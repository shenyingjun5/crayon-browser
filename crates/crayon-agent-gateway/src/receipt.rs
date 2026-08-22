//! Bounded, redacted action receipts (AGT-11, AG-011/PV-010).
//!
//! One receipt records that an agent action was executed, with closed
//! tokens and enums only: client session, tool, capability, risk, target
//! descriptor, grant id, outcome, optional error code and a timestamp.
//! There is deliberately no free-text parameter snapshot — page bodies,
//! full queries, cookies, authorizations and tokens can never enter a
//! receipt.  Receipts are in-process v1 state: bounded capacity, TTL,
//! caller-injected clock, no IO, no persistence.

use crate::grant::GrantId;
use crate::registry::is_token;
use crayon_domain::{
    AgentCapability, DataClass, DiagnosticError, DiagnosticEvent, RiskLevel,
    DIAGNOSTICS_SCHEMA_VERSION,
};
use std::collections::VecDeque;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum length of a client session token, in bytes.
const MAX_SESSION_TOKEN_LEN: usize = 64;

/// Maximum length of a tool name token, in bytes.
const MAX_TOOL_NAME_LEN: usize = 64;

/// Maximum length of a target descriptor token, in bytes.
const MAX_TARGET_LEN: usize = 32;

/// Maximum number of retained receipts (bounded store, oldest evicted).
pub const MAX_RECEIPTS: usize = 256;

/// Receipt retention window, in milliseconds (24h).
pub const RECEIPT_TTL_MS: u64 = 24 * 60 * 60 * 1000;

/// Closed action outcome recorded on a receipt.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ReceiptOutcome {
    Succeeded,
    Failed,
    Cancelled,
    Denied,
}

/// Receipt failure.  Variants are stable and carry no user data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ReceiptError {
    /// A caller-supplied token violates the closed charset or bound.
    InvalidToken,
    /// The optional error code violates the closed charset or bound.
    InvalidErrorCode,
}

impl Display for ReceiptError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidToken => "receipt token rejected",
            Self::InvalidErrorCode => "receipt error code rejected",
        };
        formatter.write_str(message)
    }
}

impl Error for ReceiptError {}

/// One executed agent action, redacted by construction.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActionReceipt {
    client: String,
    tool: String,
    capability: AgentCapability,
    risk: RiskLevel,
    target: String,
    grant: GrantId,
    outcome: ReceiptOutcome,
    error_code: Option<String>,
    timestamp_ms: u64,
    expires_at_ms: u64,
}

impl ActionReceipt {
    /// Creates a receipt.  `target` is a closed token (`tab-<id>` or
    /// `active`); no URL, title or page text may appear anywhere.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        client: &str,
        tool: &str,
        capability: AgentCapability,
        risk: RiskLevel,
        target: &str,
        grant: GrantId,
        outcome: ReceiptOutcome,
        error_code: Option<&str>,
        timestamp_ms: u64,
    ) -> Result<Self, ReceiptError> {
        if !is_token(client, MAX_SESSION_TOKEN_LEN) || !is_token(tool, MAX_TOOL_NAME_LEN) {
            return Err(ReceiptError::InvalidToken);
        }
        if !is_token(target, MAX_TARGET_LEN) {
            return Err(ReceiptError::InvalidToken);
        }
        if let Some(code) = error_code {
            if !is_token(code, 32) {
                return Err(ReceiptError::InvalidErrorCode);
            }
        }
        Ok(Self {
            client: client.to_owned(),
            tool: tool.to_owned(),
            capability,
            risk,
            target: target.to_owned(),
            grant,
            outcome,
            error_code: error_code.map(str::to_owned),
            timestamp_ms,
            expires_at_ms: timestamp_ms.saturating_add(RECEIPT_TTL_MS),
        })
    }

    #[must_use]
    pub fn client(&self) -> &str {
        &self.client
    }

    #[must_use]
    pub fn tool(&self) -> &str {
        &self.tool
    }

    #[must_use]
    pub const fn capability(&self) -> AgentCapability {
        self.capability
    }

    #[must_use]
    pub const fn risk(&self) -> RiskLevel {
        self.risk
    }

    #[must_use]
    pub fn target(&self) -> &str {
        &self.target
    }

    #[must_use]
    pub const fn outcome(&self) -> ReceiptOutcome {
        self.outcome
    }

    #[must_use]
    pub const fn timestamp_ms(&self) -> u64 {
        self.timestamp_ms
    }

    /// Maps the receipt onto a diagnostics event (PRV-08 `Diagnostic`
    /// class; every attribute is a closed token, never content).
    pub fn to_diagnostic_event(&self) -> Result<DiagnosticEvent, DiagnosticError> {
        let mut event = DiagnosticEvent::new(
            DataClass::Diagnostic,
            "agent_action_receipt",
            self.timestamp_ms,
        )?;
        event = event
            .with_attribute("tool", &self.tool)?
            .with_attribute("risk", self.risk.wire_name())?
            .with_attribute("outcome", self.outcome.wire_name())?
            .with_attribute("target", &self.target)?;
        if let Some(code) = &self.error_code {
            event = event.with_attribute("error_code", code)?;
        }
        Ok(event)
    }
}

impl ReceiptOutcome {
    /// Stable wire name for diagnostics.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Succeeded => "succeeded",
            Self::Failed => "failed",
            Self::Cancelled => "cancelled",
            Self::Denied => "denied",
        }
    }
}

/// Bounded receipt store with user preview and clear.
pub struct ReceiptStore {
    receipts: VecDeque<ActionReceipt>,
    dropped_expired_total: u64,
    dropped_evicted_total: u64,
}

impl ReceiptStore {
    #[must_use]
    pub fn new() -> Self {
        Self {
            receipts: VecDeque::new(),
            dropped_expired_total: 0,
            dropped_evicted_total: 0,
        }
    }

    /// Records a receipt, evicting the oldest when full.
    pub fn record(&mut self, receipt: ActionReceipt) {
        if self.receipts.len() >= MAX_RECEIPTS {
            self.receipts.pop_front();
            self.dropped_evicted_total = self.dropped_evicted_total.saturating_add(1);
        }
        self.receipts.push_back(receipt);
    }

    /// User-visible preview snapshot (PV-010: byte-for-byte identical to
    /// the retained records, oldest first, expired entries excluded).
    pub fn preview(&self, now_ms: u64) -> Vec<ActionReceipt> {
        self.receipts
            .iter()
            .filter(|receipt| now_ms < receipt.expires_at_ms)
            .cloned()
            .collect()
    }

    /// Clears every receipt.
    pub fn clear_all(&mut self) -> usize {
        let cleared = self.receipts.len();
        self.receipts.clear();
        cleared
    }

    /// Clears the receipts of one client session.
    pub fn clear_client(&mut self, client: &str) -> usize {
        let before = self.receipts.len();
        self.receipts.retain(|receipt| receipt.client != client);
        before - self.receipts.len()
    }

    /// Drops expired receipts; returns how many were removed.
    pub fn sweep_expired(&mut self, now_ms: u64) -> usize {
        let before = self.receipts.len();
        self.receipts
            .retain(|receipt| now_ms < receipt.expires_at_ms);
        let dropped = before - self.receipts.len();
        self.dropped_expired_total = self.dropped_expired_total.saturating_add(dropped as u64);
        dropped
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.receipts.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.receipts.is_empty()
    }

    #[must_use]
    pub fn stats(&self) -> (usize, u64, u64) {
        (
            self.receipts.len(),
            self.dropped_expired_total,
            self.dropped_evicted_total,
        )
    }
}

impl Default for ReceiptStore {
    fn default() -> Self {
        Self::new()
    }
}

/// Schema version carried by receipt-derived diagnostics events.
pub const RECEIPT_DIAGNOSTICS_SCHEMA_VERSION: u32 = DIAGNOSTICS_SCHEMA_VERSION;

#[cfg(test)]
#[path = "receipt_tests.rs"]
mod tests;
