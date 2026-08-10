//! Delivery orchestration (MED-17, Mirror semantics migrated by MED-19):
//! turns the policy decision into a concrete receiver-facing plan.
//!
//! Rules:
//! - ordinary planning failures reject plainly — they never upgrade
//!   privileges and never auto-escalate into another mode (PL-014);
//! - a runtime start failure may downgrade Direct/Relay to an external
//!   client handoff suggestion exactly once (design §9.2 step 7); there is
//!   no cyclic fallback — a handoff or rejection ends the chain;
//! - DASH relay serving is out of v1 scope: a Relay decision for a DASH
//!   candidate degrades structurally to a handoff suggestion (documented v1
//!   limit);
//! - a handoff suggestion is not a cast mode: it creates no receiver
//!   handle, relay token, capturer, encoder or WebRTC transport and must
//!   pass explicit user confirmation before any download/launch (PL-015).

use crayon_cast_policy::{decide, HandoffAvailability, PolicyContext};
use crayon_domain::{CoreError, DeviceId};
use crayon_ipc_schema::{
    CastPolicyDecision, CastPolicyInput, ExternalClientHandoff, HandoffReason, HeadersClass,
    ProtocolKind,
};
use crayon_media_observer::PlaybackObservation;
use crayon_media_probe::Protection;
use std::net::IpAddr;

/// Product desktop UA used when the upstream requires one (RefererAndUa).
const PRODUCT_UA: &str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

/// Everything the planner needs for one cast attempt.
#[derive(Clone, Debug)]
pub struct DeliveryRequest {
    pub input: CastPolicyInput,
    pub observation: PlaybackObservation,
    pub protection: Protection,
    /// Declared external-client handoff capability of the platform (PL-011).
    pub external_client_handoff: HandoffAvailability,
    pub receiver: DeviceId,
    pub receiver_ip: Option<IpAddr>,
}

/// The receiver-facing plan.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum DeliveryPlan {
    /// Receiver pulls the original URL directly (no special headers).
    Direct { url: String },
    /// Receiver pulls through the session relay (opaque URL).
    Relay { media_url: String },
    /// External-client handoff suggestion (MED-19). Pure advice: holds no
    /// media URL, relay token or receiver session; user confirmation is
    /// required and it never means "casting started" (PL-015).
    ExternalClientHandoff(ExternalClientHandoff),
    /// Stable rejection.
    Rejected(CoreError),
}

/// Session backend seam: opens a relay session for a candidate and returns
/// the receiver-facing media URL.
pub trait SessionBackend {
    fn open(
        &mut self,
        receiver: &DeviceId,
        receiver_ip: Option<IpAddr>,
        candidate_url: &str,
        protocol: ProtocolKind,
        headers_class: HeadersClass,
        page_url: &str,
    ) -> Result<String, CoreError>;
}

/// Plans one cast attempt. Pure orchestration over `decide` plus the
/// session backend; no side effects beyond the backend call for Relay.
pub fn plan_delivery(request: &DeliveryRequest, backend: &mut dyn SessionBackend) -> DeliveryPlan {
    let context = PolicyContext {
        observation: request.observation,
        protection: request.protection,
        external_client_handoff: request.external_client_handoff,
    };
    let decision = decide(&request.input, &context);
    let candidate = request.input.candidate();
    match decision {
        CastPolicyDecision::Reject { reason } => DeliveryPlan::Rejected(reason),
        CastPolicyDecision::ExternalClientHandoff(handoff) => {
            DeliveryPlan::ExternalClientHandoff(handoff)
        }
        CastPolicyDecision::Direct => DeliveryPlan::Direct {
            url: candidate.url().to_string(),
        },
        CastPolicyDecision::Relay => {
            // DASH relay 服务不在 v1：结构化降级为外部交接建议（非运行时失败降级）；
            // 平台无交接能力时按 PL-011 稳定拒绝。
            if candidate.protocol() == ProtocolKind::Dash {
                return structural_handoff(
                    HandoffReason::DashRelayUnsupported,
                    request.external_client_handoff,
                );
            }
            match backend.open(
                &request.receiver,
                request.receiver_ip,
                candidate.url(),
                candidate.protocol(),
                candidate.headers_class(),
                request.input.page().url(),
            ) {
                Ok(media_url) => DeliveryPlan::Relay { media_url },
                // PL-014：规划期普通失败直接拒绝，不提权不降级。
                Err(error) => DeliveryPlan::Rejected(error),
            }
        }
    }
}

/// Structural degrade to a handoff suggestion, or a stable capability
/// rejection when the platform declares no handoff surface (PL-011).
fn structural_handoff(reason: HandoffReason, availability: HandoffAvailability) -> DeliveryPlan {
    match availability {
        HandoffAvailability::Available => {
            DeliveryPlan::ExternalClientHandoff(ExternalClientHandoff::new(reason))
        }
        HandoffAvailability::Unavailable => {
            DeliveryPlan::Rejected(CoreError::CapabilitiesUnavailable)
        }
    }
}

/// Runtime start outcome reported by the playback layer.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StartOutcome {
    Started,
    Failed,
}

/// Single-step downgrade (design §9.2 step 7): a failed Direct/Relay start
/// may become an external-client handoff suggestion exactly once.
/// `already_downgraded` prevents cycles; handoff/rejected plans never
/// downgrade further. When the platform declares no handoff capability the
/// chain ends without a suggestion (PL-011) — a failed start never creates
/// or pollutes a cast session.
#[must_use]
pub fn downgrade_once(
    plan: &DeliveryPlan,
    start: StartOutcome,
    already_downgraded: bool,
    handoff: HandoffAvailability,
) -> Option<DeliveryPlan> {
    match (plan, start, already_downgraded, handoff) {
        (
            DeliveryPlan::Direct { .. } | DeliveryPlan::Relay { .. },
            StartOutcome::Failed,
            false,
            HandoffAvailability::Available,
        ) => Some(DeliveryPlan::ExternalClientHandoff(
            ExternalClientHandoff::new(HandoffReason::StartFailed),
        )),
        _ => None,
    }
}

/// Builds the scoped headers a relay recipe carries for this candidate:
/// Referer from the page origin and/or the product UA, per headers class.
#[must_use]
pub fn scoped_headers_for(headers_class: HeadersClass, page_url: &str) -> Vec<(String, String)> {
    let mut headers = Vec::new();
    match headers_class {
        HeadersClass::None | HeadersClass::CredentialBound => {}
        HeadersClass::RefererOnly | HeadersClass::RefererAndUa => {
            if let Ok(parsed) = url::Url::parse(page_url) {
                let origin = format!("{}://{}", parsed.scheme(), parsed.host_str().unwrap_or(""));
                headers.push(("Referer".to_string(), origin));
            }
            if headers_class == HeadersClass::RefererAndUa {
                headers.push(("User-Agent".to_string(), PRODUCT_UA.to_string()));
            }
        }
    }
    headers
}

/// Real backend over `crayon-relay`: opens a session, registers the
/// candidate recipe and returns the opaque media URL.
pub struct CoreSessionBackend {
    core: std::sync::Arc<crayon_relay::router::RelayCore>,
    media_base: String,
}

impl CoreSessionBackend {
    #[must_use]
    pub fn new(core: std::sync::Arc<crayon_relay::router::RelayCore>, media_base: String) -> Self {
        Self { core, media_base }
    }
}

impl SessionBackend for CoreSessionBackend {
    fn open(
        &mut self,
        receiver: &DeviceId,
        receiver_ip: Option<IpAddr>,
        candidate_url: &str,
        protocol: ProtocolKind,
        headers_class: HeadersClass,
        page_url: &str,
    ) -> Result<String, CoreError> {
        let parsed = url::Url::parse(candidate_url).map_err(|_| CoreError::InvalidMessage)?;
        let host = parsed
            .host_str()
            .ok_or(CoreError::InvalidMessage)?
            .to_string();
        let now = (self.core.now)();
        let mut registry = self.core.registry.lock().unwrap();
        let grant = registry
            .create_session(
                receiver.clone(),
                receiver_ip,
                vec![host.clone()],
                crayon_relay::session::DEFAULT_SESSION_TTL_MS,
                now,
            )
            .ok_or(CoreError::CapabilitiesUnavailable)?;

        let (resource_key, path) = match protocol {
            ProtocolKind::Hls => ("master", format!("/s/{}/master.m3u8", grant.token_hex)),
            ProtocolKind::Mp4 => (
                "main",
                format!(
                    "/s/{}/r/main/{}",
                    grant.token_hex,
                    parsed
                        .path_segments()
                        .and_then(|mut s| s.next_back())
                        .unwrap_or("movie.mp4")
                ),
            ),
            // 调用方已将 DASH relay 结构化降级；到这里属协议错误。
            ProtocolKind::Dash => return Err(CoreError::PolicyDenied),
        };
        let resource_id =
            crayon_domain::ResourceId::new(resource_key).map_err(|_| CoreError::InvalidMessage)?;
        registry
            .register_resource(&grant.token_hex, resource_id.clone(), &host, 0)
            .map_err(|_| CoreError::PolicyDenied)?;

        let headers = scoped_headers_for(headers_class, page_url);
        let referer = headers
            .iter()
            .find(|(k, _)| k == "Referer")
            .map(|(_, v)| v.clone());
        let ua = headers
            .iter()
            .find(|(k, _)| k == "User-Agent")
            .map(|(_, v)| v.clone());
        let recipe = crayon_relay::vault::UpstreamRecipe::new(candidate_url, referer, ua)
            .map_err(|_| CoreError::InvalidMessage)?;
        self.core
            .vault
            .lock()
            .unwrap()
            .store(&grant.session_id, resource_id, recipe)
            .map_err(|_| CoreError::CapabilitiesUnavailable)?;
        Ok(format!("{}{}", self.media_base, path))
    }
}
