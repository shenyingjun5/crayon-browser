#include "macos/media_host_adapter_mac.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using crayon::browser::cef_shell::macos::BrowserMediaFact;
using crayon::browser::cef_shell::macos::MediaHostAdapter;
using crayon::browser::cef_shell::macos::MediaHostTransport;
using crayon::browser::cef_shell::macos::MediaPlanningEventKind;
namespace mh = crayon::browser::cef_shell::macos::media_host_ipc;

class FakeTransport final : public MediaHostTransport {
public:
  bool Start(std::string) override {
    healthy_ = true;
    ++generation_;
    return true;
  }
  void Stop() override { healthy_ = false; }
  bool Enqueue(mh::Message message) override {
    if (!healthy_ || !accept_)
      return false;
    sent.push_back(std::move(message));
    return true;
  }
  std::vector<mh::Message> Drain(std::size_t maximum) override {
    const std::size_t count = std::min(maximum, inbound.size());
    std::vector<mh::Message> result;
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(inbound.front()));
      inbound.erase(inbound.begin());
    }
    return result;
  }
  bool healthy() const noexcept override { return healthy_; }
  std::uint64_t generation() const noexcept override { return generation_; }
  bool healthy_ = false, accept_ = true;
  std::uint64_t generation_ = 0;
  std::vector<mh::Message> sent, inbound;
};

bool RunObservationMapping() {
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport *fake = transport.get();
  MediaHostAdapter adapter(std::move(transport));
  if (!adapter.Start("/test/media-host"))
    return false;
  adapter.Tick();

  crayon::cef_shell::gateway::GatewayEvent media;
  media.source = crayon::cef_shell::gateway::EventSource::kMedia;
  media.tab_id = 42;
  media.navigation_id = 7;
  media.generation = 3;
  media.media.navigation_id = 7;
  media.media.source_kind =
      crayon::cef_shell::renderer::MediaSourceKind::kHttpUrl;
  media.media.source_url = "https://media.example/video.mp4";
  media.media.current_time_seconds = 1.25;
  media.media.visible_fraction = 0.5;
  adapter.Consume({BrowserMediaFact{media, "https://page.example/watch", 123}});
  if (fake->sent.size() != 2 ||
      !std::holds_alternative<mh::Navigation>(fake->sent[0]))
    return false;
  const auto *ingest = std::get_if<mh::IngestUrl>(&fake->sent[1]);
  if (!ingest || ingest->tab_id != "cef-42" || ingest->generation != 3 ||
      ingest->page_url != "https://page.example/watch" ||
      ingest->media_url != "https://media.example/video.mp4" ||
      !ingest->playback || ingest->playback->position_ms != 1250 ||
      ingest->playback->visible_area_px != 500000)
    return false;
  fake->inbound.push_back(
      mh::CandidateReply{ingest->request_id, 9, "https://media.example"});
  adapter.Tick();
  const auto planning = adapter.DrainPlanning(2);
  if (planning.size() != 1 ||
      planning.front().kind != MediaPlanningEventKind::kCandidate ||
      planning.front().candidate_id != 9 ||
      planning.front().redacted_origin != "https://media.example")
    return false;

  crayon::cef_shell::gateway::GatewayEvent protected_fact = media;
  protected_fact.source = crayon::cef_shell::gateway::EventSource::kNetwork;
  protected_fact.network.navigation_id = 7;
  protected_fact.network.eme_encrypted = true;
  adapter.Consume(
      {BrowserMediaFact{protected_fact, "https://page.example/watch", 124}});
  if (!std::holds_alternative<mh::MarkEme>(fake->sent.back()))
    return false;

  crayon::cef_shell::gateway::GatewayEvent credential = media;
  credential.source = crayon::cef_shell::gateway::EventSource::kNetwork;
  credential.network.navigation_id = 7;
  credential.network.url = "https://media.example/video.mp4";
  credential.network.kind = crayon::cef_shell::network::ResourceKind::kMedia;
  credential.network.header_class =
      crayon::cef_shell::network::HeaderClass::kAuthorization;
  adapter.Consume(
      {BrowserMediaFact{credential, "https://page.example/watch", 124}});
  const auto *credential_ingest =
      std::get_if<mh::IngestUrl>(&fake->sent.back());
  if (!credential_ingest ||
      credential_ingest->headers_class != mh::HeadersClass::kCredentialBound ||
      credential_ingest->playback)
    return false;

  crayon::cef_shell::gateway::GatewayEvent blob = media;
  blob.media.source_kind =
      crayon::cef_shell::renderer::MediaSourceKind::kBlobUrl;
  blob.media.source_url.clear();
  adapter.Consume({BrowserMediaFact{blob, "https://page.example/watch", 125}});
  if (!std::holds_alternative<mh::DecideUrlLess>(fake->sent.back()))
    return false;

  if (!adapter.AdvanceNavigation(42, 8, 4))
    return false;
  const std::size_t sent_before_stale = fake->sent.size();
  adapter.Consume({BrowserMediaFact{media, "https://page.example/watch", 126}});
  if (fake->sent.size() != sent_before_stale)
    return false;
  adapter.Stop();
  return true;
}

bool RunCastCommandPump() {
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport *fake = transport.get();
  MediaHostAdapter adapter(std::move(transport));
  if (!adapter.Start("/test/media-host"))
    return false;
  adapter.Tick();
  if (adapter.RequestStartCast(77, "receiver-1", true) ||
      !adapter.Submit(mh::Navigation{"cast-nav", "tab-cast", 1, 1}))
    return false;
  fake->inbound.push_back(mh::Ack{"cast-nav"});
  adapter.Tick();
  adapter.Drain(2);
  if (!adapter.Submit(mh::IngestUrl{
          "cast-ingest", "tab-cast", 1, 1, 1, "https://page.example/watch",
          "https://media.example/video.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}))
    return false;
  fake->inbound.push_back(
      mh::CandidateReply{"cast-ingest", 77, "https://media.example"});
  adapter.Tick();
  adapter.Drain(2);

  if (!adapter.RequestDiscovery(mh::DiscoveryAction::kStart))
    return false;
  const auto discovery = std::get<mh::Discovery>(fake->sent.back());
  fake->inbound.push_back(mh::Ack{discovery.request_id});
  adapter.Tick();
  auto cast = adapter.DrainCast(4);
  if (cast.size() != 1 || !std::holds_alternative<mh::Ack>(cast.front()))
    return false;

  if (!adapter.RequestDevicePage(std::nullopt, 0))
    return false;
  const auto list = std::get<mh::ListDevices>(fake->sent.back());
  fake->inbound.push_back(mh::DevicePageReply{
      list.request_id,
      3,
      0,
      std::nullopt,
      {{"receiver-1", "Living Room", mh::DeviceState::kReady, true}}});
  adapter.Tick();
  cast = adapter.DrainCast(4);
  if (cast.size() != 1 ||
      !std::holds_alternative<mh::DevicePageReply>(cast.front()))
    return false;

  if (!adapter.RequestStartCast(77, "receiver-1", true))
    return false;
  const auto start = std::get<mh::StartCast>(fake->sent.back());
  fake->inbound.push_back(mh::StartCastReply{
      start.request_id,
      {mh::CastStartKind::kCasting, 5, mh::DeliveryRoute::kDirect, std::nullopt,
       std::nullopt, std::nullopt}});
  adapter.Tick();
  cast = adapter.DrainCast(4);
  if (cast.size() != 1 ||
      !std::holds_alternative<mh::StartCastReply>(cast.front()) ||
      adapter.RequestStopCast(4) ||
      !std::holds_alternative<mh::PollSessionEvents>(fake->sent.back()))
    return false;

  const auto empty_poll = std::get<mh::PollSessionEvents>(fake->sent.back());
  fake->inbound.push_back(mh::SessionEventsReply{empty_poll.request_id, 0, {}});
  adapter.Tick();
  cast = adapter.DrainCast(2);
  const auto *empty_events =
      cast.size() == 1 ? std::get_if<mh::SessionEventsReply>(&cast.front())
                       : nullptr;
  if (!empty_events || !empty_events->events.empty())
    return false;

  if (!adapter.RequestStartCast(77, "receiver-1", true))
    return false;
  const auto replacement = std::get<mh::StartCast>(fake->sent.back());
  fake->inbound.push_back(mh::StartCastReply{
      replacement.request_id,
      {mh::CastStartKind::kCasting, 6, mh::DeliveryRoute::kRelay, std::nullopt,
       std::nullopt, std::nullopt}});
  adapter.Tick();
  cast = adapter.DrainCast(2);
  if (cast.size() != 1 ||
      !std::holds_alternative<mh::StartCastReply>(cast.front()) ||
      adapter.RequestStopCast(5) ||
      !std::holds_alternative<mh::PollSessionEvents>(fake->sent.back()))
    return false;

  const auto poll = std::get<mh::PollSessionEvents>(fake->sent.back());
  if (!adapter.RequestStopCast(6))
    return false;
  const auto stop = std::get<mh::StopCast>(fake->sent.back());
  const std::size_t sent_with_poll = fake->sent.size();
  adapter.Tick();
  adapter.Tick();
  if (fake->sent.size() != sent_with_poll)
    return false;
  fake->inbound.push_back(mh::Ack{stop.request_id});
  fake->inbound.push_back(mh::SessionEventsReply{
      poll.request_id,
      2,
      {{5, 99, mh::SessionPhase::kActive, mh::SessionPlayback::kPlaying,
        std::nullopt},
       {6, 1, mh::SessionPhase::kActive, mh::SessionPlayback::kPlaying,
        std::nullopt},
       {6, 1, mh::SessionPhase::kActive, mh::SessionPlayback::kPlaying,
        std::nullopt},
       {6, 2, mh::SessionPhase::kTerminated, mh::SessionPlayback::kStopped,
        mh::TerminalReason::kStoppedBySender}}});
  adapter.Tick();
  cast = adapter.DrainCast(4);
  const auto event_reply =
      std::find_if(cast.begin(), cast.end(), [](const auto &message) {
        return std::holds_alternative<mh::SessionEventsReply>(message);
      });
  const auto *events = event_reply == cast.end()
                           ? nullptr
                           : std::get_if<mh::SessionEventsReply>(&*event_reply);
  if (cast.size() != 2 || !events || events->dropped_events != 2 ||
      events->events.size() != 2 ||
      events->events.back().phase != mh::SessionPhase::kTerminated ||
      adapter.RequestStopCast(6))
    return false;

  for (const auto kind :
       {mh::CastStartKind::kHandoff, mh::CastStartKind::kRejected,
        mh::CastStartKind::kFailed}) {
    if (!adapter.RequestStartCast(77, "receiver-1", true))
      return false;
    const auto request = std::get<mh::StartCast>(fake->sent.back());
    mh::CastStartOutcome outcome;
    outcome.kind = kind;
    if (kind == mh::CastStartKind::kHandoff)
      outcome.handoff_reason = mh::HandoffReason::kStartFailed;
    else if (kind == mh::CastStartKind::kRejected)
      outcome.reject_reason = mh::CoreError::kPolicyDenied;
    else
      outcome.error = mh::CastError::kRouteLost;
    fake->inbound.push_back(mh::StartCastReply{request.request_id, outcome});
    adapter.Tick();
    cast = adapter.DrainCast(2);
    if (cast.size() != 1 ||
        !std::holds_alternative<mh::StartCastReply>(cast.front()))
      return false;
  }

  ++fake->generation_;
  adapter.Tick();
  if (!adapter.DrainCast(4).empty() || adapter.RequestStopCast(6))
    return false;
  fake->accept_ = false;
  if (adapter.RequestDiscovery(mh::DiscoveryAction::kRefresh))
    return false;
  adapter.Stop();
  return !adapter.healthy();
}

bool RunStaleStartCleanup() {
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport *fake = transport.get();
  MediaHostAdapter adapter(std::move(transport));
  if (!adapter.Start("/test/media-host"))
    return false;
  adapter.Tick();
  if (!adapter.Submit(mh::Navigation{"nav", "tab-stale", 1, 1}))
    return false;
  fake->inbound.push_back(mh::Ack{"nav"});
  adapter.Tick();
  adapter.Drain(2);
  if (!adapter.Submit(mh::IngestUrl{
          "ingest", "tab-stale", 1, 1, 1, "https://page.example/watch",
          "https://media.example/video.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}))
    return false;
  fake->inbound.push_back(
      mh::CandidateReply{"ingest", 91, "https://media.example"});
  adapter.Tick();
  adapter.Drain(2);
  if (!adapter.RequestStartCast(91, "receiver-1", true))
    return false;
  const auto start = std::get<mh::StartCast>(fake->sent.back());
  if (!adapter.Submit(mh::Navigation{"nav-new", "tab-stale", 2, 2}))
    return false;
  fake->inbound.push_back(mh::StartCastReply{
      start.request_id,
      {mh::CastStartKind::kCasting, 7, mh::DeliveryRoute::kDirect, std::nullopt,
       std::nullopt, std::nullopt}});
  adapter.Tick();
  const auto *cleanup = std::get_if<mh::StopCast>(&fake->sent.back());
  const bool cleaned = cleanup && cleanup->session_generation == 7;
  adapter.Stop();
  return cleaned;
}

bool Run() {
  if (!RunObservationMapping() || !RunCastCommandPump() ||
      !RunStaleStartCleanup())
    return false;
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport *fake = transport.get();
  MediaHostAdapter adapter(std::move(transport));
  if (!adapter.Start("/test/media-host"))
    return false;
  adapter.Tick();
  if (adapter.Submit(mh::IngestUrl{
          "no-context", "tab-1", 7, 9, 123, "https://page.example/watch",
          "https://media.example/video.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}))
    return false;
  if (!adapter.Submit(mh::Navigation{"nav-1", "tab-1", 7, 9}))
    return false;
  fake->inbound.push_back(mh::Ack{"nav-1"});
  adapter.Tick();
  if (adapter.Drain(2).size() != 1)
    return false;

  if (!adapter.Submit(mh::IngestUrl{
          "ingest-1", "tab-1", 7, 9, 123, "https://page.example/watch",
          "https://media.example/video.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}))
    return false;
  if (adapter.Submit(mh::MarkEme{"ingest-1", "tab-1", 7, 9}))
    return false;
  fake->inbound.push_back(
      mh::CandidateReply{"ingest-1", 3, "https://media.example"});
  adapter.Tick();
  if (adapter.Drain(2).size() != 1 ||
      !adapter.Submit(mh::Decide{"decide-1",
                                 3,
                                 124,
                                 {true, true, false, true, false, false, 1080},
                                 true}))
    return false;
  if (!adapter.Submit(mh::Cancel{"decide-1"}))
    return false;
  fake->inbound.push_back(
      mh::ErrorReply{"decide-1", mh::HostError::kCancelled});
  adapter.Tick();
  if (adapter.Drain(2).size() != 1)
    return false;

  if (!adapter.Submit(mh::Navigation{"nav-2", "tab-1", 8, 10}) ||
      adapter.Submit(mh::Decide{"stale", 3, 125, {}, true}) ||
      adapter.Submit(mh::IngestUrl{
          "old", "tab-1", 7, 9, 126, "https://page.example/watch",
          "https://media.example/old.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}))
    return false;
  fake->inbound.push_back(mh::DecisionReply{
      "decide-1",
      3,
      mh::Protocol::kMp4,
      {mh::DecisionKind::kDirect, std::nullopt, std::nullopt}});
  adapter.Tick();
  if (!adapter.Drain(4).empty())
    return false;

  if (!adapter.Submit(mh::CloseTab{"close-1", "tab-1", 10}) ||
      adapter.Submit(mh::IngestUrl{
          "closed", "tab-1", 8, 10, 127, "https://page.example/watch",
          "https://media.example/closed.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}))
    return false;
  fake->inbound.push_back(mh::Ack{"close-1"});
  adapter.Tick();
  if (adapter.Drain(2).size() != 1)
    return false;
  // A fast restart can hide the unhealthy transition from the UI thread.
  ++fake->generation_;
  adapter.Tick();
  if (!adapter.Drain(4).empty())
    return false;
  if (!adapter.Submit(mh::Navigation{"seed", "tab-seed", 1, 1}))
    return false;
  fake->inbound.push_back(mh::Ack{"seed"});
  adapter.Tick();
  if (adapter.Drain(2).size() != 1 ||
      !adapter.Submit(mh::MarkEme{"duplicate", "tab-seed", 1, 1}) ||
      adapter.Submit(mh::Navigation{"duplicate", "tab-ghost", 9, 9}) ||
      adapter.Submit(mh::Navigation{"invalid", "tab.with.dot", 9, 9}) ||
      !adapter.Submit(mh::Navigation{"ghost-ok", "tab-ghost", 9, 9}))
    return false;
  for (std::uint64_t index = 1; index <= 62; ++index) {
    if (!adapter.Submit(mh::Navigation{"capacity-" + std::to_string(index),
                                       "tab-" + std::to_string(index), 1,
                                       index}))
      return false;
  }
  if (adapter.Submit(
          mh::Navigation{"capacity-overflow", "tab-overflow", 1, 65}))
    return false;
  adapter.Stop();
  return !adapter.healthy();
}

} // namespace

int main() { return Run() ? 0 : 1; }
