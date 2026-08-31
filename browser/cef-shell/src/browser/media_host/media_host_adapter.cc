#include "browser/media_host/media_host_adapter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

namespace crayon::browser::cef_shell::media_host {
namespace {

constexpr std::size_t kMaxTrackedRequests = 256;
constexpr std::size_t kMaxTrackedCandidates = 256;
constexpr std::size_t kMaxTrackedTabs = 64;
constexpr std::size_t kMaxBrowserReplies = 64;
constexpr std::size_t kMaxCastReplies = 64;
constexpr std::uint32_t kVisibleAreaScale = 1'000'000;
constexpr auto kSessionPollInterval = std::chrono::milliseconds(100);

std::string WireTabId(std::uint32_t tab_id) {
  return "cef-" + std::to_string(tab_id);
}

media_host_ipc::HeadersClass
HeadersClassFor(::crayon::cef_shell::network::HeaderClass value) {
  using NetworkHeader = ::crayon::cef_shell::network::HeaderClass;
  switch (value) {
  case NetworkHeader::kReferer:
    return media_host_ipc::HeadersClass::kRefererOnly;
  case NetworkHeader::kAuthorization:
    return media_host_ipc::HeadersClass::kCredentialBound;
  case NetworkHeader::kNone:
  case NetworkHeader::kUserAgent:
  case NetworkHeader::kRange:
    return media_host_ipc::HeadersClass::kNone;
  }
  return media_host_ipc::HeadersClass::kNone;
}

media_host_ipc::Playback PlaybackFor(
    const ::crayon::cef_shell::renderer::MediaObservation &observation) {
  const double safe_position =
      std::isfinite(observation.current_time_seconds)
          ? std::max(0.0, observation.current_time_seconds)
          : 0.0;
  const double position_ms = safe_position * 1000.0;
  const double bounded_position =
      std::min(position_ms,
               static_cast<double>(std::numeric_limits<std::uint64_t>::max()));
  const double visible = std::clamp(observation.visible_fraction, 0.0, 1.0);
  return media_host_ipc::Playback{
      static_cast<std::uint64_t>(bounded_position),
      std::nullopt,
      false,
      media_host_ipc::AdContinuity::kUnknown,
      true,
      false,
      false,
      true,
      static_cast<std::uint32_t>(visible * kVisibleAreaScale)};
}

std::string ReplyRequestId(const media_host_ipc::Message &message) {
  return std::visit(
      [](const auto &value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, media_host_ipc::CandidateReply> ||
                      std::is_same_v<T, media_host_ipc::DecisionReply> ||
                      std::is_same_v<T, media_host_ipc::Ack> ||
                      std::is_same_v<T, media_host_ipc::ErrorReply> ||
                      std::is_same_v<T, media_host_ipc::DevicePageReply> ||
                      std::is_same_v<T, media_host_ipc::StartCastReply> ||
                      std::is_same_v<T, media_host_ipc::SessionEventsReply>)
          return value.request_id;
        return {};
      },
      message);
}

std::string NewRequestId(const media_host_ipc::Message &message) {
  return std::visit(
      [](const auto &value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, media_host_ipc::IngestUrl> ||
                      std::is_same_v<T, media_host_ipc::MarkEme> ||
                      std::is_same_v<T, media_host_ipc::Decide> ||
                      std::is_same_v<T, media_host_ipc::DecideUrlLess> ||
                      std::is_same_v<T, media_host_ipc::Navigation> ||
                      std::is_same_v<T, media_host_ipc::CloseTab> ||
                      std::is_same_v<T, media_host_ipc::Discovery> ||
                      std::is_same_v<T, media_host_ipc::ListDevices> ||
                      std::is_same_v<T, media_host_ipc::StartCast> ||
                      std::is_same_v<T, media_host_ipc::StopCast> ||
                      std::is_same_v<T, media_host_ipc::PollSessionEvents>)
          return value.request_id;
        return {};
      },
      message);
}

} // namespace

MediaHostAdapter::MediaHostAdapter(
    std::unique_ptr<MediaHostTransport> transport)
    : process_(std::move(transport)) {}

bool MediaHostAdapter::Start(std::string executable_path) {
  if (!process_ || !process_->Start(std::move(executable_path)))
    return false;
  saw_healthy_ = false;
  process_generation_ = 0;
  return true;
}

void MediaHostAdapter::Stop() {
  FailAll();
  tabs_.clear();
  saw_healthy_ = false;
  process_generation_ = 0;
  if (process_)
    process_->Stop();
}

bool MediaHostAdapter::healthy() const noexcept {
  return process_ && process_->healthy();
}

bool MediaHostAdapter::Submit(media_host_ipc::Message message) {
  PollReplies();
  if (!healthy())
    return false;
  media_host_ipc::CodecError codec_error =
      media_host_ipc::CodecError::kInvalidValue;
  if (!media_host_ipc::Encode(message, &codec_error))
    return false;
  const std::string new_request_id = NewRequestId(message);
  if (!new_request_id.empty() &&
      (requests_.find(new_request_id) != requests_.end() ||
       requests_.size() >= kMaxTrackedRequests))
    return false;
  std::string request_id;
  Context context;
  if (!Admit(message, &request_id, &context))
    return false;
  const auto cast_kind = std::visit(
      [](const auto &value) -> std::optional<CastRequestKind> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, media_host_ipc::Discovery>)
          return CastRequestKind::kDiscovery;
        if constexpr (std::is_same_v<T, media_host_ipc::ListDevices>)
          return CastRequestKind::kListDevices;
        if constexpr (std::is_same_v<T, media_host_ipc::StartCast>)
          return CastRequestKind::kStartCast;
        if constexpr (std::is_same_v<T, media_host_ipc::StopCast>)
          return CastRequestKind::kStopCast;
        if constexpr (std::is_same_v<T, media_host_ipc::PollSessionEvents>)
          return CastRequestKind::kPollSessionEvents;
        return std::nullopt;
      },
      message);
  if (!process_->Enqueue(std::move(message))) {
    FailAll();
    tabs_.clear();
    saw_healthy_ = false;
    process_generation_ = 0;
    return false;
  }
  if (!request_id.empty()) {
    requests_[request_id] = std::move(context);
    if (cast_kind) {
      cast_requests_[request_id] = *cast_kind;
      if (*cast_kind == CastRequestKind::kPollSessionEvents)
        poll_request_id_ = request_id;
    }
  }
  return true;
}

bool MediaHostAdapter::AdvanceNavigation(std::uint32_t tab_id,
                                         std::uint64_t navigation_id,
                                         std::uint64_t generation) {
  if (tab_id == 0 || navigation_id == 0 || generation == 0)
    return false;
  return Submit(media_host_ipc::Navigation{NextRequestId(), WireTabId(tab_id),
                                           navigation_id, generation});
}

bool MediaHostAdapter::CloseTab(std::uint32_t tab_id,
                                std::uint64_t generation) {
  if (tab_id == 0 || generation == 0)
    return false;
  return Submit(
      media_host_ipc::CloseTab{NextRequestId(), WireTabId(tab_id), generation});
}

void MediaHostAdapter::Consume(std::vector<BrowserMediaFact> facts) {
  for (const BrowserMediaFact &fact : facts) {
    const auto &event = fact.observation;
    if (fact.page_url.empty() || event.tab_id == 0 ||
        event.navigation_id == 0 || event.generation == 0 ||
        fact.observed_at_ms == 0 || !EnsureContext(fact)) {
      continue;
    }
    const std::string tab_id = WireTabId(event.tab_id);
    if (event.source == ::crayon::cef_shell::gateway::EventSource::kMedia) {
      const auto &media = event.media;
      const auto playback = PlaybackFor(media);
      if (media.source_kind ==
          ::crayon::cef_shell::renderer::MediaSourceKind::kHttpUrl) {
        static_cast<void>(Submit(media_host_ipc::IngestUrl{
            NextRequestId(), tab_id, event.navigation_id, event.generation,
            fact.observed_at_ms, fact.page_url, media.source_url,
            media_host_ipc::Source::kCurrentSrc,
            media_host_ipc::HeadersClass::kNone, playback,
            event.eme_encrypted}));
      } else {
        static_cast<void>(Submit(media_host_ipc::DecideUrlLess{
            NextRequestId(), tab_id, event.navigation_id, event.generation,
            fact.page_url, playback, event.eme_encrypted, false}));
      }
      continue;
    }
    const auto &network = event.network;
    if (network.eme_encrypted) {
      static_cast<void>(Submit(media_host_ipc::MarkEme{
          NextRequestId(), tab_id, event.navigation_id, event.generation}));
    }
    if (network.url.empty() ||
        (network.kind != ::crayon::cef_shell::network::ResourceKind::kMedia &&
         network.kind !=
             ::crayon::cef_shell::network::ResourceKind::kManifest)) {
      continue;
    }
    static_cast<void>(Submit(media_host_ipc::IngestUrl{
        NextRequestId(), tab_id, event.navigation_id, event.generation,
        fact.observed_at_ms, fact.page_url, network.url,
        media_host_ipc::Source::kNetworkRequest,
        HeadersClassFor(network.header_class), std::nullopt, false}));
  }
}

void MediaHostAdapter::Tick() {
  PollReplies();
  MaybePollSessionEvents();
}

std::vector<media_host_ipc::Message>
MediaHostAdapter::Drain(std::size_t maximum) {
  PollReplies();
  std::vector<media_host_ipc::Message> result;
  const std::size_t count = std::min(maximum, replies_.size());
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(std::move(replies_.front()));
    replies_.pop_front();
  }
  return result;
}

std::vector<MediaPlanningEvent>
MediaHostAdapter::DrainPlanning(std::size_t maximum) {
  PollReplies();
  std::vector<MediaPlanningEvent> result;
  const std::size_t count = std::min(maximum, planning_events_.size());
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(std::move(planning_events_.front()));
    planning_events_.pop_front();
  }
  return result;
}

bool MediaHostAdapter::RequestDiscovery(
    media_host_ipc::DiscoveryAction action) {
  return Submit(media_host_ipc::Discovery{NextRequestId(), action});
}

bool MediaHostAdapter::RequestDevicePage(
    std::optional<std::uint64_t> snapshot_revision, std::uint16_t offset) {
  return Submit(
      media_host_ipc::ListDevices{NextRequestId(), snapshot_revision, offset});
}

bool MediaHostAdapter::RequestStartCast(std::uint64_t candidate_id,
                                        std::string device_id,
                                        bool handoff_available) {
  return Submit(media_host_ipc::StartCast{
      NextRequestId(), candidate_id, std::move(device_id), handoff_available});
}

bool MediaHostAdapter::RequestStopCast(std::uint64_t session_generation) {
  if (!active_session_generation_ || session_generation == 0 ||
      session_generation != *active_session_generation_) {
    return false;
  }
  return Submit(media_host_ipc::StopCast{NextRequestId(), session_generation});
}

std::vector<media_host_ipc::Message>
MediaHostAdapter::DrainCast(std::size_t maximum) {
  PollReplies();
  std::vector<media_host_ipc::Message> result;
  const std::size_t count = std::min(maximum, cast_replies_.size());
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(std::move(cast_replies_.front()));
    cast_replies_.pop_front();
  }
  return result;
}

bool MediaHostAdapter::Admit(const media_host_ipc::Message &message,
                             std::string *request_id, Context *context) {
  if (const auto *value = std::get_if<media_host_ipc::IngestUrl>(&message)) {
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return Current(*context);
  }
  if (const auto *value = std::get_if<media_host_ipc::MarkEme>(&message)) {
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return Current(*context);
  }
  if (const auto *value =
          std::get_if<media_host_ipc::DecideUrlLess>(&message)) {
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return Current(*context);
  }
  if (const auto *value = std::get_if<media_host_ipc::Decide>(&message)) {
    const auto found = candidates_.find(value->candidate_id);
    if (found == candidates_.end() || !Current(found->second))
      return false;
    *request_id = value->request_id;
    *context = found->second;
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::Cancel>(&message)) {
    const auto found = requests_.find(value->request_id);
    if (found == requests_.end())
      return false;
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::Navigation>(&message)) {
    const auto found = tabs_.find(value->tab_id);
    if (found != tabs_.end() && value->generation <= found->second.generation)
      return false;
    if (found == tabs_.end() && tabs_.size() >= kMaxTrackedTabs)
      return false;
    tabs_[value->tab_id] = {value->navigation_id, value->generation, false};
    InvalidateTab(value->tab_id);
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::CloseTab>(&message)) {
    const auto found = tabs_.find(value->tab_id);
    if (found != tabs_.end() && value->generation < found->second.generation)
      return false;
    if (found == tabs_.end() && tabs_.size() >= kMaxTrackedTabs)
      return false;
    const std::uint64_t navigation =
        found == tabs_.end() ? 0 : found->second.navigation_id;
    tabs_[value->tab_id] = {navigation, value->generation, true};
    InvalidateTab(value->tab_id);
    *request_id = value->request_id;
    *context = {};
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::Discovery>(&message)) {
    *request_id = value->request_id;
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::ListDevices>(&message)) {
    *request_id = value->request_id;
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::StartCast>(&message)) {
    const auto found = candidates_.find(value->candidate_id);
    if (found == candidates_.end() || !Current(found->second))
      return false;
    *request_id = value->request_id;
    *context = found->second;
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::StopCast>(&message)) {
    if (!active_session_generation_ || value->session_generation == 0 ||
        value->session_generation != *active_session_generation_) {
      return false;
    }
    *request_id = value->request_id;
    return true;
  }
  if (const auto *value =
          std::get_if<media_host_ipc::PollSessionEvents>(&message)) {
    if (!active_session_generation_ || poll_request_id_)
      return false;
    *request_id = value->request_id;
    return true;
  }
  return false;
}

bool MediaHostAdapter::Current(const Context &context) const {
  const auto found = tabs_.find(context.tab_id);
  if (found == tabs_.end())
    return false;
  return !found->second.closed &&
         found->second.navigation_id == context.navigation_id &&
         found->second.generation == context.generation;
}

void MediaHostAdapter::PollReplies() {
  const bool is_healthy = healthy();
  if (!is_healthy) {
    if (saw_healthy_)
      FailAll();
    saw_healthy_ = false;
    process_generation_ = 0;
    return;
  }
  const std::uint64_t generation = process_->generation();
  if (!saw_healthy_ || generation == 0 || generation != process_generation_) {
    FailAll();
    tabs_.clear();
    saw_healthy_ = true;
    process_generation_ = generation;
  }
  for (auto &message : process_->Drain(kMaxBrowserReplies)) {
    const std::string request_id = ReplyRequestId(message);
    const auto found = requests_.find(request_id);
    if (request_id.empty() || found == requests_.end())
      continue;
    const auto cast_found = cast_requests_.find(request_id);
    if (!found->second.tab_id.empty() && !Current(found->second)) {
      if (cast_found != cast_requests_.end() &&
          cast_found->second == CastRequestKind::kStartCast &&
          !HandleStaleCastReply(message))
        return;
      cast_requests_.erase(request_id);
      requests_.erase(found);
      continue;
    }
    if (cast_found != cast_requests_.end()) {
      const CastRequestKind kind = cast_found->second;
      cast_requests_.erase(cast_found);
      requests_.erase(found);
      if (!HandleCastReply(std::move(message), kind))
        return;
      continue;
    }
    if (const auto *candidate =
            std::get_if<media_host_ipc::CandidateReply>(&message)) {
      if (candidate->candidate_id) {
        if (candidates_.size() >= kMaxTrackedCandidates &&
            candidates_.find(*candidate->candidate_id) == candidates_.end()) {
          FailAll();
          return;
        }
        candidates_[*candidate->candidate_id] = found->second;
      }
      if (planning_events_.size() >= kMaxBrowserReplies)
        planning_events_.pop_front();
      planning_events_.push_back(MediaPlanningEvent{
          MediaPlanningEventKind::kCandidate, candidate->candidate_id,
          candidate->redacted_origin, std::nullopt, std::nullopt,
          std::nullopt});
    } else if (const auto *decision =
                   std::get_if<media_host_ipc::DecisionReply>(&message)) {
      if (planning_events_.size() >= kMaxBrowserReplies)
        planning_events_.pop_front();
      planning_events_.push_back(
          MediaPlanningEvent{MediaPlanningEventKind::kDecision,
                             decision->candidate_id,
                             {},
                             decision->protocol,
                             decision->decision,
                             std::nullopt});
    } else if (const auto *error =
                   std::get_if<media_host_ipc::ErrorReply>(&message)) {
      if (planning_events_.size() >= kMaxBrowserReplies)
        planning_events_.pop_front();
      planning_events_.push_back(
          MediaPlanningEvent{MediaPlanningEventKind::kError,
                             std::nullopt,
                             {},
                             std::nullopt,
                             std::nullopt,
                             error->code});
    }
    if (replies_.size() >= kMaxBrowserReplies) {
      FailAll();
      return;
    }
    replies_.push_back(std::move(message));
    requests_.erase(found);
  }
}

bool MediaHostAdapter::HandleStaleCastReply(
    const media_host_ipc::Message &message) {
  const auto *start = std::get_if<media_host_ipc::StartCastReply>(&message);
  if (!start || start->outcome.kind != media_host_ipc::CastStartKind::kCasting)
    return true;
  const media_host_ipc::StopCast cleanup{NextRequestId(),
                                         *start->outcome.session_generation};
  if (process_->Enqueue(cleanup))
    return true;
  FailAll();
  return false;
}

bool MediaHostAdapter::HandleCastReply(media_host_ipc::Message message,
                                       CastRequestKind kind) {
  const bool expected =
      ((kind == CastRequestKind::kDiscovery ||
        kind == CastRequestKind::kStopCast) &&
       std::holds_alternative<media_host_ipc::Ack>(message)) ||
      (kind == CastRequestKind::kListDevices &&
       std::holds_alternative<media_host_ipc::DevicePageReply>(message)) ||
      (kind == CastRequestKind::kStartCast &&
       std::holds_alternative<media_host_ipc::StartCastReply>(message)) ||
      (kind == CastRequestKind::kPollSessionEvents &&
       std::holds_alternative<media_host_ipc::SessionEventsReply>(message)) ||
      std::holds_alternative<media_host_ipc::ErrorReply>(message);
  if (!expected) {
    FailCastState();
    return false;
  }
  if (kind == CastRequestKind::kPollSessionEvents)
    poll_request_id_.reset();

  if (const auto *start =
          std::get_if<media_host_ipc::StartCastReply>(&message)) {
    if (start->outcome.kind == media_host_ipc::CastStartKind::kCasting) {
      const std::uint64_t generation = *start->outcome.session_generation;
      if (generation <= last_session_generation_) {
        FailCastState(generation);
        return false;
      }
      active_session_generation_ = generation;
      last_session_generation_ = generation;
      last_state_revision_ = 0;
      next_session_poll_ = std::chrono::steady_clock::now();
    }
  } else if (const auto *events =
                 std::get_if<media_host_ipc::SessionEventsReply>(&message)) {
    if (events->dropped_events < last_host_dropped_) {
      FailCastState();
      return false;
    }
    last_host_dropped_ = events->dropped_events;
    media_host_ipc::SessionEventsReply filtered = *events;
    filtered.events.clear();
    for (const auto &event : events->events) {
      if (!active_session_generation_ ||
          event.session_generation != *active_session_generation_ ||
          event.state_revision <= last_state_revision_) {
        continue;
      }
      last_state_revision_ = event.state_revision;
      filtered.events.push_back(event);
      if (event.phase == media_host_ipc::SessionPhase::kTerminated) {
        active_session_generation_.reset();
        poll_request_id_.reset();
      }
    }
    message = std::move(filtered);
    next_session_poll_ =
        std::chrono::steady_clock::now() + kSessionPollInterval;
  }
  return PushCastReply(std::move(message));
}

void MediaHostAdapter::MaybePollSessionEvents() {
  if (!healthy() || !active_session_generation_ || poll_request_id_ ||
      std::chrono::steady_clock::now() < next_session_poll_) {
    return;
  }
  if (!Submit(media_host_ipc::PollSessionEvents{NextRequestId()}))
    next_session_poll_ =
        std::chrono::steady_clock::now() + kSessionPollInterval;
}

bool MediaHostAdapter::PushCastReply(media_host_ipc::Message message) {
  if (cast_replies_.size() >= kMaxCastReplies) {
    FailCastState();
    cast_replies_.push_back(media_host_ipc::ErrorReply{
        "mhv-adapter", media_host_ipc::HostError::kCapacityExceeded});
    return false;
  }
  cast_replies_.push_back(std::move(message));
  return true;
}

void MediaHostAdapter::FailCastState(
    std::optional<std::uint64_t> cleanup_generation) {
  if (!cleanup_generation)
    cleanup_generation = active_session_generation_;
  if (cleanup_generation && healthy()) {
    static_cast<void>(process_->Enqueue(
        media_host_ipc::StopCast{NextRequestId(), *cleanup_generation}));
  }
  FailAll();
}

void MediaHostAdapter::InvalidateTab(const std::string &tab_id) {
  for (auto it = requests_.begin(); it != requests_.end();) {
    if (it->second.tab_id == tab_id) {
      const auto cast = cast_requests_.find(it->first);
      if (cast != cast_requests_.end() &&
          cast->second == CastRequestKind::kStartCast) {
        ++it;
        continue;
      }
      cast_requests_.erase(it->first);
      it = requests_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = candidates_.begin(); it != candidates_.end();) {
    if (it->second.tab_id == tab_id)
      it = candidates_.erase(it);
    else
      ++it;
  }
}

void MediaHostAdapter::FailAll() {
  ++cast_state_epoch_;
  if (cast_state_epoch_ == 0)
    ++cast_state_epoch_;
  requests_.clear();
  cast_requests_.clear();
  candidates_.clear();
  replies_.clear();
  planning_events_.clear();
  cast_replies_.clear();
  active_session_generation_.reset();
  poll_request_id_.reset();
  last_session_generation_ = 0;
  last_state_revision_ = 0;
  last_host_dropped_ = 0;
  next_session_poll_ = {};
}

bool MediaHostAdapter::EnsureContext(const BrowserMediaFact &fact) {
  const auto &event = fact.observation;
  const std::string tab_id = WireTabId(event.tab_id);
  const auto found = tabs_.find(tab_id);
  if (found != tabs_.end() && !found->second.closed &&
      found->second.navigation_id == event.navigation_id &&
      found->second.generation == event.generation) {
    return true;
  }
  return AdvanceNavigation(event.tab_id, event.navigation_id, event.generation);
}

std::string MediaHostAdapter::NextRequestId() {
  return "mhv-" + std::to_string(next_request_id_++);
}

}  // namespace crayon::browser::cef_shell::media_host
