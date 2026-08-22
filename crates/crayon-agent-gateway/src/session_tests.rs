//! AGT-03 session state-machine tests: lifecycle, idempotency, deadline,
//! generation staleness, cancellation, capacity and invariant fuzzing.

use super::*;
use crayon_domain::AgentTarget;
use std::collections::{BTreeMap, HashMap};

const VERSION: SchemaVersion = SchemaVersion::CURRENT;
const NOW: u64 = 1_000;
const LATER: u64 = 5_000;

fn tab(name: &str) -> TabId {
    TabId::new(name).expect("tab id")
}

fn request(id: u64, tool: &str, key: &str, deadline_ms: u64) -> CaapRequest {
    CaapRequest::new(
        id,
        tool,
        AgentTarget::ActiveTab,
        deadline_ms,
        key,
        BTreeMap::new(),
    )
    .expect("valid request")
}

fn manager_with(client: &str) -> SessionManager {
    let mut manager = SessionManager::new();
    manager.open_session(client, VERSION).expect("open session");
    manager
}

#[test]
fn open_session_validates_name_capacity_and_duplicates() {
    let mut manager = SessionManager::new();
    assert_eq!(
        manager.open_session("", VERSION),
        Err(CaapError::InvalidMessage)
    );
    assert_eq!(
        manager.open_session("Bad Client", VERSION),
        Err(CaapError::InvalidMessage)
    );
    for index in 0..MAX_SESSIONS {
        let client = format!("client_{index}");
        manager
            .open_session(&client, VERSION)
            .expect("within capacity");
    }
    assert_eq!(
        manager.open_session("client_overflow", VERSION),
        Err(CaapError::QueueFull)
    );
    assert_eq!(
        manager.open_session("client_0", VERSION),
        Err(CaapError::InvalidMessage)
    );
    assert_eq!(manager.session_count(), MAX_SESSIONS);
    assert_eq!(manager.session_version("client_0"), Some(VERSION));
    assert_eq!(manager.session_version("ghost"), None);
}

#[test]
fn operations_on_unknown_session_are_unauthorized() {
    let mut manager = SessionManager::new();
    let req = request(1, "page.get_title", "k1", LATER);
    assert_eq!(
        manager.submit("ghost", &req, &tab("t1"), NOW),
        Err(CaapError::Unauthorized)
    );
    assert_eq!(manager.cancel("ghost", 1), Err(CaapError::Unauthorized));
    assert_eq!(manager.complete("ghost", 1), Err(CaapError::Unauthorized));
    assert!(manager.close_session("ghost").is_empty());
}

#[test]
fn submit_rejects_duplicate_request_id() {
    let mut manager = manager_with("c1");
    let req = request(7, "page.get_title", "k1", LATER);
    assert_eq!(
        manager.submit("c1", &req, &tab("t1"), NOW),
        Ok(SubmitOutcome::Accepted)
    );
    let other_key = request(7, "page.get_title", "k2", LATER);
    assert_eq!(
        manager.submit("c1", &other_key, &tab("t1"), NOW),
        Err(CaapError::InvalidMessage)
    );
}

#[test]
fn idempotency_key_dedupes_identical_and_rejects_conflicting() {
    let mut manager = manager_with("c1");
    let req = request(1, "nav.navigate", "key_a", LATER);
    manager.submit("c1", &req, &tab("t1"), NOW).expect("first");
    manager.start("c1", 1).expect("start");

    let retry = request(2, "nav.navigate", "key_a", LATER);
    assert_eq!(
        manager.submit("c1", &retry, &tab("t1"), NOW),
        Ok(SubmitOutcome::Duplicate {
            request_id: 1,
            state: TaskState::Running,
        })
    );
    assert_eq!(manager.task_count("c1"), 1);

    let mut params = BTreeMap::new();
    params.insert("url".to_owned(), "https://example.invalid/".to_owned());
    let conflicting = CaapRequest::new(
        3,
        "nav.navigate",
        AgentTarget::ActiveTab,
        LATER,
        "key_a",
        params,
    )
    .expect("valid");
    assert_eq!(
        manager.submit("c1", &conflicting, &tab("t1"), NOW),
        Err(CaapError::InvalidMessage)
    );
}

#[test]
fn expired_deadline_at_submit_converges_and_dedupes() {
    let mut manager = manager_with("c1");
    let req = request(1, "page.get_title", "k1", NOW);
    assert_eq!(
        manager.submit("c1", &req, &tab("t1"), NOW),
        Err(CaapError::DeadlineExceeded)
    );
    assert_eq!(
        manager.task_state("c1", 1),
        Some(TaskState::Failed(CaapError::DeadlineExceeded))
    );
    let retry = request(2, "page.get_title", "k1", LATER);
    assert_eq!(
        manager.submit("c1", &retry, &tab("t1"), NOW),
        Ok(SubmitOutcome::Duplicate {
            request_id: 1,
            state: TaskState::Failed(CaapError::DeadlineExceeded),
        })
    );
}

#[test]
fn sweep_expired_converges_only_overdue_live_tasks() {
    let mut manager = manager_with("c1");
    manager
        .submit(
            "c1",
            &request(1, "page.get_title", "k1", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit");
    manager
        .submit(
            "c1",
            &request(2, "page.get_title", "k2", LATER + 10_000),
            &tab("t1"),
            NOW,
        )
        .expect("submit");
    manager.start("c1", 2).expect("start");
    manager.complete("c1", 2).expect("complete");

    let converged = manager.sweep_expired(LATER);
    assert_eq!(converged.len(), 1);
    assert_eq!(converged[0].request_id, 1);
    assert_eq!(
        converged[0].state,
        TaskState::Failed(CaapError::DeadlineExceeded)
    );
    assert!(manager.sweep_expired(LATER).is_empty());
}

#[test]
fn cancel_is_idempotent_and_rejects_unknown_ids() {
    let mut manager = manager_with("c1");
    manager
        .submit(
            "c1",
            &request(1, "nav.reload", "k1", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit");
    assert_eq!(manager.cancel("c1", 1), Ok(CancelOutcome::Cancelled));
    assert_eq!(manager.task_state("c1", 1), Some(TaskState::Cancelled));
    assert_eq!(manager.cancel("c1", 1), Ok(CancelOutcome::AlreadyTerminal));
    assert_eq!(manager.cancel("c1", 99), Err(CaapError::InvalidMessage));
}

#[test]
fn start_and_chunk_sequence_follow_the_lifecycle() {
    let mut manager = manager_with("c1");
    manager
        .submit(
            "c1",
            &request(1, "page.snapshot", "k1", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit");
    assert_eq!(
        manager.next_chunk("c1", 1, false),
        Err(CaapError::InvalidMessage)
    );
    manager.start("c1", 1).expect("start");
    assert_eq!(manager.start("c1", 1), Ok(()), "start is idempotent");
    assert_eq!(manager.task_tool("c1", 1), Some("page.snapshot"));
    assert_eq!(manager.task_tool("c1", 99), None);

    assert_eq!(manager.next_chunk("c1", 1, false), Ok(0));
    assert_eq!(manager.next_chunk("c1", 1, false), Ok(1));
    assert_eq!(manager.next_chunk("c1", 1, true), Ok(2));
    assert_eq!(manager.task_state("c1", 1), Some(TaskState::Completed));
    assert_eq!(
        manager.next_chunk("c1", 1, false),
        Err(CaapError::InvalidMessage)
    );
    assert_eq!(manager.start("c1", 1), Err(CaapError::InvalidMessage));
}

#[test]
fn terminal_states_never_reopen() {
    let mut manager = manager_with("c1");
    manager
        .submit(
            "c1",
            &request(1, "page.get_title", "k1", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit");
    manager.start("c1", 1).expect("start");
    manager.complete("c1", 1).expect("complete");
    assert_eq!(manager.complete("c1", 1), Err(CaapError::InvalidMessage));
    assert_eq!(
        manager.fail("c1", 1, CaapError::Cancelled),
        Err(CaapError::InvalidMessage)
    );
    assert_eq!(manager.task_state("c1", 1), Some(TaskState::Completed));
}

#[test]
fn generation_advance_stales_only_matching_tab_tasks() {
    let mut manager = manager_with("c1");
    manager
        .submit(
            "c1",
            &request(1, "page.snapshot", "k1", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit t1");
    manager
        .submit(
            "c1",
            &request(2, "page.snapshot", "k2", LATER),
            &tab("t2"),
            NOW,
        )
        .expect("submit t2");
    manager.start("c1", 1).expect("start");

    let converged = manager.advance_generation(&tab("t1"));
    assert_eq!(converged.len(), 1);
    assert_eq!(converged[0].request_id, 1);
    assert_eq!(
        converged[0].state,
        TaskState::Failed(CaapError::TargetStale)
    );
    assert_eq!(manager.generation_of(&tab("t1")).get(), 1);
    assert_eq!(manager.generation_of(&tab("t2")).get(), 0);

    // Old results are dropped: chunks and completions on stale tasks fail.
    assert_eq!(
        manager.next_chunk("c1", 1, false),
        Err(CaapError::TargetStale)
    );
    assert_eq!(manager.complete("c1", 1), Err(CaapError::TargetStale));
    assert_eq!(manager.start("c1", 1), Err(CaapError::TargetStale));
    assert_eq!(manager.cancel("c1", 1), Ok(CancelOutcome::AlreadyTerminal));
    assert_eq!(manager.task_state("c1", 2), Some(TaskState::Queued));

    // New work on the tab binds the fresh generation and survives.
    manager
        .submit(
            "c1",
            &request(3, "page.snapshot", "k3", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit fresh");
    assert!(manager
        .advance_generation(&tab("t2"))
        .iter()
        .all(|t| t.request_id != 3));
    assert_eq!(manager.task_state("c1", 3), Some(TaskState::Queued));
}

#[test]
fn close_session_cancels_live_tasks_and_removes_session() {
    let mut manager = manager_with("c1");
    manager
        .submit(
            "c1",
            &request(1, "page.snapshot", "k1", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit 1");
    manager
        .submit(
            "c1",
            &request(2, "page.markdown", "k2", LATER),
            &tab("t1"),
            NOW,
        )
        .expect("submit 2");
    manager.start("c1", 2).expect("start");

    let converged = manager.close_session("c1");
    assert_eq!(converged.len(), 2);
    assert!(converged.iter().all(|t| t.state == TaskState::Cancelled));
    assert_eq!(manager.session_count(), 0);
    assert!(manager.close_session("c1").is_empty());
    assert_eq!(
        manager.submit(
            "c1",
            &request(3, "page.markdown", "k3", LATER),
            &tab("t1"),
            NOW
        ),
        Err(CaapError::Unauthorized)
    );
}

#[test]
fn capacity_evicts_oldest_terminal_then_sheds_load() {
    let mut manager = manager_with("c1");
    for index in 0..MAX_TASKS_PER_SESSION {
        let id = index as u64;
        let key = format!("k{index}");
        manager
            .submit(
                "c1",
                &request(id, "page.get_title", &key, LATER),
                &tab("t1"),
                NOW,
            )
            .expect("within capacity");
        manager.start("c1", id).expect("start");
        manager.complete("c1", id).expect("complete");
    }
    assert_eq!(manager.task_count("c1"), MAX_TASKS_PER_SESSION);

    // Full of terminal tasks: the oldest is evicted, idempotency released.
    let extra = request(1_000, "page.get_title", "k_extra", LATER);
    assert_eq!(
        manager.submit("c1", &extra, &tab("t1"), NOW),
        Ok(SubmitOutcome::Accepted)
    );
    assert_eq!(manager.task_count("c1"), MAX_TASKS_PER_SESSION);
    assert_eq!(manager.task_state("c1", 0), None, "oldest task evicted");
    let reused_key = request(1_001, "page.get_title", "k0", LATER);
    assert_eq!(
        manager.submit("c1", &reused_key, &tab("t1"), NOW),
        Ok(SubmitOutcome::Accepted),
        "evicted idempotency key can be reused"
    );
    assert_eq!(manager.task_state("c1", 1), None, "second oldest evicted");

    // An all-live session cannot evict: further submits are shed.
    manager.open_session("c2", VERSION).expect("open c2");
    for index in 0..MAX_TASKS_PER_SESSION {
        let id = index as u64;
        let key = format!("live{index}");
        manager
            .submit(
                "c2",
                &request(id, "page.get_title", &key, LATER),
                &tab("t1"),
                NOW,
            )
            .expect("backfill live");
    }
    manager.start("c2", 0).expect("start one live");
    assert_eq!(
        manager.submit(
            "c2",
            &request(9_999, "page.get_title", "k_shed", LATER),
            &tab("t1"),
            NOW
        ),
        Err(CaapError::QueueFull)
    );
}

#[test]
fn pseudo_random_operation_sequences_keep_invariants() {
    let mut manager = SessionManager::new();
    let mut state = 0x5EED_u64;
    let mut next_random = move || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1);
        (state >> 33) as u32
    };
    let mut terminal: HashMap<(String, u64), TaskState> = HashMap::new();
    let mut last_seq: HashMap<(String, u64), u32> = HashMap::new();
    let mut now = NOW;

    for step in 0..3_000_u32 {
        now += u64::from(next_random() % 3);
        let client = format!("c{}", next_random() % (MAX_SESSIONS as u32 + 2));
        let request_id = u64::from(next_random() % 24);
        let target = tab(&format!("t{}", next_random() % 3));
        match next_random() % 8 {
            0 => {
                let _ = manager.open_session(&client, VERSION);
            }
            1 => {
                let deadline = NOW + u64::from(next_random() % 10_000);
                let key = format!("k{}", next_random() % 48);
                let outcome = manager.submit(
                    &client,
                    &request(request_id, "page.snapshot", &key, deadline),
                    &target,
                    now,
                );
                if matches!(outcome, Ok(SubmitOutcome::Accepted)) {
                    // A fresh accept may follow an eviction of the same id;
                    // reset the mirrors for it.
                    terminal.remove(&(client.clone(), request_id));
                    last_seq.remove(&(client.clone(), request_id));
                }
            }
            2 => {
                let _ = manager.start(&client, request_id);
            }
            3 => {
                let _ = manager.cancel(&client, request_id);
            }
            4 => {
                let _ = manager.complete(&client, request_id);
            }
            5 => {
                if let Ok(seq) = manager.next_chunk(&client, request_id, next_random() % 4 == 0) {
                    let key = (client.clone(), request_id);
                    if let Some(previous) = last_seq.get(&key) {
                        assert!(seq > *previous, "chunk seq must be monotonic");
                    }
                    last_seq.insert(key, seq);
                }
            }
            6 => {
                let _ = manager.advance_generation(&target);
                let _ = manager.sweep_expired(now);
            }
            _ => {
                for task in manager.close_session(&client) {
                    terminal.insert((task.client, task.request_id), task.state);
                }
            }
        }

        assert!(manager.session_count() <= MAX_SESSIONS, "step {step}");
        for known in ["c0", "c1", "c2", "c3", "c4", "c5"] {
            assert!(
                manager.task_count(known) <= MAX_TASKS_PER_SESSION,
                "step {step}"
            );
            for request_id in 0..24_u64 {
                let key = (known.to_owned(), request_id);
                match manager.task_state(known, request_id) {
                    None => {
                        terminal.remove(&key);
                        last_seq.remove(&key);
                    }
                    Some(current) => {
                        if let Some(previous) = terminal.get(&key) {
                            assert_eq!(
                                *previous, current,
                                "terminal state must never revert at step {step}"
                            );
                        }
                        if current.is_terminal() {
                            terminal.entry(key).or_insert(current);
                        }
                    }
                }
            }
        }
    }
}
