#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/context/profile_context_factory.h"
#include "browser/media_host/alloy_cast_controller.h"
#include "browser/media_host/cast_entry_surface.h"
#include "browser/observation_gateway/cef_observation_bridge.h"
#include "browser/permission/cef_download_handler.h"
#include "browser/permission/permission_store.h"
#include "browser/page_markdown/cef_page_markdown_preview.h"
#include "browser/page_snapshot_gateway/cef_page_snapshot_bridge.h"
#include "browser/window/alloy_builtin_content.h"
#include "browser/window/alloy_activity_surface.h"
#include "browser/window/alloy_bookmarks.h"
#include "browser/window/alloy_downloads.h"
#include "browser/window/alloy_history.h"
#include "browser/window/alloy_interactions.h"
#include "browser/window/alloy_navigation.h"
#include "browser/window/alloy_omnibox.h"
#include "browser/window/alloy_page_markdown.h"
#include "browser/window/alloy_tab_transfer_surface.h"
#include "browser/window/alloy_page_tools.h"
#include "browser/window/alloy_profile_settings.h"
#include "browser/window/alloy_session_restore.h"
#include "browser/window/alloy_site_controls.h"
#include "browser/window/alloy_tab_strip.h"
#include "browser/window/alloy_window_coordinator.h"
#include "crayon/browser_engine/types.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_drag_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_context.h"
#include "include/cef_request_handler.h"
#include "windows/alloy_cast_overlay_win.h"
#include "windows/trusted_input_monitor_win.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window_delegate.h"

namespace crayon::browser::cef_shell::windows {

// Production owner for the Windows Alloy top-level window, BrowserViews, and
// base tab/navigation/Markdown surfaces. Remaining product feature surfaces
// are attached in PLT-SHELL-24W2.
class AlloyProductHostWin final : public CefClient,
                                  public CefLifeSpanHandler,
                                  public CefLoadHandler,
                                  public CefDisplayHandler,
                                  public CefRequestHandler,
                                  public CefPermissionHandler,
                                  public CefDownloadHandler,
                                  public CefKeyboardHandler,
                                  public CefContextMenuHandler,
                                  public CefDragHandler,
                                  public CefBrowserViewDelegate,
                                  public CefWindowDelegate,
                                  public window::AlloyBuiltinContentObserver {
 public:
  struct Dependencies final {
    localization::LocaleSnapshot locale;
    std::shared_ptr<mdv::MdvEntryController> mdv_entries;
    std::shared_ptr<mdv::MdvEditController> mdv_editing;
    page_markdown::PageMarkdownStrings page_markdown_strings;
    std::function<bool(const std::string&)> clipboard_write;
    gateway::PageSnapshotObserver* snapshot_observer = nullptr;
    std::function<bool()> snapshot_admission;
    std::function<void()> snapshot_events_ready;
    media_host::MediaHostAdapter* media_host = nullptr;
    observation::CefObservationBridge::EventsReadyCallback
        media_events_ready;
    observation::CefObservationBridge::LifecycleCallback media_lifecycle;
    permission::PermissionStore* permission_store = nullptr;
    context::ProfileContextFactory* profile_context_factory = nullptr;
    std::function<bool(CefRefPtr<CefRequestContext>)>
        register_incognito_content;
    CefRefPtr<CefRequestContext> request_context;
    std::wstring session_path;
    std::string bookmarks_path;
    std::string history_path;
    std::string download_directory;
  };

  struct Callbacks final {
    std::function<void(CefRefPtr<CefBrowser>)> browser_created;
    std::function<void()> all_closed;
  };

  AlloyProductHostWin(Dependencies dependencies, Callbacks callbacks);

  bool Start(std::string initial_url, std::string title,
             browser_engine::ProfileId profile_id);
  bool SetRequestContext(CefRefPtr<CefRequestContext> request_context);
  void ReleaseRequestContextsForShutdown();
  bool Close(bool force_close);
  bool started() const noexcept { return started_; }
  bool closed() const noexcept { return closed_; }
  CefRefPtr<CefBrowser> browser() const noexcept { return browser_; }
  window::AlloySessionFileResult session_load_result() const noexcept {
    return session_load_result_;
  }
  window::AlloySessionFileResult session_save_result() const noexcept {
    return session_save_result_;
  }
  bool daily_data_load_failed() const noexcept {
    return daily_data_load_failed_;
  }

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {
    return this;
  }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefFindHandler> GetFindHandler() override { return page_tools_; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }

  std::vector<gateway::SnapshotGatewayEvent> DrainPageSnapshots(
      std::size_t max_events);
  void TickPageMarkdown(
      std::vector<::crayon::cef_shell::ipc::content_host::Message> replies,
      bool content_host_healthy);
  std::vector<::crayon::cef_shell::gateway::GatewayEvent>
  DrainMediaObservations(std::size_t max_events);
  std::optional<std::string> TrustedPageUrl(
      std::uint32_t tab_id, std::uint64_t navigation_id) const;
  bool IsActiveTab(std::uint32_t tab_id) const;
  void NoteTrustedUserInput();
  void TickCast();

  cef_runtime_style_t GetBrowserRuntimeStyle() override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;
  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;
  bool OnKeyEvent(CefRefPtr<CefWindow> window,
                  const CefKeyEvent& event) override;
  void OnLayoutChanged(CefRefPtr<CefView> view,
                       const CefRect& new_bounds) override;
  void OnBrowserCreated(CefRefPtr<CefBrowserView> view,
                        CefRefPtr<CefBrowser> browser) override;
  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> view,
                          CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      int popup_id, const CefString& target_url,
      const CefString& target_frame_name,
      CefLifeSpanHandler::WindowOpenDisposition target_disposition,
      bool user_gesture, const CefPopupFeatures& popup_features,
      CefWindowInfo& window_info,
      CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
      CefRefPtr<CefDictionaryValue>& extra_info,
      bool* no_javascript_access) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                            bool can_go_back, bool can_go_forward) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int http_status_code) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode error_code, const CefString& error_text,
                   const CefString& failed_url) override;
  bool OnCertificateError(CefRefPtr<CefBrowser> browser,
                          cef_errorcode_t cert_error,
                          const CefString& request_url,
                          CefRefPtr<CefSSLInfo> ssl_info,
                          CefRefPtr<CefCallback> callback) override;
  bool OnRequestMediaAccessPermission(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString& requesting_origin,
      std::uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback) override;
  bool OnShowPermissionPrompt(
      CefRefPtr<CefBrowser> browser, std::uint64_t prompt_id,
      const CefString& requesting_origin,
      std::uint32_t requested_permissions,
      CefRefPtr<CefPermissionPromptCallback> callback) override;
  void OnDismissPermissionPrompt(
      CefRefPtr<CefBrowser> browser, std::uint64_t prompt_id,
      cef_permission_request_result_t result) override;
  bool OnBeforeDownload(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
      const CefString& suggested_name,
      CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
      CefRefPtr<CefDownloadItemCallback> callback) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                        const CefString& message, const CefString& source,
                        int line) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture,
                      bool is_redirect) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request, bool is_navigation, bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;
  bool OnKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                  CefEventHandle os_event) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id, EventFlags event_flags) override;
  bool OnDragEnter(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefDragData> drag_data,
                   DragOperationsMask mask) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status, int error_code,
                                 const CefString& error_string) override;

  void OnBuiltinBrowserClosing(CefRefPtr<CefBrowser> browser) override;
  void OnBuiltinRenderProcessTerminated(CefRefPtr<CefBrowser> browser) override;
  void OnBuiltinLoadEnd(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int http_status_code) override;
  void OnBuiltinTitleChange(CefRefPtr<CefBrowser> browser,
                            const CefString& title) override;
  void OnBuiltinLoadError(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame, cef_errorcode_t error_code,
                          const CefString& error_text,
                          const CefString& failed_url) override;
  void OnBuiltinAddressChange(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              const CefString& url) override;
  void OnBuiltinLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                   bool is_loading, bool can_go_back,
                                   bool can_go_forward) override;
  void OnBuiltinBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefContextMenuParams> params,
                                  CefRefPtr<CefMenuModel> model) override;
  bool OnBuiltinContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefContextMenuParams> params,
                                   int command_id,
                                   cef_event_flags_t event_flags) override;

 private:
  ~AlloyProductHostWin() override;

  window::AlloyTabController* controller() const noexcept;
  bool CreateTab(std::string url, browser_engine::ContentPurpose purpose,
                 std::optional<window::TabId> copy_advanced_from = std::nullopt);
  bool CreatePopupWindow(
      const window::AlloyWindowCoordinator::PopupRequest& request);
  bool CreateRestoredWindow(
      const browser_session::SessionWindowSnapshot& snapshot);
  void CompleteSessionRestore();
  void FailSessionRestore();
  bool IsRestorableProductSession(
      const browser_session::SessionProfileSnapshot& snapshot) const;
  void ScheduleSessionCheckpoint();
  void SaveSessionCheckpoint(std::uint64_t generation);
  bool SaveSessionCheckpointNow();
  bool InitializeDailyState(const browser_engine::ProfileId& profile_id);
  void CommitHistoryNavigation(CefRefPtr<CefBrowser> browser,
                               const std::string& address,
                               int http_status_code);
  void RecordRecentlyClosed(const window::TabSnapshot& tab);
  bool SaveBookmarks();
  bool SaveHistory();
  void ShutdownDailyState();
  bool CreateIncognitoWindow(const browser_engine::ProfileId& profile_id,
                             std::uint64_t generation);
  void FinalizeIncognitoContext(std::string profile_id,
                                std::uint64_t generation,
                                CefRefPtr<CefRequestContext> request_context);
  void FinalizeBrowserCreated(CefRefPtr<CefBrowserView> view,
                              CefRefPtr<CefBrowser> browser);
  bool OnRestoredBrowserReady();
  bool ActivateTab(window::TabId tab_id);
  std::vector<window::AlloyTabTransferTarget> TransferTargetsFor(
      const std::string& source_window_id) const;
  bool AttachTransferSurface(
      const std::string& source_window_id, CefRefPtr<CefWindow> window,
      CefRefPtr<CefPanel> toolbar,
      CefRefPtr<window::AlloyTabTransferSurface>& surface);
  bool MoveActiveTabToWindow(const std::string& source_window_id,
                             const std::string& target_window_id);
  void RefreshTransferSurfaces();
  void ActivateCreatedTab(window::TabId tab_id);
  void SyncChrome();
  void PostSyncChrome();
  bool BindActiveChrome();
  bool PrepareActiveChromeForClose(window::TabId tab_id);
  void ShutdownChromeForWindowClose();
  CefRefPtr<CefBrowser> BrowserForTab(window::TabId tab_id) const;
  std::optional<window::TabId> TabForView(CefRefPtr<CefBrowserView> view) const;
  bool Owns(CefRefPtr<CefBrowser> browser) const;
  std::optional<std::string> OwnerWindowIdForView(
      CefRefPtr<CefBrowserView> view) const;
  std::optional<std::string> OwnerWindowIdForBrowser(
      CefRefPtr<CefBrowser> browser) const;
  window::AlloyTabController* ControllerForBrowser(
      CefRefPtr<CefBrowser> browser) const;
  CefRefPtr<CefRequestContext> ContextForWindow(
      const std::string& window_id) const;
  void ReleaseClosingView(CefRefPtr<CefBrowser> browser);
  void FinalizeRendererCrash(CefRefPtr<CefBrowser> browser);
  void NotifyClosed();
  void OnMediaLifecycle(std::uint32_t tab_id, std::uint64_t navigation_id,
                        std::uint32_t generation, bool closed);
  bool BindCastForActiveTab();
  void DetachCastSurfaces();
  void ApplyCastSnapshot(media_host::AlloyCastController::Snapshot snapshot);
  void UpdateCastGeometry(
      const ::crayon::cef_shell::gateway::GatewayEvent& event);
  window::AlloySiteControls* SiteControlsFor(
      CefRefPtr<CefBrowser> browser) const;
  bool SynchronizeSiteControls(CefRefPtr<CefBrowser> browser,
                               const std::string& url);
  bool ResolvePermissions(
      CefRefPtr<CefBrowser> browser, const std::string& origin,
      const std::vector<browser_site_controls::PermissionKind>& kinds,
      std::string_view title_key, std::string_view body_key);
  bool ConfirmNative(std::string_view title_key, std::string_view body_key,
                     const std::string& detail) const;
  void ConfirmExternalProtocol(CefRefPtr<CefBrowser> browser,
                               std::string source_url,
                               std::string target_url);

  inline static constexpr char kPrimaryWindowId[] = "primary";

  Dependencies dependencies_;
  Callbacks callbacks_;
  std::unique_ptr<window::AlloyWindowCoordinator> coordinator_;
  std::unique_ptr<window::AlloyPageMarkdown> page_markdown_;
  std::unique_ptr<window::AlloyProfileSettings> profile_settings_;
  CefRefPtr<window::AlloyBuiltinContent> builtin_content_;
  std::unique_ptr<window::AlloyTabStrip> tab_strip_;
  std::unique_ptr<window::AlloyOmnibox> omnibox_;
  std::unique_ptr<window::AlloyNavigation> navigation_;
  observation::CefObservationBridge media_observation_bridge_;
  std::unique_ptr<media_host::AlloyCastController> cast_controller_;
  std::unique_ptr<CastEntrySurface> cast_surface_;
  std::unique_ptr<AlloyCastOverlayWin> cast_overlay_;
  std::map<std::uint32_t, std::uint32_t> media_generations_;
  std::vector<AlloyCastOverlayObservation> cast_observations_;
  CefRefPtr<permission::CefDownloadHandlerAdapter> download_handler_;
  std::unique_ptr<window::AlloyBookmarks> bookmarks_;
  std::unique_ptr<window::AlloyHistory> history_;
  std::unique_ptr<window::AlloyDownloads> downloads_;
  CefRefPtr<window::AlloyActivitySurface> activity_surface_;
  CefRefPtr<window::AlloyTabTransferSurface> transfer_surface_;
  std::map<window::TabId, std::unique_ptr<window::AlloySiteControls>>
      site_controls_;
  std::map<window::TabId, std::string> site_origins_;
  std::map<window::TabId, std::string> site_urls_;
  std::map<window::TabId, std::string> tab_titles_;
  std::map<window::TabId, std::uint64_t> history_committed_generations_;
  CefRefPtr<window::AlloyInteractions> interactions_;
  CefRefPtr<window::AlloyPageTools> page_tools_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  std::map<window::TabId, CefRefPtr<CefBrowserView>> views_;
  struct PopupWindowRecord final {
    std::string window_id;
    CefRefPtr<CefWindow> window;
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefBrowser> browser;
    CefRefPtr<CefRequestContext> request_context;
    std::unique_ptr<window::AlloyOmnibox> omnibox;
    std::unique_ptr<window::AlloyNavigation> navigation;
    CefRefPtr<CefPanel> toolbar;
    CefRefPtr<window::AlloyTabTransferSurface> transfer_surface;
    std::map<window::TabId, CefRefPtr<CefBrowserView>> views;
    std::map<window::TabId, CefRefPtr<CefBrowser>> browsers;
    window::TabId tab_id = 0;
    std::uint64_t incognito_generation = 0;
    bool incognito = false;
    bool closing = false;
  };
  std::map<std::string, PopupWindowRecord> popup_windows_;
  std::map<int, std::string> transferred_tab_titles_;
  std::map<int, std::uint64_t> transferred_history_generations_;
  std::deque<std::string> pending_popup_windows_;
  std::deque<std::string> pending_restored_windows_;
  std::deque<browser_session::SessionWindowSnapshot>
      pending_session_restore_;
  std::vector<std::string> restoring_window_ids_;
  std::size_t pending_restored_browsers_ = 0;
  std::map<std::uint64_t, CefRefPtr<CefRequestContext>>
      pending_incognito_contexts_;
  std::string title_;
  std::string profile_id_value_;
  window::TabId tab_id_ = 0;
  std::uint64_t next_navigation_id_ = 1;
  std::uint64_t cast_browser_session_ = 1;
  std::uint64_t cast_retry_after_ms_ = 0;
  TrustedExternalProtocolInput external_protocol_input_;
  std::uint64_t session_checkpoint_generation_ = 0;
  bool session_checkpoint_pending_ = false;
  window::AlloySessionFileResult session_load_result_ =
      window::AlloySessionFileResult::kNotFound;
  window::AlloySessionFileResult session_save_result_ =
      window::AlloySessionFileResult::kNotFound;
  int chrome_browser_id_ = 0;
  bool started_ = false;
  bool session_writes_enabled_ = false;
  bool session_restore_failed_ = false;
  bool bookmarks_writes_enabled_ = false;
  bool history_writes_enabled_ = false;
  bool daily_data_load_failed_ = false;
  bool primary_closing_ = false;
  bool closing_ = false;
  bool closed_ = false;

  IMPLEMENT_REFCOUNTING(AlloyProductHostWin);
  DISALLOW_COPY_AND_ASSIGN(AlloyProductHostWin);
};

}  // namespace crayon::browser::cef_shell::windows
