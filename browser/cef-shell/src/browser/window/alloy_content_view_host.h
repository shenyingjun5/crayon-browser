#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>

#include "crayon/browser_shell/content_view_registry.h"
#include "crayon/browser_engine/types.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_panel.h"

namespace crayon::browser::cef_shell::window {

class AlloyContentViewHost final {
public:
  explicit AlloyContentViewHost(std::size_t capacity);

  browser_shell::ContentViewRegistryResult
  Mount(const browser_engine::ContentViewMountRequest &request,
        CefRefPtr<CefBrowserView> view);
  browser_shell::ContentViewRegistryResult
  OnResult(const browser_engine::ContentViewResult &result);
  browser_shell::ContentViewRegistryResult
  Activate(const browser_engine::TabId &tab_id,
           browser_engine::MountEpoch epoch);
  browser_shell::ContentViewRegistryResult
  SetZoom(const browser_engine::TabId &tab_id, browser_engine::MountEpoch epoch,
          browser_engine::ZoomFactor factor);
  browser_shell::ContentViewRegistryResult
  BeginClose(const browser_engine::TabId &tab_id,
             browser_engine::MountEpoch epoch);
  browser_shell::ContentViewRegistryResult
  ReleaseViewForClose(const browser_engine::TabId &tab_id,
                      browser_engine::MountEpoch epoch);
  browser_shell::ContentViewRegistryResult
  DetachForTransfer(const browser_engine::TabId &tab_id,
                    browser_engine::MountEpoch epoch);

  CefRefPtr<CefPanel> container() const noexcept { return container_; }
  const std::optional<std::string> &active_tab_id() const noexcept {
    return active_tab_id_;
  }
  std::size_t view_count() const noexcept { return views_.size(); }
  bool ReleaseAfterClosed();

private:
  struct HostedView final {
    browser_engine::MountEpoch epoch;
    CefRefPtr<CefBrowserView> view;
  };

  const browser_shell::ContentViewRecord *
  FindCurrent(const browser_engine::TabId &tab_id,
              browser_engine::MountEpoch epoch) const noexcept;
  void RemoveView(const browser_engine::TabId &tab_id);

  browser_shell::ContentViewRegistry registry_;
  CefRefPtr<CefPanel> container_;
  std::map<std::string, HostedView> views_;
  std::optional<std::string> active_tab_id_;
};

} // namespace crayon::browser::cef_shell::window
