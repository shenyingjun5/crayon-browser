#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_CONTROLLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_CONTROLLER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "browser/page_snapshot_gateway/cef_page_snapshot_bridge.h"
#include "browser/permission/cef_download_handler.h"
#include "browser/permission/cef_permission_handler.h"
#include "browser/permission/permission_store.h"
#include "browser/window/tab_model.h"
#include "include/cef_client.h"
#include "include/cef_command_handler.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_drag_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/wrapper/cef_message_router.h"

namespace crayon::browser::cef_shell::window {

class TabController;

// Normalizes CEF browser callbacks into TabModel state transitions. The
// controller is owned by the application and outlives every browser.
// All methods run on the CEF UI thread.
class WindowClient final : public CefClient,
                           public CefLifeSpanHandler,
                           public CefDisplayHandler,
                           public CefLoadHandler,
                           public CefRequestHandler,
                           public CefFocusHandler,
                           public CefCommandHandler,
                           public CefDragHandler,
                           public CefContextMenuHandler,
                           public CefKeyboardHandler {
 public:
  WindowClient(TabController* controller,
               permission::PermissionStore* permission_store);

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading,
                            bool canGoBack, bool canGoForward) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status, int error_code,
                                 const CefString& error_string) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture,
                      bool is_redirect) override;
  void OnGotFocus(CefRefPtr<CefBrowser> browser) override;
  bool OnChromeCommand(CefRefPtr<CefBrowser> browser, int command_id,
                       cef_window_open_disposition_t disposition) override;
  bool OnDragEnter(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefDragData> dragData,
                   DragOperationsMask mask) override;
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id, EventFlags event_flags) override;
  bool OnKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                  CefEventHandle os_event) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  std::optional<browser_engine::SnapshotRequestId> StartPageSnapshot(
      CefRefPtr<CefBrowser> browser, std::uint64_t tab_id,
      std::uint64_t navigation_id, browser_engine::SnapshotMode mode);
  std::vector<gateway::SnapshotGatewayEvent> DrainPageSnapshots(
      std::size_t max_events);
  gateway::SnapshotGatewayResult CancelPageSnapshot(
      const browser_engine::SnapshotRequestId& request_id);
  void SetPageSnapshotObserver(gateway::PageSnapshotObserver* observer) {
    page_snapshot_bridge_.SetObserver(observer);
  }
  void AdvancePageSnapshotNavigation(CefRefPtr<CefBrowser> browser,
                                     std::uint64_t tab_id,
                                     std::uint64_t navigation_id);
  void ClosePageSnapshotBrowser(CefRefPtr<CefBrowser> browser,
                                std::uint64_t tab_id, bool renderer_gone);

  // Permission handlers: default-deny unless explicitly allowed by the store.
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {
    return permission_handler_;
  }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override {
    return download_handler_;
  }

 private:
  TabController* controller_;
  CefRefPtr<permission::CefPermissionHandlerAdapter> permission_handler_;
  CefRefPtr<permission::CefDownloadHandlerAdapter> download_handler_;

  /// Lazily creates the page router on the CEF UI thread (the client is
  /// constructed before CefInitialize).
  CefMessageRouterBrowserSide* EnsurePageRouter();
  CefRefPtr<CefMessageRouterBrowserSide> page_router_;
  gateway::CefPageSnapshotBridge page_snapshot_bridge_;

  IMPLEMENT_REFCOUNTING(WindowClient);
  DISALLOW_COPY_AND_ASSIGN(WindowClient);
};

// Single owner of the browser window/tab lifecycle. Windows are Chrome-style
// browser windows created through CefBrowserHost::CreateBrowser, so the tab
// strip, omnibox and window chrome come from CEF itself; this controller only
// tracks state and executes commands. Shared by the Windows and macOS shells.
// All methods run on the CEF UI thread.
class TabController final : public CefBaseRefCounted {
 public:
  using BrowserCreatedCallback =
      std::function<void(CefRefPtr<CefBrowser> browser)>;
  using ChromeCommandCallback = std::function<void(int command_id)>;
  using BrowsersClosedCallback = std::function<void()>;
  using PageLoadCompletedCallback =
      std::function<void(CefRefPtr<CefBrowser> browser)>;
  using PageSnapshotEventsReadyCallback = std::function<void()>;

  explicit TabController(
      std::string initial_url,
      BrowserCreatedCallback browser_created_callback = {},
      std::optional<std::string> new_tab_url = std::nullopt,
      permission::PermissionStore* permission_store = nullptr);

  // Creates the first Chrome-style browser window. Returns false when CEF
  // rejected the browser creation.
  bool CreateMainWindow();

  // Commands. Unknown or closing tabs are ignored.
  void NewWindow();
  void CloseActiveTab();
  void GoBack();
  void GoForward();
  void Reload();
  void Stop();
  void ZoomIn();
  void ZoomOut();
  void ResetZoom();

  void CloseAllBrowsers(bool force_close);
  // Brings back a window after a Dock reopen: creates a fresh window when no
  // browser is alive, otherwise leaves existing windows untouched.
  void ShowMainWindow();

  const TabModel& model() const noexcept { return model_; }
  CefRefPtr<WindowClient> client() const { return client_; }
  permission::PermissionStore* permission_store() const {
    return permission_store_;
  }
  void SetChromeCommandCallback(ChromeCommandCallback callback);

  // MDV-09 local-entry hooks: consulted by WindowClient before the
  // default behavior; a true return swallows the command/navigation.
  // Both optional, owned by the shell assembly.
  using LocalEntryCommandHandler =
      std::function<bool(CefRefPtr<CefBrowser> browser, int command_id)>;
  using NavigationInterceptor = std::function<bool(
      CefRefPtr<CefBrowser> browser, const CefString& url, bool user_gesture)>;
  void SetLocalEntryCommandHandler(LocalEntryCommandHandler handler);
  void SetNavigationInterceptor(NavigationInterceptor interceptor);

  // MDV-10 page-query delegate (crayon://mdv editing binding).  The
  // WindowClient owns the browser-side message router and forwards
  // queries to this delegate.
  using PageQueryHandler = std::function<bool(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t, const CefString&,
      bool, CefRefPtr<CefMessageRouterBrowserSide::Callback>)>;
  void SetPageQueryHandler(PageQueryHandler handler);

  // MDV-11: local-entry drag and context-menu delegates (consulted
  // before default behavior; true = handled).  Optional.
  using LocalEntryDragHandler =
      std::function<bool(CefRefPtr<CefBrowser>, CefRefPtr<CefDragData>,
                         CefDragHandler::DragOperationsMask)>;
  using ContextMenuAugmenter =
      std::function<bool(CefRefPtr<CefBrowser>, CefRefPtr<CefContextMenuParams>,
                         CefRefPtr<CefMenuModel>)>;
  using ContextMenuCommandHandler =
      std::function<bool(CefRefPtr<CefBrowser>, int)>;
  void SetLocalEntryDragHandler(LocalEntryDragHandler handler);
  void SetContextMenuAugmenter(ContextMenuAugmenter augmenter);
  void SetContextMenuCommandHandler(ContextMenuCommandHandler handler);

  // MDV-11: Ctrl+S save hook (browser-level keyboard interception; the
  // accelerator table does not reliably surface Ctrl+S in this runtime).
  using SaveCommandHandler = std::function<bool(CefRefPtr<CefBrowser>)>;
  void SetSaveCommandHandler(SaveCommandHandler handler);
  bool HandleSaveKey(CefRefPtr<CefBrowser> browser);

  bool HandleLocalEntryDrag(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefDragData> dragData,
                            CefDragHandler::DragOperationsMask mask);
  bool HandleContextMenuAugment(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefContextMenuParams> params,
                                CefRefPtr<CefMenuModel> model);
  bool HandleContextMenuCommand(CefRefPtr<CefBrowser> browser, int command_id);
  bool HandlePageQuery(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      int64_t query_id, const CefString& request, bool persistent,
      CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  /// Consults the local-entry command handler (true = swallowed).
  bool HandleLocalEntryCommand(CefRefPtr<CefBrowser> browser, int command_id);
  /// Consults the navigation interceptor (true = cancelled).
  bool InterceptNavigation(CefRefPtr<CefBrowser> browser, const CefString& url,
                           bool user_gesture);
  void SetBrowsersClosedCallback(BrowsersClosedCallback callback);
  void SetPageLoadCompletedCallback(PageLoadCompletedCallback callback);
  void SetPageSnapshotEventsReadyCallback(
      PageSnapshotEventsReadyCallback callback);
  void SetPageSnapshotObserver(gateway::PageSnapshotObserver* observer);
  void SetPageSnapshotAdmission(std::function<bool()> admission);
  void OnPageSnapshotEventsReady();

  std::optional<browser_engine::SnapshotRequestId> StartPageSnapshot(
      CefRefPtr<CefBrowser> browser,
      browser_engine::SnapshotMode mode =
          browser_engine::SnapshotMode::kStandard);
  std::vector<gateway::SnapshotGatewayEvent> DrainPageSnapshots(
      std::size_t max_events);
  gateway::SnapshotGatewayResult CancelPageSnapshot(
      const browser_engine::SnapshotRequestId& request_id);

  // Normalized callbacks from WindowClient.
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser);
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser);
  void OnBrowserFocused(CefRefPtr<CefBrowser> browser);
  void OnAddressUpdated(CefRefPtr<CefBrowser> browser, const std::string& url);
  void OnLoadingUpdated(CefRefPtr<CefBrowser> browser, bool is_loading,
                        bool can_go_back, bool can_go_forward);
  void OnRenderProcessGone(CefRefPtr<CefBrowser> browser);
  void OnChromeCommand(int command_id);
  bool RedirectBuiltInNewTab(CefRefPtr<CefFrame> frame,
                             const std::string& target_url);

 private:
  bool CreateBrowserWindow();
  CefRefPtr<CefBrowser> ActiveBrowser() const;
  void ApplyZoom(TabId tab_id);

  const std::string initial_url_;
  const BrowserCreatedCallback browser_created_callback_;
  const std::optional<std::string> new_tab_url_;
  ChromeCommandCallback chrome_command_callback_;
  LocalEntryCommandHandler local_entry_command_handler_;
  NavigationInterceptor navigation_interceptor_;
  PageQueryHandler page_query_handler_;
  LocalEntryDragHandler local_entry_drag_handler_;
  ContextMenuAugmenter context_menu_augmenter_;
  ContextMenuCommandHandler context_menu_command_handler_;
  SaveCommandHandler save_command_handler_;
  BrowsersClosedCallback browsers_closed_callback_;
  PageLoadCompletedCallback page_load_completed_callback_;
  PageSnapshotEventsReadyCallback page_snapshot_events_ready_callback_;
  std::function<bool()> page_snapshot_admission_;
  TabModel model_;
  permission::PermissionStore* permission_store_;
  CefRefPtr<WindowClient> client_;
  std::map<int, CefRefPtr<CefBrowser>> browsers_;
  std::size_t pending_new_tab_commands_ = 0;
  bool close_initiated_ = false;

  IMPLEMENT_REFCOUNTING(TabController);
  DISALLOW_COPY_AND_ASSIGN(TabController);
};

}  // namespace crayon::browser::cef_shell::window

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_CONTROLLER_H_
