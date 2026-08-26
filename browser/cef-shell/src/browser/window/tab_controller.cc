#include "browser/window/tab_controller.h"

#include <cmath>
#include <utility>

#include "include/cef_app.h"
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
}

bool WindowClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  return false;
}

void WindowClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
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
  controller_->OnRenderProcessGone(browser);
}

bool WindowClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame,
                                            CefProcessId source_process,
                                            CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return EnsurePageRouter()->OnProcessMessageReceived(browser, frame,
                                                      source_process,
                                                      message);
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

void WindowClient::OnGotFocus(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
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

void TabController::SetPageQueryHandler(PageQueryHandler handler) {
  CEF_REQUIRE_UI_THREAD();
  page_query_handler_ = std::move(handler);
}

bool TabController::HandlePageQuery(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query_id,
    const CefString& request, bool persistent,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  return page_query_handler_ && page_query_handler_(browser, frame, query_id,
                                                    request, persistent,
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
  bool already_redirected = false;
  if (main_frame) {
    already_redirected =
        RedirectBuiltInNewTab(main_frame, main_frame->GetURL().ToString());
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
}

void TabController::OnBrowserClosing(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const int browser_id = browser->GetIdentifier();
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
  }
  model_.UpdateLoading(browser_id, is_loading, can_go_back, can_go_forward);
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
  return CefBrowserHost::CreateBrowser(window_info, client_, initial_url_,
                                       browser_settings, nullptr, nullptr);
}

CefRefPtr<CefBrowser> TabController::ActiveBrowser() const {
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
