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

  CommandResult EmitPermissionRequest(const PermissionRequest& request);
  CommandResult EmitTrustedInput(const TrustedInputFact& fact);
  CommandResult EmitObservation(const ObservationEvent& event);
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

  CommandResult RequireRunning() const noexcept;
  CommandResult RequireTab(const TabId& tab_id) const noexcept;
  bool IsCurrentNavigation(const TabId& tab_id,
                           NavigationId navigation_id) const noexcept;
  void QueueProfileEvent(ProfileEvent event);
  void QueueTabEvent(TabEvent event);
  void QueueNavigation(const TabId& tab_id, const BrowserUrl& url);

  EngineEventSink* event_sink_ = nullptr;
  bool stopped_ = false;
  std::map<ProfileId, ProfileConfig> profiles_;
  std::set<ProfileId> retired_profiles_;
  std::map<TabId, TabState> tabs_;
  std::set<TabId> retired_tabs_;
  std::map<SubscriptionId, ObservationSubscription> subscriptions_;
  std::set<SubscriptionId> retired_subscriptions_;
  std::map<PermissionRequestId, PermissionRequest> pending_permissions_;
  std::set<PermissionRequestId> resolved_permissions_;
  std::deque<std::function<void(EngineEventSink&)>> pending_events_;
  std::uint64_t next_navigation_id_ = 1;
};

}  // namespace crayon::browser_engine::testing
