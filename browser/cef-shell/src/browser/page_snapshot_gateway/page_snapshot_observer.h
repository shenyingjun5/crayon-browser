#pragma once

#include "crayon/browser_engine/snapshot.h"

namespace crayon::browser::cef_shell::gateway {

class PageSnapshotObserver {
 public:
  virtual ~PageSnapshotObserver() = default;
  virtual void OnSnapshotStarted(
      const browser_engine::SnapshotRequest& request) = 0;
  virtual void OnSnapshotCancelled(
      const browser_engine::SnapshotRequestId& request_id) = 0;
  virtual void OnSnapshotNavigation(
      const browser_engine::TabId& tab_id,
      browser_engine::NavigationId navigation_id) = 0;
  virtual void OnSnapshotClosed(const browser_engine::TabId& tab_id) = 0;
  virtual void OnSnapshotShutdown() = 0;
};

}  // namespace crayon::browser::cef_shell::gateway
