use super::*;
use crate::{NavigationBinding, OutputLevel, TruncationInfo};
use crayon_domain::{SessionGeneration, TabId};

fn paragraph(value: impl Into<String>) -> ContentBlock {
    ContentBlock::Paragraph { text: value.into() }
}

fn snapshot(tab: &str, generation: u64, revision: u64, blocks: Vec<ContentBlock>) -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(
            TabId::new(tab).unwrap(),
            SessionGeneration::from_raw(generation),
        ),
        "https://example.test/delta".into(),
        format!("Revision {revision}"),
        revision,
        TruncationInfo::default(),
        blocks,
    )
    .unwrap()
}

#[test]
fn splice_reuses_prefix_suffix_and_advances_revision() {
    let old = snapshot(
        "delta",
        1,
        1,
        vec![paragraph("a"), paragraph("old"), paragraph("z")],
    );
    let new = snapshot(
        "delta",
        1,
        2,
        vec![
            paragraph("a"),
            paragraph("new-1"),
            paragraph("new-2"),
            paragraph("z"),
        ],
    );
    let delta = SnapshotDelta::between(&old, &new).unwrap();
    assert_eq!(delta.kind(), DeltaKind::Splice);
    assert_eq!(delta.start(), 1);
    assert_eq!(delta.delete_count(), 1);
    assert_eq!(delta.inserted().len(), 2);
    assert_eq!(delta.reused_blocks(), 2);
    assert!(delta.serialized_bytes() > 0);
}

#[test]
fn metadata_only_revision_emits_one_terminal_chunk() {
    let old = snapshot("metadata", 1, 1, vec![paragraph("same")]);
    let new = snapshot("metadata", 1, 2, vec![paragraph("same")]);
    let delta = SnapshotDelta::between(&old, &new).unwrap();
    assert_eq!(delta.kind(), DeltaKind::NoChange);
    let mut stream = DeltaStream::new(delta);
    let chunk = stream
        .next_chunk(SessionGeneration::from_raw(1), 2)
        .unwrap();
    assert!(chunk.blocks.is_empty());
    assert!(chunk.metadata.is_some());
    assert!(chunk.terminal);
    assert_eq!(
        stream.next_chunk(SessionGeneration::from_raw(1), 2),
        Err(DeltaError::Complete)
    );
    assert!(!stream.cancel());
    assert_eq!(
        stream.next_chunk(SessionGeneration::from_raw(1), 2),
        Err(DeltaError::Complete)
    );
}

#[test]
fn insertion_and_deletion_only_splices_keep_exact_boundaries() {
    let base = snapshot(
        "edits",
        1,
        1,
        vec![paragraph("a"), paragraph("b"), paragraph("c")],
    );
    let inserted = snapshot(
        "edits",
        1,
        2,
        vec![
            paragraph("a"),
            paragraph("new"),
            paragraph("b"),
            paragraph("c"),
        ],
    );
    let insert_delta = SnapshotDelta::between(&base, &inserted).unwrap();
    assert_eq!(insert_delta.start(), 1);
    assert_eq!(insert_delta.delete_count(), 0);
    assert_eq!(insert_delta.inserted(), &[paragraph("new")]);

    let deleted = snapshot("edits", 1, 3, vec![paragraph("a"), paragraph("c")]);
    let delete_delta = SnapshotDelta::between(&inserted, &deleted).unwrap();
    assert_eq!(delete_delta.start(), 1);
    assert_eq!(delete_delta.delete_count(), 2);
    assert!(delete_delta.inserted().is_empty());
}

#[test]
fn navigation_and_revision_must_advance() {
    let old = snapshot("a", 1, 4, vec![paragraph("a")]);
    assert_eq!(
        SnapshotDelta::between(&old, &snapshot("b", 1, 5, vec![paragraph("a")])),
        Err(DeltaError::StaleGeneration)
    );
    assert_eq!(
        SnapshotDelta::between(&old, &snapshot("a", 2, 5, vec![paragraph("a")])),
        Err(DeltaError::StaleGeneration)
    );
    assert_eq!(
        SnapshotDelta::between(&old, &snapshot("a", 1, 4, vec![paragraph("a")])),
        Err(DeltaError::RevisionNotAdvanced)
    );
}

#[test]
fn large_change_falls_back_to_replace_all_and_chunks_in_order() {
    let old = snapshot(
        "large",
        1,
        1,
        (0..300).map(|i| paragraph(format!("old-{i}"))).collect(),
    );
    let new = snapshot(
        "large",
        1,
        2,
        (0..600).map(|i| paragraph(format!("new-{i}"))).collect(),
    );
    let delta = SnapshotDelta::between(&old, &new).unwrap();
    assert_eq!(delta.kind(), DeltaKind::ReplaceAll);
    let mut stream = DeltaStream::new(delta);
    let mut blocks = 0;
    let mut sequence = 0;
    loop {
        let chunk = stream
            .next_chunk(SessionGeneration::from_raw(1), 2)
            .unwrap();
        assert_eq!(chunk.sequence, sequence);
        assert!(chunk.blocks.len() <= MAX_DELTA_CHUNK_BLOCKS);
        blocks += chunk.blocks.len();
        stream.acknowledge(sequence).unwrap();
        sequence += 1;
        if chunk.terminal {
            break;
        }
    }
    assert_eq!(blocks, 600);
    assert_eq!(sequence, 10);
    assert!(stream.delta.inserted.is_empty());
}

#[test]
fn slow_consumer_gets_explicit_backpressure_and_ordered_ack() {
    let old = snapshot("pressure", 1, 1, Vec::new());
    let new = snapshot(
        "pressure",
        1,
        2,
        (0..400).map(|i| paragraph(format!("p-{i}"))).collect(),
    );
    let mut stream = DeltaStream::new(SnapshotDelta::between(&old, &new).unwrap());
    for sequence in 0..MAX_UNACKED_DELTA_CHUNKS as u32 {
        assert_eq!(
            stream
                .next_chunk(SessionGeneration::from_raw(1), 2)
                .unwrap()
                .sequence,
            sequence
        );
    }
    assert_eq!(
        stream.next_chunk(SessionGeneration::from_raw(1), 2),
        Err(DeltaError::Backpressure)
    );
    assert_eq!(stream.acknowledge(1), Err(DeltaError::InvalidAck));
    stream.acknowledge(0).unwrap();
    assert_eq!(
        stream
            .next_chunk(SessionGeneration::from_raw(1), 2)
            .unwrap()
            .sequence,
        4
    );
}

#[test]
fn stale_fence_and_cancel_release_stream_payload() {
    let old = snapshot("stale", 1, 1, Vec::new());
    let new = snapshot("stale", 1, 2, vec![paragraph("new")]);
    let delta = SnapshotDelta::between(&old, &new).unwrap();
    let mut stale = DeltaStream::new(delta.clone());
    assert_eq!(
        stale.next_chunk(SessionGeneration::from_raw(2), 2),
        Err(DeltaError::StaleGeneration)
    );
    assert_eq!(
        stale.next_chunk(SessionGeneration::from_raw(1), 2),
        Err(DeltaError::Cancelled)
    );
    let mut cancelled = DeltaStream::new(delta);
    assert!(cancelled.cancel());
    assert!(!cancelled.cancel());
    assert_eq!(cancelled.unacked_chunks(), 0);
    assert_eq!(
        cancelled.next_chunk(SessionGeneration::from_raw(1), 2),
        Err(DeltaError::Cancelled)
    );

    let revision_delta = SnapshotDelta::between(&old, &new).unwrap();
    let mut stale_revision = DeltaStream::new(revision_delta);
    assert_eq!(
        stale_revision.next_chunk(SessionGeneration::from_raw(1), 1),
        Err(DeltaError::StaleRevision)
    );
}
