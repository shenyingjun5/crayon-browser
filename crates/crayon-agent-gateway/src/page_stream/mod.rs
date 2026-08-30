//! Generation-fenced page stream fan-out (AGT-06, AG-006).
//!
//! The hub is the single owner of per-client stream state over published
//! verified snapshots. It adds what the pull-based `SnapshotOwner` does
//! not provide: authorized fan-out to bounded, profile-scoped client
//! queues with explicit backpressure (drop-oldest plus a counted gap),
//! generation fencing and bounded instrumentation. No locks, no IO, no
//! system clock — the hub is pure state over injected facts.

use crate::grant::ProfileScope;
use crayon_domain::{SessionGeneration, TabId};
use crayon_page_data::PageSnapshot;
use std::collections::{BTreeMap, VecDeque};

/// Maximum number of concurrent stream clients.
pub const MAX_STREAM_CLIENTS: usize = 8;

/// Maximum queued chunks per client; overflow drops the oldest chunk.
pub const MAX_QUEUED_CHUNKS: usize = 16;

/// Maximum length of a client identifier.
pub const MAX_CLIENT_ID_BYTES: usize = 64;

/// Client identifier validation failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ClientIdError {
    Empty,
    TooLong,
    InvalidCharset,
}

impl std::fmt::Display for ClientIdError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Empty => formatter.write_str("client id must not be empty"),
            Self::TooLong => formatter.write_str("client id exceeds the maximum length"),
            Self::InvalidCharset => {
                formatter.write_str("client id contains characters outside [A-Za-z0-9_-]")
            }
        }
    }
}

impl std::error::Error for ClientIdError {}

/// Validated client identifier; mirrors the session token charset.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct StreamClientId(String);

impl StreamClientId {
    /// Wraps a validated client identifier.
    pub fn new(value: &str) -> Result<Self, ClientIdError> {
        if value.is_empty() {
            return Err(ClientIdError::Empty);
        }
        if value.len() > MAX_CLIENT_ID_BYTES {
            return Err(ClientIdError::TooLong);
        }
        if !value
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
        {
            return Err(ClientIdError::InvalidCharset);
        }
        Ok(Self(value.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for StreamClientId {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// One queued chunk: a monotonic per-client sequence and the verified
/// snapshot. Sequence gaps after overflow are detectable by consumers.
#[derive(Clone, Debug, PartialEq)]
pub struct StreamChunk {
    pub seq: u64,
    pub snapshot: PageSnapshot,
}

/// Stream error. Stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StreamError {
    UnknownClient,
    DuplicateClient,
    CapacityExceeded,
    ShutDown,
}

impl std::fmt::Display for StreamError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::UnknownClient => "stream client is unknown or closed",
            Self::DuplicateClient => "stream client is already subscribed",
            Self::CapacityExceeded => "stream client capacity exceeded",
            Self::ShutDown => "page stream hub is shut down",
        })
    }
}

impl std::error::Error for StreamError {}

#[derive(Debug, Default)]
struct ClientStream {
    profile: Option<ProfileScope>,
    tab: Option<TabId>,
    generation: Option<SessionGeneration>,
    next_seq: u64,
    queue: VecDeque<StreamChunk>,
    delivered: u64,
}

/// Bounded lifetime counters; diagnostics only, never correctness inputs.
#[derive(Debug, Default, Clone, Copy, Eq, PartialEq)]
pub struct StreamStats {
    pub clients: usize,
    pub queued: usize,
    pub delivered: u64,
    pub dropped: u64,
    pub cancelled_by_generation: u64,
}

/// Single owner of page stream fan-out state.
#[derive(Debug, Default)]
pub struct PageStreamHub {
    clients: BTreeMap<StreamClientId, ClientStream>,
    delivered: u64,
    dropped: u64,
    cancelled_by_generation: u64,
    shut_down: bool,
}

impl PageStreamHub {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Subscribes a client to one tab/generation within a profile scope.
    /// The subscription is fenced: only snapshots matching the binding are
    /// delivered.
    pub fn subscribe(
        &mut self,
        client: StreamClientId,
        profile: ProfileScope,
        tab: TabId,
        generation: SessionGeneration,
    ) -> Result<(), StreamError> {
        if self.shut_down {
            return Err(StreamError::ShutDown);
        }
        if self.clients.contains_key(&client) {
            return Err(StreamError::DuplicateClient);
        }
        if self.clients.len() >= MAX_STREAM_CLIENTS {
            return Err(StreamError::CapacityExceeded);
        }
        self.clients.insert(
            client,
            ClientStream {
                profile: Some(profile),
                tab: Some(tab),
                generation: Some(generation),
                next_seq: 0,
                queue: VecDeque::new(),
                delivered: 0,
            },
        );
        Ok(())
    }

    /// Fans one verified snapshot out to matching subscribers. Queue
    /// overflow drops the oldest chunk and counts the gap; delivery order
    /// and sequence stay monotonic per client.
    pub fn publish(&mut self, snapshot: &PageSnapshot) {
        if self.shut_down {
            return;
        }
        let binding = snapshot.navigation();
        for stream in self.clients.values_mut() {
            let Some(subscribed_generation) = stream.generation else {
                continue;
            };
            let Some(subscribed_tab) = &stream.tab else {
                continue;
            };
            if *subscribed_tab != binding.tab_id || subscribed_generation != binding.generation {
                continue;
            }
            if stream.queue.len() >= MAX_QUEUED_CHUNKS {
                stream.queue.pop_front();
                self.dropped += 1;
            }
            let seq = stream.next_seq;
            stream.next_seq += 1;
            stream.queue.push_back(StreamChunk {
                seq,
                snapshot: snapshot.clone(),
            });
        }
    }

    /// Pops the next chunk for one client; `None` when the client is
    /// unknown or its queue is empty.
    pub fn next_chunk(&mut self, client: &StreamClientId) -> Option<StreamChunk> {
        let stream = self.clients.get_mut(client)?;
        let chunk = stream.queue.pop_front()?;
        stream.delivered += 1;
        self.delivered += 1;
        Some(chunk)
    }

    /// Cancels one client's subscription idempotently; queued chunks are
    /// dropped. Returns whether a live subscription was removed.
    pub fn cancel(&mut self, client: &StreamClientId) -> bool {
        self.clients.remove(client).is_some()
    }

    /// Cancels every subscription of a tab whose generation is older than
    /// the given one; newer generations of the same tab keep streaming.
    /// Returns how many subscriptions were cancelled.
    pub fn advance_generation(&mut self, tab: &TabId, generation: SessionGeneration) -> usize {
        let before = self.clients.len();
        self.clients.retain(|_, stream| {
            let Some(subscribed) = stream.generation else {
                return false;
            };
            let Some(subscribed_tab) = &stream.tab else {
                return false;
            };
            *subscribed_tab != *tab || subscribed >= generation
        });
        let cancelled = before - self.clients.len();
        self.cancelled_by_generation += cancelled as u64;
        cancelled
    }

    /// Closes every subscription of one profile scope (profile switch or
    /// close); no queued content may leak across the boundary.
    pub fn close_profile(&mut self, profile: &ProfileScope) -> usize {
        let before = self.clients.len();
        self.clients
            .retain(|_, stream| stream.profile.as_ref() != Some(profile));
        before - self.clients.len()
    }

    /// Idempotent shutdown; drops all subscription state.
    pub fn shut_down(&mut self) -> usize {
        let dropped = self.clients.len();
        self.clients.clear();
        self.shut_down = true;
        dropped
    }

    /// Snapshot of the bounded counters.
    #[must_use]
    pub fn stats(&self) -> StreamStats {
        StreamStats {
            clients: self.clients.len(),
            queued: self.clients.values().map(|s| s.queue.len()).sum(),
            delivered: self.delivered,
            dropped: self.dropped,
            cancelled_by_generation: self.cancelled_by_generation,
        }
    }
}

#[cfg(test)]
mod stream_tests;
