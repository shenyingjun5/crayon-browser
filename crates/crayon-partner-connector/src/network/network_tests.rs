//! HB-012 coverage: allowlist, DNS re-validation SSRF guards, https-only
//! redirects, hop budget and response size budgets.

use super::{
    EndpointAllowlist, ExchangePort, ExchangeRequest, ExchangeResponse, PolicyNetworkPort,
    ResolvedAddress, ResolverPort,
};
use crate::api::{ConnectorCall, ConnectorError, NetworkPort};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

fn v4(octets: [u8; 4]) -> ResolvedAddress {
    ResolvedAddress::new(IpAddr::V4(Ipv4Addr::from(octets)))
}

fn v6(segments: [u16; 8]) -> ResolvedAddress {
    ResolvedAddress::new(IpAddr::V6(Ipv6Addr::new(
        segments[0],
        segments[1],
        segments[2],
        segments[3],
        segments[4],
        segments[5],
        segments[6],
        segments[7],
    )))
}

struct FakeResolver {
    addresses: Vec<ResolvedAddress>,
}

impl ResolverPort for FakeResolver {
    fn resolve(&mut self, _host: &str) -> Result<Vec<ResolvedAddress>, ConnectorError> {
        Ok(self.addresses.clone())
    }
}

struct FakeExchange {
    responses: Vec<ExchangeResponse>,
    calls: Vec<String>,
}

impl ExchangePort for FakeExchange {
    fn exchange(
        &mut self,
        request: &ExchangeRequest<'_>,
    ) -> Result<ExchangeResponse, ConnectorError> {
        self.calls.push(format!("{}{}", request.host, request.path));
        self.responses.pop().ok_or(ConnectorError::InvalidName)
    }
}

fn port(
    addresses: Vec<ResolvedAddress>,
    responses: Vec<ExchangeResponse>,
) -> PolicyNetworkPort<FakeResolver, FakeExchange> {
    let mut allowlist = EndpointAllowlist::new();
    allowlist.register("api.partner.example");
    PolicyNetworkPort::new(
        allowlist,
        FakeResolver { addresses },
        FakeExchange {
            responses,
            calls: Vec::new(),
        },
    )
}

fn call() -> ConnectorCall<'static> {
    ConnectorCall::new("api.partner.example", "{}").expect("call")
}

fn ok_response(body: &str) -> ExchangeResponse {
    ExchangeResponse {
        body: body.to_owned(),
        redirect: None,
        truncated: false,
    }
}

#[test]
fn public_resolution_and_response_pass() {
    let mut port = port(vec![v4([93, 184, 216, 34])], vec![ok_response("{}")]);
    assert_eq!(port.send(&call()), Ok("{}".to_owned()));
}

#[test]
fn allowlist_rejects_unregistered_endpoints() {
    let mut port = port(vec![v4([93, 184, 216, 34])], vec![]);
    let other = ConnectorCall::new("evil.partner.example", "{}").expect("call");
    assert_eq!(port.send(&other), Err(ConnectorError::TargetForbidden));
}

#[test]
fn ssrf_guard_rejects_private_address_classes() {
    for blocked in [
        v4([127, 0, 0, 1]),                          // loopback
        v4([10, 0, 0, 5]),                           // private
        v4([192, 168, 1, 1]),                        // private
        v4([172, 16, 0, 9]),                         // private
        v4([169, 254, 169, 254]),                    // cloud metadata
        v4([0, 0, 0, 0]),                            // unspecified
        v6([0, 0, 0, 0, 0, 0, 0, 1]),                // ::1 loopback
        v6([0xfc00, 0, 0, 0, 0, 0, 0, 1]),           // unique local
        v6([0, 0, 0, 0, 0, 0xffff, 0x7f00, 0x0001]), // v4-mapped loopback
    ] {
        let mut port = port(vec![blocked], vec![ok_response("{}")]);
        assert_eq!(
            port.send(&call()),
            Err(ConnectorError::TargetForbidden),
            "blocked address must fail: {blocked:?}"
        );
    }
}

#[test]
fn one_private_among_public_still_rejects_rebinding() {
    let mut port = port(
        vec![v4([93, 184, 216, 34]), v4([10, 0, 0, 1])],
        vec![ok_response("{}")],
    );
    assert_eq!(port.send(&call()), Err(ConnectorError::TargetForbidden));
}

#[test]
fn redirect_stays_within_registered_host_and_budget() {
    let mut port = port(
        vec![v4([93, 184, 216, 34])],
        vec![
            ExchangeResponse {
                body: String::new(),
                redirect: Some("https://api.partner.example/v2".to_owned()),
                truncated: false,
            },
            ok_response("final"),
        ],
    );
    assert_eq!(port.send(&call()), Ok("final".to_owned()));
}

#[test]
fn redirect_escape_is_rejected() {
    let mut port = port(
        vec![v4([93, 184, 216, 34])],
        vec![ExchangeResponse {
            body: String::new(),
            redirect: Some("https://evil.example/steal".to_owned()),
            truncated: false,
        }],
    );
    assert_eq!(port.send(&call()), Err(ConnectorError::TargetForbidden));
}

#[test]
fn redirect_budget_exhaustion_fails_closed() {
    let redirect = || ExchangeResponse {
        body: String::new(),
        redirect: Some("https://api.partner.example/next".to_owned()),
        truncated: false,
    };
    let mut port = port(
        vec![v4([93, 184, 216, 34])],
        vec![redirect(), redirect(), redirect(), redirect(), redirect()],
    );
    assert_eq!(port.send(&call()), Err(ConnectorError::TargetForbidden));
}

#[test]
fn response_over_budget_fails_closed() {
    let mut port = port(
        vec![v4([93, 184, 216, 34])],
        vec![ExchangeResponse {
            body: "x".repeat(super::MAX_RESPONSE_BYTES + 1),
            redirect: None,
            truncated: false,
        }],
    );
    assert_eq!(port.send(&call()), Err(ConnectorError::PayloadTooLarge));
}

#[test]
fn truncated_response_fails_closed() {
    let mut port = port(
        vec![v4([93, 184, 216, 34])],
        vec![ExchangeResponse {
            body: String::new(),
            redirect: None,
            truncated: true,
        }],
    );
    assert_eq!(port.send(&call()), Err(ConnectorError::PayloadTooLarge));
}
