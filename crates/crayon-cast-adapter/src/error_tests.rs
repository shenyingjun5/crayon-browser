//! CS-008 error-mapping contract tests against the real pinned-SDK error
//! type. The behavioural table over the public mapping lives in
//! `tests/facade_contract.rs`; this module pins `SenderErrorKind` to the real
//! `cast_sender_core::ErrorKind` (a non-exhaustive match would silently
//! mismap new SDK categories after a revision upgrade).

use super::{CastError, SenderErrorKind};
use cast_sender_core::{CastSenderError, ErrorKind};

/// The SDK-05 service must perform exactly this conversion at the boundary.
/// Keeping the exhaustive match here compile-pins `SenderErrorKind` to the
/// real SDK enum: an added or renamed `ErrorKind` variant breaks this build.
fn kind_of(error: &CastSenderError) -> SenderErrorKind {
    match error.kind {
        ErrorKind::Device => SenderErrorKind::Device,
        ErrorKind::Network => SenderErrorKind::Network,
        ErrorKind::Http => SenderErrorKind::Http,
        ErrorKind::Image => SenderErrorKind::Image,
        ErrorKind::Control => SenderErrorKind::Control,
        ErrorKind::InvalidInput => SenderErrorKind::InvalidInput,
        ErrorKind::State => SenderErrorKind::State,
    }
}

/// Maps a real SDK error through the same two steps the service will use.
fn map(error: &CastSenderError) -> CastError {
    CastError::from_sender_error(kind_of(error), &error.code)
}

#[test]
fn cs_008_real_sdk_errors_map_to_stable_product_codes() {
    let cases: &[(CastSenderError, CastError)] = &[
        (
            CastSenderError::new(ErrorKind::Device, "DEVICE_NOT_FOUND", "ignored message"),
            CastError::DeviceNotFound,
        ),
        (
            CastSenderError::new(
                ErrorKind::Device,
                "CAST_CODE_DEVICE_NOT_FOUND",
                "未找到接收端",
            ),
            CastError::DeviceNotFound,
        ),
        (
            CastSenderError::new(
                ErrorKind::Network,
                "SENDER_DEVICE_ROUTE_EXPIRED",
                "routes expired",
            ),
            CastError::RouteLost,
        ),
        (
            CastSenderError::new(ErrorKind::Network, "NETWORK_ROUTE_LOST", "route lost"),
            CastError::RouteLost,
        ),
        (
            CastSenderError::new(
                ErrorKind::Network,
                "NO_USABLE_LAN_INTERFACE",
                "no interface",
            ),
            CastError::NetworkUnavailable,
        ),
        (
            CastSenderError::new(ErrorKind::Http, "HTTP_FILE_NOT_FOUND", "http"),
            CastError::ReceiverUnreachable,
        ),
        (
            CastSenderError::new(ErrorKind::Image, "IMAGE_UNKNOWN_RECEIVER", "image"),
            CastError::UnsupportedByReceiver,
        ),
        (
            CastSenderError::new(
                ErrorKind::Control,
                "CONTROL_CAST_EXTENSION_MISSING",
                "no extension",
            ),
            CastError::UnsupportedByReceiver,
        ),
        (
            CastSenderError::new(ErrorKind::Control, "CONTROL_AV_TRANSPORT_MISSING", "no avt"),
            CastError::ReceiverProtocol,
        ),
        (
            CastSenderError::invalid_input("bad argument"),
            CastError::InvalidInput,
        ),
        (
            CastSenderError::new(
                ErrorKind::State,
                "CAST_SESSION_STALE_GENERATION",
                "stale handle",
            ),
            CastError::StaleSessionGeneration,
        ),
        (
            CastSenderError::new(ErrorKind::State, "CAST_SESSION_NOT_FOUND", "no session"),
            CastError::NoActiveSession,
        ),
        (
            CastSenderError::new(
                ErrorKind::State,
                "CAST_SESSION_START_FAILED",
                "start failed",
            ),
            CastError::CastStartFailed,
        ),
        (
            CastSenderError::state("unexpected state"),
            CastError::InvalidState,
        ),
    ];
    for (sdk_error, expected) in cases {
        assert_eq!(map(sdk_error), *expected, "code {}", sdk_error.code);
    }
}

#[test]
fn cs_008_mapping_never_reads_the_natural_language_message() {
    // Same kind + stable code, opposite messages: the product error must be
    // identical, proving the mapping cannot depend on `message` content.
    let chinese = CastSenderError::new(
        ErrorKind::Device,
        "DEVICE_NOT_FOUND",
        "未找到接收端，请确认在同一网络",
    );
    let english = CastSenderError::new(ErrorKind::Device, "DEVICE_NOT_FOUND", "device not found");
    assert_eq!(map(&chinese), map(&english));
}

#[test]
fn cs_008_unknown_codes_degrade_to_the_category_default() {
    // A future SDK revision may add codes; the mapping stays total and
    // degrades inside the same category instead of panicking or guessing.
    let unknown = CastSenderError::new(ErrorKind::Control, "SOME_FUTURE_CODE", "future");
    assert_eq!(map(&unknown), CastError::ReceiverProtocol);
}

#[test]
fn cs_008_every_sdk_error_kind_is_covered() {
    // Constructing one error per real SDK kind exercises `kind_of` for the
    // whole pinned enum; combined with the exhaustive match above this fails
    // the build if the SDK adds a category.
    for kind in [
        ErrorKind::Device,
        ErrorKind::Network,
        ErrorKind::Http,
        ErrorKind::Image,
        ErrorKind::Control,
        ErrorKind::InvalidInput,
        ErrorKind::State,
    ] {
        let error = CastSenderError::new(kind, "PROBE", "probe");
        let _ = map(&error);
    }
}
