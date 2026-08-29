#include "fake_browser_engine.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace crayon::browser_engine::testing {

FakeBrowserEngine::~FakeBrowserEngine() { Stop(); }

CommandResult FakeBrowserEngine::Start(EngineEventSink& event_sink) {
  if (stopped_) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidState);
  }
  if (event_sink_ == &event_sink) {
    return CommandResult::Accepted();
  }
  if (event_sink_ != nullptr) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidState);
  }
  event_sink_ = &event_sink;
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::Stop() noexcept {
  if (stopped_) {
    return CommandResult::Accepted();
  }
  stopped_ = true;
  event_sink_ = nullptr;
  pending_events_.clear();
  profiles_.clear();
  tabs_.clear();
  subscriptions_.clear();
  pending_permissions_.clear();
  snapshots_.clear();
  pending_snapshot_events_.clear();
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::CreateProfile(const ProfileConfig& config) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (!IsValid(config.mode)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  if (profiles_.count(config.profile_id) != 0 ||
      retired_profiles_.count(config.profile_id) != 0) {
    return CommandResult::Rejected(EngineErrorCode::kAlreadyExists);
  }
  profiles_.emplace(config.profile_id, config);
  QueueProfileEvent(
      ProfileEvent{ProfileEventKind::kCreated, config.profile_id});
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::DestroyProfile(const ProfileId& profile_id) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (retired_profiles_.count(profile_id) != 0) {
    return CommandResult::Accepted();
  }
  const auto profile = profiles_.find(profile_id);
  if (profile == profiles_.end()) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  const bool has_active_tab =
      std::any_of(tabs_.begin(), tabs_.end(), [&profile_id](const auto& item) {
        return item.second.profile_id == profile_id;
      });
  if (has_active_tab) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidState);
  }
  profiles_.erase(profile);
  retired_profiles_.insert(profile_id);
  QueueProfileEvent(ProfileEvent{ProfileEventKind::kDestroyed, profile_id});
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::CreateTab(const TabCreateRequest& request) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (profiles_.count(request.profile_id) == 0) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  if (tabs_.count(request.tab_id) != 0 ||
      retired_tabs_.count(request.tab_id) != 0) {
    return CommandResult::Rejected(EngineErrorCode::kAlreadyExists);
  }
  tabs_.emplace(request.tab_id,
                TabState{request.profile_id, request.initial_url,
                         NavigationId::FromRaw(0)});
  QueueTabEvent(
      TabEvent{TabEventKind::kCreated, request.profile_id, request.tab_id});
  if (request.initial_url.has_value()) {
    QueueNavigation(request.tab_id, *request.initial_url);
  }
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::CloseTab(const TabId& tab_id) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (retired_tabs_.count(tab_id) != 0) {
    return CommandResult::Accepted();
  }
  const auto tab = tabs_.find(tab_id);
  if (tab == tabs_.end()) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  const auto profile_id = tab->second.profile_id;
  TerminateSnapshots(tab_id, SnapshotTerminalStatus::kCancelled,
                     EngineErrorCode::kNone);
  std::vector<SubscriptionId> removed_subscriptions;
  for (const auto& item : subscriptions_) {
    if (item.second.tab_id == tab_id) {
      removed_subscriptions.push_back(item.first);
    }
  }
  for (const auto& subscription_id : removed_subscriptions) {
    subscriptions_.erase(subscription_id);
    retired_subscriptions_.insert(subscription_id);
  }
  for (auto permission = pending_permissions_.begin();
       permission != pending_permissions_.end();) {
    if (permission->second.tab_id == tab_id) {
      permission = pending_permissions_.erase(permission);
    } else {
      ++permission;
    }
  }
  tabs_.erase(tab);
  retired_tabs_.insert(tab_id);
  QueueTabEvent(TabEvent{TabEventKind::kClosed, profile_id, tab_id});
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::Navigate(const NavigationRequest& request) {
  if (const auto state = RequireTab(request.tab_id); !state.accepted()) {
    return state;
  }
  QueueNavigation(request.tab_id, request.url);
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::GoBack(const TabId& tab_id) {
  return RequireTab(tab_id);
}

CommandResult FakeBrowserEngine::GoForward(const TabId& tab_id) {
  return RequireTab(tab_id);
}

CommandResult FakeBrowserEngine::Reload(const TabId& tab_id) {
  return RequireTab(tab_id);
}

CommandResult FakeBrowserEngine::StopLoading(const TabId& tab_id) {
  return RequireTab(tab_id);
}

CommandResult FakeBrowserEngine::SetZoom(const TabId& tab_id, ZoomFactor zoom) {
  static_cast<void>(zoom);
  return RequireTab(tab_id);
}

CommandResult FakeBrowserEngine::ResolvePermission(
    const PermissionResolution& resolution) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (!IsValid(resolution.decision)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  if (resolved_permissions_.count(resolution.request_id) != 0) {
    return CommandResult::Accepted();
  }
  const auto pending = pending_permissions_.find(resolution.request_id);
  if (pending == pending_permissions_.end()) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  pending_permissions_.erase(pending);
  resolved_permissions_.insert(resolution.request_id);
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::Subscribe(
    const ObservationSubscription& subscription) {
  if (const auto state = RequireTab(subscription.tab_id); !state.accepted()) {
    return state;
  }
  if (!IsValid(subscription.topic)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  if (subscriptions_.count(subscription.subscription_id) != 0 ||
      retired_subscriptions_.count(subscription.subscription_id) != 0) {
    return CommandResult::Rejected(EngineErrorCode::kAlreadyExists);
  }
  subscriptions_.emplace(subscription.subscription_id, subscription);
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::Unsubscribe(
    const SubscriptionId& subscription_id) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (retired_subscriptions_.count(subscription_id) != 0) {
    return CommandResult::Accepted();
  }
  if (subscriptions_.erase(subscription_id) == 0) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  retired_subscriptions_.insert(subscription_id);
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::StartSnapshot(const SnapshotRequest& request,
                                               SnapshotStreamSink& sink) {
  if (const auto state = RequireTab(request.tab_id); !state.accepted()) {
    return state;
  }
  if (!IsValid(request.mode)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  if (request.navigation_id.value() == 0 ||
      !IsCurrentNavigation(request.tab_id, request.navigation_id)) {
    return CommandResult::Rejected(EngineErrorCode::kStaleNavigation);
  }
  if (snapshots_.count(request.request_id) != 0 ||
      retired_snapshots_.count(request.request_id) != 0) {
    return CommandResult::Rejected(EngineErrorCode::kAlreadyExists);
  }
  if (snapshots_.size() >= kMaxSnapshotStreams) {
    return CommandResult::Rejected(EngineErrorCode::kCapacityExceeded);
  }
  snapshots_.emplace(request.request_id,
                     SnapshotState{request, &sink, 0, 0, 0, false});
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::CancelSnapshot(
    const SnapshotRequestId& request_id) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (retired_snapshots_.count(request_id) != 0) {
    return CommandResult::Accepted();
  }
  const auto snapshot = snapshots_.find(request_id);
  if (snapshot == snapshots_.end()) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  TerminateSnapshot(request_id, SnapshotTerminalStatus::kCancelled,
                    EngineErrorCode::kNone);
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::EmitSnapshotChunk(const SnapshotChunk& chunk) {
  const auto snapshot = snapshots_.find(chunk.request_id);
  if (snapshot == snapshots_.end()) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  auto& state = snapshot->second;
  if (!IsValid(chunk, state.request.mode)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  const auto tab = tabs_.find(chunk.tab_id);
  if (chunk.sequence == 0 &&
      (tab == tabs_.end() || !tab->second.current_url.has_value() ||
       !(chunk.document->url == *tab->second.current_url))) {
    return CommandResult::Rejected(EngineErrorCode::kStaleNavigation);
  }
  std::size_t chunk_fact_bytes = 0;
  for (const auto& fact : chunk.facts) {
    const auto fact_bytes = SnapshotFactByteSize(fact);
    if (!fact_bytes.has_value()) {
      return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
    }
    chunk_fact_bytes += *fact_bytes;
  }
  if (chunk.facts.size() >
          SnapshotModeMaxFacts(state.request.mode) - state.fact_count ||
      chunk_fact_bytes >
          SnapshotModeMaxBytes(state.request.mode) - state.byte_count) {
    return CommandResult::Rejected(EngineErrorCode::kCapacityExceeded);
  }
  if (state.terminal_queued || chunk.tab_id != state.request.tab_id ||
      chunk.navigation_id != state.request.navigation_id ||
      chunk.sequence != state.next_sequence ||
      chunk.sequence >= kMaxSnapshotChunks) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  ++state.next_sequence;
  state.fact_count += chunk.facts.size();
  state.byte_count += chunk_fact_bytes;
  pending_snapshot_events_.push_back([this, chunk]() {
    const auto current = snapshots_.find(chunk.request_id);
    if (current != snapshots_.end() &&
        IsCurrentNavigation(chunk.tab_id, chunk.navigation_id)) {
      current->second.sink->OnSnapshotChunk(chunk);
    }
  });
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::CompleteSnapshot(
    const SnapshotRequestId& request_id) {
  const auto snapshot = snapshots_.find(request_id);
  if (snapshot == snapshots_.end()) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  if (snapshot->second.next_sequence == 0) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidState);
  }
  if (snapshot->second.terminal_queued) {
    return CommandResult::Accepted();
  }
  snapshot->second.terminal_queued = true;
  const auto request = snapshot->second.request;
  pending_snapshot_events_.push_back([this, request]() {
    const auto current = snapshots_.find(request.request_id);
    if (current == snapshots_.end()) {
      return;
    }
    auto* sink = current->second.sink;
    snapshots_.erase(current);
    retired_snapshots_.insert(request.request_id);
    sink->OnSnapshotTerminal(SnapshotTerminal{
        request.request_id, request.tab_id, request.navigation_id,
        SnapshotTerminalStatus::kCompleted, EngineErrorCode::kNone});
  });
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::EmitPermissionRequest(
    const PermissionRequest& request) {
  if (const auto state = RequireTab(request.tab_id); !state.accepted()) {
    return state;
  }
  if (!IsValid(request.permission)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  if (!IsCurrentNavigation(request.tab_id, request.navigation_id)) {
    return CommandResult::Rejected(EngineErrorCode::kStaleNavigation);
  }
  if (pending_permissions_.count(request.request_id) != 0 ||
      resolved_permissions_.count(request.request_id) != 0) {
    return CommandResult::Rejected(EngineErrorCode::kAlreadyExists);
  }
  pending_permissions_.emplace(request.request_id, request);
  pending_events_.push_back([this, request](EngineEventSink& sink) {
    if (pending_permissions_.count(request.request_id) != 0 &&
        IsCurrentNavigation(request.tab_id, request.navigation_id)) {
      sink.OnPermissionRequest(request);
    }
  });
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::EmitTrustedInput(
    const TrustedInputFact& fact) {
  if (const auto state = RequireTab(fact.tab_id); !state.accepted()) {
    return state;
  }
  if (!IsValid(fact.kind)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  if (!IsCurrentNavigation(fact.tab_id, fact.navigation_id)) {
    return CommandResult::Rejected(EngineErrorCode::kStaleNavigation);
  }
  pending_events_.push_back([this, fact](EngineEventSink& sink) {
    if (IsCurrentNavigation(fact.tab_id, fact.navigation_id)) {
      sink.OnTrustedInput(fact);
    }
  });
  return CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::EmitObservation(
    const ObservationEvent& event) {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  if (!IsValid(event.kind)) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  const auto subscription = subscriptions_.find(event.subscription_id);
  if (subscription == subscriptions_.end()) {
    return CommandResult::Rejected(EngineErrorCode::kNotFound);
  }
  if (subscription->second.tab_id != event.tab_id) {
    return CommandResult::Rejected(EngineErrorCode::kInvalidArgument);
  }
  if (!IsCurrentNavigation(event.tab_id, event.navigation_id)) {
    return CommandResult::Rejected(EngineErrorCode::kStaleNavigation);
  }
  pending_events_.push_back([this, event](EngineEventSink& sink) {
    if (subscriptions_.count(event.subscription_id) != 0 &&
        IsCurrentNavigation(event.tab_id, event.navigation_id)) {
      sink.OnObservation(event);
    }
  });
  return CommandResult::Accepted();
}

std::size_t FakeBrowserEngine::DispatchEvents() {
  std::size_t dispatched = 0;
  while (event_sink_ != nullptr && !pending_events_.empty()) {
    auto callback = std::move(pending_events_.front());
    pending_events_.pop_front();
    callback(*event_sink_);
    ++dispatched;
  }
  while (event_sink_ != nullptr && !pending_snapshot_events_.empty()) {
    auto callback = std::move(pending_snapshot_events_.front());
    pending_snapshot_events_.pop_front();
    callback();
    ++dispatched;
  }
  return dispatched;
}

CommandResult FakeBrowserEngine::RequireRunning() const noexcept {
  return event_sink_ == nullptr
             ? CommandResult::Rejected(EngineErrorCode::kInvalidState)
             : CommandResult::Accepted();
}

CommandResult FakeBrowserEngine::RequireTab(
    const TabId& tab_id) const noexcept {
  if (const auto state = RequireRunning(); !state.accepted()) {
    return state;
  }
  return tabs_.count(tab_id) == 0
             ? CommandResult::Rejected(EngineErrorCode::kNotFound)
             : CommandResult::Accepted();
}

bool FakeBrowserEngine::IsCurrentNavigation(
    const TabId& tab_id, NavigationId navigation_id) const noexcept {
  const auto tab = tabs_.find(tab_id);
  return tab != tabs_.end() && tab->second.navigation_id == navigation_id;
}

void FakeBrowserEngine::QueueProfileEvent(ProfileEvent event) {
  pending_events_.push_back([event = std::move(event)](EngineEventSink& sink) {
    sink.OnProfileEvent(event);
  });
}

void FakeBrowserEngine::QueueTabEvent(TabEvent event) {
  pending_events_.push_back([event = std::move(event)](EngineEventSink& sink) {
    sink.OnTabEvent(event);
  });
}

void FakeBrowserEngine::QueueNavigation(const TabId& tab_id,
                                        const BrowserUrl& url) {
  TerminateSnapshots(tab_id, SnapshotTerminalStatus::kStaleNavigation,
                     EngineErrorCode::kStaleNavigation);
  const auto navigation_id = NavigationId::FromRaw(next_navigation_id_++);
  auto& tab = tabs_.at(tab_id);
  tab.current_url = url;
  tab.navigation_id = navigation_id;
  for (const auto kind :
       {NavigationEventKind::kStarted, NavigationEventKind::kCommitted,
        NavigationEventKind::kCompleted}) {
    const NavigationEvent event{kind, tab_id, navigation_id, url,
                                EngineErrorCode::kNone};
    pending_events_.push_back([this, event](EngineEventSink& sink) {
      const auto current = tabs_.find(event.tab_id);
      if (current != tabs_.end() &&
          current->second.navigation_id == event.navigation_id) {
        sink.OnNavigationEvent(event);
      }
    });
  }
}

void FakeBrowserEngine::TerminateSnapshots(const TabId& tab_id,
                                           SnapshotTerminalStatus status,
                                           EngineErrorCode error) {
  std::vector<SnapshotRequestId> request_ids;
  for (const auto& entry : snapshots_) {
    if (entry.second.request.tab_id == tab_id) {
      request_ids.push_back(entry.first);
    }
  }
  for (const auto& request_id : request_ids) {
    TerminateSnapshot(request_id, status, error);
  }
}

void FakeBrowserEngine::TerminateSnapshot(const SnapshotRequestId& request_id,
                                          SnapshotTerminalStatus status,
                                          EngineErrorCode error) {
  const auto snapshot = snapshots_.find(request_id);
  if (snapshot == snapshots_.end()) {
    return;
  }
  const auto request = snapshot->second.request;
  auto* sink = snapshot->second.sink;
  snapshots_.erase(snapshot);
  retired_snapshots_.insert(request_id);
  pending_snapshot_events_.push_back([request, sink, status, error]() {
    sink->OnSnapshotTerminal(
        SnapshotTerminal{request.request_id, request.tab_id,
                         request.navigation_id, status, error});
  });
}

}  // namespace crayon::browser_engine::testing
