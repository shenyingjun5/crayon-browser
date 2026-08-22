//! CAAP session state machine (AGT-03).
//!
//! Owns the client session set, per-session task lifecycles, target
//! generation tracking, idempotency dedupe, deadline sweeps and bounded
//! queues.  Everything is synchronous and single-threaded: no clocks, no
//! IO, no locks — `now_ms` is injected by the caller and the transport
//! layer (AGT-12) dispatches the returned events.
//!
//! Terminal states (`Completed`/`Failed`/`Cancelled`) never reopen; stale
//! generations and expired deadlines converge tasks into `Failed` with
//! the matching `CaapError`, and late chunks or completions against them
//! are rejected so old results never flow out.

use crate::registry::is_token;
use crayon_domain::{AgentTarget, CaapError, SessionGeneration, TabId};
use crayon_ipc_schema::{CaapRequest, SchemaVersion};
use std::collections::{BTreeMap, HashMap};

/// Maximum simultaneous client sessions.
pub const MAX_SESSIONS: usize = 4;

/// Maximum task records retained per session (terminal records are
/// evicted oldest-first when full; only an all-live set sheds load).
pub const MAX_TASKS_PER_SESSION: usize = 64;

/// Maximum client name length in bytes (registry token charset).
pub const MAX_CLIENT_NAME_LEN: usize = 64;

/// Lifecycle of one task.  Terminal states never transition again.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskState {
    /// Accepted, waiting for the dispatcher to start it.
    Queued,
    /// Being executed; chunks may be emitted.
    Running,
    /// Final chunk emitted or explicitly completed.
    Completed,
    /// Converged to a stable error (stale target, deadline, ...).
    Failed(CaapError),
    /// Cancelled by the client, the user or session teardown.
    Cancelled,
}

impl TaskState {
    /// Reports whether the state is terminal.
    #[must_use]
    pub const fn is_terminal(self) -> bool {
        matches!(self, Self::Completed | Self::Failed(_) | Self::Cancelled)
    }
}

/// Outcome of a successful `submit`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SubmitOutcome {
    /// A new task was queued.
    Accepted,
    /// The idempotency key was already registered with the identical
    /// fingerprint; carries the existing task's id and state.
    Duplicate { request_id: u64, state: TaskState },
}

/// Outcome of a successful `cancel`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CancelOutcome {
    /// The task transitioned to `Cancelled`.
    Cancelled,
    /// The task was already terminal; idempotent no-op.
    AlreadyTerminal,
}

/// A task force-converged by a deadline sweep or generation advance.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConvergedTask {
    pub client: String,
    pub request_id: u64,
    pub state: TaskState,
}

/// One registered task.  Internal record; inspect via `SessionManager`.
#[derive(Clone, Debug)]
struct TaskRecord {
    tool: String,
    tab: TabId,
    generation: SessionGeneration,
    deadline_ms: u64,
    idempotency_key: String,
    fingerprint: String,
    state: TaskState,
    next_seq: u32,
    ticket: u64,
}

/// One client session with its bounded task set.
#[derive(Debug)]
struct ClientSession {
    version: SchemaVersion,
    tasks: BTreeMap<u64, TaskRecord>,
    idempotency: HashMap<String, u64>,
}

impl ClientSession {
    fn task_mut(&mut self, request_id: u64) -> Option<&mut TaskRecord> {
        self.tasks.get_mut(&request_id)
    }
}

/// The session set: bounded, deterministic, fully synchronous.
#[derive(Default)]
pub struct SessionManager {
    sessions: BTreeMap<String, ClientSession>,
    generations: HashMap<TabId, SessionGeneration>,
    next_ticket: u64,
}

impl SessionManager {
    /// An empty session set.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    #[must_use]
    pub fn session_count(&self) -> usize {
        self.sessions.len()
    }

    /// Task count of one session (0 when unknown).
    #[must_use]
    pub fn task_count(&self, client: &str) -> usize {
        self.sessions.get(client).map_or(0, |s| s.tasks.len())
    }

    /// Current state of one task, if known.
    #[must_use]
    pub fn task_state(&self, client: &str, request_id: u64) -> Option<TaskState> {
        self.sessions
            .get(client)
            .and_then(|s| s.tasks.get(&request_id))
            .map(|t| t.state)
    }

    /// Tool name of one task (for receipts and diagnostics).
    #[must_use]
    pub fn task_tool<'a>(&'a self, client: &str, request_id: u64) -> Option<&'a str> {
        self.sessions
            .get(client)
            .and_then(|s| s.tasks.get(&request_id))
            .map(|t| t.tool.as_str())
    }

    /// Negotiated schema version of a session, if open.
    #[must_use]
    pub fn session_version(&self, client: &str) -> Option<SchemaVersion> {
        self.sessions.get(client).map(|s| s.version)
    }

    /// Current generation of a tab (tabs start at `INITIAL`).
    #[must_use]
    pub fn generation_of(&self, tab: &TabId) -> SessionGeneration {
        self.generations
            .get(tab)
            .copied()
            .unwrap_or(SessionGeneration::INITIAL)
    }

    /// Opens a session for a handshaken client.
    pub fn open_session(&mut self, client: &str, version: SchemaVersion) -> Result<(), CaapError> {
        if !is_token(client, MAX_CLIENT_NAME_LEN) {
            return Err(CaapError::InvalidMessage);
        }
        if self.sessions.contains_key(client) {
            return Err(CaapError::InvalidMessage);
        }
        if self.sessions.len() >= MAX_SESSIONS {
            return Err(CaapError::QueueFull);
        }
        self.sessions.insert(
            client.to_owned(),
            ClientSession {
                version,
                tasks: BTreeMap::new(),
                idempotency: HashMap::new(),
            },
        );
        Ok(())
    }

    /// Closes a session: every non-terminal task converges to `Cancelled`
    /// and the whole session is removed.  Unknown sessions are a no-op.
    pub fn close_session(&mut self, client: &str) -> Vec<ConvergedTask> {
        let Some(mut session) = self.sessions.remove(client) else {
            return Vec::new();
        };
        let mut converged = Vec::new();
        for (request_id, task) in &mut session.tasks {
            if !task.state.is_terminal() {
                task.state = TaskState::Cancelled;
                converged.push(ConvergedTask {
                    client: client.to_owned(),
                    request_id: *request_id,
                    state: TaskState::Cancelled,
                });
            }
        }
        converged
    }

    /// Submits a request as a new queued task, honouring idempotency
    /// dedupe, per-session capacity and the caller-injected deadline.
    /// `tab` is the already-resolved concrete target tab.
    pub fn submit(
        &mut self,
        client: &str,
        request: &CaapRequest,
        tab: &TabId,
        now_ms: u64,
    ) -> Result<SubmitOutcome, CaapError> {
        let generation = self.generation_of(tab);
        let session = self
            .sessions
            .get_mut(client)
            .ok_or(CaapError::Unauthorized)?;
        let fingerprint = fingerprint_of(request);
        if let Some(existing_id) = session.idempotency.get(request.idempotency_key()) {
            let existing = &session.tasks[existing_id];
            if existing.fingerprint == fingerprint {
                return Ok(SubmitOutcome::Duplicate {
                    request_id: *existing_id,
                    state: existing.state,
                });
            }
            return Err(CaapError::InvalidMessage);
        }
        if session.tasks.contains_key(&request.id()) {
            return Err(CaapError::InvalidMessage);
        }
        evict_oldest_terminal(session)?;
        self.next_ticket += 1;
        let task = TaskRecord {
            tool: request.tool().to_owned(),
            tab: tab.clone(),
            generation,
            deadline_ms: request.deadline_ms(),
            idempotency_key: request.idempotency_key().to_owned(),
            fingerprint,
            state: TaskState::Queued,
            next_seq: 0,
            ticket: self.next_ticket,
        };
        session
            .idempotency
            .insert(task.idempotency_key.clone(), request.id());
        session.tasks.insert(request.id(), task);
        if request.deadline_ms() <= now_ms {
            let task = &mut session.tasks.get_mut(&request.id()).expect("just inserted");
            task.state = TaskState::Failed(CaapError::DeadlineExceeded);
            return Err(CaapError::DeadlineExceeded);
        }
        Ok(SubmitOutcome::Accepted)
    }

    /// Moves a queued task to running.  Terminal tasks are rejected.
    pub fn start(&mut self, client: &str, request_id: u64) -> Result<(), CaapError> {
        let task = self.live_task_mut(client, request_id)?;
        match task.state {
            TaskState::Queued => {
                task.state = TaskState::Running;
                Ok(())
            }
            TaskState::Running => Ok(()),
            TaskState::Failed(CaapError::TargetStale) => Err(CaapError::TargetStale),
            _ => Err(CaapError::InvalidMessage),
        }
    }

    /// Cancels a task; cancellation of terminal tasks is an idempotent
    /// no-op, unknown ids are rejected.
    pub fn cancel(&mut self, client: &str, request_id: u64) -> Result<CancelOutcome, CaapError> {
        let task = self.live_task_mut(client, request_id)?;
        if task.state.is_terminal() {
            return Ok(CancelOutcome::AlreadyTerminal);
        }
        task.state = TaskState::Cancelled;
        Ok(CancelOutcome::Cancelled)
    }

    /// Marks a running task completed (non-streaming tools).
    pub fn complete(&mut self, client: &str, request_id: u64) -> Result<(), CaapError> {
        self.transition_running(client, request_id, TaskState::Completed)
    }

    /// Marks a running task failed with a stable error.
    pub fn fail(
        &mut self,
        client: &str,
        request_id: u64,
        error: CaapError,
    ) -> Result<(), CaapError> {
        self.transition_running(client, request_id, TaskState::Failed(error))
    }

    /// Allocates the next chunk sequence for a running task; a final
    /// chunk completes the task.  Stale or non-running tasks are
    /// rejected so old results never flow out.
    pub fn next_chunk(
        &mut self,
        client: &str,
        request_id: u64,
        is_final: bool,
    ) -> Result<u32, CaapError> {
        let task = self.live_task_mut(client, request_id)?;
        match task.state {
            TaskState::Running => {
                let seq = task.next_seq;
                task.next_seq = task.next_seq.saturating_add(1);
                if is_final {
                    task.state = TaskState::Completed;
                }
                Ok(seq)
            }
            TaskState::Failed(CaapError::TargetStale) => Err(CaapError::TargetStale),
            _ => Err(CaapError::InvalidMessage),
        }
    }

    /// Converges every non-terminal task whose deadline elapsed.
    pub fn sweep_expired(&mut self, now_ms: u64) -> Vec<ConvergedTask> {
        let mut converged = Vec::new();
        for (client, session) in &mut self.sessions {
            for (request_id, task) in &mut session.tasks {
                if !task.state.is_terminal() && task.deadline_ms <= now_ms {
                    task.state = TaskState::Failed(CaapError::DeadlineExceeded);
                    converged.push(ConvergedTask {
                        client: client.clone(),
                        request_id: *request_id,
                        state: task.state,
                    });
                }
            }
        }
        converged
    }

    /// Advances a tab generation; every non-terminal task bound to an
    /// older generation of that tab converges to `Failed(TargetStale)`.
    pub fn advance_generation(&mut self, tab: &TabId) -> Vec<ConvergedTask> {
        let current = self.generation_of(tab);
        let Some(next) = current.advance() else {
            return Vec::new();
        };
        self.generations.insert(tab.clone(), next);
        let mut converged = Vec::new();
        for (client, session) in &mut self.sessions {
            for (request_id, task) in &mut session.tasks {
                if task.tab == *tab && !task.state.is_terminal() && next.supersedes(task.generation)
                {
                    task.state = TaskState::Failed(CaapError::TargetStale);
                    converged.push(ConvergedTask {
                        client: client.clone(),
                        request_id: *request_id,
                        state: task.state,
                    });
                }
            }
        }
        converged
    }

    fn live_task_mut(
        &mut self,
        client: &str,
        request_id: u64,
    ) -> Result<&mut TaskRecord, CaapError> {
        let session = self
            .sessions
            .get_mut(client)
            .ok_or(CaapError::Unauthorized)?;
        session
            .task_mut(request_id)
            .ok_or(CaapError::InvalidMessage)
    }

    fn transition_running(
        &mut self,
        client: &str,
        request_id: u64,
        target: TaskState,
    ) -> Result<(), CaapError> {
        let task = self.live_task_mut(client, request_id)?;
        match task.state {
            TaskState::Running => {
                task.state = target;
                Ok(())
            }
            TaskState::Failed(CaapError::TargetStale) => Err(CaapError::TargetStale),
            _ => Err(CaapError::InvalidMessage),
        }
    }
}

/// Canonical idempotency fingerprint: tool, wire target and sorted
/// `key=value` params.  Internal only, never on the wire.
fn fingerprint_of(request: &CaapRequest) -> String {
    let mut out = request.tool().to_owned();
    out.push('|');
    match request.target() {
        AgentTarget::Tab { tab } => out.push_str(tab.as_str()),
        AgentTarget::ActiveTab => out.push_str("active_tab"),
    }
    for (key, value) in request.params() {
        out.push('|');
        out.push_str(key);
        out.push('=');
        out.push_str(value);
    }
    out
}

/// Evicts the oldest terminal task when the session is at capacity;
/// sheds the load with `QueueFull` only when every task is live.
fn evict_oldest_terminal(session: &mut ClientSession) -> Result<(), CaapError> {
    if session.tasks.len() < MAX_TASKS_PER_SESSION {
        return Ok(());
    }
    let evict = session
        .tasks
        .iter()
        .filter(|(_, task)| task.state.is_terminal())
        .min_by_key(|(_, task)| task.ticket)
        .map(|(id, _)| *id);
    let Some(id) = evict else {
        return Err(CaapError::QueueFull);
    };
    if let Some(task) = session.tasks.remove(&id) {
        session.idempotency.remove(&task.idempotency_key);
    }
    Ok(())
}

#[cfg(test)]
#[path = "session_tests.rs"]
mod tests;
