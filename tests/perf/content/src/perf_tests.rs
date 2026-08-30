use crayon_content_markdown::render_snapshot;
use crayon_domain::{SessionGeneration, TabId};
use crayon_page_data::{
    ContentBlock, DeltaError, DeltaStream, NavigationBinding, OutputLevel, PageSnapshot,
    SnapshotDelta, SnapshotIndex, TruncationInfo, MAX_UNACKED_DELTA_CHUNKS,
};
use std::time::{Duration, Instant};

const PERF_SAMPLES: usize = 40;
const FIXTURE_BLOCKS: usize = 100;
const FIXTURE_BLOCK_BYTES: usize = 1024;
const INDEX_P95_BUDGET: Duration = Duration::from_millis(50);
const PIPELINE_P95_BUDGET: Duration = Duration::from_millis(500);
const SOAK_REVISIONS: u64 = 10_000;

fn snapshot(revision: u64, changed: Option<usize>) -> PageSnapshot {
    let blocks = (0..FIXTURE_BLOCKS)
        .map(|index| {
            let marker = if changed == Some(index) { 'y' } else { 'x' };
            ContentBlock::Paragraph {
                text: marker.to_string().repeat(FIXTURE_BLOCK_BYTES),
            }
        })
        .collect();
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(
            TabId::new("cnt-perf-tab").unwrap(),
            SessionGeneration::from_raw(1),
        ),
        "https://example.test/perf".to_owned(),
        "CNT deterministic performance fixture".to_owned(),
        revision,
        TruncationInfo::default(),
        blocks,
    )
    .unwrap()
}

fn p95(samples: &mut [Duration]) -> Duration {
    samples.sort_unstable();
    samples[(samples.len() * 95).div_ceil(100).saturating_sub(1)]
}

#[test]
fn cnt_07_100kb_index_delta_markdown_p95_baseline() {
    let old = snapshot(1, None);
    let current = snapshot(2, Some(FIXTURE_BLOCKS / 2));
    let mut index_samples = Vec::with_capacity(PERF_SAMPLES);
    let mut pipeline_samples = Vec::with_capacity(PERF_SAMPLES);
    let mut first_chunk_samples = Vec::with_capacity(PERF_SAMPLES);
    let mut serialized_bytes = 0usize;
    let mut reused_blocks = 0usize;

    for _ in 0..PERF_SAMPLES {
        let index_start = Instant::now();
        let index = SnapshotIndex::build(&current);
        index_samples.push(index_start.elapsed());
        assert_eq!(index.total_positions(), FIXTURE_BLOCKS);

        let pipeline_start = Instant::now();
        let delta = SnapshotDelta::between(&old, &current).unwrap();
        serialized_bytes = delta.serialized_bytes();
        reused_blocks = delta.reused_blocks();
        let mut stream = DeltaStream::new(delta);
        let first_start = Instant::now();
        let first = stream
            .next_chunk(SessionGeneration::from_raw(1), 2)
            .unwrap();
        first_chunk_samples.push(first_start.elapsed());
        stream.acknowledge(first.sequence).unwrap();
        assert!(first.terminal);
        let markdown = render_snapshot(&current).unwrap();
        assert!(markdown.markdown().len() >= 100_000);
        pipeline_samples.push(pipeline_start.elapsed());
    }

    let index_p95 = p95(&mut index_samples);
    let pipeline_p95 = p95(&mut pipeline_samples);
    let first_chunk_p95 = p95(&mut first_chunk_samples);
    let reuse_percent = reused_blocks * 100 / FIXTURE_BLOCKS;
    eprintln!(
        "CNT-07 perf samples={PERF_SAMPLES} index_p95_us={} first_chunk_p95_us={} complete_p95_us={} serialized_bytes={serialized_bytes} reuse_percent={reuse_percent}",
        index_p95.as_micros(),
        first_chunk_p95.as_micros(),
        pipeline_p95.as_micros(),
    );
    assert!(index_p95 <= INDEX_P95_BUDGET, "index P95 {index_p95:?}");
    assert!(
        pipeline_p95 <= PIPELINE_P95_BUDGET,
        "pipeline P95 {pipeline_p95:?}"
    );
    assert!(reuse_percent >= 99);
}

#[test]
fn cnt_07_ten_thousand_revision_soak_stays_bounded() {
    let mut previous = snapshot(1, None);
    let mut total_chunks = 0u64;
    for revision in 2..=SOAK_REVISIONS + 1 {
        let current = snapshot(revision, Some(revision as usize % FIXTURE_BLOCKS));
        let delta = SnapshotDelta::between(&previous, &current).unwrap();
        let mut stream = DeltaStream::new(delta);
        loop {
            assert!(stream.unacked_chunks() <= MAX_UNACKED_DELTA_CHUNKS);
            let chunk = stream
                .next_chunk(SessionGeneration::from_raw(1), revision)
                .unwrap();
            total_chunks = total_chunks.saturating_add(1);
            stream.acknowledge(chunk.sequence).unwrap();
            if chunk.terminal {
                assert_eq!(
                    stream.next_chunk(SessionGeneration::from_raw(1), revision),
                    Err(DeltaError::Complete)
                );
                break;
            }
        }
        previous = current;
    }
    assert!((SOAK_REVISIONS..=SOAK_REVISIONS.saturating_mul(2)).contains(&total_chunks));
}
