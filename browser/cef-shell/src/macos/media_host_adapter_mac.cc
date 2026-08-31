#include "macos/media_host_adapter_mac.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

namespace crayon::browser::cef_shell::macos {
namespace {

constexpr std::size_t kMaxTrackedRequests = 256;
constexpr std::size_t kMaxTrackedCandidates = 256;
constexpr std::size_t kMaxTrackedTabs = 64;
constexpr std::size_t kMaxBrowserReplies = 64;
constexpr std::uint32_t kVisibleAreaScale = 1'000'000;

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
                      std::is_same_v<T, media_host_ipc::ErrorReply>)
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
                      std::is_same_v<T, media_host_ipc::CloseTab>)
          return value.request_id;
        return {};
      },
      message);
}

} // namespace

MediaHostAdapter::MediaHostAdapter()
    : MediaHostAdapter(std::make_unique<MediaHostProcess>()) {}

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
  if (!process_->Enqueue(std::move(message))) {
    FailAll();
    tabs_.clear();
    saw_healthy_ = false;
    process_generation_ = 0;
    return false;
  }
  if (!request_id.empty())
    requests_[request_id] = std::move(context);
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

void MediaHostAdapter::Tick() { PollReplies(); }

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
    if (!found->second.tab_id.empty() && !Current(found->second)) {
      requests_.erase(found);
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

void MediaHostAdapter::InvalidateTab(const std::string &tab_id) {
  for (auto it = requests_.begin(); it != requests_.end();) {
    if (it->second.tab_id == tab_id)
      it = requests_.erase(it);
    else
      ++it;
  }
  for (auto it = candidates_.begin(); it != candidates_.end();) {
    if (it->second.tab_id == tab_id)
      it = candidates_.erase(it);
    else
      ++it;
  }
}

void MediaHostAdapter::FailAll() {
  requests_.clear();
  candidates_.clear();
  replies_.clear();
  planning_events_.clear();
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

} // namespace crayon::browser::cef_shell::macos
