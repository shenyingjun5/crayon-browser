#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "browser/window/alloy_content_view_host.h"
#include "browser/window/tab_model.h"
#include "crayon/browser_engine/content_view.h"
#include "crayon/browser_session/session_snapshot.h"
#include "crayon/browser_tabs/advanced_tab_strip_state_machine.h"
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"

namespace crayon::browser::cef_shell::window {

// Owns tab identity and asynchronous BrowserView lifecycle for one Alloy
// window. CEF callbacks must be forwarded on the browser UI thread.
class AlloyTabController final {
public:
  struct TransferredTab final {
    TabId original_id;
    browser_engine::ContentViewMountRequest previous_mount;
    browser_engine::ContentCapabilitySet capabilities;
    TabSnapshot snapshot;
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefBrowser> browser;
    bool pinned;
    bool muted;
    std::optional<std::string> group;
  };

  explicit AlloyTabController(browser_engine::ProfileId profile_id,
                              std::size_t capacity = kMaximumTabsPerWindow);

  std::optional<TabId> BeginCreate(CefRefPtr<CefBrowserView> view,
                                   browser_engine::ContentPurpose purpose,
                                   browser_engine::NavigationId navigation_id);
  std::optional<TabId>
  BeginRestore(CefRefPtr<CefBrowserView> view,
               browser_engine::NavigationId navigation_id,
               const browser_session::SessionTabSnapshot &snapshot);
  bool OnBrowserCreated(CefRefPtr<CefBrowserView> view,
                        CefRefPtr<CefBrowser> browser,
                        browser_engine::ContentCapabilitySet capabilities);
  bool SynchronizeRuntimeState(CefRefPtr<CefBrowser> browser);
  bool OnAddressChange(CefRefPtr<CefBrowser> browser, std::string address);
  bool OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                            bool can_go_back, bool can_go_forward);
  bool OnCreateFailed(CefRefPtr<CefBrowserView> view,
                      browser_engine::EngineErrorCode error);
  bool Activate(TabId tab_id);
  bool PinTab(TabId tab_id, bool pinned);
  bool MuteTab(TabId tab_id, bool muted);
  bool SetTabGroup(TabId tab_id, std::optional<std::string> group);
  bool CopyAdvancedState(TabId source_tab_id, TabId target_tab_id);
  std::vector<TabId> SearchTabs(const std::string &query) const;
  std::vector<TabId> advanced_ordered_tabs() const;
  bool IsPinned(TabId tab_id) const;
  bool IsMuted(TabId tab_id) const;
  std::optional<std::string> TabGroup(TabId tab_id) const;
  std::optional<std::vector<browser_session::SessionTabSnapshot>>
  SnapshotSessionTabs() const;
  std::optional<std::size_t> active_tab_index() const;
  bool BeginClose(TabId tab_id);
  bool RequestClose(TabId tab_id, bool force_close = false);
  bool CancelClose(TabId tab_id);
  bool OnDoClose(CefRefPtr<CefBrowser> browser);
  bool ReleaseAfterDoClose(CefRefPtr<CefBrowser> browser);
  bool OnBeforeClose(CefRefPtr<CefBrowser> browser);
  bool OnRenderProcessGone(CefRefPtr<CefBrowser> browser);
  bool FinalizeRendererCrash(CefRefPtr<CefBrowser> browser);
  bool RequestNextClose(bool force_close = false);
  std::optional<TransferredTab> TransferOut(TabId tab_id);
  std::optional<TabId> AdoptTransfer(TransferredTab transfer,
                                     bool preserve_id = false);
  void CloseAll(bool force_close);

  const TabModel &model() const noexcept { return model_; }
  bool OwnsView(CefRefPtr<CefBrowserView> view) const;
  bool OwnsBrowser(CefRefPtr<CefBrowser> browser) const;
  CefRefPtr<CefPanel> container() const noexcept { return host_.container(); }
  std::size_t pending_count() const noexcept { return records_.size(); }
  bool ReleaseAfterClosed();

private:
  struct Record final {
    browser_engine::ContentViewMountRequest mount;
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefBrowser> browser;
    browser_engine::ContentCapabilitySet capabilities;
    bool close_requested;
    bool view_released;
    bool terminal_reported;
    bool restore_muted;
  };

  using RecordMap = std::map<TabId, Record>;
  RecordMap::iterator FindByView(CefRefPtr<CefBrowserView> view);
  RecordMap::const_iterator FindByView(CefRefPtr<CefBrowserView> view) const;
  RecordMap::iterator FindByBrowser(CefRefPtr<CefBrowser> browser);
  RecordMap::const_iterator FindByBrowser(CefRefPtr<CefBrowser> browser) const;
  static std::optional<browser_engine::TabId> EngineTabId(TabId tab_id);
  static std::string AdvancedId(TabId tab_id);

  browser_engine::ProfileId profile_id_;
  AlloyContentViewHost host_;
  TabModel model_;
  browser_tabs::AdvancedTabStripStateMachine advanced_;
  RecordMap records_;
  std::uint64_t next_mount_epoch_ = 1;
};

} // namespace crayon::browser::cef_shell::window
