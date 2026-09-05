#include "browser/media_host/media_host_adapter.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using crayon::browser::cef_shell::media_host::BrowserMediaFact;
using crayon::browser::cef_shell::media_host::MediaHostAdapter;
using crayon::browser::cef_shell::media_host::MediaHostTransport;
using crayon::browser::cef_shell::media_host::MediaPlanningEventKind;
namespace mh = crayon::browser::cef_shell::media_host::media_host_ipc;
namespace mh2 = crayon::browser::cef_shell::media_host::ipc_v2;

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
  bool EnqueuePlayer(mh2::PlayerMessage message) override {
    if (!healthy_ || !accept_players_ || !player_messages_)
      return false;
    sent_players.push_back(std::move(message));
    return true;
  }
  bool EnqueuePlayerList(mh2::PlayerListRequest request) override {
    if (!healthy_ || !accept_player_lists_ || !player_messages_)
      return false;
    sent_player_lists.push_back(std::move(request));
    return true;
  }
  std::vector<mh2::PlayerPageReply>
  DrainPlayerPages(std::size_t maximum) override {
    const std::size_t count = std::min(maximum, inbound_player_pages.size());
    std::vector<mh2::PlayerPageReply> result;
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(inbound_player_pages.front()));
      inbound_player_pages.erase(inbound_player_pages.begin());
    }
    return result;
  }
  bool EnqueueDraft(mh2::DraftCommand command) override {
    if (!healthy_ || !accept_drafts_ || !draft_messages_)
      return false;
    sent_drafts.push_back(std::move(command));
    return true;
  }
  std::vector<mh2::DraftStateReply>
  DrainDraftStates(std::size_t maximum) override {
    const std::size_t count = std::min(maximum, inbound_drafts.size());
    std::vector<mh2::DraftStateReply> result;
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(inbound_drafts.front()));
      inbound_drafts.erase(inbound_drafts.begin());
    }
    return result;
  }
  bool supports_player_messages() const noexcept override {
    return player_messages_;
  }
  bool supports_drafts() const noexcept override { return draft_messages_; }
  bool supports_connect() const noexcept override { return connect_messages_; }
  std::uint64_t player_session_id() const noexcept override { return 77; }
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
  bool healthy_ = false, accept_ = true, accept_players_ = true,
       accept_player_lists_ = true, accept_drafts_ = true,
       player_messages_ = true, draft_messages_ = true,
       connect_messages_ = true;
  std::uint64_t generation_ = 0;
  std::vector<mh::Message> sent, inbound;
  std::vector<mh2::PlayerMessage> sent_players;
  std::vector<mh2::PlayerListRequest> sent_player_lists;
  std::vector<mh2::PlayerPageReply> inbound_player_pages;
  std::vector<mh2::DraftCommand> sent_drafts;
  std::vector<mh2::DraftStateReply> inbound_drafts;
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
  media.player_reference =
      crayon::cef_shell::input_proof::PlayerReference{5, 2};
  media.media.navigation_id = 7;
  media.media.source_kind =
      crayon::cef_shell::renderer::MediaSourceKind::kHttpUrl;
  media.media.source_url = "https://media.example/video.mp4";
  media.media.current_time_seconds = 1.25;
  media.media.visible_fraction = 0.5;
  media.media.element_kind =
      crayon::cef_shell::renderer::MediaElementKind::kVideo;
  adapter.Consume({BrowserMediaFact{media, "https://page.example/watch", 123}});
  if (fake->sent_players.size() != 1)
    return false;
  const auto *player = std::get_if<mh2::PlayerFact>(&fake->sent_players[0]);
  if (!player || player->context.session_id != 77 ||
      player->context.host_generation != fake->generation_ ||
      player->context.tab_id != 42 || player->context.navigation_id != 7 ||
      player->context.tab_generation != 3 || player->context.instance_id != 5 ||
      player->context.source_revision != 2 || player->position_ms != 1250 ||
      player->visible_fraction_ppm != 500000 || !player->has_video ||
      player->has_audio ||
      player->media_url != "https://media.example/video.mp4")
    return false;
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
  const std::string ingest_request_id = ingest->request_id;

  const auto page_request = adapter.RequestPlayerPage(42, 7, 3, 0, 0, 2);
  if (!page_request || fake->sent_player_lists.size() != 1 ||
      fake->sent_player_lists[0].context.request_id != *page_request)
    return false;
  const auto page_context = fake->sent_player_lists[0].context;
  fake->inbound_player_pages.push_back(
      {page_context,
       3,
       mh2::PlayerPageStatus::kOk,
       0,
       std::nullopt,
       {{5, 2, mh2::PlayerSourceKind::kHttpUrl, true, true, true, false,
         "https://media.example"}}});
  adapter.Tick();
  const auto pages = adapter.DrainPlayerPages(2);
  if (pages.size() != 1 || pages[0].request_id != *page_request ||
      pages[0].snapshot_revision != 3 || pages[0].players.size() != 1 ||
      pages[0].players[0].instance_id != 5 ||
      pages[0].players[0].redacted_origin != "https://media.example")
    return false;
  fake->inbound_player_pages.push_back(
      {page_context, 3, mh2::PlayerPageStatus::kOk, 0, std::nullopt, {}});
  adapter.Tick();
  if (adapter.dropped_player_pages_total() != 1)
    return false;
  const auto wrong_request = adapter.RequestPlayerPage(42, 7, 3, 3, 0, 2);
  if (!wrong_request || fake->sent_player_lists.size() != 2)
    return false;
  auto wrong_context = fake->sent_player_lists.back().context;
  ++wrong_context.tab_id;
  fake->inbound_player_pages.push_back(
      {wrong_context, 3, mh2::PlayerPageStatus::kOk, 0, std::nullopt, {}});
  adapter.Tick();
  if (adapter.dropped_player_pages_total() != 2)
    return false;

  const auto draft_request = adapter.RequestDraft(
      mh2::DraftAction::kOpen, "default", 42, 7, 3, 0, 0);
  if (!draft_request || fake->sent_drafts.size() != 1 ||
      fake->sent_drafts.front().context.request_id != *draft_request)
    return false;
  const auto draft_context = fake->sent_drafts.front().context;
  fake->inbound_drafts.push_back(
      {draft_context, 91, 1, mh2::DraftPhase::kChoosing,
       mh2::DraftError::kNone, std::nullopt, {}, false, false,
       mh2::DraftRoute::kNone, std::nullopt, mh2::DraftReason::kNone,
       std::nullopt});
  const auto draft_states = adapter.DrainDraftStates(2);
  if (draft_states.size() != 1 || draft_states.front().draft_id != 91 ||
      draft_states.front().context.request_id != *draft_request)
    return false;
  fake->accept_player_lists_ = false;
  if (adapter.RequestPlayerPage(42, 7, 3, 0, 0, 2) ||
      adapter.dropped_player_pages_total() != 3)
    return false;
  fake->accept_player_lists_ = true;

  crayon::cef_shell::gateway::GatewayEvent removed = media;
  removed.player_removed = true;
  adapter.Consume(
      {BrowserMediaFact{removed, "https://page.example/watch", 124}});
  if (fake->sent_players.size() != 2 || fake->sent.size() != 2)
    return false;
  const auto *removed_context =
      std::get_if<mh2::PlayerContext>(&fake->sent_players[1]);
  if (!removed_context || removed_context->session_id != 77 ||
      removed_context->host_generation != fake->generation_ ||
      removed_context->tab_id != 42 || removed_context->navigation_id != 7 ||
      removed_context->tab_generation != 3 ||
      removed_context->instance_id != 5 ||
      removed_context->source_revision != 2)
    return false;

  fake->accept_players_ = false;
  adapter.Consume({BrowserMediaFact{media, "https://page.example/watch", 125}});
  if (adapter.dropped_player_messages_total() != 1 ||
      fake->sent_players.size() != 2 || fake->sent.size() != 3)
    return false;
  fake->accept_players_ = true;
  fake->inbound.push_back(
      mh::CandidateReply{ingest_request_id, 9, "https://media.example"});
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

  const auto late_request = adapter.RequestPlayerPage(42, 7, 3, 3, 0, 2);
  if (!late_request || fake->sent_player_lists.size() != 3)
    return false;
  const auto late_context = fake->sent_player_lists.back().context;
  if (!adapter.AdvanceNavigation(42, 8, 4))
    return false;
  fake->inbound_player_pages.push_back(
      {late_context, 3, mh2::PlayerPageStatus::kOk, 0, std::nullopt, {}});
  adapter.Tick();
  if (adapter.dropped_player_pages_total() != 4 ||
      !adapter.DrainPlayerPages(2).empty())
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

  const auto resolve_request_id = adapter.RequestResolveCastCode("AB1 CD2");
  if (!resolve_request_id)
    return false;
  const auto resolve = std::get<mh::ResolveCastCode>(fake->sent.back());
  if (*resolve_request_id != resolve.request_id)
    return false;
  fake->inbound.push_back(mh::ResolveCastCodeReply{
      resolve.request_id,
      mh::Device{"receiver-1", "Living Room", mh::DeviceState::kReady, true},
      std::nullopt});
  adapter.Tick();
  cast = adapter.DrainCast(4);
  if (cast.size() != 1 ||
      !std::holds_alternative<mh::ResolveCastCodeReply>(cast.front()))
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

  if (adapter.RequestControlCast(4, mh::CastControlAction::kPause,
                                 std::nullopt) ||
      adapter.RequestControlCast(5, mh::CastControlAction::kPause, 1) ||
      adapter.RequestControlCast(5, mh::CastControlAction::kSeek,
                                 mh::kMaxSeekSeconds + 1))
    return false;
  const auto pause_request_id = adapter.RequestControlCast(
      5, mh::CastControlAction::kPause, std::nullopt);
  if (!pause_request_id)
    return false;
  const auto pause = std::get<mh::ControlCast>(fake->sent.back());
  if (*pause_request_id != pause.request_id)
    return false;
  fake->inbound.push_back(
      mh::ControlCastReply{pause.request_id, 5, std::nullopt});
  adapter.Tick();
  cast = adapter.DrainCast(2);
  if (cast.size() != 1 ||
      !std::holds_alternative<mh::ControlCastReply>(cast.front()))
    return false;
  const auto seek_request_id =
      adapter.RequestControlCast(5, mh::CastControlAction::kSeek, 30);
  if (!seek_request_id)
    return false;
  const auto seek = std::get<mh::ControlCast>(fake->sent.back());
  if (*seek_request_id != seek.request_id)
    return false;
  fake->inbound.push_back(
      mh::ControlCastReply{seek.request_id, 5, mh::CastError::kRouteLost});
  adapter.Tick();
  cast = adapter.DrainCast(2);
  const auto *control_reply =
      cast.size() == 1 ? std::get_if<mh::ControlCastReply>(&cast.front())
                       : nullptr;
  if (!control_reply || control_reply->error != mh::CastError::kRouteLost)
    return false;

  if (!adapter.RequestControlCast(5, mh::CastControlAction::kPause,
                                  std::nullopt))
    return false;
  const auto late_control = std::get<mh::ControlCast>(fake->sent.back());

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
  fake->inbound.push_back(
      mh::ControlCastReply{late_control.request_id, 5, std::nullopt});
  adapter.Tick();
  if (!adapter.DrainCast(2).empty())
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

bool RunPlayerPageCapacity() {
  auto transport = std::make_unique<FakeTransport>();
  MediaHostAdapter adapter(std::move(transport));
  if (!adapter.Start("/test/media-host"))
    return false;
  adapter.Tick();
  if (!adapter.AdvanceNavigation(42, 1, 1))
    return false;
  for (std::size_t index = 0; index < 64; ++index) {
    if (!adapter.RequestPlayerPage(42, 1, 1, 0, 0, 1))
      return false;
  }
  if (adapter.RequestPlayerPage(42, 1, 1, 0, 0, 1) ||
      adapter.dropped_player_pages_total() != 1)
    return false;
  adapter.Stop();
  return true;
}

bool Run() {
  if (!RunObservationMapping() || !RunCastCommandPump() ||
      !RunStaleStartCleanup() || !RunPlayerPageCapacity())
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
  const std::uint64_t cast_epoch_before_restart = adapter.cast_state_epoch();
  ++fake->generation_;
  adapter.Tick();
  if (!adapter.Drain(4).empty() ||
      adapter.cast_state_epoch() == cast_epoch_before_restart)
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

bool DraftCommitBindsSessionControls() {
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport* fake = transport.get();
  MediaHostAdapter adapter(std::move(transport));
  if (!adapter.Start("/test/media-host"))
    return false;
  adapter.Tick();
  if (!adapter.AdvanceNavigation(42, 7, 3))
    return false;
  const auto request = adapter.RequestDraft(mh2::DraftAction::kCommit,
                                            "default", 42, 7, 3, 91, 8);
  if (!request || fake->sent_drafts.empty())
    return false;
  const auto context = fake->sent_drafts.back().context;
  fake->inbound_drafts.push_back(
      {context,
       91,
       9,
       mh2::DraftPhase::kCommitted,
       mh2::DraftError::kNone,
       mh2::DraftMediaRef{5, 2},
       "device-1",
       true,
       false,
       mh2::DraftRoute::kNone,
       std::nullopt,
       mh2::DraftReason::kNone,
       47});
  const auto states = adapter.DrainDraftStates(1);
  if (states.size() != 1 || states.front().session_generation != 47 ||
      !adapter.RequestStopCast(47))
    return false;
  return std::holds_alternative<mh::StopCast>(fake->sent.back()) &&
         std::get<mh::StopCast>(fake->sent.back()).session_generation == 47;
}

} // namespace

int main() { return Run() && DraftCommitBindsSessionControls() ? 0 : 1; }
