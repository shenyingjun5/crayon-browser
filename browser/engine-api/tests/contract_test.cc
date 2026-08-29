#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "crayon/browser_engine/browser_engine.h"
#include "fake_browser_engine.h"

namespace {

using crayon::browser_engine::BrowserUrl;
using crayon::browser_engine::CommandResult;
using crayon::browser_engine::EngineErrorCode;
using crayon::browser_engine::EngineEventSink;
using crayon::browser_engine::NavigationEvent;
using crayon::browser_engine::NavigationEventKind;
using crayon::browser_engine::NavigationId;
using crayon::browser_engine::NavigationRequest;
using crayon::browser_engine::ObservationEvent;
using crayon::browser_engine::ObservationKind;
using crayon::browser_engine::ObservationSubscription;
using crayon::browser_engine::ObservationTopic;
using crayon::browser_engine::PermissionDecision;
using crayon::browser_engine::PermissionKind;
using crayon::browser_engine::PermissionRequest;
using crayon::browser_engine::PermissionRequestId;
using crayon::browser_engine::PermissionResolution;
using crayon::browser_engine::ProfileConfig;
using crayon::browser_engine::ProfileEvent;
using crayon::browser_engine::ProfileEventKind;
using crayon::browser_engine::ProfileId;
using crayon::browser_engine::ProfileMode;
using crayon::browser_engine::SnapshotChunk;
using crayon::browser_engine::SnapshotDocumentMetadata;
using crayon::browser_engine::SnapshotFact;
using crayon::browser_engine::SnapshotFactKind;
using crayon::browser_engine::SnapshotMode;
using crayon::browser_engine::SnapshotRequest;
using crayon::browser_engine::SnapshotRequestId;
using crayon::browser_engine::SnapshotStreamSink;
using crayon::browser_engine::SnapshotTerminal;
using crayon::browser_engine::SnapshotTerminalStatus;
using crayon::browser_engine::SubscriptionId;
using crayon::browser_engine::TabCreateRequest;
using crayon::browser_engine::TabEvent;
using crayon::browser_engine::TabEventKind;
using crayon::browser_engine::TabId;
using crayon::browser_engine::ToStableCode;
using crayon::browser_engine::TrustedInputFact;
using crayon::browser_engine::TrustedInputKind;
using crayon::browser_engine::ZoomFactor;
using crayon::browser_engine::testing::FakeBrowserEngine;

void Check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Id>
Id MakeId(const char* value) {
  auto id = Id::TryCreate(value);
  Check(id.has_value(), "test ID must be valid");
  return std::move(*id);
}

BrowserUrl MakeUrl(const char* value) {
  auto url = BrowserUrl::TryParse(value);
  Check(url.has_value(), "test URL must be valid");
  return std::move(*url);
}

class RecordingSink final : public EngineEventSink {
 public:
  void OnProfileEvent(const ProfileEvent& event) override {
    events.push_back(event.kind == ProfileEventKind::kCreated
                         ? "profile.created"
                         : "profile.destroyed");
  }

  void OnTabEvent(const TabEvent& event) override {
    events.push_back(event.kind == TabEventKind::kCreated ? "tab.created"
                                                          : "tab.closed");
  }

  void OnNavigationEvent(const NavigationEvent& event) override {
    if (event.kind == NavigationEventKind::kStarted) {
      events.push_back("navigation.started");
    } else if (event.kind == NavigationEventKind::kCommitted) {
      events.push_back("navigation.committed");
    } else if (event.kind == NavigationEventKind::kCompleted) {
      events.push_back("navigation.completed");
    } else {
      events.push_back("navigation.failed");
    }
  }

  void OnPermissionRequest(const PermissionRequest&) override {
    events.push_back("permission.requested");
  }

  void OnTrustedInput(const TrustedInputFact&) override {
    events.push_back("input.trusted");
  }

  void OnObservation(const ObservationEvent&) override {
    events.push_back("observation");
  }

  std::vector<std::string> events;
};

class RecordingSnapshotSink final : public SnapshotStreamSink {
 public:
  void OnSnapshotChunk(const SnapshotChunk& chunk) override {
    sequences.push_back(chunk.sequence);
  }

  void OnSnapshotTerminal(const SnapshotTerminal& terminal) override {
    terminals.push_back(terminal.status);
  }

  std::vector<std::uint32_t> sequences;
  std::vector<SnapshotTerminalStatus> terminals;
};

void TestStrongTypesAndErrors() {
  Check(!ProfileId::TryCreate("").has_value(), "empty ID must fail");
  Check(!ProfileId::TryCreate("bad id").has_value(), "ID whitespace must fail");
  Check(!ProfileId::TryCreate("../profile").has_value(),
        "ID path characters must fail");
  Check(!ProfileId::TryCreate("\xC3\xA9").has_value(),
        "non-ASCII ID must fail deterministically");
  Check(!BrowserUrl::TryParse("").has_value(), "empty URL must fail");
  Check(!BrowserUrl::TryParse("file:///tmp/data").has_value(),
        "file URL must fail");
  Check(!BrowserUrl::TryParse("https://user@example.test/").has_value(),
        "URL userinfo must fail");
  Check(!BrowserUrl::TryParse("https://example.test/a b").has_value(),
        "URL whitespace must fail");
  Check(!BrowserUrl::TryParse("https://bad_host.test/").has_value(),
        "malformed host must fail");
  Check(!BrowserUrl::TryParse(std::string("https://") + "\xC3\xA9.test/")
             .has_value(),
        "non-ASCII host must fail deterministically");
  Check(!BrowserUrl::TryParse("https://example.test:bad/").has_value(),
        "nonnumeric port must fail");
  Check(!BrowserUrl::TryParse("https://example.test:65536/").has_value(),
        "out-of-range port must fail");
  Check(!BrowserUrl::TryParse("https://[::1]/").has_value(),
        "unsupported IPv6 authority must fail closed");
  Check(BrowserUrl::TryParse("HTTPS://example.test/path").has_value(),
        "HTTPS URL must pass case-insensitively");
  Check(BrowserUrl::TryParse("http://127.0.0.1:8080/path").has_value(),
        "IPv4 URL with valid port must pass");
  Check(!ZoomFactor::TryCreate(0.0).has_value(), "zero zoom must fail");
  Check(!ZoomFactor::TryCreate(std::nan("")).has_value(), "NaN zoom must fail");
  Check(ZoomFactor::TryCreate(1.25).has_value(), "normal zoom must pass");
  Check(std::string(ToStableCode(EngineErrorCode::kStaleNavigation)) ==
            "stale_navigation",
        "error code must be stable");
  Check(CommandResult::Rejected(EngineErrorCode::kNone).error() ==
            EngineErrorCode::kInvalidArgument,
        "rejected result cannot carry none");
}

void TestLifecycleAndEventOrder() {
  FakeBrowserEngine engine;
  RecordingSink sink;
  RecordingSink replacement_sink;
  const auto profile_id = MakeId<ProfileId>("profile-01");
  const auto tab_id = MakeId<TabId>("tab-01");

  Check(engine.CreateProfile(ProfileConfig{profile_id, ProfileMode::kPrivate})
                .error() == EngineErrorCode::kInvalidState,
        "command before start must fail");
  Check(engine.Start(sink).accepted(), "start must pass");
  Check(engine.Start(sink).accepted(), "same sink start must be idempotent");
  Check(
      engine.Start(replacement_sink).error() == EngineErrorCode::kInvalidState,
      "running adapter must reject sink replacement");
  Check(engine.CreateProfile(ProfileConfig{profile_id, ProfileMode::kPrivate})
            .accepted(),
        "profile create must be accepted");
  Check(engine
            .CreateTab(TabCreateRequest{profile_id, tab_id,
                                        MakeUrl("https://example.test/")})
            .accepted(),
        "tab create must be accepted");
  Check(sink.events.empty(), "commands must not synchronously callback");
  Check(engine.DispatchEvents() == 5,
        "expected profile/tab/navigation event count");
  const std::vector<std::string> expected{
      "profile.created", "tab.created", "navigation.started",
      "navigation.committed", "navigation.completed"};
  Check(sink.events == expected, "event order must be deterministic");

  Check(engine.CloseTab(tab_id).accepted(), "tab close must pass");
  Check(engine.CloseTab(tab_id).accepted(), "tab close must be idempotent");
  Check(engine.DestroyProfile(profile_id).accepted(),
        "profile destroy must pass");
  Check(engine.DestroyProfile(profile_id).accepted(),
        "profile destroy must be idempotent");
  Check(engine.DispatchEvents() == 2, "close/destroy must each emit once");
  Check(engine.Stop().accepted(), "stop must pass");
  Check(engine.Stop().accepted(), "stop must be idempotent");
}

void TestInvalidCommandsAndPermission() {
  FakeBrowserEngine engine;
  RecordingSink sink;
  const auto profile_id = MakeId<ProfileId>("profile-invalid");
  const auto missing_profile = MakeId<ProfileId>("profile-missing");
  const auto tab_id = MakeId<TabId>("tab-invalid");
  const auto missing_tab = MakeId<TabId>("tab-missing");
  const auto request_id = MakeId<PermissionRequestId>("permission-01");
  const auto invalid_request_id =
      MakeId<PermissionRequestId>("permission-invalid");

  Check(engine.Start(sink).accepted(), "start must pass");
  Check(engine.CreateProfile(
                  ProfileConfig{profile_id, static_cast<ProfileMode>(99)})
                .error() == EngineErrorCode::kInvalidArgument,
        "unknown profile mode must fail");
  Check(
      engine.CreateProfile(ProfileConfig{profile_id, ProfileMode::kPersistent})
          .accepted(),
      "valid profile must pass");
  Check(
      engine.CreateProfile(ProfileConfig{profile_id, ProfileMode::kPersistent})
              .error() == EngineErrorCode::kAlreadyExists,
      "duplicate profile must fail");
  Check(
      engine.CreateTab(TabCreateRequest{missing_profile, tab_id, std::nullopt})
              .error() == EngineErrorCode::kNotFound,
      "missing profile must fail");
  Check(engine.CreateTab(TabCreateRequest{profile_id, tab_id, std::nullopt})
            .accepted(),
        "valid tab must pass");
  Check(engine.Navigate(NavigationRequest{missing_tab,
                                          MakeUrl("https://example.test/")})
                .error() == EngineErrorCode::kNotFound,
        "missing tab navigation must fail");
  Check(engine.SetZoom(tab_id, *ZoomFactor::TryCreate(1.5)).accepted(),
        "valid zoom must pass");

  Check(engine.EmitPermissionRequest(PermissionRequest{request_id, tab_id,
                                                       NavigationId::FromRaw(1),
                                                       PermissionKind::kCamera})
                .error() == EngineErrorCode::kStaleNavigation,
        "permission for stale navigation must fail");
  Check(engine
                .EmitPermissionRequest(PermissionRequest{
                    invalid_request_id, tab_id, NavigationId::FromRaw(0),
                    static_cast<PermissionKind>(99)})
                .error() == EngineErrorCode::kInvalidArgument,
        "unknown permission kind must fail");
  const PermissionRequest request{request_id, tab_id, NavigationId::FromRaw(0),
                                  PermissionKind::kCamera};
  Check(engine.EmitPermissionRequest(request).accepted(),
        "permission event must queue");
  Check(engine.DispatchEvents() == 3,
        "profile/tab/permission events must dispatch");
  Check(engine
                .ResolvePermission(PermissionResolution{
                    request_id, static_cast<PermissionDecision>(99)})
                .error() == EngineErrorCode::kInvalidArgument,
        "unknown permission decision must fail");
  Check(engine
            .ResolvePermission(
                PermissionResolution{request_id, PermissionDecision::kDeny})
            .accepted(),
        "permission resolution must pass");
  Check(engine
            .ResolvePermission(
                PermissionResolution{request_id, PermissionDecision::kDeny})
            .accepted(),
        "permission resolution must be idempotent");
}

void TestSubscriptionAndReleaseFences() {
  RecordingSink sink;
  const auto profile_id = MakeId<ProfileId>("profile-fence");
  const auto tab_id = MakeId<TabId>("tab-fence");
  const auto subscription_id = MakeId<SubscriptionId>("subscription-01");
  const auto other_tab_id = MakeId<TabId>("tab-other");
  {
    FakeBrowserEngine engine;
    Check(engine.Start(sink).accepted(), "start must pass");
    Check(engine.CreateProfile(ProfileConfig{profile_id, ProfileMode::kPrivate})
              .accepted(),
          "profile must pass");
    Check(engine.CreateTab(TabCreateRequest{profile_id, tab_id, std::nullopt})
              .accepted(),
          "tab must pass");
    Check(engine
              .Subscribe(ObservationSubscription{subscription_id, tab_id,
                                                 ObservationTopic::kMedia})
              .accepted(),
          "subscription must pass");
    Check(engine
                  .EmitObservation(ObservationEvent{
                      subscription_id, other_tab_id, NavigationId::FromRaw(0),
                      ObservationKind::kMediaActivity})
                  .error() == EngineErrorCode::kInvalidArgument,
          "subscription event must match its tab");
    Check(engine
              .EmitObservation(ObservationEvent{
                  subscription_id, tab_id, NavigationId::FromRaw(0),
                  ObservationKind::kMediaActivity})
              .accepted(),
          "observation must queue");
    Check(engine.Unsubscribe(subscription_id).accepted(),
          "unsubscribe must pass");
    Check(engine.Unsubscribe(subscription_id).accepted(),
          "unsubscribe must be idempotent");
    Check(engine.DispatchEvents() == 3,
          "suppressed observation still drains its queue slot");
    Check(std::find(sink.events.begin(), sink.events.end(), "observation") ==
              sink.events.end(),
          "unsubscribe must suppress queued observation callback");

    Check(
        engine
            .EmitTrustedInput(TrustedInputFact{tab_id, NavigationId::FromRaw(0),
                                               TrustedInputKind::kMouse, 1})
            .accepted(),
        "trusted input must queue");
    const auto event_count_before_stop = sink.events.size();
    Check(engine.Stop().accepted(), "stop must pass");
    Check(engine.pending_event_count() == 0, "stop must clear pending events");
    Check(engine.DispatchEvents() == 0, "stopped adapter must not dispatch");
    Check(sink.events.size() == event_count_before_stop,
          "stop must fence callbacks");
    Check(engine.Start(sink).error() == EngineErrorCode::kInvalidState,
          "stopped adapter must not restart");
  }
  const auto event_count_before_destroy = sink.events.size();
  {
    FakeBrowserEngine engine;
    Check(engine.Start(sink).accepted(), "second start must pass");
    Check(engine
              .CreateProfile(ProfileConfig{MakeId<ProfileId>("profile-destroy"),
                                           ProfileMode::kPrivate})
              .accepted(),
          "queued profile event must pass");
    Check(engine.pending_event_count() == 1,
          "destructor test requires one pending event");
  }
  Check(sink.events.size() == event_count_before_destroy,
        "destructor must suppress pending callbacks");
}

void TestBoundedSnapshotStreamAndCancellation() {
  FakeBrowserEngine engine;
  RecordingSink event_sink;
  RecordingSnapshotSink snapshot_sink;
  const auto profile_id = MakeId<ProfileId>("profile-snapshot");
  const auto tab_id = MakeId<TabId>("tab-snapshot");
  const auto request_id = MakeId<SnapshotRequestId>("snapshot-01");
  const auto stale_request_id = MakeId<SnapshotRequestId>("snapshot-stale");
  Check(engine.Start(event_sink).accepted(), "snapshot engine start must pass");
  Check(engine.CreateProfile(ProfileConfig{profile_id, ProfileMode::kPrivate})
            .accepted(),
        "snapshot profile must pass");
  Check(engine
            .CreateTab(TabCreateRequest{
                profile_id, tab_id, MakeUrl("https://example.test/article")})
            .accepted(),
        "snapshot tab must pass");
  Check(engine.DispatchEvents() == 5, "initial navigation must dispatch");

  Check(engine
            .StartSnapshot(
                SnapshotRequest{request_id, tab_id, NavigationId::FromRaw(1),
                                SnapshotMode::kStandard},
                snapshot_sink)
            .accepted(),
        "current navigation snapshot must start");
  Check(engine.StartSnapshot(SnapshotRequest{stale_request_id, tab_id,
                                             NavigationId::FromRaw(0),
                                             SnapshotMode::kStandard},
                             snapshot_sink)
                .error() == EngineErrorCode::kStaleNavigation,
        "stale navigation snapshot must fail");
  Check(engine.CompleteSnapshot(request_id).error() ==
            EngineErrorCode::kInvalidState,
        "completed stream must carry document metadata");

  SnapshotFact paragraph;
  paragraph.kind = SnapshotFactKind::kParagraph;
  paragraph.text = "Visible paragraph";
  Check(engine
            .EmitSnapshotChunk(SnapshotChunk{
                request_id,
                tab_id,
                NavigationId::FromRaw(1),
                0,
                SnapshotDocumentMetadata{
                    MakeUrl("https://example.test/article"), "Article"},
                {paragraph}})
            .accepted(),
        "first bounded chunk must queue");
  Check(snapshot_sink.sequences.empty(), "snapshot callback must be async");
  Check(engine.CompleteSnapshot(request_id).accepted(),
        "snapshot completion must queue");
  Check(engine.DispatchEvents() == 2, "chunk and terminal must dispatch");
  Check(snapshot_sink.sequences == std::vector<std::uint32_t>{0},
        "snapshot sequence must be stable");
  Check(snapshot_sink.terminals ==
            std::vector<SnapshotTerminalStatus>{
                SnapshotTerminalStatus::kCompleted},
        "snapshot must complete exactly once");

  const auto cancel_id = MakeId<SnapshotRequestId>("snapshot-cancel");
  Check(engine
            .StartSnapshot(
                SnapshotRequest{cancel_id, tab_id, NavigationId::FromRaw(1),
                                SnapshotMode::kCompact},
                snapshot_sink)
            .accepted(),
        "cancellable snapshot must start");
  Check(engine
            .EmitSnapshotChunk(SnapshotChunk{
                cancel_id,
                tab_id,
                NavigationId::FromRaw(1),
                0,
                SnapshotDocumentMetadata{
                    MakeUrl("https://example.test/article"), "Article"},
                {paragraph}})
            .accepted(),
        "cancelled chunk may queue before cancellation");
  Check(engine.CancelSnapshot(cancel_id).accepted(), "cancel must pass");
  Check(engine.CancelSnapshot(cancel_id).accepted(),
        "cancel must be idempotent");
  Check(engine.DispatchEvents() == 2,
        "suppressed chunk slot and cancellation terminal must drain");
  Check(snapshot_sink.sequences == std::vector<std::uint32_t>{0},
        "cancel must suppress queued chunk callback");
  Check(snapshot_sink.terminals.back() == SnapshotTerminalStatus::kCancelled,
        "cancel must emit one terminal");

  const auto navigation_id =
      MakeId<SnapshotRequestId>("snapshot-navigation-fence");
  Check(engine
            .StartSnapshot(
                SnapshotRequest{navigation_id, tab_id, NavigationId::FromRaw(1),
                                SnapshotMode::kStandard},
                snapshot_sink)
            .accepted(),
        "navigation-fenced snapshot must start");
  Check(engine
            .Navigate(
                NavigationRequest{tab_id, MakeUrl("https://example.test/next")})
            .accepted(),
        "new navigation must pass");
  Check(engine.DispatchEvents() == 4,
        "stale terminal and navigation events must dispatch");
  Check(snapshot_sink.terminals.back() ==
            SnapshotTerminalStatus::kStaleNavigation,
        "navigation must fence snapshot");
}

void TestSnapshotFactContract() {
  SnapshotFact paragraph;
  paragraph.kind = SnapshotFactKind::kParagraph;
  paragraph.text = "visible";
  Check(crayon::browser_engine::IsValid(paragraph),
        "normal paragraph must be valid");
  paragraph.text = std::string("bad\0text", 8);
  Check(!crayon::browser_engine::IsValid(paragraph),
        "control characters must fail");
  paragraph.text = std::string("\xF0\x28\x8C\x28", 4);
  Check(!crayon::browser_engine::IsValid(paragraph),
        "malformed UTF-8 must fail");
  paragraph.text.assign(
      crayon::browser_engine::kMaxCompactSnapshotFactTextBytes + 1, 'a');
  Check(crayon::browser_engine::IsValid(paragraph, SnapshotMode::kStandard) &&
            !crayon::browser_engine::IsValid(paragraph, SnapshotMode::kCompact),
        "compact text budget must be narrower");

  SnapshotFact list_item;
  list_item.kind = SnapshotFactKind::kListItem;
  list_item.text = "item";
  list_item.depth = 1;
  list_item.ordered = true;
  list_item.ordinal = 0;
  Check(!crayon::browser_engine::IsValid(list_item),
        "ordered list ordinal must start at one");
  list_item.ordinal = 1;
  Check(crayon::browser_engine::IsValid(list_item),
        "bounded ordered list item must pass");

  SnapshotFact code;
  code.kind = SnapshotFactKind::kCodeBlock;
  code.text.assign(20 * 1024, 'x');
  code.language = "cpp";
  Check(crayon::browser_engine::IsValid(code, SnapshotMode::kCompact),
        "code uses its dedicated schema budget");
  code.language = "C++";
  Check(!crayon::browser_engine::IsValid(code),
        "language token must follow closed syntax");
}

}  // namespace

int main() {
  try {
    TestStrongTypesAndErrors();
    TestLifecycleAndEventOrder();
    TestInvalidCommandsAndPermission();
    TestSubscriptionAndReleaseFences();
    TestBoundedSnapshotStreamAndCancellation();
    TestSnapshotFactContract();
    std::cout << "browser_engine_contract: passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "browser_engine_contract: " << error.what() << '\n';
    return 1;
  }
}
