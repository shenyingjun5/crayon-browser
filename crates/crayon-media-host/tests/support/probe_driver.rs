use crate::desktop::{drive_probe, DecisionOutcome, ReaderEvent};
use crayon_app_runtime::media_host_runtime::{
    MediaHostPendingQueue, MediaHostRuntimeError, MAX_MEDIA_HOST_PENDING_MESSAGES,
};
use crayon_ipc_schema::MediaHostMessage;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use tokio::sync::{mpsc, oneshot};

struct DropProbe(Arc<AtomicBool>);
impl Drop for DropProbe {
    fn drop(&mut self) {
        self.0.store(true, Ordering::SeqCst);
    }
}

#[tokio::test]
async fn cancelling_an_active_probe_drops_its_work() {
    let (sender, mut receiver) = mpsc::channel(2);
    let (started, wait_started) = oneshot::channel();
    let dropped = Arc::new(AtomicBool::new(false));
    let guard = DropProbe(Arc::clone(&dropped));
    let probe = async move {
        let _guard = guard;
        started.send(()).unwrap();
        std::future::pending::<Result<(), MediaHostRuntimeError>>().await
    };
    let mut pending = MediaHostPendingQueue::default();
    let mut output = Vec::new();
    let run = drive_probe(
        probe,
        "active",
        true,
        &mut receiver,
        &mut pending,
        &mut output,
    );
    let cancel = async {
        wait_started.await.unwrap();
        sender
            .send(ReaderEvent::Message(MediaHostMessage::Cancel {
                request_id: "active".into(),
            }))
            .await
            .unwrap();
    };
    let (result, ()) = tokio::join!(run, cancel);
    assert!(matches!(result.unwrap(), DecisionOutcome::Cancelled));
    assert!(dropped.load(Ordering::SeqCst));
    assert!(pending.is_empty());
}

#[tokio::test]
async fn queued_navigation_wins_over_a_simultaneously_ready_probe() {
    let (sender, mut receiver) = mpsc::channel(2);
    sender
        .send(ReaderEvent::Message(MediaHostMessage::Navigation {
            request_id: "nav".into(),
            tab_id: "tab-1".into(),
            navigation_id: 2,
            generation: 2,
        }))
        .await
        .unwrap();
    let mut pending = MediaHostPendingQueue::default();
    let result = drive_probe(
        async { Ok(()) },
        "active",
        true,
        &mut receiver,
        &mut pending,
        &mut Vec::new(),
    )
    .await
    .unwrap();
    assert!(matches!(result, DecisionOutcome::Cancelled));
    assert_eq!(pending.len(), 1);
}

#[tokio::test]
async fn queued_stop_from_an_earlier_probe_prevents_any_new_probe_poll() {
    let (_sender, mut receiver) = mpsc::channel(2);
    let mut pending = MediaHostPendingQueue::default();
    pending
        .accept_during_preflight(
            "old",
            MediaHostMessage::StopCast {
                request_id: "stop".into(),
                session_generation: 1,
            },
        )
        .unwrap();
    let polled = Arc::new(AtomicBool::new(false));
    let signal = Arc::clone(&polled);
    let result = drive_probe(
        async move {
            signal.store(true, Ordering::SeqCst);
            Ok(())
        },
        "active",
        true,
        &mut receiver,
        &mut pending,
        &mut Vec::new(),
    )
    .await
    .unwrap();
    assert!(matches!(result, DecisionOutcome::Cancelled));
    assert!(!polled.load(Ordering::SeqCst));
}

#[tokio::test]
async fn input_flood_has_a_bounded_processing_budget() {
    let (sender, mut receiver) = mpsc::channel(MAX_MEDIA_HOST_PENDING_MESSAGES);
    for index in 0..MAX_MEDIA_HOST_PENDING_MESSAGES {
        sender
            .send(ReaderEvent::Message(MediaHostMessage::PollSessionEvents {
                request_id: format!("poll-{index}"),
            }))
            .await
            .unwrap();
    }
    let mut pending = MediaHostPendingQueue::default();
    let result = drive_probe(
        async { Ok(()) },
        "active",
        true,
        &mut receiver,
        &mut pending,
        &mut Vec::new(),
    )
    .await
    .unwrap();
    assert!(matches!(result, DecisionOutcome::Cancelled));
    assert_eq!(pending.len(), MAX_MEDIA_HOST_PENDING_MESSAGES);
}
