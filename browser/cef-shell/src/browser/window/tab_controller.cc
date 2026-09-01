#include "browser/window/tab_controller.h"

#include "browser/window/popup_target.h"

#include <cmath>
#include <utility>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/cef_id_mappers.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

// Chromium's default zoom step: one zoom level multiplies the factor by 1.2.
constexpr double kZoomStepFactor = 1.2;

double ZoomLevelForFactor(double factor) {
  return std::log(factor) / std::log(kZoomStepFactor);
}

}  // namespace

namespace {

/// Forwards router queries to the shell-assembly delegate (MDV-10).
class PageQueryRouterHandler final
    : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit PageQueryRouterHandler(TabController* controller)
      : controller_(controller) {}

  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
               int64_t query_id, const CefString& request, bool persistent,
               CefRefPtr<Callback> callback) override {
    return controller_->HandlePageQuery(browser, frame, query_id, request,
                                        persistent, std::move(callback));
  }

 private:
  TabController* controller_;
};

}  // namespace

WindowClient::WindowClient(TabController* controller,
                           permission::PermissionStore* permission_store)
    : controller_(controller) {
  if (permission_store) {
    permission_handler_ =
        new permission::CefPermissionHandlerAdapter(permission_store);
    download_handler_ =
        new permission::CefDownloadHandlerAdapter(permission_store);
  }
}

void WindowClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  controller_->OnBrowserCreated(browser);
#if defined(_WIN32)
  if (controller_->model().active_tab().has_value()) {
    // Chrome runtime keeps the first CreateBrowser'd WebContents hidden
    // until the tab strip mutates (WasHidden/show-window calls do not).
    // One synthetic new+close cycle after the window settles forces the
    // visibility recompute that IntersectionObserver-driven lazy renders
    // (Mermaid) depend on (MDV-20W).
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(
            [](CefRefPtr<TabController> controller) {
              controller->MaybeRunInitialVisibilityNudge();
            },
            CefRefPtr<TabController>(controller_)),
        300);
  }
#endif
}

bool WindowClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame, int popup_id,
                                 const CefString& target_url,
                                 const CefString& target_frame_name,
                                 CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                                 bool user_gesture,
                                 const CefPopupFeatures& popupFeatures,
                                 CefWindowInfo& windowInfo,
                                 CefRefPtr<CefClient>& client,
                                 CefBrowserSettings& settings,
                                 CefRefPtr<CefDictionaryValue>& extra_info,
                                 bool* no_javascript_access) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(frame);
  static_cast<void>(popup_id);
  static_cast<void>(target_frame_name);
  static_cast<void>(target_disposition);
  static_cast<void>(popupFeatures);
  static_cast<void>(windowInfo);
  static_cast<void>(client);
  static_cast<void>(settings);
  static_cast<void>(extra_info);
  static_cast<void>(no_javascript_access);
  // The single-window shell never spawns a standalone popup window; the
  // controller decides between a new tab and silent denial.
  return controller_->HandlePopupRequest(browser, target_url.ToString(),
                                         user_gesture);
}

bool WindowClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  return false;
}

void WindowClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  browser->GetHost()->WasHidden(true);
  const TabSnapshot* tab =
      controller_->model().FindByBrowser(browser->GetIdentifier());
  if (tab) {
    ClosePageSnapshotBrowser(browser, tab->id, false);
    CloseMediaObservationBrowser(browser, static_cast<std::uint32_t>(tab->id));
  }
  controller_->OnBrowserClosing(browser);
}

void WindowClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain()) {
    return;
  }
  const std::string target_url = url.ToString();
  if (controller_->RedirectBuiltInNewTab(frame, target_url)) {
    return;
  }
  controller_->OnAddressUpdated(browser, target_url);
}

void WindowClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                        bool isLoading, bool canGoBack,
                                        bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();
  controller_->OnLoadingUpdated(browser, isLoading, canGoBack, canGoForward);
}

void WindowClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                             TerminationStatus status,
                                             int error_code,
                                             const CefString& error_string) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(status);
  static_cast<void>(error_code);
  static_cast<void>(error_string);
  EnsurePageRouter()->OnRenderProcessTerminated(browser);
  const TabSnapshot* tab =
      controller_->model().FindByBrowser(browser->GetIdentifier());
  if (tab) {
    ClosePageSnapshotBrowser(browser, tab->id, true);
    CloseMediaObservationBrowser(browser, static_cast<std::uint32_t>(tab->id));
  }
  controller_->OnRenderProcessGone(browser);
}

bool WindowClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (media_observation_bridge_.OnProcessMessageReceived(
          browser, frame, source_process, message)) {
    return true;
  }
  if (page_snapshot_bridge_.OnProcessMessageReceived(browser, frame,
                                                     source_process, message)) {
    controller_->OnPageSnapshotEventsReady();
    return true;
  }
  return EnsurePageRouter()->OnProcessMessageReceived(browser, frame,
                                                      source_process, message);
}

CefRefPtr<CefResourceRequestHandler> WindowClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool is_navigation, bool is_download,
    const CefString& request_initiator, bool& disable_default_handling) {
  static_cast<void>(frame);
  static_cast<void>(is_navigation);
  static_cast<void>(is_download);
  static_cast<void>(request_initiator);
  disable_default_handling = false;
  CefRefPtr<WindowClient> owner(this);
  return media_observation_bridge_.CreateResourceRequestHandler(
      browser, request,
      [this](observation::CefNetworkResourceFact fact) {
        media_observation_bridge_.OnNetworkResourceFact(std::move(fact));
      },
      owner);
}

std::optional<browser_engine::SnapshotRequestId>
WindowClient::StartPageSnapshot(CefRefPtr<CefBrowser> browser,
                                std::uint64_t tab_id,
                                std::uint64_t navigation_id,
                                browser_engine::SnapshotMode mode) {
  return page_snapshot_bridge_.StartSnapshot(browser, tab_id, navigation_id,
                                             mode);
}

std::vector<gateway::SnapshotGatewayEvent> WindowClient::DrainPageSnapshots(
    std::size_t max_events) {
  CEF_REQUIRE_UI_THREAD();
  return page_snapshot_bridge_.Drain(max_events);
}

gateway::SnapshotGatewayResult WindowClient::CancelPageSnapshot(
    const browser_engine::SnapshotRequestId& request_id) {
  CEF_REQUIRE_UI_THREAD();
  return page_snapshot_bridge_.CancelSnapshot(request_id);
}

void WindowClient::AdvancePageSnapshotNavigation(CefRefPtr<CefBrowser> browser,
                                                 std::uint64_t tab_id,
                                                 std::uint64_t navigation_id) {
  page_snapshot_bridge_.AdvanceNavigation(browser, tab_id, navigation_id);
  controller_->OnPageSnapshotEventsReady();
}

void WindowClient::ClosePageSnapshotBrowser(CefRefPtr<CefBrowser> browser,
                                            std::uint64_t tab_id,
                                            bool renderer_gone) {
  if (renderer_gone) {
    page_snapshot_bridge_.RendererGone(browser, tab_id);
  } else {
    page_snapshot_bridge_.CloseBrowser(browser, tab_id);
  }
  controller_->OnPageSnapshotEventsReady();
}

void WindowClient::AdvanceMediaObservationNavigation(
    CefRefPtr<CefBrowser> browser, std::uint32_t tab_id,
    std::uint64_t navigation_id) {
  media_observation_bridge_.AdvanceNavigation(browser, tab_id, navigation_id);
}

void WindowClient::CloseMediaObservationBrowser(CefRefPtr<CefBrowser> browser,
                                                std::uint32_t tab_id) {
  media_observation_bridge_.CloseBrowser(browser, tab_id);
}

void WindowClient::SetActiveMediaObservationTab(std::uint32_t tab_id) {
  media_observation_bridge_.SetActiveTab(tab_id);
}

void WindowClient::NoteTrustedUserInput(CefRefPtr<CefBrowser> browser) {
  media_observation_bridge_.NoteTrustedUserInput(browser);
}

std::vector<::crayon::cef_shell::gateway::GatewayEvent>
WindowClient::DrainMediaObservations(std::size_t max_events) {
  return media_observation_bridge_.Drain(max_events);
}

observation::MediaObservationDiagnostics
WindowClient::media_observation_diagnostics() const {
  return media_observation_bridge_.diagnostics();
}

void WindowClient::SetMediaObservationEventsReadyCallback(
    observation::CefObservationBridge::EventsReadyCallback callback) {
  media_observation_bridge_.SetEventsReadyCallback(std::move(callback));
}

void WindowClient::SetMediaObservationLifecycleCallback(
    observation::CefObservationBridge::LifecycleCallback callback) {
  media_observation_bridge_.SetLifecycleCallback(std::move(callback));
}

CefMessageRouterBrowserSide* WindowClient::EnsurePageRouter() {
  CEF_REQUIRE_UI_THREAD();
  if (!page_router_) {
    CefMessageRouterConfig router_config;
    router_config.js_query_function = "mdvQuery";
    page_router_ = CefMessageRouterBrowserSide::Create(router_config);
    page_router_->AddHandler(new PageQueryRouterHandler(controller_), true);
  }
  return page_router_.get();
}

bool WindowClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefRequest> request,
                                  bool user_gesture, bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(is_redirect);
  if (request) {
    EnsurePageRouter()->OnBeforeBrowse(browser, frame);
  }
  if (request && controller_->InterceptNavigation(browser, request->GetURL(),
                                                  user_gesture)) {
    return true;
  }
  return request && controller_->RedirectBuiltInNewTab(
                        frame, request->GetURL().ToString());
}

bool WindowClient::OnDragEnter(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefDragData> dragData,
                               DragOperationsMask mask) {
  CEF_REQUIRE_UI_THREAD();
  return controller_->HandleLocalEntryDrag(browser, dragData, mask);
}

bool WindowClient::OnFileDialog(
    CefRefPtr<CefBrowser> browser, FileDialogMode mode, const CefString& title,
    const CefString& default_file_path,
    const std::vector<CefString>& accept_filters,
    const std::vector<CefString>& accept_extensions,
    const std::vector<CefString>& accept_descriptions,
    CefRefPtr<CefFileDialogCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  return controller_->HandleFileDialog(
      browser, mode, title, default_file_path, accept_filters,
      accept_extensions, accept_descriptions, std::move(callback));
}

void WindowClient::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       CefRefPtr<CefContextMenuParams> params,
                                       CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  controller_->HandleContextMenuAugment(browser, params, model);
}

bool WindowClient::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefContextMenuParams> params,
                                        int command_id,
                                        EventFlags event_flags) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(frame);
  static_cast<void>(params);
  static_cast<void>(event_flags);
  return controller_->HandleContextMenuCommand(browser, command_id);
}

bool WindowClient::OnKeyEvent(CefRefPtr<CefBrowser> browser,
                              const CefKeyEvent& event,
                              CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(os_event);
  // Ctrl+S: intercept before any accelerator/page handling.
  if (event.type == KEYEVENT_KEYUP &&
      (event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
      (event.windows_key_code == 'S' || event.windows_key_code == 's')) {
    return controller_->HandleSaveKey(browser);
  }
  return false;
}

bool WindowClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                 const CefKeyEvent& event,
                                 CefEventHandle os_event,
                                 bool* is_keyboard_shortcut) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(os_event);
  static_cast<void>(is_keyboard_shortcut);
  if (event.type == KEYEVENT_RAWKEYDOWN) {
    NoteTrustedUserInput(browser);
  }
  return false;
}

void WindowClient::OnGotFocus(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // Focusing implies the browser is shown; re-mark visible.
  browser->GetHost()->WasHidden(false);
  controller_->OnBrowserFocused(browser);
}

bool WindowClient::OnChromeCommand(CefRefPtr<CefBrowser> browser,
                                   int command_id,
                                   cef_window_open_disposition_t disposition) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(disposition);
  if (controller_->HandleLocalEntryCommand(browser, command_id)) {
    return true;
  }
  controller_->OnChromeCommand(command_id);
  return false;
}

TabController::TabController(std::string initial_url,
                             BrowserCreatedCallback browser_created_callback,
                             std::optional<std::string> new_tab_url,
                             permission::PermissionStore* permission_store)
    : initial_url_(std::move(initial_url)),
      browser_created_callback_(std::move(browser_created_callback)),
      new_tab_url_(std::move(new_tab_url)),
      permission_store_(permission_store),
      client_(new WindowClient(this, permission_store)) {}

bool TabController::CreateMainWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (!model_.empty()) {
    return false;
  }
  return CreateBrowserWindow();
}

void TabController::SetChromeCommandCallback(ChromeCommandCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  chrome_command_callback_ = std::move(callback);
}

void TabController::SetBrowserFocusedCallback(BrowserFocusedCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  browser_focused_callback_ = std::move(callback);
}

void TabController::SetBrowserClosingCallback(BrowserClosingCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  browser_closing_callback_ = std::move(callback);
}

void TabController::SetLocalEntryCommandHandler(
    LocalEntryCommandHandler handler) {
  CEF_REQUIRE_UI_THREAD();
  local_entry_command_handler_ = std::move(handler);
}

void TabController::SetNavigationInterceptor(
    NavigationInterceptor interceptor) {
  CEF_REQUIRE_UI_THREAD();
  navigation_interceptor_ = std::move(interceptor);
}

void TabController::SetFileDialogHandler(FileDialogHandler handler) {
  CEF_REQUIRE_UI_THREAD();
  file_dialog_handler_ = std::move(handler);
}

void TabController::SetPageQueryHandler(PageQueryHandler handler) {
  CEF_REQUIRE_UI_THREAD();
  page_query_handler_ = std::move(handler);
}

void TabController::SetSaveCommandHandler(SaveCommandHandler handler) {
  CEF_REQUIRE_UI_THREAD();
  save_command_handler_ = std::move(handler);
}

bool TabController::HandleSaveKey(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  return save_command_handler_ && save_command_handler_(browser);
}

void TabController::SetLocalEntryDragHandler(LocalEntryDragHandler handler) {
  CEF_REQUIRE_UI_THREAD();
  local_entry_drag_handler_ = std::move(handler);
}

void TabController::SetContextMenuAugmenter(ContextMenuAugmenter augmenter) {
  CEF_REQUIRE_UI_THREAD();
  context_menu_augmenter_ = std::move(augmenter);
}

void TabController::SetContextMenuCommandHandler(
    ContextMenuCommandHandler handler) {
  CEF_REQUIRE_UI_THREAD();
  context_menu_command_handler_ = std::move(handler);
}

bool TabController::HandleLocalEntryDrag(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDragData> dragData,
    CefDragHandler::DragOperationsMask mask) {
  CEF_REQUIRE_UI_THREAD();
  return local_entry_drag_handler_ &&
         local_entry_drag_handler_(browser, dragData, mask);
}

bool TabController::HandleFileDialog(
    CefRefPtr<CefBrowser> browser, CefDialogHandler::FileDialogMode mode,
    const CefString& title, const CefString& default_file_path,
    const std::vector<CefString>& accept_filters,
    const std::vector<CefString>& accept_extensions,
    const std::vector<CefString>& accept_descriptions,
    CefRefPtr<CefFileDialogCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  return file_dialog_handler_ &&
         file_dialog_handler_(browser, mode, title, default_file_path,
                              accept_filters, accept_extensions,
                              accept_descriptions, std::move(callback));
}

bool TabController::HandleContextMenuAugment(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  return context_menu_augmenter_ &&
         context_menu_augmenter_(browser, params, model);
}

bool TabController::HandleContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                             int command_id) {
  CEF_REQUIRE_UI_THREAD();
  return context_menu_command_handler_ &&
         context_menu_command_handler_(browser, command_id);
}

bool TabController::HandlePageQuery(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query_id,
    const CefString& request, bool persistent,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  return page_query_handler_ &&
         page_query_handler_(browser, frame, query_id, request, persistent,
                             std::move(callback));
}

bool TabController::HandleLocalEntryCommand(CefRefPtr<CefBrowser> browser,
                                            int command_id) {
  CEF_REQUIRE_UI_THREAD();
  return local_entry_command_handler_ &&
         local_entry_command_handler_(browser, command_id);
}

bool TabController::InterceptNavigation(CefRefPtr<CefBrowser> browser,
                                        const CefString& url,
                                        bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  return navigation_interceptor_ &&
         navigation_interceptor_(browser, url, user_gesture);
}

void TabController::SetBrowsersClosedCallback(BrowsersClosedCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  browsers_closed_callback_ = std::move(callback);
}

void TabController::SetPageLoadCompletedCallback(
    PageLoadCompletedCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  page_load_completed_callback_ = std::move(callback);
}

void TabController::SetPageSnapshotEventsReadyCallback(
    PageSnapshotEventsReadyCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  page_snapshot_events_ready_callback_ = std::move(callback);
}

void TabController::SetPageSnapshotObserver(
    gateway::PageSnapshotObserver* observer) {
  CEF_REQUIRE_UI_THREAD();
  client_->SetPageSnapshotObserver(observer);
}

void TabController::SetPageSnapshotAdmission(std::function<bool()> admission) {
  CEF_REQUIRE_UI_THREAD();
  page_snapshot_admission_ = std::move(admission);
}

void TabController::OnPageSnapshotEventsReady() {
  CEF_REQUIRE_UI_THREAD();
  if (page_snapshot_events_ready_callback_) {
    page_snapshot_events_ready_callback_();
  }
}

void TabController::SetMediaObservationEventsReadyCallback(
    MediaObservationEventsReadyCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  media_observation_events_ready_callback_ = std::move(callback);
  client_->SetMediaObservationEventsReadyCallback([this] {
    if (media_observation_events_ready_callback_) {
      media_observation_events_ready_callback_();
    }
  });
}

void TabController::SetMediaObservationLifecycleCallback(
    MediaObservationLifecycleCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  client_->SetMediaObservationLifecycleCallback(std::move(callback));
}

std::vector<::crayon::cef_shell::gateway::GatewayEvent>
TabController::DrainMediaObservations(std::size_t max_events) {
  CEF_REQUIRE_UI_THREAD();
  return client_->DrainMediaObservations(max_events);
}

std::optional<std::string> TabController::TrustedPageUrl(
    std::uint32_t tab_id, std::uint64_t navigation_id) const {
  CEF_REQUIRE_UI_THREAD();
  const TabSnapshot* tab = model_.Find(tab_id);
  if (!tab || tab->navigation_generation != navigation_id ||
      tab->lifecycle != TabLifecycle::kReady) {
    return std::nullopt;
  }
  return tab->url;
}

observation::MediaObservationDiagnostics
TabController::media_observation_diagnostics() const {
  CEF_REQUIRE_UI_THREAD();
  return client_->media_observation_diagnostics();
}

void TabController::NoteTrustedUserInputForActiveTab() {
  CEF_REQUIRE_UI_THREAD();
  client_->NoteTrustedUserInput(ActiveBrowser());
}

std::optional<browser_engine::SnapshotRequestId>
TabController::StartPageSnapshot(CefRefPtr<CefBrowser> browser,
                                 browser_engine::SnapshotMode mode) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || (page_snapshot_admission_ && !page_snapshot_admission_())) {
    return std::nullopt;
  }
  const TabSnapshot* tab = model_.FindByBrowser(browser->GetIdentifier());
  if (!tab || tab->lifecycle != TabLifecycle::kReady || tab->loading ||
      tab->navigation_generation == 0) {
    return std::nullopt;
  }
  return client_->StartPageSnapshot(browser, tab->id,
                                    tab->navigation_generation, mode);
}

std::vector<gateway::SnapshotGatewayEvent> TabController::DrainPageSnapshots(
    std::size_t max_events) {
  CEF_REQUIRE_UI_THREAD();
  return client_->DrainPageSnapshots(max_events);
}

gateway::SnapshotGatewayResult TabController::CancelPageSnapshot(
    const browser_engine::SnapshotRequestId& request_id) {
  CEF_REQUIRE_UI_THREAD();
  const auto result = client_->CancelPageSnapshot(request_id);
  if (result == gateway::SnapshotGatewayResult::kAccepted) {
    OnPageSnapshotEventsReady();
  }
  return result;
}

void TabController::NewWindow() {
  CEF_REQUIRE_UI_THREAD();
  CreateBrowserWindow();
}

void TabController::CloseActiveTab() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (!browser) {
    return;
  }
  const TabSnapshot* tab = model_.FindByBrowser(browser->GetIdentifier());
  if (tab) {
    model_.RequestClose(tab->id);
  }
  browser->GetHost()->TryCloseBrowser();
}

void TabController::GoBack() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser && browser->CanGoBack()) {
    browser->GoBack();
  }
}

void TabController::GoForward() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser && browser->CanGoForward()) {
    browser->GoForward();
  }
}

void TabController::Reload() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser) {
    browser->Reload();
  }
}

void TabController::Stop() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = ActiveBrowser();
  if (browser) {
    browser->StopLoad();
  }
}

void TabController::ZoomIn() {
  CEF_REQUIRE_UI_THREAD();
  const std::optional<TabId> active = model_.active_tab();
  if (!active.has_value()) {
    return;
  }
  const TabSnapshot* tab = model_.Find(*active);
  if (!tab) {
    return;
  }
  const double factor =
      std::min(tab->zoom_factor * kZoomStepFactor, kMaximumWindowZoomFactor);
  if (model_.SetZoom(*active, factor)) {
    ApplyZoom(*active);
  }
}

void TabController::ZoomOut() {
  CEF_REQUIRE_UI_THREAD();
  const std::optional<TabId> active = model_.active_tab();
  if (!active.has_value()) {
    return;
  }
  const TabSnapshot* tab = model_.Find(*active);
  if (!tab) {
    return;
  }
  const double factor =
      std::max(tab->zoom_factor / kZoomStepFactor, kMinimumWindowZoomFactor);
  if (model_.SetZoom(*active, factor)) {
    ApplyZoom(*active);
  }
}

void TabController::ResetZoom() {
  CEF_REQUIRE_UI_THREAD();
  const std::optional<TabId> active = model_.active_tab();
  if (!active.has_value()) {
    return;
  }
  if (model_.SetZoom(*active, kDefaultZoomFactor)) {
    ApplyZoom(*active);
  }
}

void TabController::CloseAllBrowsers(bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  close_initiated_ = true;
  if (browsers_.empty()) {
    CefQuitMessageLoop();
    return;
  }
  for (const auto& entry : browsers_) {
    if (entry.second) {
      entry.second->GetHost()->CloseBrowser(force_close);
    }
  }
}

void TabController::ShowMainWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (model_.empty() && !close_initiated_) {
    CreateBrowserWindow();
  }
}

void TabController::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
  // CEF-16: a tab created to host a queued popup target loads that URL; the
  // built-in new-tab redirect must not overwrite it.
  std::optional<std::string> popup_url;
  if (!pending_popup_urls_.empty()) {
    popup_url = std::move(pending_popup_urls_.front());
    pending_popup_urls_.pop_front();
  }
  bool already_redirected = false;
  if (main_frame) {
    if (popup_url.has_value()) {
      main_frame->LoadURL(*popup_url);
      already_redirected = true;
    } else {
      already_redirected =
          RedirectBuiltInNewTab(main_frame, main_frame->GetURL().ToString());
    }
  }

  // Bind the oldest creating tab if one belongs to our own CreateBrowser
  // request; otherwise adopt a tab created through the Chrome UI.
  std::optional<TabId> creating_tab;
  for (const TabId id : model_.ordered_tabs()) {
    const TabSnapshot* tab = model_.Find(id);
    if (tab && tab->lifecycle == TabLifecycle::kCreating) {
      creating_tab = id;
      break;
    }
  }
  const bool created_from_new_tab_command =
      !creating_tab.has_value() && pending_new_tab_commands_ > 0;
  if (created_from_new_tab_command) {
    --pending_new_tab_commands_;
    if (main_frame && !already_redirected) {
      main_frame->LoadURL(*new_tab_url_);
    }
  }

  const int browser_id = browser->GetIdentifier();
  browsers_[browser_id] = browser;
  if (!creating_tab.has_value()) {
    creating_tab = model_.CreateTab();
  }
  if (!creating_tab.has_value() ||
      !model_.BindBrowser(*creating_tab, browser_id)) {
    // The model rejected the binding (capacity or id collision); the browser
    // must not survive without an owner.
    browsers_.erase(browser_id);
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  if (browser_created_callback_) {
    browser_created_callback_(browser);
  }
  const TabSnapshot* bound_tab = model_.FindByBrowser(browser_id);
  if (bound_tab) {
    client_->SetActiveMediaObservationTab(
        static_cast<std::uint32_t>(bound_tab->id));
  }
}

void TabController::OnBrowserClosing(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const int browser_id = browser->GetIdentifier();
  if (browser_closing_callback_) {
    browser_closing_callback_(browser);
  }
  model_.DetachBrowser(browser_id);
  browsers_.erase(browser_id);
  if (model_.empty()) {
    if (browsers_closed_callback_) {
      browsers_closed_callback_();
    }
    CefQuitMessageLoop();
  }
}

void TabController::OnBrowserFocused(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const TabSnapshot* tab = model_.FindByBrowser(browser->GetIdentifier());
  if (tab) {
    model_.Activate(tab->id);
    client_->SetActiveMediaObservationTab(static_cast<std::uint32_t>(tab->id));
    if (browser_focused_callback_) {
      browser_focused_callback_(browser);
    }
  }
}

void TabController::OnAddressUpdated(CefRefPtr<CefBrowser> browser,
                                     const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  model_.UpdateAddress(browser->GetIdentifier(), url);
}

void TabController::OnLoadingUpdated(CefRefPtr<CefBrowser> browser,
                                     bool is_loading, bool can_go_back,
                                     bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  const int browser_id = browser->GetIdentifier();
  if (is_loading) {
    model_.BeginNavigation(browser_id);
    const TabSnapshot* tab = model_.FindByBrowser(browser_id);
    if (tab) {
      client_->AdvancePageSnapshotNavigation(browser, tab->id,
                                             tab->navigation_generation);
      client_->AdvanceMediaObservationNavigation(
          browser, static_cast<std::uint32_t>(tab->id),
          tab->navigation_generation);
    }
  }
  model_.UpdateLoading(browser_id, is_loading, can_go_back, can_go_forward);
  if (!is_loading) {
    const TabSnapshot* tab = model_.FindByBrowser(browser_id);
    if (tab && tab->navigation_generation == 0) {
      model_.BeginNavigation(browser_id);
      tab = model_.FindByBrowser(browser_id);
    }
    if (page_load_completed_callback_ && tab &&
        tab->lifecycle == TabLifecycle::kReady) {
      page_load_completed_callback_(browser);
    }
  }
}

void TabController::OnRenderProcessGone(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  model_.MarkCrashed(browser->GetIdentifier());
}

void TabController::OnChromeCommand(int command_id) {
  CEF_REQUIRE_UI_THREAD();
  static const int kNewTabCommandId = cef_id_for_command_id_name("IDC_NEW_TAB");
  if (command_id == kNewTabCommandId && new_tab_url_.has_value() &&
      pending_new_tab_commands_ < kMaximumTabsPerWindow) {
    ++pending_new_tab_commands_;
  }
  if (chrome_command_callback_) {
    chrome_command_callback_(command_id);
  }
}

bool TabController::HandlePopupRequest(CefRefPtr<CefBrowser> browser,
                                       const std::string& target_url,
                                       bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  // The tab strip is the only window surface: queueing capacity equals the
  // popup policy's per-opener bound and a full tab strip closes the door.
  const bool tab_capacity_reached =
      model_.size() >= kMaximumTabsPerWindow;
  if (EvaluatePopupTarget(target_url, user_gesture,
                          pending_popup_urls_.size(),
                          tab_capacity_reached) !=
      PopupTargetAction::kOpenInNewTab) {
    return true;  // fail closed: no new window, no new tab
  }
  pending_popup_urls_.push_back(target_url);
  static const int kNewTabCommandId = cef_id_for_command_id_name("IDC_NEW_TAB");
  if (browser && kNewTabCommandId > 0) {
    browser->GetHost()->ExecuteChromeCommand(kNewTabCommandId,
                                             CEF_WOD_CURRENT_TAB);
    return true;
  }
  // The tab command is unavailable in this build; drop the queued target.
  pending_popup_urls_.pop_back();
  return true;
}

bool TabController::RedirectBuiltInNewTab(CefRefPtr<CefFrame> frame,
                                          const std::string& target_url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || !frame->IsMain() || !new_tab_url_.has_value() ||
      target_url != "chrome://newtab/") {
    return false;
  }
  frame->LoadURL(*new_tab_url_);
  return true;
}

bool TabController::CreateBrowserWindow() {
  CefWindowInfo window_info;
  window_info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
  CefBrowserSettings browser_settings;
#if defined(_WIN32)
  // The visibility nudge mutates the tab strip through chrome commands; only
  // the product shell (which owns a real new-tab URL) may run it.  Test
  // harnesses without a new-tab URL keep their fixture tab untouched.
  initial_visibility_nudge_pending_ = new_tab_url_.has_value();
#endif
  return CefBrowserHost::CreateBrowser(window_info, client_, initial_url_,
                                       browser_settings, nullptr, nullptr);
}

#if defined(_WIN32)
void TabController::MaybeRunInitialVisibilityNudge() {
  CEF_REQUIRE_UI_THREAD();
  if (!initial_visibility_nudge_pending_ || model_.empty()) {
    return;
  }
  const std::optional<TabId> active = model_.active_tab();
  if (!active.has_value()) {
    return;
  }
  const TabSnapshot* tab = model_.Find(*active);
  if (!tab) {
    return;
  }
  const auto found = browsers_.find(tab->browser_id);
  if (found == browsers_.end()) {
    return;
  }
  initial_visibility_nudge_pending_ = false;
  // Chrome runtime: the first CreateBrowser'd WebContents reports hidden
  // until the tab strip mutates.  One new+close cycle forces a recompute
  // so IntersectionObserver-driven lazy rendering fires for real users.
  CefRefPtr<CefBrowser> browser = found->second;
  static const int kNewTabCommandId =
      cef_id_for_command_id_name("IDC_NEW_TAB");
  static const int kCloseTabCommandId =
      cef_id_for_command_id_name("IDC_CLOSE_TAB");
  if (kNewTabCommandId <= 0 || kCloseTabCommandId <= 0) {
    return;
  }
  browser->GetHost()->ExecuteChromeCommand(kNewTabCommandId, CEF_WOD_CURRENT_TAB);
  browser->GetHost()->ExecuteChromeCommand(kCloseTabCommandId, CEF_WOD_CURRENT_TAB);
}
#endif

CefRefPtr<CefBrowser> TabController::ActiveBrowser() const {
  CEF_REQUIRE_UI_THREAD();
  const std::optional<TabId> active = model_.active_tab();
  if (!active.has_value()) {
    return nullptr;
  }
  const TabSnapshot* tab = model_.Find(*active);
  if (!tab) {
    return nullptr;
  }
  const auto found = browsers_.find(tab->browser_id);
  return found == browsers_.end() ? nullptr : found->second;
}

void TabController::ApplyZoom(TabId tab_id) {
  const TabSnapshot* tab = model_.Find(tab_id);
  if (!tab) {
    return;
  }
  const auto found = browsers_.find(tab->browser_id);
  if (found != browsers_.end() && found->second) {
    found->second->GetHost()->SetZoomLevel(
        ZoomLevelForFactor(tab->zoom_factor));
  }
}

}  // namespace crayon::browser::cef_shell::window
