#include "windows/alloy_product_host_win.h"

#include <cstdint>
#include <optional>
#include <utility>

#include "crayon/browser_engine/content_view.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "crayon/browser_privacy/privacy_defaults.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::windows {
namespace {

constexpr char kPrimaryWindowId[] = "primary";
constexpr int kInitialWindowWidth = 1200;
constexpr int kInitialWindowHeight = 800;

std::optional<browser_engine::ContentCapabilitySet> InitialCapabilities() {
  const auto bit = [](browser_engine::ContentCapability capability) {
    return 1U << static_cast<unsigned>(capability);
  };
  return browser_engine::ContentCapabilitySet::TryCreate(
      bit(browser_engine::ContentCapability::kNavigate) |
      bit(browser_engine::ContentCapability::kSnapshot) |
      bit(browser_engine::ContentCapability::kTrustedInput) |
      bit(browser_engine::ContentCapability::kMediaObservation) |
      bit(browser_engine::ContentCapability::kZoom) |
      bit(browser_engine::ContentCapability::kFind));
}

std::string Localized(localization::AppLocale locale, std::string_view key) {
  const auto value = localization::LocaleCatalog(locale).Find(key);
  return value ? std::string(*value) : std::string{};
}

bool IsCertificateOrSslError(cef_errorcode_t error) {
  const int value = static_cast<int>(error);
  return value == -107 || (value <= -200 && value >= -299);
}

}  // namespace

AlloyProductHostWin::AlloyProductHostWin(Dependencies dependencies,
                                         Callbacks callbacks)
    : dependencies_(std::move(dependencies)),
      callbacks_(std::move(callbacks)) {}

AlloyProductHostWin::~AlloyProductHostWin() = default;

bool AlloyProductHostWin::Start(std::string initial_url, std::string title,
                                browser_engine::ProfileId profile_id) {
  CEF_REQUIRE_UI_THREAD();
  if (started_ || closed_ || initial_url.empty() || title.empty()) {
    return false;
  }
  profile_id_value_ = profile_id.value();
  coordinator_ = std::make_unique<window::AlloyWindowCoordinator>(
      window::AlloyWindowCoordinator::Callbacks{});
  if (!coordinator_->CreatePrimary(kPrimaryWindowId, std::move(profile_id))) {
    coordinator_.reset();
    return false;
  }
  auto* tab_controller = controller();
  if (!tab_controller || !dependencies_.mdv_entries ||
      !dependencies_.mdv_editing || !dependencies_.clipboard_write) {
    coordinator_.reset();
    return false;
  }
  page_markdown_ = std::make_unique<window::AlloyPageMarkdown>(
      tab_controller, dependencies_.mdv_editing,
      dependencies_.page_markdown_strings, dependencies_.clipboard_write, this);
  page_markdown_->SetSnapshotObserver(dependencies_.snapshot_observer);
  page_markdown_->SetSnapshotAdmission(dependencies_.snapshot_admission);
  page_markdown_->SetEventsReadyCallback(dependencies_.snapshot_events_ready);
  builtin_content_ = new window::AlloyBuiltinContent(dependencies_.mdv_entries,
                                                     dependencies_.mdv_editing,
                                                     page_markdown_.get());
  const auto locale = dependencies_.locale.locale;
  tab_strip_ = std::make_unique<window::AlloyTabStrip>(
      window::AlloyTabStrip::Strings{Localized(locale, "tabs.new"),
                                     Localized(locale, "tabs.close"),
                                     Localized(locale, "tabs.fallback")},
      window::AlloyTabStrip::Callbacks{
          [this] {
            static_cast<void>(
                CreateTab("crayon://newtab",
                          browser_engine::ContentPurpose::kControlledBuiltIn));
            PostSyncChrome();
          },
          [this](window::TabId id) {
            static_cast<void>(ActivateTab(id));
            PostSyncChrome();
          },
          [this](window::TabId id) {
            if (controller()) {
              const bool active = PrepareActiveChromeForClose(id);
              if (!controller()->RequestClose(id, false)) {
                if (active) SyncChrome();
              } else if (!active) {
                PostSyncChrome();
              }
            }
          }});
  omnibox_ = std::make_unique<window::AlloyOmnibox>(
      window::AlloyOmnibox::Strings{Localized(locale, "address.placeholder"),
                                    Localized(locale, "omnibox.edit")},
      window::AlloyOmnibox::Callbacks{
          {},
          [this](const window::OmniboxSubmission& submission) {
            if (navigation_) {
              static_cast<void>(navigation_->Navigate(submission));
            }
          },
          [this] { SyncChrome(); }},
      browser_privacy::DefaultPrivacyDefaults());
  navigation_ = std::make_unique<window::AlloyNavigation>(
      window::AlloyNavigation::Strings{
          Localized(locale, "nav.back"), Localized(locale, "nav.forward"),
          Localized(locale, "nav.reload"), Localized(locale, "nav.stop"),
          Localized(locale, "nav.identity.unknown"),
          Localized(locale, "nav.identity.secure"),
          Localized(locale, "nav.identity.pending"),
          Localized(locale, "nav.identity.insecure"),
          Localized(locale, "nav.identity.local"),
          Localized(locale, "nav.identity.error")},
      window::AlloyNavigation::Callbacks{[this](const std::string& address) {
        if (omnibox_) static_cast<void>(omnibox_->SetAddress(address));
      }});
  if (!tab_strip_->panel() || !omnibox_->panel() || !navigation_->panel() ||
      !CreateTab(std::move(initial_url),
                 browser_engine::ContentPurpose::kControlledBuiltIn)) {
    coordinator_.reset();
    return false;
  }
  tab_id_ = views_.begin()->first;
  title_ = std::move(title);
  started_ = true;
  CefWindow::CreateTopLevelWindow(this);
  return true;
}

std::vector<gateway::SnapshotGatewayEvent>
AlloyProductHostWin::DrainPageSnapshots(std::size_t max_events) {
  CEF_REQUIRE_UI_THREAD();
  return page_markdown_ ? page_markdown_->DrainSnapshots(max_events)
                        : std::vector<gateway::SnapshotGatewayEvent>{};
}

void AlloyProductHostWin::TickPageMarkdown(
    std::vector<::crayon::cef_shell::ipc::content_host::Message> replies,
    bool content_host_healthy) {
  CEF_REQUIRE_UI_THREAD();
  if (page_markdown_) {
    page_markdown_->Tick(std::move(replies), content_host_healthy);
  }
}

bool AlloyProductHostWin::Close(bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  if (!started_ || closed_ || closing_ || !coordinator_) {
    return false;
  }
  closing_ = true;
  const auto active = controller() ? controller()->model().active_tab()
                                   : std::optional<window::TabId>{};
  const bool prepared = active && PrepareActiveChromeForClose(*active);
  if (!coordinator_->BeginCloseWindow(kPrimaryWindowId, force_close)) {
    closing_ = false;
    if (prepared) SyncChrome();
    return false;
  }
  if (controller() && controller()->pending_count() == 0 && window_) {
    window_->Close();
  }
  return true;
}

cef_runtime_style_t AlloyProductHostWin::GetBrowserRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

cef_runtime_style_t AlloyProductHostWin::GetWindowRuntimeStyle() {
  return CEF_RUNTIME_STYLE_ALLOY;
}

void AlloyProductHostWin::OnWindowCreated(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  if (!started_ || closing_ || window_ || !window || !tab_controller ||
      !coordinator_->AttachWindow(kPrimaryWindowId, window)) {
    if (!closing_) {
      Close(true);
    }
    return;
  }
  window_ = window;
  CefBoxLayoutSettings settings;
  auto layout = window_->SetToBoxLayout(settings);
  toolbar_ = CefPanel::CreatePanel(nullptr);
  CefBoxLayoutSettings toolbar_settings;
  toolbar_settings.horizontal = true;
  auto toolbar_layout = toolbar_->SetToBoxLayout(toolbar_settings);
  toolbar_->AddChildView(navigation_->panel());
  toolbar_->AddChildView(omnibox_->panel());
  toolbar_layout->SetFlexForView(omnibox_->panel(), 1);
  window_->AddChildView(tab_strip_->panel());
  window_->AddChildView(toolbar_);
  window_->AddChildView(tab_controller->container());
  layout->SetFlexForView(tab_controller->container(), 1);
  window_->SetTitle(title_);
  window_->SetSize(CefSize(kInitialWindowWidth, kInitialWindowHeight));
  window_->Layout();
  window_->Show();
  window_->Activate();
  SyncChrome();
  PostSyncChrome();
}

bool AlloyProductHostWin::OnAccelerator(CefRefPtr<CefWindow> window,
                                        int command_id) {
  CEF_REQUIRE_UI_THREAD();
  return window_ && window && window_->IsSame(window) && interactions_ &&
         interactions_->HandleAccelerator(
             command_id,
             static_cast<cef_event_flags_t>(EVENTFLAG_CONTROL_DOWN));
}

bool AlloyProductHostWin::OnKeyEvent(CefRefPtr<CefWindow> window,
                                     const CefKeyEvent& event) {
  CEF_REQUIRE_UI_THREAD();
  return window_ && window && window_->IsSame(window) && interactions_ &&
         interactions_->HandleAccelerator(
             event.windows_key_code,
             static_cast<cef_event_flags_t>(event.modifiers));
}

bool AlloyProductHostWin::CanClose(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !window || !window_->IsSame(window)) {
    return true;
  }
  if (!closing_) {
    static_cast<void>(Close(false));
    return false;
  }
  return !controller() || controller()->pending_count() == 0;
}

void AlloyProductHostWin::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !window || !window_->IsSame(window) || !coordinator_ ||
      !coordinator_->OnWindowClosed(kPrimaryWindowId)) {
    return;
  }
  window_ = nullptr;
  if (coordinator_->Shutdown()) {
    coordinator_.reset();
    NotifyClosed();
  }
}

void AlloyProductHostWin::OnBrowserCreated(CefRefPtr<CefBrowserView> view,
                                           CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // Mounting a BrowserView into an already-visible container can synchronously
  // enter this callback before CreateTab has committed its view-to-tab map.
  // Finalize on the next UI task so the controller and product projection are
  // observed atomically.
  CefPostTask(TID_UI,
              base::BindOnce(&AlloyProductHostWin::FinalizeBrowserCreated,
                             CefRefPtr<AlloyProductHostWin>(this),
                             std::move(view), std::move(browser)));
}

void AlloyProductHostWin::FinalizeBrowserCreated(
    CefRefPtr<CefBrowserView> view, CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  const auto capabilities = InitialCapabilities();
  const auto tab_id = TabForView(view);
  if (!view || !browser || !tab_controller || !capabilities || !tab_id ||
      !tab_controller->OnBrowserCreated(view, browser, *capabilities)) {
    if (!closing_) {
      Close(true);
    }
    return;
  }
  browser_ = browser;
  if (callbacks_.browser_created) {
    callbacks_.browser_created(browser_);
  }
  CefPostTask(TID_UI,
              base::BindOnce(&AlloyProductHostWin::ActivateCreatedTab,
                             CefRefPtr<AlloyProductHostWin>(this), *tab_id));
}

void AlloyProductHostWin::OnBrowserDestroyed(CefRefPtr<CefBrowserView> view,
                                             CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (view_ && view && view_->IsSame(view)) {
    view_ = nullptr;
  }
  if (browser_ && browser && browser_->IsSame(browser)) {
    browser_ = nullptr;
  }
  const auto tab = TabForView(view);
  if (tab) views_.erase(*tab);
  SyncChrome();
}

bool AlloyProductHostWin::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  if (!Owns(browser) || !tab_controller ||
      !tab_controller->OnDoClose(browser)) {
    return false;
  }
  CefPostTask(TID_UI,
              base::BindOnce(&AlloyProductHostWin::ReleaseClosingView,
                             CefRefPtr<AlloyProductHostWin>(this), browser));
  return true;
}

void AlloyProductHostWin::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnAfterCreated(std::move(browser));
  }
}

void AlloyProductHostWin::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnBeforeClose(std::move(browser));
  }
}

void AlloyProductHostWin::OnBuiltinBrowserClosing(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  if (!Owns(browser) || !tab_controller ||
      !tab_controller->OnBeforeClose(browser)) {
    return;
  }
  if (browser_ && browser && browser_->IsSame(browser)) {
    browser_ = nullptr;
  }
  if (tab_controller->pending_count() == 0 && window_) {
    if (!closing_ && coordinator_) {
      closing_ = true;
      static_cast<void>(
          coordinator_->BeginCloseWindow(kPrimaryWindowId, false));
    }
    ShutdownChromeForWindowClose();
    window_->Close();
  } else {
    SyncChrome();
  }
}

void AlloyProductHostWin::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                        const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnTitleChange(std::move(browser), title);
  }
}

void AlloyProductHostWin::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnAddressChange(std::move(browser), std::move(frame),
                                      url);
  }
}

void AlloyProductHostWin::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                               bool is_loading,
                                               bool can_go_back,
                                               bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnLoadingStateChange(std::move(browser), is_loading,
                                           can_go_back, can_go_forward);
  }
}

void AlloyProductHostWin::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    int http_status_code) {
  CEF_REQUIRE_UI_THREAD();
  if (Owns(browser) && frame && frame->IsMain() && controller()) {
    static_cast<void>(controller()->SynchronizeRuntimeState(browser));
  }
  if (builtin_content_) {
    builtin_content_->OnLoadEnd(std::move(browser), std::move(frame),
                                http_status_code);
  }
}

void AlloyProductHostWin::OnLoadError(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      ErrorCode error_code,
                                      const CefString& error_text,
                                      const CefString& failed_url) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnLoadError(std::move(browser), std::move(frame),
                                  error_code, error_text, failed_url);
  }
}

bool AlloyProductHostWin::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                           cef_log_severity_t level,
                                           const CefString& message,
                                           const CefString& source, int line) {
  CEF_REQUIRE_UI_THREAD();
  return builtin_content_ &&
         builtin_content_->OnConsoleMessage(std::move(browser), level, message,
                                            source, line);
}

bool AlloyProductHostWin::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefRequest> request,
                                         bool user_gesture, bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  return builtin_content_ && builtin_content_->OnBeforeBrowse(
                                 std::move(browser), std::move(frame),
                                 std::move(request), user_gesture, is_redirect);
}

bool AlloyProductHostWin::OnKeyEvent(CefRefPtr<CefBrowser> browser,
                                     const CefKeyEvent& event,
                                     CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_ &&
      builtin_content_->OnKeyEvent(browser, event, os_event)) {
    return true;
  }
  return interactions_ && interactions_->HandleAccelerator(
                              event.windows_key_code,
                              static_cast<cef_event_flags_t>(event.modifiers));
}

bool AlloyProductHostWin::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return builtin_content_ && builtin_content_->OnProcessMessageReceived(
                                 std::move(browser), std::move(frame),
                                 source_process, std::move(message));
}

void AlloyProductHostWin::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnBeforeContextMenu(std::move(browser), std::move(frame),
                                          std::move(params), std::move(model));
  }
}

bool AlloyProductHostWin::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, int command_id,
    EventFlags event_flags) {
  CEF_REQUIRE_UI_THREAD();
  return builtin_content_ && builtin_content_->OnContextMenuCommand(
                                 std::move(browser), std::move(frame),
                                 std::move(params), command_id, event_flags);
}

bool AlloyProductHostWin::OnDragEnter(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefDragData> drag_data,
                                      DragOperationsMask mask) {
  CEF_REQUIRE_UI_THREAD();
  return interactions_ && interactions_->OnDragEnter(
                              std::move(browser), std::move(drag_data), mask);
}

void AlloyProductHostWin::OnRenderProcessTerminated(
    CefRefPtr<CefBrowser> browser, TerminationStatus status, int error_code,
    const CefString& error_string) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_) {
    builtin_content_->OnRenderProcessTerminated(browser, status, error_code,
                                                error_string);
    return;
  }
  OnBuiltinRenderProcessTerminated(std::move(browser));
}

void AlloyProductHostWin::OnBuiltinRenderProcessTerminated(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser) || !controller() ||
      !controller()->OnRenderProcessGone(browser)) {
    return;
  }
  closing_ = true;
  CefPostTask(TID_UI,
              base::BindOnce(&AlloyProductHostWin::FinalizeRendererCrash,
                             CefRefPtr<AlloyProductHostWin>(this), browser));
}

void AlloyProductHostWin::OnBuiltinLoadEnd(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           int http_status_code) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser) || !frame || !frame->IsMain()) return;
  const std::string address = frame->GetURL();
  if (navigation_) {
    static_cast<void>(navigation_->OnLoadEnd(browser, address));
  }
  if (omnibox_) {
    static_cast<void>(omnibox_->OnNavigationFinished(
        http_status_code >= 200 && http_status_code < 400, address));
  }
}

void AlloyProductHostWin::OnBuiltinLoadError(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             cef_errorcode_t error_code,
                                             const CefString&,
                                             const CefString& failed_url) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser) || !frame || !frame->IsMain()) return;
  if (navigation_) {
    static_cast<void>(navigation_->OnLoadError(browser, failed_url.ToString(),
                                               IsCertificateOrSslError(
                                                   error_code)));
  }
  if (omnibox_) {
    static_cast<void>(
        omnibox_->OnNavigationFinished(false, failed_url.ToString()));
  }
}

void AlloyProductHostWin::OnBuiltinAddressChange(CefRefPtr<CefBrowser> browser,
                                                 CefRefPtr<CefFrame> frame,
                                                 const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (Owns(browser) && frame && frame->IsMain() && navigation_) {
    static_cast<void>(navigation_->OnAddressChange(browser, url.ToString()));
  }
}

void AlloyProductHostWin::OnBuiltinLoadingStateChange(
    CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
    bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser)) return;
  if (navigation_) {
    static_cast<void>(navigation_->OnLoadingStateChange(
        browser, is_loading, can_go_back, can_go_forward));
  }
  if (is_loading && page_tools_ && controller()) {
    const auto* tab =
        controller()->model().FindByBrowser(browser->GetIdentifier());
    if (tab) {
      static_cast<void>(page_tools_->OnNavigation(tab->navigation_generation));
    }
  }
}

void AlloyProductHostWin::OnBuiltinBeforeContextMenu(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  if (interactions_) {
    interactions_->OnBeforeContextMenu(std::move(browser), std::move(frame),
                                       std::move(params), std::move(model));
  }
}

bool AlloyProductHostWin::OnBuiltinContextMenuCommand(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, int command_id,
    cef_event_flags_t event_flags) {
  CEF_REQUIRE_UI_THREAD();
  return interactions_ && interactions_->OnContextMenuCommand(
                              std::move(browser), std::move(frame),
                              std::move(params), command_id, event_flags);
}

window::AlloyTabController* AlloyProductHostWin::controller() const noexcept {
  return coordinator_ ? coordinator_->controller(kPrimaryWindowId) : nullptr;
}

bool AlloyProductHostWin::CreateTab(std::string url,
                                    browser_engine::ContentPurpose purpose) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  if (!tab_controller || url.empty() || next_navigation_id_ == 0) return false;
  CefBrowserSettings settings;
  auto view = CefBrowserView::CreateBrowserView(this, url, settings, nullptr,
                                                nullptr, this);
  const auto tab =
      view ? tab_controller->BeginCreate(
                 view, purpose,
                 browser_engine::NavigationId::FromRaw(next_navigation_id_++))
           : std::nullopt;
  if (!tab) return false;
  views_.emplace(*tab, view);
  if (!view_) view_ = view;
  SyncChrome();
  return true;
}

bool AlloyProductHostWin::ActivateTab(window::TabId tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  if (!tab_controller || !tab_controller->Activate(tab_id)) return false;
  view_ = views_.count(tab_id) ? views_.at(tab_id) : nullptr;
  browser_ = BrowserForTab(tab_id);
  SyncChrome();
  return true;
}

void AlloyProductHostWin::ActivateCreatedTab(window::TabId tab_id) {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || closed_ || ActivateTab(tab_id)) {
    PostSyncChrome();
    return;
  }
  auto* tab_controller = controller();
  if (tab_controller) {
    static_cast<void>(tab_controller->RequestClose(tab_id, true));
  }
  PostSyncChrome();
}

CefRefPtr<CefBrowser> AlloyProductHostWin::BrowserForTab(
    window::TabId tab_id) const {
  const auto found = views_.find(tab_id);
  return found != views_.end() && found->second ? found->second->GetBrowser()
                                                : nullptr;
}

std::optional<window::TabId> AlloyProductHostWin::TabForView(
    CefRefPtr<CefBrowserView> view) const {
  if (!view) return std::nullopt;
  for (const auto& [id, candidate] : views_) {
    if (candidate && candidate->IsSame(view)) return id;
  }
  return std::nullopt;
}

void AlloyProductHostWin::SyncChrome() {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  if (!tab_controller) return;
  if (tab_strip_) static_cast<void>(tab_strip_->Sync(tab_controller->model()));
  static_cast<void>(BindActiveChrome());
  if (window_) window_->Layout();
}

void AlloyProductHostWin::PostSyncChrome() {
  CEF_REQUIRE_UI_THREAD();
  CefPostTask(TID_UI, base::BindOnce(&AlloyProductHostWin::SyncChrome,
                                     CefRefPtr<AlloyProductHostWin>(this)));
}

bool AlloyProductHostWin::BindActiveChrome() {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  const auto active = tab_controller ? tab_controller->model().active_tab()
                                     : std::optional<window::TabId>{};
  if (!active) return false;
  auto browser = BrowserForTab(*active);
  auto view = views_.count(*active) ? views_.at(*active) : nullptr;
  if (!browser || !view || !window_ || !toolbar_) return false;
  if (chrome_browser_id_ == browser->GetIdentifier()) return true;

  if (interactions_) {
    interactions_->Shutdown();
    interactions_ = nullptr;
  }
  if (page_tools_) {
    page_tools_->Shutdown();
    page_tools_ = nullptr;
  }
  if (!navigation_->Bind("tab-" + std::to_string(*active), browser)) {
    return false;
  }
  const auto* snapshot = tab_controller->model().Find(*active);
  if (snapshot && !snapshot->url.empty()) {
    static_cast<void>(omnibox_->SetAddress(snapshot->url));
  }
  page_tools_ = new window::AlloyPageTools(browser, window_, profile_id_value_);
  if (snapshot) {
    static_cast<void>(
        page_tools_->OnNavigation(snapshot->navigation_generation));
  }

  window::AlloyInteractions::Callbacks callbacks;
  callbacks.open_markdown = [this](CefRefPtr<CefBrowser> target) {
    return dependencies_.mdv_entries &&
           dependencies_.mdv_entries->HandleOpenFileCommand(std::move(target));
  };
  callbacks.navigate = [](CefRefPtr<CefBrowser> target,
                          const std::string& url) {
    if (!target || !target->GetMainFrame() || url.empty()) return false;
    target->GetMainFrame()->LoadURL(url);
    return true;
  };
  callbacks.drag_enter = [this](CefRefPtr<CefBrowser> target,
                                CefRefPtr<CefDragData> data,
                                CefDragHandler::DragOperationsMask mask) {
    return dependencies_.mdv_entries &&
           dependencies_.mdv_entries->HandleDragEnter(std::move(target),
                                                      std::move(data), mask);
  };
  callbacks.augment_context_menu = [this](
                                       CefRefPtr<CefBrowser> target,
                                       CefRefPtr<CefContextMenuParams> params,
                                       CefRefPtr<CefMenuModel> model) {
    return dependencies_.mdv_entries &&
           dependencies_.mdv_entries->HandleContextMenuAugment(
               std::move(target), std::move(params), std::move(model));
  };
  callbacks.context_menu_command = [this](CefRefPtr<CefBrowser> target,
                                          int command_id) {
    return dependencies_.mdv_entries &&
           dependencies_.mdv_entries->HandleContextMenuCommand(
               std::move(target), command_id);
  };
  callbacks.cancel_transient = [this] {
    if (omnibox_) static_cast<void>(omnibox_->Cancel());
  };
  interactions_ =
      new window::AlloyInteractions(dependencies_.locale, std::move(callbacks));
  if (!interactions_->Attach(window_, view, browser, toolbar_)) {
    interactions_ = nullptr;
    return false;
  }
  chrome_browser_id_ = browser->GetIdentifier();
  browser_ = browser;
  view_ = view;
  return true;
}

bool AlloyProductHostWin::PrepareActiveChromeForClose(window::TabId tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  const auto active = tab_controller ? tab_controller->model().active_tab()
                                     : std::optional<window::TabId>{};
  if (!active || *active != tab_id) return false;
  if (interactions_) {
    interactions_->Shutdown();
    interactions_ = nullptr;
  }
  if (page_tools_) {
    page_tools_->Shutdown();
    page_tools_ = nullptr;
  }
  chrome_browser_id_ = 0;
  view_ = nullptr;
  browser_ = nullptr;
  return true;
}

void AlloyProductHostWin::ShutdownChromeForWindowClose() {
  CEF_REQUIRE_UI_THREAD();
  if (interactions_) {
    interactions_->Shutdown();
    interactions_ = nullptr;
  }
  if (page_tools_) {
    page_tools_->Shutdown();
    page_tools_ = nullptr;
  }
  if (navigation_) {
    navigation_->Shutdown();
    navigation_.reset();
  }
  if (omnibox_) {
    omnibox_->Shutdown();
    omnibox_.reset();
  }
  if (tab_strip_) {
    tab_strip_->Shutdown();
    tab_strip_.reset();
  }
  if (toolbar_ && toolbar_->IsValid()) {
    toolbar_->RemoveAllChildViews();
  }
  toolbar_ = nullptr;
  view_ = nullptr;
  browser_ = nullptr;
  views_.clear();
  chrome_browser_id_ = 0;
}

bool AlloyProductHostWin::Owns(CefRefPtr<CefBrowser> browser) const {
  auto* tab_controller = controller();
  return browser && tab_controller && tab_controller->OwnsBrowser(browser);
}

void AlloyProductHostWin::ReleaseClosingView(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  std::optional<window::TabId> closing_tab;
  CefRefPtr<CefBrowserView> closing_view;
  for (const auto& [id, candidate] : views_) {
    auto candidate_browser = candidate ? candidate->GetBrowser() : nullptr;
    if (candidate_browser && browser && candidate_browser->IsSame(browser)) {
      closing_tab = id;
      closing_view = candidate;
      break;
    }
  }
  const bool closing_active =
      closing_tab && tab_controller &&
      tab_controller->model().active_tab() == closing_tab;
  if (!tab_controller || !tab_controller->ReleaseAfterDoClose(browser)) {
    return;
  }
  if (view_ && closing_view && view_->IsSame(closing_view)) {
    view_ = nullptr;
  }
  if (browser_ && browser && browser_->IsSame(browser)) {
    browser_ = nullptr;
  }
  if (closing_tab) {
    views_.erase(*closing_tab);
  }
  if (!closing_active) {
    SyncChrome();
  }
}

void AlloyProductHostWin::FinalizeRendererCrash(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  if (!tab_controller || !tab_controller->FinalizeRendererCrash(browser)) {
    return;
  }
  view_ = nullptr;
  browser_ = nullptr;
  views_.clear();
  chrome_browser_id_ = 0;
  if (interactions_) {
    interactions_->Shutdown();
    interactions_ = nullptr;
  }
  if (page_tools_) {
    page_tools_->Shutdown();
    page_tools_ = nullptr;
  }
  if (navigation_) {
    navigation_->Shutdown();
    navigation_.reset();
  }
  if (omnibox_) {
    omnibox_->Shutdown();
    omnibox_.reset();
  }
  if (tab_strip_) {
    tab_strip_->Shutdown();
    tab_strip_.reset();
  }
  toolbar_ = nullptr;
  if (tab_controller->pending_count() == 0 && window_) {
    window_->Close();
  }
}

void AlloyProductHostWin::NotifyClosed() {
  if (closed_) {
    return;
  }
  closed_ = true;
  started_ = false;
  closing_ = false;
  view_ = nullptr;
  browser_ = nullptr;
  if (builtin_content_) {
    builtin_content_->Shutdown();
    builtin_content_ = nullptr;
  }
  if (page_markdown_) {
    page_markdown_->Shutdown();
    page_markdown_.reset();
  }
  auto callback = std::move(callbacks_.all_closed);
  callbacks_ = {};
  if (callback) {
    callback();
  }
}

}  // namespace crayon::browser::cef_shell::windows
