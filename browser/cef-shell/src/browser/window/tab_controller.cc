#include "browser/window/tab_controller.h"

#include <cmath>
#include <utility>

#include "include/cef_app.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

// Chromium's default zoom step: one zoom level multiplies the factor by 1.2.
constexpr double kZoomStepFactor = 1.2;

double ZoomLevelForFactor(double factor) {
  return std::log(factor) / std::log(kZoomStepFactor);
}

}  // namespace

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
  controller_->OnAddressUpdated(browser, url.ToString());
}

void WindowClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                        bool isLoading,
                                        bool canGoBack,
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
  controller_->OnRenderProcessGone(browser);
}

void WindowClient::OnGotFocus(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  controller_->OnBrowserFocused(browser);
}

TabController::TabController(std::string initial_url,
                             BrowserCreatedCallback browser_created_callback)
    : initial_url_(std::move(initial_url)),
      browser_created_callback_(std::move(browser_created_callback)),
      client_(new WindowClient(this)) {}

bool TabController::CreateMainWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (!model_.empty()) {
    return false;
  }
  return CreateBrowserWindow();
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
  const TabSnapshot* tab =
      model_.FindByBrowser(browser->GetIdentifier());
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
  const int browser_id = browser->GetIdentifier();
  browsers_[browser_id] = browser;

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
                                     bool is_loading,
                                     bool can_go_back,
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
