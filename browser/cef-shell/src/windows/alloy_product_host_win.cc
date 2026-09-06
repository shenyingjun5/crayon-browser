#include "windows/alloy_product_host_win.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <optional>
#include <utility>

#include "crayon/browser_engine/content_view.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "crayon/browser_privacy/privacy_defaults.h"
#include "browser/permission/site_origin.h"
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
constexpr std::size_t kMaximumCastObservations = 16;
constexpr std::uint64_t kCastBindRetryMilliseconds = 500;
constexpr std::uint64_t kPermissionPromptLifetimeMilliseconds = 30'000;
constexpr std::uint64_t kExternalProtocolInputLifetimeMilliseconds = 2'000;
constexpr std::uint32_t kMediaAccessVideo = 1U << 1;
constexpr std::uint32_t kMediaAccessAudio = 1U << 2;
constexpr std::uint32_t kPermissionClipboard = 1U << 4;
constexpr std::uint32_t kPermissionGeolocation = 1U << 8;
constexpr std::uint32_t kPermissionNotifications = 1U << 15;

std::uint64_t NowMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

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

browser_site_controls::CertErrorKind CertificateKind(cef_errorcode_t error) {
  switch (static_cast<int>(error)) {
    case -200:
      return browser_site_controls::CertErrorKind::kNameMismatch;
    case -201:
      return browser_site_controls::CertErrorKind::kExpired;
    case -202:
    case -206:
      return browser_site_controls::CertErrorKind::kUntrusted;
    default:
      return browser_site_controls::CertErrorKind::kGeneric;
  }
}

class ProductResourceHandlerWin final : public CefResourceRequestHandler {
 public:
  using ProtocolCallback = std::function<void(
      CefRefPtr<CefBrowser>, std::string, std::string)>;

  ProductResourceHandlerWin(CefRefPtr<CefResourceRequestHandler> observer,
                            ProtocolCallback protocol_callback)
      : observer_(std::move(observer)),
        protocol_callback_(std::move(protocol_callback)) {}

  void OnResourceLoadComplete(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefRequest> request,
                              CefRefPtr<CefResponse> response,
                              URLRequestStatus status,
                              int64_t received_content_length) override {
    CEF_REQUIRE_IO_THREAD();
    if (observer_) {
      observer_->OnResourceLoadComplete(
          browser, frame, request, response, status, received_content_length);
    }
  }

  void OnProtocolExecution(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
                           bool& allow_os_execution) override {
    CEF_REQUIRE_IO_THREAD();
    allow_os_execution = false;
    if (browser && request && protocol_callback_) {
      std::string source_url = request->GetFirstPartyForCookies().ToString();
      if (source_url.empty()) source_url = request->GetReferrerURL().ToString();
      protocol_callback_(std::move(browser), std::move(source_url),
                         request->GetURL().ToString());
    }
  }

 private:
  CefRefPtr<CefResourceRequestHandler> observer_;
  ProtocolCallback protocol_callback_;

  IMPLEMENT_REFCOUNTING(ProductResourceHandlerWin);
  DISALLOW_COPY_AND_ASSIGN(ProductResourceHandlerWin);
};

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
      !dependencies_.mdv_editing || !dependencies_.clipboard_write ||
      !dependencies_.permission_store) {
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
  download_handler_ = new permission::CefDownloadHandlerAdapter(
      dependencies_.permission_store);
  const auto locale = dependencies_.locale.locale;
  media_observation_bridge_.SetEventsReadyCallback(
      dependencies_.media_events_ready);
  media_observation_bridge_.SetLifecycleCallback(
      [this](std::uint32_t tab_id, std::uint64_t navigation_id,
             std::uint32_t generation, bool closed) {
        OnMediaLifecycle(tab_id, navigation_id, generation, closed);
      });
  if (!dependencies_.media_host) {
    coordinator_.reset();
    return false;
  }
  cast_controller_ = std::make_unique<media_host::AlloyCastController>(
      dependencies_.media_host,
      [this](auto snapshot) { ApplyCastSnapshot(std::move(snapshot)); },
      Localized(locale, "cast.selection.video_fallback"),
      Localized(locale, "cast.selection.device_fallback"));
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

std::vector<::crayon::cef_shell::gateway::GatewayEvent>
AlloyProductHostWin::DrainMediaObservations(std::size_t max_events) {
  CEF_REQUIRE_UI_THREAD();
  auto events = media_observation_bridge_.Drain(max_events);
  for (const auto& event : events) UpdateCastGeometry(event);
  return events;
}

std::optional<std::string> AlloyProductHostWin::TrustedPageUrl(
    std::uint32_t tab_id, std::uint64_t navigation_id) const {
  CEF_REQUIRE_UI_THREAD();
  const auto* tab = controller() ? controller()->model().Find(tab_id) : nullptr;
  if (!tab || tab->lifecycle != window::TabLifecycle::kReady ||
      tab->navigation_generation != navigation_id) {
    return std::nullopt;
  }
  return tab->url;
}

bool AlloyProductHostWin::IsActiveTab(std::uint32_t tab_id) const {
  CEF_REQUIRE_UI_THREAD();
  return controller() && controller()->model().active_tab() == tab_id;
}

void AlloyProductHostWin::NoteTrustedUserInput() {
  CEF_REQUIRE_UI_THREAD();
  media_observation_bridge_.NoteTrustedUserInput(browser_);
  const auto* tab = browser_ && controller()
                        ? controller()->model().FindByBrowser(
                              browser_->GetIdentifier())
                        : nullptr;
  if (tab && controller()->model().active_tab() == tab->id) {
    trusted_input_tab_ = tab->id;
    trusted_input_generation_ = tab->navigation_generation;
    trusted_input_at_ms_ = NowMilliseconds();
  }
}

void AlloyProductHostWin::TickCast() {
  CEF_REQUIRE_UI_THREAD();
  const auto now = NowMilliseconds();
  if (!cast_surface_ && browser_ && now >= cast_retry_after_ms_) {
    cast_retry_after_ms_ = now + kCastBindRetryMilliseconds;
    static_cast<void>(BindCastForActiveTab());
  }
  if (cast_controller_) cast_controller_->Tick();
  if (cast_surface_) cast_surface_->Tick();
  if (cast_overlay_) cast_overlay_->Tick();
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
  if (!window_ || !window || !window_->IsSame(window)) return false;
  if (cast_surface_ && cast_surface_->HandleAccelerator(command_id)) {
    return true;
  }
  return interactions_ && interactions_->HandleAccelerator(
                              command_id, static_cast<cef_event_flags_t>(
                                              EVENTFLAG_CONTROL_DOWN));
}

bool AlloyProductHostWin::OnKeyEvent(CefRefPtr<CefWindow> window,
                                     const CefKeyEvent& event) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !window || !window_->IsSame(window)) return false;
  if (cast_surface_ && cast_surface_->HandleKeyEvent(event)) return true;
  return interactions_ && interactions_->HandleAccelerator(
                              event.windows_key_code,
                              static_cast<cef_event_flags_t>(event.modifiers));
}

void AlloyProductHostWin::OnLayoutChanged(CefRefPtr<CefView>,
                                           const CefRect&) {
  CEF_REQUIRE_UI_THREAD();
  if (cast_surface_) cast_surface_->LayoutChanged();
  if (cast_overlay_) cast_overlay_->Invalidate();
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
  const auto* initial_tab = tab_controller->model().Find(*tab_id);
  if (initial_tab && initial_tab->navigation_generation == 0) {
    static_cast<void>(tab_controller->OnLoadingStateChange(
        browser, true, browser->CanGoBack(), browser->CanGoForward()));
  }
  if (!browser->IsLoading()) {
    static_cast<void>(tab_controller->OnLoadingStateChange(
        browser, false, browser->CanGoBack(), browser->CanGoForward()));
  }
  if (const auto* tab = tab_controller->model().Find(*tab_id)) {
    site_controls_[*tab_id] = std::make_unique<window::AlloySiteControls>(
        dependencies_.permission_store);
    const auto main_frame = browser->GetMainFrame();
    static_cast<void>(SynchronizeSiteControls(
        browser, main_frame ? main_frame->GetURL().ToString() : std::string{}));
    media_observation_bridge_.AdvanceNavigation(
        browser, static_cast<std::uint32_t>(*tab_id),
        tab->navigation_generation);
    media_observation_bridge_.BindCurrentMainFrame(browser);
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
  const auto* closing =
      browser && tab_controller
          ? tab_controller->model().FindByBrowser(browser->GetIdentifier())
          : nullptr;
  const auto closing_tab = closing ? std::optional<window::TabId>(closing->id)
                                   : std::nullopt;
  if (!Owns(browser) || !tab_controller ||
      !tab_controller->OnBeforeClose(browser)) {
    return;
  }
  if (closing_tab) {
    const auto controls = site_controls_.find(*closing_tab);
    if (controls != site_controls_.end()) {
      controls->second->Shutdown();
      site_controls_.erase(controls);
    }
    site_origins_.erase(*closing_tab);
    site_urls_.erase(*closing_tab);
  }
  if (closing_tab) {
    media_observation_bridge_.CloseBrowser(
        browser, static_cast<std::uint32_t>(*closing_tab));
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

bool AlloyProductHostWin::OnCertificateError(
    CefRefPtr<CefBrowser> browser, cef_errorcode_t cert_error,
    const CefString& request_url, CefRefPtr<CefSSLInfo>,
    CefRefPtr<CefCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser) || !callback || !controller()) return false;
  const auto* tab =
      controller()->model().FindByBrowser(browser->GetIdentifier());
  const auto origin =
      permission::ExtractSiteOrigin(request_url.ToString());
  if (!tab || !origin || tab->navigation_generation == 0) return false;
  auto controls = std::make_unique<window::AlloySiteControls>(
      dependencies_.permission_store);
  if (!controls->OnNavigation(tab->navigation_generation, *origin)) {
    return false;
  }
  const auto existing = site_controls_.find(tab->id);
  if (existing != site_controls_.end()) existing->second->Shutdown();
  auto* controls_ptr = controls.get();
  site_controls_[tab->id] = std::move(controls);
  site_origins_[tab->id] = *origin;
  site_urls_[tab->id] = request_url.ToString();
  const auto request = controls_ptr->BeginCertificateError(
      tab->navigation_generation, CertificateKind(cert_error),
      [callback](bool allowed) {
        if (allowed) callback->Continue();
        else callback->Cancel();
      });
  if (!request) return false;
  const bool allow = ConfirmNative("security.certificate.title",
                                   "security.certificate.body", *origin);
  return controls_ptr->ResolveCertificate(
             *request, allow ? browser_site_controls::CertDecision::kProceedOnce
                             : browser_site_controls::CertDecision::kGoBack) ==
         window::AlloySiteControlResult::kSuccess;
}

bool AlloyProductHostWin::OnRequestMediaAccessPermission(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
    const CefString& requesting_origin, std::uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!callback) return true;
  constexpr std::uint32_t kMappedMediaAccess =
      kMediaAccessVideo | kMediaAccessAudio;
  const auto origin =
      permission::ExtractSiteOrigin(requesting_origin.ToString());
  if (!origin || requested_permissions == 0 ||
      (requested_permissions & ~kMappedMediaAccess) != 0) {
    callback->Cancel();
    return true;
  }
  std::vector<browser_site_controls::PermissionKind> kinds;
  if ((requested_permissions & kMediaAccessVideo) != 0) {
    kinds.push_back(browser_site_controls::PermissionKind::kCamera);
  }
  if ((requested_permissions & kMediaAccessAudio) != 0) {
    kinds.push_back(browser_site_controls::PermissionKind::kMicrophone);
  }
  if (ResolvePermissions(browser, *origin, kinds, "security.permission.title",
                         "security.permission.body")) {
    callback->Continue(requested_permissions);
  } else {
    callback->Cancel();
  }
  return true;
}

bool AlloyProductHostWin::OnShowPermissionPrompt(
    CefRefPtr<CefBrowser> browser, std::uint64_t,
    const CefString& requesting_origin, std::uint32_t requested_permissions,
    CefRefPtr<CefPermissionPromptCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!callback) return true;
  constexpr std::uint32_t kMappedPermissions =
      kPermissionNotifications | kPermissionGeolocation | kPermissionClipboard;
  const auto origin =
      permission::ExtractSiteOrigin(requesting_origin.ToString());
  if (!origin || requested_permissions == 0 ||
      (requested_permissions & ~kMappedPermissions) != 0) {
    callback->Continue(CEF_PERMISSION_RESULT_DENY);
    return true;
  }
  std::vector<browser_site_controls::PermissionKind> kinds;
  if ((requested_permissions & kPermissionNotifications) != 0) {
    kinds.push_back(browser_site_controls::PermissionKind::kNotifications);
  }
  if ((requested_permissions & kPermissionGeolocation) != 0) {
    kinds.push_back(browser_site_controls::PermissionKind::kGeolocation);
  }
  if ((requested_permissions & kPermissionClipboard) != 0) {
    kinds.push_back(browser_site_controls::PermissionKind::kClipboardRead);
    kinds.push_back(browser_site_controls::PermissionKind::kClipboardWrite);
  }
  callback->Continue(ResolvePermissions(browser, *origin, kinds,
                                        "security.permission.title",
                                        "security.permission.body")
                         ? CEF_PERMISSION_RESULT_ACCEPT
                         : CEF_PERMISSION_RESULT_DENY);
  return true;
}

void AlloyProductHostWin::OnDismissPermissionPrompt(
    CefRefPtr<CefBrowser>, std::uint64_t,
    cef_permission_request_result_t) {
  CEF_REQUIRE_UI_THREAD();
}

bool AlloyProductHostWin::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  auto* controls = SiteControlsFor(browser);
  const auto* tab = browser && controller()
                        ? controller()->model().FindByBrowser(
                              browser->GetIdentifier())
                        : nullptr;
  const auto origin = browser && browser->GetMainFrame()
                          ? permission::ExtractSiteOrigin(
                                browser->GetMainFrame()->GetURL().ToString())
                          : std::nullopt;
  if (!controls || !tab || !origin || !download_handler_ || !download_item ||
      !callback ||
      !ResolvePermissions(
          browser, *origin,
          {browser_site_controls::PermissionKind::kDownload},
          "security.download.title", "security.download.body")) {
    return true;
  }
  return download_handler_->OnBeforeDownload(
      std::move(browser), std::move(download_item), suggested_name,
      std::move(callback));
}

void AlloyProductHostWin::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (download_handler_) {
    download_handler_->OnDownloadUpdated(std::move(browser),
                                         std::move(download_item),
                                         std::move(callback));
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

CefRefPtr<CefResourceRequestHandler>
AlloyProductHostWin::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest> request, bool, bool, const CefString&,
    bool& disable_default_handling) {
  disable_default_handling = false;
  CefRefPtr<AlloyProductHostWin> owner(this);
  auto observer = media_observation_bridge_.CreateResourceRequestHandler(
      browser, request,
      [this](observation::CefNetworkResourceFact fact) {
        media_observation_bridge_.OnNetworkResourceFact(std::move(fact));
      },
      owner);
  return new ProductResourceHandlerWin(
      std::move(observer),
      [owner](CefRefPtr<CefBrowser> source, std::string source_url,
              std::string target_url) {
        CefPostTask(
            TID_UI,
            CefCreateClosureTask(base::BindOnce(
                &AlloyProductHostWin::ConfirmExternalProtocol, owner,
                std::move(source), std::move(source_url),
                std::move(target_url))));
      });
}

bool AlloyProductHostWin::OnKeyEvent(CefRefPtr<CefBrowser> browser,
                                     const CefKeyEvent& event,
                                     CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  if (builtin_content_ &&
      builtin_content_->OnKeyEvent(browser, event, os_event)) {
    return true;
  }
  if (cast_surface_ && cast_surface_->HandleKeyEvent(event)) return true;
  return interactions_ && interactions_->HandleAccelerator(
                              event.windows_key_code,
                              static_cast<cef_event_flags_t>(event.modifiers));
}

bool AlloyProductHostWin::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (media_observation_bridge_.OnProcessMessageReceived(
          browser, frame, source_process, message)) {
    return true;
  }
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
  if (const auto* tab =
          controller()->model().FindByBrowser(browser->GetIdentifier())) {
    media_observation_bridge_.CloseBrowser(
        browser, static_cast<std::uint32_t>(tab->id));
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
  media_observation_bridge_.BindCurrentMainFrame(browser);
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
    if (controller()) {
      static_cast<void>(controller()->OnAddressChange(browser, url.ToString()));
      static_cast<void>(SynchronizeSiteControls(browser, url.ToString()));
    }
    static_cast<void>(navigation_->OnAddressChange(browser, url.ToString()));
  }
}

void AlloyProductHostWin::OnBuiltinLoadingStateChange(
    CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
    bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser)) return;
  std::uint64_t previous_generation = 0;
  if (controller()) {
    if (const auto* previous =
            controller()->model().FindByBrowser(browser->GetIdentifier())) {
      previous_generation = previous->navigation_generation;
    }
    static_cast<void>(controller()->OnLoadingStateChange(
        browser, is_loading, can_go_back, can_go_forward));
    const auto* current =
        controller()->model().FindByBrowser(browser->GetIdentifier());
    if (current && current->navigation_generation != previous_generation) {
      media_observation_bridge_.AdvanceNavigation(
          browser, static_cast<std::uint32_t>(current->id),
          current->navigation_generation);
    }
  }
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

  DetachCastSurfaces();
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
  static_cast<void>(BindCastForActiveTab());
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
  DetachCastSurfaces();
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
  DetachCastSurfaces();
  if (cast_controller_) {
    cast_controller_->Shutdown();
    cast_controller_.reset();
  }
  if (download_handler_) {
    download_handler_->Shutdown();
    download_handler_ = nullptr;
  }
  for (auto& [id, controls] : site_controls_) {
    static_cast<void>(id);
    controls->Shutdown();
  }
  site_controls_.clear();
  site_origins_.clear();
  site_urls_.clear();
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
  DetachCastSurfaces();
  if (cast_controller_) {
    cast_controller_->Shutdown();
    cast_controller_.reset();
  }
  if (download_handler_) {
    download_handler_->Shutdown();
    download_handler_ = nullptr;
  }
  for (auto& [id, controls] : site_controls_) {
    static_cast<void>(id);
    controls->Shutdown();
  }
  site_controls_.clear();
  site_origins_.clear();
  site_urls_.clear();
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
  media_observation_bridge_.SetEventsReadyCallback({});
  media_observation_bridge_.SetLifecycleCallback({});
  auto callback = std::move(callbacks_.all_closed);
  callbacks_ = {};
  if (callback) {
    callback();
  }
}

void AlloyProductHostWin::OnMediaLifecycle(std::uint32_t tab_id,
                                           std::uint64_t navigation_id,
                                           std::uint32_t generation,
                                           bool closed) {
  CEF_REQUIRE_UI_THREAD();
  if (closed) {
    media_generations_.erase(tab_id);
    cast_observations_.clear();
  } else {
    media_generations_[tab_id] = generation;
  }
  if (closed && dependencies_.media_lifecycle) {
    dependencies_.media_lifecycle(tab_id, navigation_id, generation, closed);
  }
  if (IsActiveTab(tab_id)) {
    if (closed) DetachCastSurfaces();
    else static_cast<void>(BindCastForActiveTab());
  }
}

bool AlloyProductHostWin::BindCastForActiveTab() {
  CEF_REQUIRE_UI_THREAD();
  auto* tab_controller = controller();
  const auto active = tab_controller ? tab_controller->model().active_tab()
                                     : std::optional<window::TabId>{};
  if (!active || !browser_ || !view_ || !window_ || !toolbar_ ||
      !cast_controller_) {
    return false;
  }
  DetachCastSurfaces();
  const auto* tab = tab_controller->model().Find(*active);
  if (!tab || tab->navigation_generation == 0) {
    return false;
  }
  auto generation =
      media_generations_.find(static_cast<std::uint32_t>(*active));
  if (generation == media_generations_.end()) {
    media_observation_bridge_.AdvanceNavigation(
        browser_, static_cast<std::uint32_t>(*active),
        tab->navigation_generation);
    generation =
        media_generations_.find(static_cast<std::uint32_t>(*active));
  }
  if (generation == media_generations_.end()) return false;
  const browser_cast_view::CastViewContext context{
      cast_browser_session_, profile_id_value_,
      static_cast<std::uint32_t>(*active), tab->navigation_generation,
      generation->second};
  media_observation_bridge_.SetActiveTab(
      static_cast<std::uint32_t>(*active));
  cast_surface_ = std::make_unique<CastEntrySurface>(
      dependencies_.locale, [] { return NowMilliseconds(); },
      [this](browser_cast_view::CastSelectionIntent intent) {
        if (cast_controller_) {
          static_cast<void>(cast_controller_->HandleIntent(intent));
        }
      });
  if (!cast_surface_->Attach(window_, view_, toolbar_)) {
    cast_surface_.reset();
    return false;
  }
  cast_surface_->BindContext(context);
  if (!cast_controller_->BindContext(context)) {
    DetachCastSurfaces();
    return false;
  }
  cast_overlay_ = std::make_unique<AlloyCastOverlayWin>(
      CefString(Localized(dependencies_.locale.locale,
                          "cast.selection.overlay"))
          .ToWString(),
      [] { return NowMilliseconds(); },
      [this](browser_cast_view::CastMediaRef media) {
        return cast_controller_ && cast_controller_->OpenForMedia(media);
      });
  if (!cast_overlay_->Attach(window_->GetWindowHandle(),
                             browser_->GetHost()->GetWindowHandle())) {
    cast_overlay_.reset();
    return true;
  }
  cast_overlay_->BindContext(context);
  cast_observations_.clear();
  cast_retry_after_ms_ = 0;
  return true;
}

void AlloyProductHostWin::DetachCastSurfaces() {
  CEF_REQUIRE_UI_THREAD();
  if (cast_overlay_) {
    cast_overlay_->Detach();
    cast_overlay_.reset();
  }
  if (cast_surface_) {
    cast_surface_->Detach();
    cast_surface_.reset();
  }
  cast_observations_.clear();
}

void AlloyProductHostWin::ApplyCastSnapshot(
    media_host::AlloyCastController::Snapshot snapshot) {
  CEF_REQUIRE_UI_THREAD();
  if (cast_surface_) static_cast<void>(cast_surface_->Apply(snapshot));
  if (cast_overlay_) {
    static_cast<void>(cast_overlay_->Apply(std::move(snapshot)));
    cast_overlay_->SetObservations(cast_observations_);
  }
}

void AlloyProductHostWin::UpdateCastGeometry(
    const ::crayon::cef_shell::gateway::GatewayEvent& event) {
  CEF_REQUIRE_UI_THREAD();
  if (event.source != ::crayon::cef_shell::gateway::EventSource::kMedia ||
      !event.player_reference || !IsActiveTab(event.tab_id) ||
      !cast_controller_) {
    return;
  }
  const browser_cast_view::CastMediaRef media{
      event.player_reference->instance_id,
      event.player_reference->source_revision};
  cast_observations_.erase(
      std::remove_if(cast_observations_.begin(), cast_observations_.end(),
                     [&media](const auto& value) {
                       return value.anchor.media == media;
                     }),
      cast_observations_.end());
  if (!event.player_removed &&
      cast_observations_.size() < kMaximumCastObservations) {
    const auto now = NowMilliseconds();
    AlloyCastOverlayObservation value;
    value.anchor.context = cast_controller_->snapshot().context;
    value.anchor.view_revision = cast_controller_->snapshot().view_revision;
    value.anchor.media = media;
    value.anchor.expires_at_ms =
        now + browser_cast_view::kCastGeometryLifetimeMs;
    value.anchor.supported = event.media.geometry_supported;
    value.anchor.x = event.media.geometry_x;
    value.anchor.y = event.media.geometry_y;
    value.anchor.width = event.media.geometry_width;
    value.anchor.height = event.media.geometry_height;
    value.viewport_width = event.media.viewport_width;
    value.viewport_height = event.media.viewport_height;
    cast_observations_.push_back(std::move(value));
  }
  if (cast_overlay_) cast_overlay_->SetObservations(cast_observations_);
}

window::AlloySiteControls* AlloyProductHostWin::SiteControlsFor(
    CefRefPtr<CefBrowser> browser) const {
  CEF_REQUIRE_UI_THREAD();
  const auto* tab = browser && controller()
                        ? controller()->model().FindByBrowser(
                              browser->GetIdentifier())
                        : nullptr;
  if (!tab) return nullptr;
  const auto found = site_controls_.find(tab->id);
  return found == site_controls_.end() ? nullptr : found->second.get();
}

bool AlloyProductHostWin::SynchronizeSiteControls(
    CefRefPtr<CefBrowser> browser, const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  const auto* tab = browser && controller()
                        ? controller()->model().FindByBrowser(
                              browser->GetIdentifier())
                        : nullptr;
  const auto origin = permission::ExtractSiteOrigin(url);
  if (!tab || !origin || tab->navigation_generation == 0) return false;
  auto& controls = site_controls_[tab->id];
  if (!controls) {
    controls = std::make_unique<window::AlloySiteControls>(
        dependencies_.permission_store);
  }
  if (!controls->OnNavigation(tab->navigation_generation, *origin)) {
    return false;
  }
  site_origins_[tab->id] = *origin;
  site_urls_[tab->id] = url;
  return true;
}

bool AlloyProductHostWin::ResolvePermissions(
    CefRefPtr<CefBrowser> browser, const std::string& origin,
    const std::vector<browser_site_controls::PermissionKind>& kinds,
    std::string_view title_key, std::string_view body_key) {
  CEF_REQUIRE_UI_THREAD();
  auto* controls = SiteControlsFor(browser);
  const auto* tab = browser && controller()
                        ? controller()->model().FindByBrowser(
                              browser->GetIdentifier())
                        : nullptr;
  if (!controls || !tab || kinds.empty()) return false;

  std::optional<bool> grant;
  for (const auto kind : kinds) {
    bool allowed = false;
    const auto now = NowMilliseconds();
    const auto request = controls->BeginPermission(
        tab->navigation_generation, origin, kind, now,
        now + kPermissionPromptLifetimeMilliseconds,
        [&allowed](bool value) { allowed = value; });
    if (!request) return false;
    if (*request != 0) {
      if (!grant) grant = ConfirmNative(title_key, body_key, origin);
      if (controls->ResolvePermission(
              *request,
              *grant ? window::AlloyPermissionDecision::kAllowSession
                     : window::AlloyPermissionDecision::kDeny,
              NowMilliseconds()) != window::AlloySiteControlResult::kSuccess) {
        return false;
      }
    }
    if (!allowed) return false;
  }
  return true;
}

bool AlloyProductHostWin::ConfirmNative(std::string_view title_key,
                                        std::string_view body_key,
                                        const std::string& detail) const {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !window_->GetWindowHandle() || detail.empty()) return false;
  const std::string body =
      Localized(dependencies_.locale.locale, body_key) + "\n\n" + detail;
  const auto title =
      CefString(Localized(dependencies_.locale.locale, title_key)).ToWString();
  return MessageBoxW(static_cast<HWND>(window_->GetWindowHandle()),
                     CefString(body).ToWString().c_str(), title.c_str(),
                     MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

void AlloyProductHostWin::ConfirmExternalProtocol(
    CefRefPtr<CefBrowser> browser, std::string source_url,
    std::string target_url) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser) || target_url.empty() || !controller()) return;
  const auto separator = target_url.find(':');
  if (separator == std::string::npos) return;
  std::string scheme = target_url.substr(0, separator);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  const auto* tab =
      controller()->model().FindByBrowser(browser->GetIdentifier());
  const auto source_origin = permission::ExtractSiteOrigin(source_url);
  auto* controls = SiteControlsFor(browser);
  const auto origin = tab ? site_origins_.find(tab->id) : site_origins_.end();
  const auto source = tab ? site_urls_.find(tab->id) : site_urls_.end();
  const auto now = NowMilliseconds();
  const bool has_trusted_input =
      tab && trusted_input_tab_ == tab->id &&
      trusted_input_generation_ == tab->navigation_generation &&
      trusted_input_at_ms_ <= now &&
      now - trusted_input_at_ms_ <= kExternalProtocolInputLifetimeMilliseconds;
  trusted_input_tab_ = 0;
  trusted_input_generation_ = 0;
  trusted_input_at_ms_ = 0;
  if (!tab || !source_origin || origin == site_origins_.end() ||
      source == site_urls_.end() || *source_origin != origin->second ||
      !controls || !has_trusted_input) {
    return;
  }
  const auto request = controls->BeginExternalProtocol(
      tab->navigation_generation, origin->second, scheme, target_url,
      [target_url](bool allowed) {
        if (allowed) {
          const auto target = CefString(target_url).ToWString();
          static_cast<void>(ShellExecuteW(nullptr, L"open", target.c_str(),
                                          nullptr, nullptr, SW_SHOWNORMAL));
        }
      });
  if (!request || *request == 0) return;
  const bool allow = ConfirmNative("security.external.title",
                                   "security.external.body", target_url);
  static_cast<void>(controls->ResolveExternalProtocol(
      *request, allow ? browser_site_controls::ProtocolDecision::kAllowOnce
                      : browser_site_controls::ProtocolDecision::kDeny));
  if (browser->GetMainFrame()) browser->GetMainFrame()->LoadURL(source->second);
}

}  // namespace crayon::browser::cef_shell::windows
