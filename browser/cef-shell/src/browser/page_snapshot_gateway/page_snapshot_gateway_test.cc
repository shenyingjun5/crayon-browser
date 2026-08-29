#include "browser/page_snapshot_gateway/page_snapshot_gateway.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {

using crayon::browser_engine::BrowserUrl;
using crayon::browser_engine::EngineErrorCode;
using crayon::browser_engine::NavigationId;
using crayon::browser_engine::SnapshotChunk;
using crayon::browser_engine::SnapshotFact;
using crayon::browser_engine::SnapshotFactKind;
using crayon::browser_engine::SnapshotMode;
using crayon::browser_engine::SnapshotRequest;
using crayon::browser_engine::SnapshotRequestId;
using crayon::browser_engine::SnapshotTerminal;
using crayon::browser_engine::SnapshotTerminalStatus;
using crayon::browser_engine::TabId;
using crayon::cef_shell::gateway::IpcSourceKind;
using crayon::cef_shell::gateway::PageSnapshotGateway;
using crayon::cef_shell::gateway::RendererSource;
using crayon::cef_shell::gateway::SnapshotGatewayResult;

void Check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Id>
Id MakeId(const std::string& value) {
  auto id = Id::TryCreate(value);
  Check(id.has_value(), "test ID must be valid");
  return std::move(*id);
}

SnapshotRequest MakeRequest(const std::string& id,
                            std::uint64_t navigation = 5) {
  return SnapshotRequest{
      MakeId<SnapshotRequestId>(id), MakeId<TabId>("tab-gateway"),
      NavigationId::FromRaw(navigation), SnapshotMode::kStandard};
}

SnapshotFact Paragraph(const std::string& text = "visible") {
  SnapshotFact fact;
  fact.kind = SnapshotFactKind::kParagraph;
  fact.text = text;
  return fact;
}

BrowserUrl ExpectedUrl() {
  auto url = BrowserUrl::TryParse("https://example.test/article");
  Check(url.has_value(), "expected URL must be valid");
  return std::move(*url);
}

SnapshotChunk Chunk(const SnapshotRequest& request, std::uint32_t sequence) {
  return SnapshotChunk{
      request.request_id,
      request.tab_id,
      request.navigation_id,
      sequence,
      sequence == 0
          ? std::optional<
                crayon::browser_engine::
                    SnapshotDocumentMetadata>{crayon::browser_engine::
                                                  SnapshotDocumentMetadata{
                                                      ExpectedUrl(), "Article"}}
          : std::nullopt,
      {Paragraph()}};
}

RendererSource TrustedSource() {
  return RendererSource{IpcSourceKind::kRenderer, 17, 23, true};
}

void TestSourceNavigationAndSequenceValidation() {
  PageSnapshotGateway gateway;
  const auto request = MakeRequest("gateway-validation");
  Check(gateway.BeginRequest(request, TrustedSource(), ExpectedUrl()) ==
            SnapshotGatewayResult::kAccepted,
        "trusted Browser request must start");

  auto page_source = TrustedSource();
  page_source.kind = IpcSourceKind::kPage;
  Check(gateway.SubmitChunk(page_source, Chunk(request, 0)) ==
            SnapshotGatewayResult::kRejectedSource,
        "page-forged source must fail");
  auto wrong_process = TrustedSource();
  wrong_process.process_id = 18;
  Check(gateway.SubmitChunk(wrong_process, Chunk(request, 0)) ==
            SnapshotGatewayResult::kRejectedSource,
        "wrong renderer process must fail");
  auto stale = Chunk(request, 0);
  stale.navigation_id = NavigationId::FromRaw(4);
  Check(gateway.SubmitChunk(TrustedSource(), std::move(stale)) ==
            SnapshotGatewayResult::kRejectedStaleNavigation,
        "old navigation must fail");
  auto wrong_document = Chunk(request, 0);
  auto other_url = BrowserUrl::TryParse("https://other.test/article");
  Check(other_url.has_value(), "other URL must be valid");
  wrong_document.document->url = std::move(*other_url);
  Check(gateway.SubmitChunk(TrustedSource(), std::move(wrong_document)) ==
            SnapshotGatewayResult::kRejectedStaleNavigation,
        "renderer document URL must match Browser navigation");
  Check(gateway.SubmitChunk(TrustedSource(), Chunk(request, 1)) ==
            SnapshotGatewayResult::kRejectedSequence,
        "out-of-order first chunk must fail");
  Check(gateway.SubmitChunk(TrustedSource(), Chunk(request, 0)) ==
            SnapshotGatewayResult::kAccepted,
        "exact source and sequence must pass");
  Check(gateway.SubmitChunk(TrustedSource(), Chunk(request, 0)) ==
            SnapshotGatewayResult::kRejectedSequence,
        "duplicate sequence must fail");

  const SnapshotTerminal completed{
      request.request_id, request.tab_id, request.navigation_id,
      SnapshotTerminalStatus::kCompleted, EngineErrorCode::kNone};
  Check(gateway.SubmitTerminal(TrustedSource(), completed) ==
            SnapshotGatewayResult::kAccepted,
        "valid terminal must pass");
  Check(gateway.SubmitTerminal(TrustedSource(), completed) ==
            SnapshotGatewayResult::kRejectedNotFound,
        "message after terminal must fail");
  const auto events = gateway.Drain(8);
  Check(events.size() == 2 &&
            std::holds_alternative<SnapshotChunk>(events[0]) &&
            std::holds_alternative<SnapshotTerminal>(events[1]),
        "verified stream must drain in order");
  const auto stats = gateway.stats();
  Check(stats.rejected_source_total == 2 && stats.rejected_stale_total == 2 &&
            stats.rejected_sequence_total == 2,
        "rejection counters must be deterministic");
}

void TestCancelNavigationAndBackpressure() {
  PageSnapshotGateway gateway;
  const auto cancel = MakeRequest("gateway-cancel");
  Check(gateway.BeginRequest(cancel, TrustedSource(), ExpectedUrl()) ==
            SnapshotGatewayResult::kAccepted,
        "cancel request must start");
  Check(gateway.SubmitChunk(TrustedSource(), Chunk(cancel, 0)) ==
            SnapshotGatewayResult::kAccepted,
        "cancel request chunk must pass");
  Check(gateway.Cancel(cancel.request_id) == SnapshotGatewayResult::kAccepted,
        "cancel must pass");
  Check(gateway.Cancel(cancel.request_id) == SnapshotGatewayResult::kIdempotent,
        "cancel must be idempotent");
  auto events = gateway.Drain(8);
  Check(
      events.size() == 1 && std::get<SnapshotTerminal>(events.front()).status ==
                                SnapshotTerminalStatus::kCancelled,
      "cancel must discard chunks and retain one terminal");

  const auto stale = MakeRequest("gateway-navigation", 5);
  Check(gateway.BeginRequest(stale, TrustedSource(), ExpectedUrl()) ==
            SnapshotGatewayResult::kAccepted,
        "navigation request must start");
  Check(gateway.AdvanceNavigation(stale.tab_id, NavigationId::FromRaw(6)) == 1,
        "navigation must cancel old request");
  events = gateway.Drain(8);
  Check(
      events.size() == 1 && std::get<SnapshotTerminal>(events.front()).status ==
                                SnapshotTerminalStatus::kStaleNavigation,
      "navigation must emit stale terminal");

  const auto pressure = MakeRequest("gateway-pressure", 6);
  Check(gateway.BeginRequest(pressure, TrustedSource(), ExpectedUrl()) ==
            SnapshotGatewayResult::kAccepted,
        "pressure request must start");
  for (std::uint32_t sequence = 0;
       sequence < crayon::cef_shell::gateway::kMaxQueuedSnapshotEvents - 1;
       ++sequence) {
    Check(gateway.SubmitChunk(TrustedSource(), Chunk(pressure, sequence)) ==
              SnapshotGatewayResult::kAccepted,
          "reserved terminal budget must admit bounded chunks");
  }
  Check(gateway.SubmitChunk(
            TrustedSource(),
            Chunk(pressure,
                  crayon::cef_shell::gateway::kMaxQueuedSnapshotEvents - 1)) ==
            SnapshotGatewayResult::kRejectedBackpressure,
        "full queue must reject without blocking");
  Check(gateway.Cancel(pressure.request_id) == SnapshotGatewayResult::kAccepted,
        "cancel under pressure must retain terminal capacity");
  events = gateway.Drain(32);
  Check(events.size() == 1 &&
            std::holds_alternative<SnapshotTerminal>(events.front()),
        "cancel under pressure must clear partial stream");
  Check(gateway.stats().dropped_backpressure_total == 1,
        "backpressure counter must increment");
}

void TestMalformedAndShutdown() {
  PageSnapshotGateway gateway;
  const auto request = MakeRequest("gateway-malformed");
  Check(gateway.BeginRequest(request, TrustedSource(), ExpectedUrl()) ==
            SnapshotGatewayResult::kAccepted,
        "malformed request setup must pass");
  Check(gateway.SubmitTerminal(
            TrustedSource(),
            SnapshotTerminal{
                request.request_id, request.tab_id, request.navigation_id,
                SnapshotTerminalStatus::kCompleted, EngineErrorCode::kNone}) ==
            SnapshotGatewayResult::kRejectedInvalid,
        "completed stream without document metadata must fail");
  auto malformed = Chunk(request, 0);
  malformed.facts.front().text.clear();
  Check(gateway.SubmitChunk(TrustedSource(), std::move(malformed)) ==
            SnapshotGatewayResult::kRejectedInvalid,
        "malformed fact must fail");
  gateway.ShutDown();
  Check(gateway.stats().active_requests == 0 &&
            gateway.stats().queued_events == 0,
        "shutdown must release state");
  Check(gateway.BeginRequest(MakeRequest("gateway-after-shutdown"),
                             TrustedSource(), ExpectedUrl()) ==
            SnapshotGatewayResult::kRejectedInvalid,
        "shutdown must fence new request");

  PageSnapshotGateway retirement;
  for (std::size_t index = 0;
       index < crayon::cef_shell::gateway::kMaxRetiredSnapshotRequests + 1;
       ++index) {
    const auto bounded = MakeRequest("retired-" + std::to_string(index), 9);
    Check(retirement.BeginRequest(bounded, TrustedSource(), ExpectedUrl()) ==
              SnapshotGatewayResult::kAccepted,
          "bounded retirement request must start");
    Check(retirement.Cancel(bounded.request_id) ==
              SnapshotGatewayResult::kAccepted,
          "bounded retirement request must cancel");
    Check(retirement.Drain(1).size() == 1,
          "bounded retirement terminal must drain");
  }
  Check(retirement.BeginRequest(MakeRequest("retired-0", 9), TrustedSource(),
                                ExpectedUrl()) ==
            SnapshotGatewayResult::kAccepted,
        "retired request memory must stay bounded");
}

}  // namespace

int main() {
  try {
    TestSourceNavigationAndSequenceValidation();
    TestCancelNavigationAndBackpressure();
    TestMalformedAndShutdown();
    std::cout << "page_snapshot_gateway_test: passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "page_snapshot_gateway_test: " << error.what() << '\n';
    return 1;
  }
}
