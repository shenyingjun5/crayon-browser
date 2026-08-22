//! AGT-12A transport guard tests: framing matrix, single-client
//! admission, rate limiting, replay rejection, strikes and stop.

use super::*;

fn header(len: usize) -> Vec<u8> {
    (len as u32).to_be_bytes().to_vec()
}

#[test]
fn frame_codec_round_trip() {
    let frame = FrameCodec::encode(b"hello");
    let mut codec = FrameCodec::new();
    assert_eq!(
        codec.feed(&frame).unwrap(),
        DecodedFrame::Complete(b"hello".to_vec())
    );
    assert_eq!(codec.take().unwrap(), DecodedFrame::Incomplete);
}

#[test]
fn frame_codec_partial_chunks() {
    let frame = FrameCodec::encode(&[7u8; 100]);
    let mut codec = FrameCodec::new();
    assert_eq!(codec.feed(&frame[..3]).unwrap(), DecodedFrame::Incomplete);
    assert_eq!(codec.feed(&frame[3..50]).unwrap(), DecodedFrame::Incomplete);
    assert_eq!(
        codec.feed(&frame[50..]).unwrap(),
        DecodedFrame::Complete(vec![7u8; 100])
    );
    assert_eq!(codec.pending_bytes(), 0);
}

#[test]
fn frame_codec_oversize_and_back_to_back() {
    let mut codec = FrameCodec::new();
    let mut oversize = header(MAX_FRAME_BYTES + 1);
    oversize.extend_from_slice(&[0u8; 16]);
    assert_eq!(
        codec.feed(&oversize).unwrap(),
        DecodedFrame::Oversize {
            declared: (MAX_FRAME_BYTES + 1) as u32
        }
    );
    // Back-to-back legal frames after one complete decode.
    let mut stream = FrameCodec::encode(b"a");
    stream.extend(FrameCodec::encode(b"bb"));
    let mut codec2 = FrameCodec::new();
    assert_eq!(
        codec2.feed(&stream).unwrap(),
        DecodedFrame::Complete(b"a".to_vec())
    );
    assert_eq!(
        codec2.take().unwrap(),
        DecodedFrame::Complete(b"bb".to_vec())
    );
    assert_eq!(codec2.take().unwrap(), DecodedFrame::Incomplete);
}

#[test]
fn frame_codec_max_legal_size_accepted() {
    let frame = FrameCodec::encode(&vec![1u8; MAX_FRAME_BYTES]);
    let mut codec = FrameCodec::new();
    assert_eq!(
        codec.feed(&frame).unwrap(),
        DecodedFrame::Complete(vec![1u8; MAX_FRAME_BYTES])
    );
}

#[test]
fn frame_codec_buffer_bound_fails_closed() {
    let mut codec = FrameCodec::new();
    // Legal incomplete frames can hold at most MAX+3 pending bytes, so
    // the fail-closed bound is reachable only through a single hostile
    // oversized chunk — which must be rejected without buffering.
    let huge = vec![0u8; MAX_FRAME_BYTES * 2 + 1];
    assert_eq!(codec.feed(&huge), Err(TransportError::FrameMalformed));
    assert_eq!(codec.pending_bytes(), 0);
}

#[test]
fn single_client_admission_matrix() {
    let mut guard = TransportGuard::new();
    assert_eq!(guard.bind_client("cli-dev"), Ok(()));
    assert_eq!(guard.bind_client("cli-dev"), Ok(())); // idempotent rebind
    assert_eq!(
        guard.bind_client("mcp-dev"),
        Err(TransportError::ClientBound)
    );
    guard.disconnect();
    assert_eq!(guard.bound_client(), None);
    assert_eq!(guard.bind_client("mcp-dev"), Ok(()));
}

#[test]
fn rate_limit_burst_and_refill() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    // Drain the burst inside one window (no refill elapses).
    for _ in 0..RATE_BURST {
        assert_eq!(guard.admit_rate(0), Ok(()));
    }
    assert_eq!(guard.admit_rate(0), Err(TransportError::RateLimited));
    assert_eq!(
        guard.admit_rate(RATE_INTERVAL_MS - 1),
        Err(TransportError::RateLimited)
    );
    // Refill after one interval passes.
    assert_eq!(
        guard.admit_rate((RATE_BURST as u64) * RATE_INTERVAL_MS + RATE_INTERVAL_MS),
        Ok(())
    );
    // Unbound client is stopped.
    guard.disconnect();
    assert_eq!(guard.admit_rate(0), Err(TransportError::Stopped));
}

#[test]
fn request_id_replay_rejected_with_bounded_window() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    assert_eq!(guard.admit_request_id(1), Ok(()));
    assert_eq!(guard.admit_request_id(1), Err(TransportError::Replayed));
    for id in 2..(MAX_SEEN_IDS as u64 + 10) {
        assert_eq!(guard.admit_request_id(id), Ok(()));
    }
    // The window slid: id 1 was evicted and is accepted again (bounded
    // memory takes precedence over unbounded replay memory; session
    // idempotency provides the stronger semantic dedupe).
    assert_eq!(guard.admit_request_id(1), Ok(()));
    guard.disconnect();
    assert_eq!(guard.admit_request_id(9), Err(TransportError::Stopped));
}

#[test]
fn strikes_drop_client_at_threshold() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    for _ in 0..MAX_STRIKES - 1 {
        assert_eq!(guard.strike(), Ok(()));
    }
    assert_eq!(guard.strike(), Err(TransportError::StrikesExceeded));
    assert_eq!(guard.bound_client(), None);
    assert_eq!(guard.strikes(), 0);
    // State fully reset: a new client binds cleanly.
    assert_eq!(guard.bind_client("next"), Ok(()));
}

#[test]
fn stop_is_idempotent_and_releases() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    guard.stop();
    guard.stop();
    assert_eq!(guard.bound_client(), None);
    assert_eq!(guard.bind_client("other"), Ok(()));
}

#[test]
fn error_display_and_caap_mapping_golden() {
    use crayon_domain::CaapError;
    let cases: &[(TransportError, &str, CaapError)] = &[
        (
            TransportError::FrameTooLarge,
            "frame exceeds size limit",
            CaapError::InvalidMessage,
        ),
        (
            TransportError::FrameMalformed,
            "frame malformed",
            CaapError::InvalidMessage,
        ),
        (
            TransportError::ClientBound,
            "another client already holds the transport",
            CaapError::Unauthorized,
        ),
        (
            TransportError::RateLimited,
            "rate limit exceeded",
            CaapError::QueueFull,
        ),
        (
            TransportError::Replayed,
            "request id replayed",
            CaapError::InvalidMessage,
        ),
        (
            TransportError::StrikesExceeded,
            "too many protocol violations",
            CaapError::Unauthorized,
        ),
        (
            TransportError::Stopped,
            "transport stopped",
            CaapError::Unauthorized,
        ),
    ];
    for (error, message, caap) in cases {
        assert_eq!(error.to_string(), *message);
        assert_eq!(error.to_caap_error(), *caap);
    }
}

/// Deterministic pseudo-random sequence (LCG): a hostile client mix of
/// oversize, malformed and replay frames must never panic, never exceed
/// the strike bound without a drop, and always keep bounded state.
#[test]
fn lcg_hostile_stream_invariants() {
    let mut state: u64 = 0x9E37_79B9_7F4A_7C15;
    let mut next = move || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };

    let mut guard = TransportGuard::new();
    guard.bind_client("hostile").unwrap();
    let mut codec = FrameCodec::new();
    let mut clock = 0_u64;
    let mut next_id = 1_u64;
    for step in 0..3_000_u64 {
        clock += 3;
        let choice = next() % 10;
        if choice < 4 {
            // Well-formed frame with a fresh request id.
            let payload = vec![step as u8; (next() % 64) as usize];
            let frame = FrameCodec::encode(&payload);
            let decoded = codec.feed(&frame).unwrap();
            assert!(matches!(decoded, DecodedFrame::Complete(_)));
            if guard.admit_rate(clock).is_ok() {
                assert_eq!(guard.admit_request_id(next_id), Ok(()));
                next_id += 1;
            }
        } else if choice < 6 {
            // Oversize declaration: a strike, never a panic.
            let _ = codec.feed(&header(MAX_FRAME_BYTES + 1 + (next() % 4) as usize));
            if guard.strike().is_err() {
                guard.bind_client("hostile").unwrap();
            }
        } else if choice < 8 {
            // Replay of an old id.
            if next_id > 2 && guard.admit_rate(clock).is_ok() {
                let _ = guard.admit_request_id(next_id - 1);
            }
        } else {
            // Idle tick lets the bucket refill.
            clock += RATE_INTERVAL_MS;
        }
        assert!(guard.strikes() < MAX_STRIKES || guard.bound_client().is_none());
        assert!(codec.pending_bytes() <= MAX_FRAME_BYTES * 2);
    }
}
