#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>

#include "crayon/browser_engine/adapter.h"

namespace crayon::browser_engine::testing {

class FakeBrowserEngine final : public BrowserEngineAdapter {
 public:
  FakeBrowserEngine() = default;
  ~FakeBrowserEngine() override;

  FakeBrowserEngine(const FakeBrowserEngine&) = delete;
  FakeBrowserEngine& operator=(const FakeBrowserEngine&) = delete;

  CommandResult Start(EngineEventSink& event_sink) override;
  CommandResult Stop() noexcept override;
  CommandResult CreateProfile(const ProfileConfig& config) override;
  CommandResult DestroyProfile(const ProfileId& profile_id) override;
  CommandResult CreateTab(const TabCreateRequest& request) override;
  CommandResult CloseTab(const TabId& tab_id) override;
  CommandResult Navigate(const NavigationRequest& request) override;
  CommandResult GoBack(const TabId& tab_id) override;
  CommandResult GoForward(const TabId& tab_id) override;
  CommandResult Reload(const TabId& tab_id) override;
  CommandResult StopLoading(const TabId& tab_id) override;
  CommandResult SetZoom(const TabId& tab_id, ZoomFactor zoom) override;
  CommandResult ResolvePermission(
      const PermissionResolution& resolution) override;
  CommandResult Subscribe(const ObservationSubscription& subscription) override;
  CommandResult Unsubscribe(const SubscriptionId& subscription_id) override;
  CommandResult StartSnapshot(const SnapshotRequest& request,
                              SnapshotStreamSink& sink) override;
  CommandResult CancelSnapshot(const SnapshotRequestId& request_id) override;

  CommandResult EmitPermissionRequest(const PermissionRequest& request);
  CommandResult EmitTrustedInput(const TrustedInputFact& fact);
  CommandResult EmitObservation(const ObservationEvent& event);
  CommandResult EmitSnapshotChunk(const SnapshotChunk& chunk);
  CommandResult CompleteSnapshot(const SnapshotRequestId& request_id);
  std::size_t DispatchEvents();
  std::size_t pending_event_count() const noexcept {
    return pending_events_.size();
  }

 private:
  struct TabState final {
    ProfileId profile_id;
    std::optional<BrowserUrl> current_url;
    NavigationId navigation_id;
  };

  struct SnapshotState final {
    SnapshotRequest request;
    SnapshotStreamSink* sink = nullptr;
    std::uint32_t next_sequence = 0;
    std::size_t fact_count = 0;
    std::size_t byte_count = 0;
    bool terminal_queued = false;
  };

  CommandResult RequireRunning() const noexcept;
  CommandResult RequireTab(const TabId& tab_id) const noexcept;
  bool IsCurrentNavigation(const TabId& tab_id,
                           NavigationId navigation_id) const noexcept;
  void QueueProfileEvent(ProfileEvent event);
  void QueueTabEvent(TabEvent event);
  void QueueNavigation(const TabId& tab_id, const BrowserUrl& url);
  void TerminateSnapshot(const SnapshotRequestId& request_id,
                         SnapshotTerminalStatus status, EngineErrorCode error);
  void TerminateSnapshots(const TabId& tab_id, SnapshotTerminalStatus status,
                          EngineErrorCode error);

  EngineEventSink* event_sink_ = nullptr;
  bool stopped_ = false;
  std::map<ProfileId, ProfileConfig> profiles_;
  std::set<ProfileId> retired_profiles_;
  std::map<TabId, TabState> tabs_;
  std::set<TabId> retired_tabs_;
  std::map<SubscriptionId, ObservationSubscription> subscriptions_;
  std::set<SubscriptionId> retired_subscriptions_;
  std::map<PermissionRequestId, PermissionRequest> pending_permissions_;
  std::map<SnapshotRequestId, SnapshotState> snapshots_;
  std::set<SnapshotRequestId> retired_snapshots_;
  std::set<PermissionRequestId> resolved_permissions_;
  std::deque<std::function<void(EngineEventSink&)>> pending_events_;
  std::deque<std::function<void()>> pending_snapshot_events_;
  std::uint64_t next_navigation_id_ = 1;
};

}  // namespace crayon::browser_engine::testing
