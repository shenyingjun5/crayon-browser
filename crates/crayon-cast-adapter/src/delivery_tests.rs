//! Crate-internal tests for delivery execution (SDK-09): real-facade
//! fail-closed paths and URL redaction. Fake-driven CS-005 behaviour lives in
//! `tests/delivery.rs`.

use crate::delivery::{deliver, DeliveryRoute, PlannedDelivery};
use crate::dto::{CastMediaUrl, DeliveryProtocol};
use crate::error::CastError;
use crate::service::{SenderCastFacade, SenderCastFacadeConfig};
use crayon_domain::DeviceId;

fn planned(
    device: &str,
    route: DeliveryRoute,
    protocol: DeliveryProtocol,
    url: &str,
) -> PlannedDelivery {
    PlannedDelivery::new(
        DeviceId::new(device).expect("test device id"),
        route,
        protocol,
        CastMediaUrl::new(url).expect("test url"),
    )
}

#[test]
fn real_facade_delivery_without_connection_fails_closed() {
    let facade = SenderCastFacade::new(SenderCastFacadeConfig::default());
    let order = planned(
        "dev-01",
        DeliveryRoute::Direct,
        DeliveryProtocol::Mp4,
        "https://cdn.example/v/film.mp4?sig=example",
    );
    // No connection: the stale-plan guard fires before any receiver traffic.
    assert_eq!(deliver(&facade, &order), Err(CastError::InvalidState));
}

#[test]
fn planned_delivery_debug_redacts_url() {
    let order = planned(
        "dev-01",
        DeliveryRoute::Relay,
        DeliveryProtocol::Hls,
        "http://127.0.0.1:18932/s/0123456789abcdef/master.m3u8",
    );
    let rendered = format!("{order:?}");
    assert!(rendered.contains("REDACTED"));
    assert!(
        !rendered.contains("0123456789abcdef"),
        "relay token path must not leak into Debug output"
    );
}

#[test]
fn planned_delivery_accessors_roundtrip() {
    let order = planned(
        "dev-01",
        DeliveryRoute::Direct,
        DeliveryProtocol::Hls,
        "https://cdn.example/v/master.m3u8",
    );
    assert_eq!(order.device(), &DeviceId::new("dev-01").expect("id"));
    assert_eq!(order.route(), DeliveryRoute::Direct);
    assert_eq!(order.protocol(), DeliveryProtocol::Hls);
    assert_eq!(order.url().as_str(), "https://cdn.example/v/master.m3u8");
}
