//! Partner connector interface types (HUB-09).
//!
//! Closed, validated descriptors and boundary port traits. Every type is
//! constructed through validating constructors and fails closed; the
//! partner namespace (`partner_connector::*`) shares no symbols with the
//! inbound agent registry.

/// Maximum bytes of a connector id or display name.
pub const MAX_CONNECTOR_NAME_BYTES: usize = 64;
/// Maximum bytes of one scope entry.
pub const MAX_SCOPE_BYTES: usize = 64;
/// Maximum scopes per connector descriptor.
pub const MAX_SCOPES: usize = 16;
/// Maximum bytes of one outbound call payload (budget, not advisory).
pub const MAX_CALL_PAYLOAD_BYTES: usize = 64 * 1024;
/// Maximum bytes of an audit detail token.
pub const MAX_AUDIT_TOKEN_BYTES: usize = 64;

/// Failure of descriptor/scope/call validation. Variants carry no input.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ConnectorError {
    /// Empty, overlong or charset-violating identifier.
    InvalidName,
    /// Version is empty, overlong or outside `[0-9A-Za-z.+-]`.
    InvalidVersion,
    /// A scope is empty, overlong, contains whitespace or a wildcard.
    InvalidScope,
    /// Too many scopes for one descriptor.
    ScopeBudgetExceeded,
    /// The call payload exceeds the bounded budget.
    PayloadTooLarge,
    /// The call payload is not valid UTF-8 (always rejected).
    PayloadNotUtf8,
}

/// Identifier for one partner connector: partner-owned namespace prefix
/// and connector name, `[a-z0-9_-]`, separated by exactly one `.`.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConnectorId {
    namespace: String,
    name: String,
}

impl ConnectorId {
    /// Validates and wraps `<namespace>.<name>` (each `[a-z0-9_-]{1,63}`).
    pub fn new(raw: &str) -> Result<Self, ConnectorError> {
        let (namespace, name) = raw.split_once('.').ok_or(ConnectorError::InvalidName)?;
        if !valid_token(namespace) || !valid_token(name) {
            return Err(ConnectorError::InvalidName);
        }
        Ok(Self {
            namespace: namespace.to_owned(),
            name: name.to_owned(),
        })
    }

    #[must_use]
    pub fn namespace(&self) -> &str {
        &self.namespace
    }

    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }
}

fn valid_token(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_CONNECTOR_NAME_BYTES
        && value
            .bytes()
            .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'_' || b == b'-')
}

/// Connector descriptor: identity, version and the minimal scope set the
/// partner requires. Scopes are exact-match strings — wildcards (`*`) are
/// rejected so a descriptor can never widen its own authority.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConnectorDescriptor {
    id: ConnectorId,
    version: String,
    scopes: Vec<String>,
}

impl ConnectorDescriptor {
    pub fn new(id: ConnectorId, version: &str, scopes: &[&str]) -> Result<Self, ConnectorError> {
        if version.is_empty() || version.len() > MAX_SCOPE_BYTES {
            return Err(ConnectorError::InvalidVersion);
        }
        if !version
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'.' || b == b'+' || b == b'-')
        {
            return Err(ConnectorError::InvalidVersion);
        }
        if scopes.len() > MAX_SCOPES {
            return Err(ConnectorError::ScopeBudgetExceeded);
        }
        let mut normalized = Vec::with_capacity(scopes.len());
        for scope in scopes {
            if scope.is_empty()
                || scope.len() > MAX_SCOPE_BYTES
                || scope.bytes().any(|b| b.is_ascii_whitespace())
                || scope.contains('*')
            {
                return Err(ConnectorError::InvalidScope);
            }
            normalized.push((*scope).to_owned());
        }
        normalized.sort();
        normalized.dedup();
        Ok(Self {
            id,
            version: version.to_owned(),
            scopes: normalized,
        })
    }

    #[must_use]
    pub fn id(&self) -> &ConnectorId {
        &self.id
    }

    #[must_use]
    pub fn version(&self) -> &str {
        &self.version
    }

    /// Sorted, deduplicated scope set.
    #[must_use]
    pub fn scopes(&self) -> &[String] {
        &self.scopes
    }
}

/// Outbound connector session identity. Intentionally a distinct type from
/// any inbound session string: no constructor accepts an inbound session
/// token, and no accessor exposes the inner value, so the two namespaces
/// cannot be conflated at the type level.
#[derive(Clone, Debug, Eq, PartialEq, Hash)]
pub struct ConnectorSessionId(u64);

impl ConnectorSessionId {
    /// Mints the next session id from the host's monotonic counter.
    pub fn mint(counter: &mut u64) -> Self {
        *counter += 1;
        Self(*counter)
    }

    #[must_use]
    pub fn value(self) -> u64 {
        self.0
    }
}

/// One outbound call: endpoint reference plus bounded payload. The
/// endpoint is an opaque partner-owned reference — DNS, allowlist and SSRF
/// policy belong to the `NetworkPort` implementation (HUB-12).
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConnectorCall<'a> {
    endpoint_ref: &'a str,
    payload: &'a str,
}

impl<'a> ConnectorCall<'a> {
    pub fn new(endpoint_ref: &'a str, payload: &'a str) -> Result<Self, ConnectorError> {
        if endpoint_ref.is_empty() || endpoint_ref.len() > MAX_SCOPE_BYTES {
            return Err(ConnectorError::InvalidName);
        }
        if !payload.is_ascii() {
            return Err(ConnectorError::PayloadNotUtf8);
        }
        if payload.len() > MAX_CALL_PAYLOAD_BYTES {
            return Err(ConnectorError::PayloadTooLarge);
        }
        Ok(Self {
            endpoint_ref,
            payload,
        })
    }

    #[must_use]
    pub fn endpoint_ref(&self) -> &str {
        self.endpoint_ref
    }

    #[must_use]
    pub fn payload(&self) -> &str {
        self.payload
    }
}

/// Trust boundary (HUB-10 implements): source signature, compatibility,
/// revocation and kill-switch checks.
pub trait TrustPort {
    /// Whether the connector may run at all (signature valid, not revoked,
    /// not kill-switched).
    fn is_trusted(&mut self, id: &ConnectorId, version: &str) -> bool;
}

/// Token vault boundary (HUB-11 implements): provider/tenant tokens never
/// leave the vault except as opaque handles.
pub trait TokenVaultPort {
    /// Stores a token for `(connector, account)`; the implementation must
    /// enforce OS-user/Profile isolation.
    fn store(&mut self, connector: &ConnectorId, account: &str, token: &[u8]);
    /// Returns an opaque handle reference count only — never the token.
    fn token_handle(&self, connector: &ConnectorId, account: &str) -> Option<u64>;
}

/// Network boundary (HUB-12 implements): endpoint allowlist, DNS/
/// redirect re-validation, SSRF guards and message budgets.
pub trait NetworkPort {
    /// Executes one bounded call. Implementations must re-validate the
    /// resolved address against the allowlist before every exchange.
    fn send(&mut self, call: &ConnectorCall<'_>) -> Result<String, ConnectorError>;
}

/// Desensitized audit event (HUB-15 wires the sink): closed tokens only —
/// no payload, no token material, no endpoint URL.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConnectorAuditEvent {
    pub connector_namespace: String,
    pub connector_name: String,
    pub outcome: ConnectorOutcome,
    /// Payload byte count (a bound, never the bytes).
    pub payload_bytes: usize,
}

/// Closed call outcomes for audit.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ConnectorOutcome {
    Completed,
    Rejected,
    Failed,
}

impl ConnectorAuditEvent {
    /// Builds an event from a completed call, erasing the payload.
    pub fn from_call(
        id: &ConnectorId,
        call: &ConnectorCall<'_>,
        outcome: ConnectorOutcome,
    ) -> Result<Self, ConnectorError> {
        if call.payload.len() > MAX_AUDIT_TOKEN_BYTES * 1024 {
            return Err(ConnectorError::PayloadTooLarge);
        }
        Ok(Self {
            connector_namespace: id.namespace().to_owned(),
            connector_name: id.name().to_owned(),
            outcome,
            payload_bytes: call.payload.len(),
        })
    }
}
