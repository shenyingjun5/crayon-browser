#pragma once

#include "crayon/browser_engine/event_sink.h"
#include "crayon/browser_engine/result.h"
#include "crayon/browser_engine/types.h"

namespace crayon::browser_engine {

class BrowserEngineAdapter {
 public:
  virtual ~BrowserEngineAdapter() = default;

  virtual CommandResult Start(EngineEventSink& event_sink) = 0;
  virtual CommandResult Stop() noexcept = 0;

  virtual CommandResult CreateProfile(const ProfileConfig& config) = 0;
  virtual CommandResult DestroyProfile(const ProfileId& profile_id) = 0;

  virtual CommandResult CreateTab(const TabCreateRequest& request) = 0;
  virtual CommandResult CloseTab(const TabId& tab_id) = 0;
  virtual CommandResult Navigate(const NavigationRequest& request) = 0;
  virtual CommandResult GoBack(const TabId& tab_id) = 0;
  virtual CommandResult GoForward(const TabId& tab_id) = 0;
  virtual CommandResult Reload(const TabId& tab_id) = 0;
  virtual CommandResult StopLoading(const TabId& tab_id) = 0;
  virtual CommandResult SetZoom(const TabId& tab_id, ZoomFactor zoom) = 0;

  virtual CommandResult ResolvePermission(
      const PermissionResolution& resolution) = 0;
  virtual CommandResult Subscribe(
      const ObservationSubscription& subscription) = 0;
  virtual CommandResult Unsubscribe(const SubscriptionId& subscription_id) = 0;
};

}  // namespace crayon::browser_engine
