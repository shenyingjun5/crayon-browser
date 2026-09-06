#include "windows/alloy_product_host_win.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <optional>
#include <utility>

#include "crayon/browser_engine/content_view.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "crayon/browser_privacy/privacy_defaults.h"
#include "browser/permission/site_origin.h"
#include "include/base/cef_callback.h"
#include "include/cef_request_context_handler.h"
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
constexpr int64_t kSessionCheckpointDelayMilliseconds = 250;
constexpr int64_t kSessionRestoreDelayMilliseconds = 250;
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

std::uint64_t NowUnixSeconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

bool Utf8PathExists(const std::string& path) {
  if (path.empty()) return true;
  std::error_code error;
  const bool exists =
      std::filesystem::exists(std::filesystem::u8path(path), error);
  return error || exists;
}

bool IsSafeHistoryTitle(std::string_view title) {
  if (title.empty() || title.size() > browser_history::kMaxTitleBytes) {
    return false;
  }
  return std::none_of(title.begin(), title.end(), [](char character) {
    const auto value = static_cast<unsigned char>(character);
    return value < 0x20 || value == 0x7F;
  });
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

window::AlloyOmnibox::Strings OmniboxStrings(localization::AppLocale locale) {
  return {Localized(locale, "address.placeholder"),
          Localized(locale, "omnibox.edit")};
}

window::AlloyNavigation::Strings NavigationStrings(
    localization::AppLocale locale) {
  return {Localized(locale, "nav.back"),
          Localized(locale, "nav.forward"),
          Localized(locale, "nav.reload"),
          Localized(locale, "nav.stop"),
          Localized(locale, "nav.identity.unknown"),
          Localized(locale, "nav.identity.secure"),
          Localized(locale, "nav.identity.pending"),
          Localized(locale, "nav.identity.insecure"),
          Localized(locale, "nav.identity.local"),
          Localized(locale, "nav.identity.error")};
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

class OneShotRequestContextHandler final : public CefRequestContextHandler {
 public:
  using Callback =
      std::function<void(CefRefPtr<CefRequestContext> request_context)>;

  explicit OneShotRequestContextHandler(Callback callback)
      : callback_(std::move(callback)) {}

  void OnRequestContextInitialized(
      CefRefPtr<CefRequestContext> request_context) override {
    CEF_REQUIRE_UI_THREAD();
    auto callback = std::move(callback_);
    callback_ = {};
    if (callback) callback(std::move(request_context));
  }

 private:
  Callback callback_;

  IMPLEMENT_REFCOUNTING(OneShotRequestContextHandler);
  DISALLOW_COPY_AND_ASSIGN(OneShotRequestContextHandler);
};

}  // namespace

AlloyProductHostWin::AlloyProductHostWin(Dependencies dependencies,
                                         Callbacks callbacks)
    : dependencies_(std::move(dependencies)),
      callbacks_(std::move(callbacks)) {}

AlloyProductHostWin::~AlloyProductHostWin() = default;

bool AlloyProductHostWin::SetRequestContext(
    CefRefPtr<CefRequestContext> request_context) {
  CEF_REQUIRE_UI_THREAD();
  if (started_ || closed_ || dependencies_.request_context ||
      !request_context || request_context->GetCachePath().empty()) {
    return false;
  }
  dependencies_.request_context = std::move(request_context);
  return true;
}

void AlloyProductHostWin::ReleaseRequestContextsForShutdown() {
  CEF_REQUIRE_UI_THREAD();
  pending_incognito_contexts_.clear();
  for (auto& [window_id, popup] : popup_windows_) {
    static_cast<void>(window_id);
    popup.request_context = nullptr;
  }
  dependencies_.request_context = nullptr;
}

bool AlloyProductHostWin::Start(std::string initial_url, std::string title,
                                browser_engine::ProfileId profile_id) {
  CEF_REQUIRE_UI_THREAD();
  if (started_ || closed_ || initial_url.empty() || title.empty() ||
      !dependencies_.profile_context_factory ||
      !dependencies_.register_incognito_content ||
      !dependencies_.request_context) {
    return false;
  }
  profile_id_value_ = profile_id.value();
  const auto persistent_context =
      dependencies_.profile_context_factory->GetPersistentContext(
          profile_id_value_);
  if (!persistent_context ||
      !persistent_context->IsSame(dependencies_.request_context)) {
    return false;
  }
  coordinator_ = std::make_unique<window::AlloyWindowCoordinator>(
      window::AlloyWindowCoordinator::Callbacks{
          [this](const window::AlloyWindowCoordinator::PopupRequest& request) {
            return CreatePopupWindow(request);
          },
          [this](const std::string& window_id) {
            if (window_id == kPrimaryWindowId) {
              if (window_) window_->Activate();
              return;
            }
            const auto found = popup_windows_.find(window_id);
            if (found != popup_windows_.end() && found->second.window) {
              found->second.window->Activate();
            }
          },
          [this](const browser_session::SessionWindowSnapshot& snapshot) {
            pending_session_restore_.push_back(snapshot);
            restoring_window_ids_.push_back(snapshot.window_id);
            return true;
          }});
  auto restored_snapshot = window::AlloySessionRestore::LoadCheckpoint(
      dependencies_.session_path, profile_id_value_, &session_load_result_);
  const bool restore_requested =
      restored_snapshot && IsRestorableProductSession(*restored_snapshot);
  if (restored_snapshot && !restore_requested) {
    session_load_result_ = window::AlloySessionFileResult::kCorrupt;
  }
  session_writes_enabled_ =
      session_load_result_ == window::AlloySessionFileResult::kNotFound ||
      session_load_result_ == window::AlloySessionFileResult::kSuccess;
  if (restore_requested) {
    if (!coordinator_->RestoreSession(*restored_snapshot, profile_id)) {
      return false;
    }
  } else if (!coordinator_->CreatePrimary(kPrimaryWindowId, profile_id)) {
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
  profile_settings_ = std::make_unique<window::AlloyProfileSettings>(
      window::AlloyProfileSettings::Callbacks{
          {},
          [this](const browser_engine::ProfileId& source_profile,
                 std::uint64_t generation) {
            return CreateIncognitoWindow(source_profile, generation);
          },
          {},
          {}});
  const auto settings_profile =
      browser_engine::ProfileId::TryCreate(profile_id_value_);
  if (!settings_profile ||
      !profile_settings_->AddProfile(
          *settings_profile, profile_id_value_,
          browser_profiles_view::ProfileEntryKind::kRegular)) {
    profile_settings_.reset();
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
  if (!InitializeDailyState(*settings_profile)) {
    builtin_content_ = nullptr;
    page_markdown_.reset();
    profile_settings_.reset();
    coordinator_.reset();
    return false;
  }
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
      OmniboxStrings(locale),
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
      NavigationStrings(locale),
      window::AlloyNavigation::Callbacks{[this](const std::string& address) {
        if (omnibox_) static_cast<void>(omnibox_->SetAddress(address));
      }});
  if (!tab_strip_->panel() || !omnibox_->panel() || !navigation_->panel() ||
      (!restore_requested &&
       !CreateTab(std::move(initial_url),
                  browser_engine::ContentPurpose::kControlledBuiltIn))) {
    coordinator_.reset();
    return false;
  }
  if (!restore_requested) {
    tab_id_ = views_.begin()->first;
  }
  title_ = std::move(title);
  started_ = true;
  if (restore_requested) {
    // RestoreSession owns only the domain transaction. Re-entering CEF Views
    // creation from its callback can violate Chrome runtime observer teardown
    // on a warm profile. Materialize BrowserViews after that callback returns.
    if (!CefPostDelayedTask(
            TID_UI,
            CefCreateClosureTask(base::BindOnce(
                &AlloyProductHostWin::CompleteSessionRestore,
                CefRefPtr<AlloyProductHostWin>(this))),
            kSessionRestoreDelayMilliseconds)) {
      started_ = false;
      coordinator_.reset();
      return false;
    }
  } else {
    CefWindow::CreateTopLevelWindow(this);
  }
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
  static_cast<void>(SaveSessionCheckpointNow());
  closing_ = true;
  if (coordinator_->has_window(kPrimaryWindowId)) {
    const auto active = controller() ? controller()->model().active_tab()
                                     : std::optional<window::TabId>{};
    const bool prepared = active && PrepareActiveChromeForClose(*active);
    if (!primary_closing_ &&
        !coordinator_->BeginCloseWindow(kPrimaryWindowId, force_close)) {
      closing_ = false;
      if (prepared) SyncChrome();
      return false;
    }
    primary_closing_ = true;
  }
  for (auto& [window_id, popup] : popup_windows_) {
    if (popup.closing) continue;
    if (!coordinator_->BeginCloseWindow(window_id, force_close)) continue;
    popup.closing = true;
    auto* popup_controller = coordinator_->controller(window_id);
    if (popup_controller && popup_controller->pending_count() == 0 &&
        popup.window) {
      popup.window->Close();
    }
  }
  if (primary_closing_ && controller() &&
      controller()->pending_count() == 0 && window_) {
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
  if (session_restore_failed_) {
    if (window) window->Close();
    return;
  }
  std::optional<std::string> pending_window_id;
  if (!pending_restored_windows_.empty()) {
    pending_window_id = std::move(pending_restored_windows_.front());
    pending_restored_windows_.pop_front();
  } else if (!pending_popup_windows_.empty()) {
    pending_window_id = std::move(pending_popup_windows_.front());
    pending_popup_windows_.pop_front();
  }
  if (pending_window_id && *pending_window_id != kPrimaryWindowId) {
    const std::string& window_id = *pending_window_id;
    const auto found = popup_windows_.find(window_id);
    auto* popup_controller =
        coordinator_ ? coordinator_->controller(window_id) : nullptr;
    if (!started_ || closing_ || !window || found == popup_windows_.end() ||
        !popup_controller || found->second.window ||
        !coordinator_->AttachWindow(window_id, window)) {
      if (window) window->Close();
      return;
    }
    found->second.window = window;
    CefBoxLayoutSettings popup_settings;
    auto popup_layout = window->SetToBoxLayout(popup_settings);
    if (found->second.incognito && found->second.navigation &&
        found->second.omnibox) {
      found->second.toolbar = CefPanel::CreatePanel(nullptr);
      CefBoxLayoutSettings toolbar_settings;
      toolbar_settings.horizontal = true;
      auto toolbar_layout =
          found->second.toolbar->SetToBoxLayout(toolbar_settings);
      found->second.toolbar->AddChildView(found->second.navigation->panel());
      found->second.toolbar->AddChildView(found->second.omnibox->panel());
      toolbar_layout->SetFlexForView(found->second.omnibox->panel(), 1);
      window->AddChildView(found->second.toolbar);
    }
    window->AddChildView(popup_controller->container());
    popup_layout->SetFlexForView(popup_controller->container(), 1);
    window->SetTitle(found->second.incognito
                         ? CefString(Localized(dependencies_.locale.locale,
                                               "privacy.incognito"))
                         : CefString(title_));
    window->SetSize(CefSize(720, 560));
    window->Layout();
    window->Show();
    window->Activate();
    return;
  }
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
  if (session_restore_failed_) return true;
  for (auto& [window_id, popup] : popup_windows_) {
    if (!popup.window || !window || !popup.window->IsSame(window)) continue;
    auto* popup_controller =
        coordinator_ ? coordinator_->controller(window_id) : nullptr;
    if (!popup.closing) {
      static_cast<void>(SaveSessionCheckpointNow());
      if (!coordinator_ ||
          !coordinator_->BeginCloseWindow(window_id, false)) {
        return false;
      }
      popup.closing = true;
    }
    return !popup_controller || popup_controller->pending_count() == 0;
  }
  if (!window_ || !window || !window_->IsSame(window)) {
    return true;
  }
  if (!primary_closing_) {
    static_cast<void>(SaveSessionCheckpointNow());
    const auto active = controller() ? controller()->model().active_tab()
                                     : std::optional<window::TabId>{};
    const bool prepared = active && PrepareActiveChromeForClose(*active);
    if (!coordinator_ ||
        !coordinator_->BeginCloseWindow(kPrimaryWindowId, false)) {
      if (prepared) SyncChrome();
      return false;
    }
    primary_closing_ = true;
    return false;
  }
  return !controller() || controller()->pending_count() == 0;
}

void AlloyProductHostWin::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  for (auto found = popup_windows_.begin(); found != popup_windows_.end();
       ++found) {
    if (!found->second.window || !window ||
        !found->second.window->IsSame(window)) {
      continue;
    }
    const std::string window_id = found->first;
    found->second.window = nullptr;
    if (!coordinator_ || !coordinator_->OnWindowClosed(window_id)) return;
    popup_windows_.erase(found);
    ScheduleSessionCheckpoint();
    if (coordinator_->window_count() == 0 && coordinator_->Shutdown()) {
      coordinator_.reset();
      NotifyClosed();
    }
    return;
  }
  if (!window_ || !window || !window_->IsSame(window) || !coordinator_ ||
      !coordinator_->OnWindowClosed(kPrimaryWindowId)) {
    return;
  }
  window_ = nullptr;
  ScheduleSessionCheckpoint();
  if (coordinator_->window_count() == 0 && coordinator_->Shutdown()) {
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
  const auto owner = OwnerWindowIdForView(view);
  auto* tab_controller = owner && coordinator_
                             ? coordinator_->controller(*owner)
                             : nullptr;
  const auto capabilities = InitialCapabilities();
  if (session_restore_failed_ && view && browser && tab_controller &&
      capabilities) {
    static_cast<void>(
        tab_controller->OnBrowserCreated(view, browser, *capabilities));
    return;
  }
  std::optional<window::TabId> tab_id;
  if (owner && *owner == kPrimaryWindowId) {
    tab_id = TabForView(view);
  } else if (owner) {
    const auto popup = popup_windows_.find(*owner);
    if (popup != popup_windows_.end() && view) {
      for (const auto& [id, candidate] : popup->second.views) {
        if (candidate && candidate->IsSame(view)) {
          tab_id = id;
          break;
        }
      }
    }
  }
  if (!view || !browser || !tab_controller || !capabilities || !tab_id ||
      !tab_controller->OnBrowserCreated(view, browser, *capabilities)) {
    if (owner && *owner != kPrimaryWindowId) {
      const auto popup = popup_windows_.find(*owner);
      if (popup != popup_windows_.end() && !popup->second.closing &&
          coordinator_ && coordinator_->BeginCloseWindow(*owner, true)) {
        popup->second.closing = true;
        if (popup->second.window) popup->second.window->Close();
      }
    } else if (!closing_) {
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
  if (owner && *owner != kPrimaryWindowId) {
    auto popup = popup_windows_.find(*owner);
    if (popup != popup_windows_.end()) {
      popup->second.browsers[*tab_id] = browser;
      if (popup->second.tab_id == *tab_id) popup->second.browser = browser;
      if (popup->second.incognito && popup->second.navigation &&
          popup->second.omnibox) {
        const auto main_frame = browser->GetMainFrame();
        const std::string address =
            main_frame ? main_frame->GetURL().ToString() : std::string{};
        if (!popup->second.navigation->Bind(*owner, browser) ||
            !popup->second.omnibox->SetAddress(address)) {
          if (!popup->second.closing && coordinator_ &&
              coordinator_->BeginCloseWindow(*owner, true)) {
            popup->second.closing = true;
          }
          if (popup->second.window) popup->second.window->Close();
          return;
        }
      }
    }
    if (callbacks_.browser_created) callbacks_.browser_created(browser);
    if (OnRestoredBrowserReady()) return;
    static_cast<void>(tab_controller->Activate(*tab_id));
    return;
  }
  if (!dependencies_.request_context && browser->GetHost()) {
    dependencies_.request_context = browser->GetHost()->GetRequestContext();
  }
  if (!dependencies_.request_context) {
    if (!closing_) Close(true);
    return;
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
  if (OnRestoredBrowserReady()) return;
  CefPostTask(TID_UI,
              base::BindOnce(&AlloyProductHostWin::ActivateCreatedTab,
                             CefRefPtr<AlloyProductHostWin>(this), *tab_id));
}

void AlloyProductHostWin::OnBrowserDestroyed(CefRefPtr<CefBrowserView> view,
                                             CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  for (auto& [window_id, popup] : popup_windows_) {
    static_cast<void>(window_id);
    for (auto found = popup.views.begin(); found != popup.views.end();) {
      if (found->second && view && found->second->IsSame(view)) {
        popup.browsers.erase(found->first);
        found = popup.views.erase(found);
      } else {
        ++found;
      }
    }
    for (auto found = popup.browsers.begin(); found != popup.browsers.end();) {
      if (found->second && browser && found->second->IsSame(browser)) {
        found = popup.browsers.erase(found);
      } else {
        ++found;
      }
    }
    if (popup.view && view && popup.view->IsSame(view)) {
      popup.view = nullptr;
      popup.browser = nullptr;
    }
  }
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
  auto* tab_controller = ControllerForBrowser(browser);
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

bool AlloyProductHostWin::OnBeforePopup(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int,
    const CefString& target_url, const CefString&,
    CefLifeSpanHandler::WindowOpenDisposition,
    bool user_gesture, const CefPopupFeatures&, CefWindowInfo&,
    CefRefPtr<CefClient>&, CefBrowserSettings&,
    CefRefPtr<CefDictionaryValue>&, bool*) {
  CEF_REQUIRE_UI_THREAD();
  const auto owner = OwnerWindowIdForBrowser(browser);
  if (!owner || !frame || !frame->IsMain() || !coordinator_) return true;
  static_cast<void>(coordinator_->RequestPopup(
      *owner, std::move(browser), target_url.ToString(), user_gesture));
  return true;
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
  const auto owner = OwnerWindowIdForBrowser(browser);
  auto* tab_controller = owner && coordinator_
                             ? coordinator_->controller(*owner)
                             : nullptr;
  const auto* closing =
      browser && tab_controller
          ? tab_controller->model().FindByBrowser(browser->GetIdentifier())
          : nullptr;
  const auto closing_tab = closing ? std::optional<window::TabId>(closing->id)
                                    : std::nullopt;
  if (!Owns(browser) || !tab_controller) {
    return;
  }
  if (!closing_ && !primary_closing_ && owner &&
      *owner == kPrimaryWindowId && closing) {
    RecordRecentlyClosed(*closing);
  }
  const bool finalized = tab_controller->OnBeforeClose(browser);
  if (!finalized &&
      (!session_restore_failed_ || tab_controller->OwnsBrowser(browser))) {
    return;
  }
  if (session_restore_failed_) {
    if (tab_controller->pending_count() == 0 && coordinator_) {
      if (*owner == kPrimaryWindowId && window_) {
        window_->Close();
        return;
      }
      auto popup = popup_windows_.find(*owner);
      if (popup != popup_windows_.end() && popup->second.window) {
        popup->second.window->Close();
        return;
      }
      static_cast<void>(coordinator_->OnWindowClosed(*owner));
      popup_windows_.erase(*owner);
    }
    if (coordinator_ && coordinator_->window_count() == 0 &&
        coordinator_->Shutdown()) {
      coordinator_.reset();
      NotifyClosed();
    }
    return;
  }
  if (owner && *owner == kPrimaryWindowId && closing_tab) {
    const auto controls = site_controls_.find(*closing_tab);
    if (controls != site_controls_.end()) {
      controls->second->Shutdown();
      site_controls_.erase(controls);
    }
    site_origins_.erase(*closing_tab);
    site_urls_.erase(*closing_tab);
    tab_titles_.erase(*closing_tab);
    history_committed_generations_.erase(*closing_tab);
  }
  if (owner && *owner == kPrimaryWindowId && closing_tab) {
    media_observation_bridge_.CloseBrowser(
        browser, static_cast<std::uint32_t>(*closing_tab));
  }
  if (owner && *owner == kPrimaryWindowId && browser_ && browser &&
      browser_->IsSame(browser)) {
    browser_ = nullptr;
  }
  if (owner && *owner != kPrimaryWindowId) {
    auto popup = popup_windows_.find(*owner);
    if (popup == popup_windows_.end()) return;
    if (closing_tab) popup->second.browsers.erase(*closing_tab);
    if (closing_tab && popup->second.tab_id == *closing_tab) {
      popup->second.browser = nullptr;
    }
    if (tab_controller->pending_count() != 0 && popup->second.closing) {
      static_cast<void>(tab_controller->RequestNextClose(false));
    } else if (tab_controller->pending_count() == 0) {
      if (!popup->second.closing && coordinator_ &&
          coordinator_->BeginCloseWindow(*owner, false)) {
        popup->second.closing = true;
      }
      if (popup->second.window) popup->second.window->Close();
    }
    return;
  }
  if (primary_closing_ && tab_controller->pending_count() != 0) {
    static_cast<void>(tab_controller->RequestNextClose(false));
    return;
  }
  if (tab_controller->pending_count() == 0 && window_) {
    if (!primary_closing_ && coordinator_) {
      primary_closing_ = true;
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
  ScheduleSessionCheckpoint();
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
  const auto owner = OwnerWindowIdForBrowser(browser);
  auto* tab_controller = owner && coordinator_
                             ? coordinator_->controller(*owner)
                             : nullptr;
  if (!owner || !tab_controller ||
      !tab_controller->OnRenderProcessGone(browser)) {
    return;
  }
  if (*owner != kPrimaryWindowId) {
    static_cast<void>(tab_controller->FinalizeRendererCrash(browser));
    auto popup = popup_windows_.find(*owner);
    if (popup != popup_windows_.end() && !popup->second.closing &&
        coordinator_->BeginCloseWindow(*owner, true)) {
      popup->second.closing = true;
    }
    return;
  }
  if (const auto* tab =
          tab_controller->model().FindByBrowser(browser->GetIdentifier())) {
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
  if (auto* tab_controller = ControllerForBrowser(browser)) {
    static_cast<void>(tab_controller->SynchronizeRuntimeState(browser));
  }
  const auto owner = OwnerWindowIdForBrowser(browser);
  if (owner && *owner != kPrimaryWindowId) {
    auto found = popup_windows_.find(*owner);
    if (found != popup_windows_.end() && found->second.incognito &&
        found->second.navigation && frame) {
      static_cast<void>(found->second.navigation->OnLoadEnd(
          browser, frame->GetURL().ToString()));
      if (found->second.omnibox) {
        static_cast<void>(found->second.omnibox->OnNavigationFinished(
            http_status_code >= 200 && http_status_code < 400,
            frame->GetURL().ToString()));
      }
    }
  }
  if (!owner || *owner != kPrimaryWindowId) return;
  media_observation_bridge_.BindCurrentMainFrame(browser);
  const std::string address = frame->GetURL();
  if (navigation_) {
    static_cast<void>(navigation_->OnLoadEnd(browser, address));
  }
  if (omnibox_) {
    static_cast<void>(omnibox_->OnNavigationFinished(
        http_status_code >= 200 && http_status_code < 400, address));
  }
  CommitHistoryNavigation(browser, address, http_status_code);
}

void AlloyProductHostWin::OnBuiltinTitleChange(
    CefRefPtr<CefBrowser> browser, const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || !controller()) return;
  const auto owner = OwnerWindowIdForBrowser(browser);
  const auto* tab = controller()->model().FindByBrowser(browser->GetIdentifier());
  if (!owner || *owner != kPrimaryWindowId || !tab) return;
  const std::string value = title.ToString();
  if (IsSafeHistoryTitle(value)) {
    tab_titles_[tab->id] = value;
  } else {
    tab_titles_.erase(tab->id);
  }
}

void AlloyProductHostWin::OnBuiltinLoadError(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             cef_errorcode_t error_code,
                                             const CefString&,
                                             const CefString& failed_url) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser) || !frame || !frame->IsMain()) return;
  const auto owner = OwnerWindowIdForBrowser(browser);
  if (owner && *owner != kPrimaryWindowId) {
    auto found = popup_windows_.find(*owner);
    if (found != popup_windows_.end() && found->second.incognito &&
        found->second.navigation) {
      static_cast<void>(found->second.navigation->OnLoadError(
          browser, failed_url.ToString(),
          IsCertificateOrSslError(error_code)));
      if (found->second.omnibox) {
        static_cast<void>(found->second.omnibox->OnNavigationFinished(
            false, failed_url.ToString()));
      }
    }
  }
  if (!owner || *owner != kPrimaryWindowId) return;
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
  if (Owns(browser) && frame && frame->IsMain()) {
    auto* tab_controller = ControllerForBrowser(browser);
    if (tab_controller) {
      static_cast<void>(
          tab_controller->OnAddressChange(browser, url.ToString()));
    }
    const auto owner = OwnerWindowIdForBrowser(browser);
    if (owner && *owner == kPrimaryWindowId && navigation_) {
      if (tab_controller) {
        static_cast<void>(SynchronizeSiteControls(browser, url.ToString()));
      }
      static_cast<void>(navigation_->OnAddressChange(browser, url.ToString()));
      const auto* active_tab =
          tab_controller
              ? tab_controller->model().FindByBrowser(browser->GetIdentifier())
              : nullptr;
      if (bookmarks_ && active_tab && IsActiveTab(active_tab->id)) {
        static_cast<void>(bookmarks_->RefreshForUrl(url.ToString()));
      }
    } else if (owner) {
      auto found = popup_windows_.find(*owner);
      if (found != popup_windows_.end() && found->second.incognito &&
          found->second.navigation) {
        static_cast<void>(found->second.navigation->OnAddressChange(
            browser, url.ToString()));
      }
    }
  }
}

void AlloyProductHostWin::OnBuiltinLoadingStateChange(
    CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
    bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (!Owns(browser)) return;
  auto* tab_controller = ControllerForBrowser(browser);
  std::uint64_t previous_generation = 0;
  if (tab_controller) {
    if (const auto* previous =
            tab_controller->model().FindByBrowser(browser->GetIdentifier())) {
      previous_generation = previous->navigation_generation;
    }
    static_cast<void>(tab_controller->OnLoadingStateChange(
        browser, is_loading, can_go_back, can_go_forward));
    const auto* current =
        tab_controller->model().FindByBrowser(browser->GetIdentifier());
    const auto owner = OwnerWindowIdForBrowser(browser);
    if (owner && *owner == kPrimaryWindowId && current &&
        current->navigation_generation != previous_generation) {
      media_observation_bridge_.AdvanceNavigation(
          browser, static_cast<std::uint32_t>(current->id),
          current->navigation_generation);
    }
  }
  const auto owner = OwnerWindowIdForBrowser(browser);
  if (owner && *owner == kPrimaryWindowId && navigation_) {
    static_cast<void>(navigation_->OnLoadingStateChange(
        browser, is_loading, can_go_back, can_go_forward));
  } else if (owner) {
    auto found = popup_windows_.find(*owner);
    if (found != popup_windows_.end() && found->second.incognito &&
        found->second.navigation) {
      static_cast<void>(found->second.navigation->OnLoadingStateChange(
          browser, is_loading, can_go_back, can_go_forward));
    }
  }
  if (owner && *owner == kPrimaryWindowId && is_loading && page_tools_ &&
      tab_controller) {
    const auto* tab =
        tab_controller->model().FindByBrowser(browser->GetIdentifier());
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
  auto view = CefBrowserView::CreateBrowserView(
      this, url, settings, nullptr, dependencies_.request_context, this);
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

bool AlloyProductHostWin::IsRestorableProductSession(
    const browser_session::SessionProfileSnapshot& snapshot) const {
  if (!browser_session::IsValid(snapshot) ||
      snapshot.profile_id != profile_id_value_) {
    return false;
  }
  std::size_t primary_count = 0;
  for (const auto& session_window : snapshot.windows) {
    if (session_window.window_id == kPrimaryWindowId) {
      ++primary_count;
    }
    if (session_window.window_id.rfind("incognito-", 0) == 0) {
      return false;
    }
  }
  return primary_count == 1;
}

bool AlloyProductHostWin::CreateRestoredWindow(
    const browser_session::SessionWindowSnapshot& snapshot) {
  CEF_REQUIRE_UI_THREAD();
  auto* restored_controller =
      coordinator_ ? coordinator_->controller(snapshot.window_id) : nullptr;
  const bool primary = snapshot.window_id == kPrimaryWindowId;
  if (!restored_controller || snapshot.tabs.empty() || next_navigation_id_ == 0 ||
      (primary && !views_.empty()) ||
      (!primary && popup_windows_.count(snapshot.window_id) != 0)) {
    return false;
  }

  PopupWindowRecord restored_popup;
  restored_popup.window_id = snapshot.window_id;
  restored_popup.request_context = dependencies_.request_context;
  std::map<window::TabId, CefRefPtr<CefBrowserView>> restored_views;
  window::TabId active_tab = 0;
  CefBrowserSettings settings;
  for (std::size_t index = 0; index < snapshot.tabs.size(); ++index) {
    if (next_navigation_id_ == 0) return false;
    auto restored_view = CefBrowserView::CreateBrowserView(
        this, snapshot.tabs[index].url, settings, nullptr,
        dependencies_.request_context, this);
    if (!restored_view) return false;
    const auto restored_tab =
        restored_controller->BeginRestore(
            restored_view,
            browser_engine::NavigationId::FromRaw(next_navigation_id_++),
            snapshot.tabs[index]);
    if (!restored_tab ||
        !restored_views.emplace(*restored_tab, restored_view).second) {
      return false;
    }
    if (index == snapshot.active_index) active_tab = *restored_tab;
  }
  if (active_tab == 0) return false;

  if (primary) {
    views_ = std::move(restored_views);
    tab_id_ = active_tab;
    view_ = views_.at(active_tab);
  } else {
    restored_popup.views = std::move(restored_views);
    restored_popup.tab_id = active_tab;
    restored_popup.view = restored_popup.views.at(active_tab);
    popup_windows_.emplace(snapshot.window_id, std::move(restored_popup));
  }
  pending_restored_windows_.push_back(snapshot.window_id);
  return true;
}

void AlloyProductHostWin::CompleteSessionRestore() {
  CEF_REQUIRE_UI_THREAD();
  if (!started_ || closing_ || closed_ || !coordinator_ ||
      pending_session_restore_.empty()) {
    return;
  }
  const std::size_t window_count = pending_session_restore_.size();
  for (const auto& snapshot : pending_session_restore_) {
    pending_restored_browsers_ += snapshot.tabs.size();
  }
  while (!pending_session_restore_.empty()) {
    const auto snapshot = std::move(pending_session_restore_.front());
    pending_session_restore_.pop_front();
    if (!CreateRestoredWindow(snapshot)) {
      FailSessionRestore();
      return;
    }
  }
  if (tab_id_ == 0 || views_.count(tab_id_) == 0 ||
      pending_restored_windows_.size() != window_count) {
    FailSessionRestore();
    return;
  }
  view_ = views_.at(tab_id_);
  for (std::size_t index = 0; index < window_count; ++index) {
    CefWindow::CreateTopLevelWindow(this);
  }
}

void AlloyProductHostWin::FailSessionRestore() {
  CEF_REQUIRE_UI_THREAD();
  if (session_restore_failed_ || !coordinator_) return;
  session_restore_failed_ = true;
  session_writes_enabled_ = false;
  closing_ = true;
  pending_restored_browsers_ = 0;
  pending_session_restore_.clear();
  pending_restored_windows_.clear();
  for (const auto& window_id : restoring_window_ids_) {
    if (coordinator_->has_window(window_id)) {
      static_cast<void>(coordinator_->BeginCloseWindow(window_id, true));
    }
  }
  for (const auto& window_id : restoring_window_ids_) {
    auto* failed_controller = coordinator_->controller(window_id);
    if (failed_controller && failed_controller->pending_count() == 0) {
      static_cast<void>(coordinator_->OnWindowClosed(window_id));
      popup_windows_.erase(window_id);
    }
  }
  if (coordinator_->window_count() == 0 && coordinator_->Shutdown()) {
    coordinator_.reset();
    NotifyClosed();
  }
}

void AlloyProductHostWin::ScheduleSessionCheckpoint() {
  CEF_REQUIRE_UI_THREAD();
  if (!started_ || closing_ || !session_writes_enabled_ ||
      dependencies_.session_path.empty()) {
    return;
  }
  if (session_checkpoint_pending_) return;
  if (++session_checkpoint_generation_ == 0) {
    session_checkpoint_generation_ = 1;
  }
  session_checkpoint_pending_ = true;
  CefPostDelayedTask(
      TID_UI,
      CefCreateClosureTask(base::BindOnce(
          &AlloyProductHostWin::SaveSessionCheckpoint,
          CefRefPtr<AlloyProductHostWin>(this), session_checkpoint_generation_)),
      kSessionCheckpointDelayMilliseconds);
}

void AlloyProductHostWin::SaveSessionCheckpoint(std::uint64_t generation) {
  CEF_REQUIRE_UI_THREAD();
  if (generation != session_checkpoint_generation_) return;
  session_checkpoint_pending_ = false;
  if (closing_) return;
  static_cast<void>(SaveSessionCheckpointNow());
}

bool AlloyProductHostWin::SaveSessionCheckpointNow() {
  CEF_REQUIRE_UI_THREAD();
  if (!session_writes_enabled_ || dependencies_.session_path.empty() ||
      !coordinator_) {
    return false;
  }
  const auto profile = browser_engine::ProfileId::TryCreate(profile_id_value_);
  auto snapshot =
      profile ? coordinator_->SnapshotSession(*profile) : std::nullopt;
  if (!snapshot || snapshot->windows.empty()) return false;
  bool has_primary = false;
  for (const auto& session_window : snapshot->windows) {
    has_primary = has_primary || session_window.window_id == kPrimaryWindowId;
  }
  if (!has_primary) snapshot->windows.front().window_id = kPrimaryWindowId;
  if (!IsRestorableProductSession(*snapshot)) return false;
  session_save_result_ = window::AlloySessionRestore::SaveCheckpoint(
      dependencies_.session_path, *snapshot,
      browser_session::WindowKind::kRegular);
  return session_save_result_ == window::AlloySessionFileResult::kSuccess;
}

bool AlloyProductHostWin::InitializeDailyState(
    const browser_engine::ProfileId& profile_id) {
  CEF_REQUIRE_UI_THREAD();
  if (bookmarks_ || history_ || downloads_ || download_handler_ ||
      dependencies_.bookmarks_path.empty() ||
      dependencies_.history_path.empty() ||
      dependencies_.download_directory.empty()) {
    return false;
  }
  bookmarks_ = std::make_unique<window::AlloyBookmarks>(
      profile_id,
      window::AlloyBookmarks::Callbacks{
          [this](const std::string& url) {
            if (!browser_ || !browser_->GetMainFrame()) return false;
            browser_->GetMainFrame()->LoadURL(url);
            return true;
          },
          [this](const std::string& url) {
            return CreateTab(url, browser_engine::ContentPurpose::kWeb);
          }});
  history_ = std::make_unique<window::AlloyHistory>(
      profile_id, false,
      window::AlloyHistory::Callbacks{[this](const std::string& url) {
        return CreateTab(url, browser_engine::ContentPurpose::kWeb);
      }});
  downloads_ = std::make_unique<window::AlloyDownloads>(
      dependencies_.download_directory, &Utf8PathExists,
      window::AlloyDownloads::Callbacks{
          [this](std::uint64_t id, const std::string& path) {
            return download_handler_ &&
                   download_handler_->ConfirmPending(id, path);
          },
          [this](std::uint64_t id) {
            return download_handler_ && download_handler_->DiscardPending(id);
          },
          [this](std::uint64_t id) {
            return download_handler_ && download_handler_->Pause(id);
          },
          [this](std::uint64_t id) {
            return download_handler_ && download_handler_->Resume(id);
          },
          [this](std::uint64_t id) {
            return download_handler_ && download_handler_->Cancel(id);
          },
          {}});
  download_handler_ = new permission::CefDownloadHandlerAdapter(
      dependencies_.permission_store, downloads_.get());

  std::error_code path_error;
  const auto bookmarks_path =
      std::filesystem::u8path(dependencies_.bookmarks_path);
  const bool bookmarks_exist =
      std::filesystem::exists(bookmarks_path, path_error);
  if (path_error || (bookmarks_exist &&
                     !bookmarks_->LoadFromFile(dependencies_.bookmarks_path))) {
    daily_data_load_failed_ = true;
  } else {
    bookmarks_writes_enabled_ = true;
  }
  path_error.clear();
  const auto history_path = std::filesystem::u8path(dependencies_.history_path);
  const bool history_exists = std::filesystem::exists(history_path, path_error);
  if (path_error ||
      (history_exists && !history_->LoadFromFile(dependencies_.history_path))) {
    daily_data_load_failed_ = true;
  } else {
    history_writes_enabled_ = true;
  }
  return true;
}

void AlloyProductHostWin::CommitHistoryNavigation(
    CefRefPtr<CefBrowser> browser, const std::string& address,
    int http_status_code) {
  CEF_REQUIRE_UI_THREAD();
  if (!history_ || !history_writes_enabled_ || !browser || address.empty() ||
      http_status_code < 200 || http_status_code >= 400 || !controller()) {
    return;
  }
  const auto* tab =
      controller()->model().FindByBrowser(browser->GetIdentifier());
  if (!tab || tab->url != address || tab->navigation_generation == 0) return;
  const auto committed = history_committed_generations_.find(tab->id);
  if (committed != history_committed_generations_.end() &&
      committed->second >= tab->navigation_generation) {
    return;
  }
  const auto title = tab_titles_.find(tab->id);
  const std::string& history_title =
      title != tab_titles_.end() && IsSafeHistoryTitle(title->second)
          ? title->second
          : address;
  if (!history_->BeginNavigation(tab->navigation_generation) ||
      history_->CommitNavigation(tab->navigation_generation, address,
                                 history_title, NowUnixSeconds()) !=
          window::AlloyHistoryResult::kSuccess) {
    return;
  }
  history_committed_generations_[tab->id] = tab->navigation_generation;
  static_cast<void>(SaveHistory());
}

void AlloyProductHostWin::RecordRecentlyClosed(const window::TabSnapshot& tab) {
  CEF_REQUIRE_UI_THREAD();
  if (!history_ || !history_writes_enabled_ || tab.url.empty()) return;
  const auto title = tab_titles_.find(tab.id);
  const std::string& closed_title =
      title != tab_titles_.end() && IsSafeHistoryTitle(title->second)
          ? title->second
          : tab.url;
  if (history_->RecordClosedTab(tab.url, closed_title, NowUnixSeconds()) ==
      window::AlloyHistoryResult::kSuccess) {
    static_cast<void>(SaveHistory());
  }
}

bool AlloyProductHostWin::SaveBookmarks() {
  CEF_REQUIRE_UI_THREAD();
  if (!bookmarks_ || !bookmarks_writes_enabled_) return false;
  if (bookmarks_->SaveToFile(dependencies_.bookmarks_path)) return true;
  bookmarks_writes_enabled_ = false;
  return false;
}

bool AlloyProductHostWin::SaveHistory() {
  CEF_REQUIRE_UI_THREAD();
  if (!history_ || !history_writes_enabled_) return false;
  if (history_->SaveToFile(dependencies_.history_path)) return true;
  history_writes_enabled_ = false;
  return false;
}

void AlloyProductHostWin::ShutdownDailyState() {
  CEF_REQUIRE_UI_THREAD();
  if (download_handler_) {
    download_handler_->Shutdown();
    download_handler_ = nullptr;
  }
  if (downloads_) {
    static_cast<void>(downloads_->Shutdown());
    downloads_.reset();
  }
  if (history_) {
    static_cast<void>(history_->Shutdown());
    history_.reset();
  }
  if (bookmarks_) {
    static_cast<void>(bookmarks_->Shutdown());
    bookmarks_.reset();
  }
  history_committed_generations_.clear();
  tab_titles_.clear();
  bookmarks_writes_enabled_ = false;
  history_writes_enabled_ = false;
}

bool AlloyProductHostWin::CreatePopupWindow(
    const window::AlloyWindowCoordinator::PopupRequest& request) {
  CEF_REQUIRE_UI_THREAD();
  auto* popup_controller =
      coordinator_ ? coordinator_->controller(request.window_id) : nullptr;
  auto request_context = ContextForWindow(request.opener_window_id);
  const auto opener = popup_windows_.find(request.opener_window_id);
  const bool incognito =
      opener != popup_windows_.end() && opener->second.incognito;
  const std::uint64_t incognito_generation =
      incognito ? opener->second.incognito_generation : 0;
  if (!popup_controller || request.window_id.empty() || request.url.empty() ||
      popup_windows_.count(request.window_id) != 0 ||
      next_navigation_id_ == 0 || !request_context) {
    return false;
  }
  CefBrowserSettings settings;
  auto view = CefBrowserView::CreateBrowserView(
      this, request.url, settings, nullptr, request_context, this);
  const auto tab =
      view ? popup_controller->BeginCreate(
                 view, browser_engine::ContentPurpose::kWeb,
                 browser_engine::NavigationId::FromRaw(next_navigation_id_++))
           : std::nullopt;
  if (!tab) return false;
  PopupWindowRecord popup;
  popup.window_id = request.window_id;
  popup.view = view;
  popup.request_context = request_context;
  popup.views.emplace(*tab, view);
  popup.tab_id = *tab;
  popup.incognito_generation = incognito_generation;
  popup.incognito = incognito;
  popup_windows_.emplace(request.window_id, std::move(popup));
  pending_popup_windows_.push_back(request.window_id);
  CefWindow::CreateTopLevelWindow(this);
  return true;
}

bool AlloyProductHostWin::CreateIncognitoWindow(
    const browser_engine::ProfileId& profile_id, std::uint64_t generation) {
  CEF_REQUIRE_UI_THREAD();
  if (!started_ || closing_ || !coordinator_ || generation == 0 ||
      profile_id.value() != profile_id_value_ ||
      !dependencies_.profile_context_factory ||
      coordinator_->window_count() + pending_incognito_contexts_.size() >=
          browser_windows::kMaxWindows ||
      pending_incognito_contexts_.count(generation) != 0) {
    return false;
  }
  CefRefPtr<AlloyProductHostWin> self(this);
  auto request_context =
      dependencies_.profile_context_factory->CreateTemporaryContext(
          new OneShotRequestContextHandler(
              [self = std::move(self), profile = profile_id.value(), generation](
                  CefRefPtr<CefRequestContext> initialized) {
                self->FinalizeIncognitoContext(profile, generation,
                                                std::move(initialized));
              }));
  if (!request_context || !request_context->GetCachePath().empty()) {
    return false;
  }
  pending_incognito_contexts_.emplace(generation, std::move(request_context));
  return true;
}

void AlloyProductHostWin::FinalizeIncognitoContext(
    std::string profile_id, std::uint64_t generation,
    CefRefPtr<CefRequestContext> request_context) {
  CEF_REQUIRE_UI_THREAD();
  auto pending = pending_incognito_contexts_.find(generation);
  if (!started_ || closing_ || !coordinator_ ||
      profile_id != profile_id_value_ ||
      pending == pending_incognito_contexts_.end() || !pending->second ||
      !request_context || !pending->second->IsSame(request_context) ||
      !request_context->GetCachePath().empty() ||
      !dependencies_.register_incognito_content(request_context)) {
    if (pending != pending_incognito_contexts_.end()) {
      pending_incognito_contexts_.erase(pending);
    }
    return;
  }
  const std::string window_id = "incognito-" + std::to_string(generation);
  const auto logical_profile = browser_engine::ProfileId::TryCreate(profile_id);
  auto incognito_omnibox = std::make_unique<window::AlloyOmnibox>(
      OmniboxStrings(dependencies_.locale.locale),
      window::AlloyOmnibox::Callbacks{
          {},
          [this, window_id](const window::OmniboxSubmission& submission) {
            auto found = popup_windows_.find(window_id);
            if (found != popup_windows_.end() && found->second.navigation) {
              static_cast<void>(found->second.navigation->Navigate(submission));
            }
          },
          {}},
      browser_privacy::DefaultPrivacyDefaults());
  auto* incognito_omnibox_ptr = incognito_omnibox.get();
  auto incognito_navigation = std::make_unique<window::AlloyNavigation>(
      NavigationStrings(dependencies_.locale.locale),
      window::AlloyNavigation::Callbacks{
          [incognito_omnibox_ptr](const std::string& address) {
            if (incognito_omnibox_ptr) {
              static_cast<void>(incognito_omnibox_ptr->SetAddress(address));
            }
          }});
  if (!logical_profile ||
      !incognito_omnibox->panel() || !incognito_navigation->panel() ||
      !coordinator_->CreatePrimary(window_id, *logical_profile, false)) {
    pending_incognito_contexts_.erase(pending);
    return;
  }
  auto* incognito_controller = coordinator_->controller(window_id);
  CefBrowserSettings settings;
  auto view = CefBrowserView::CreateBrowserView(
      this, browser_new_tab::kNewTabUrl, settings, nullptr, request_context,
      this);
  const auto tab = view && incognito_controller && next_navigation_id_ != 0
                       ? incognito_controller->BeginCreate(
                             view,
                             browser_engine::ContentPurpose::kControlledBuiltIn,
                             browser_engine::NavigationId::FromRaw(
                                 next_navigation_id_++))
                       : std::nullopt;
  if (!tab) {
    static_cast<void>(coordinator_->CancelPendingWindow(window_id));
    pending_incognito_contexts_.erase(pending);
    return;
  }
  PopupWindowRecord popup;
  popup.window_id = window_id;
  popup.view = view;
  popup.request_context = request_context;
  popup.omnibox = std::move(incognito_omnibox);
  popup.navigation = std::move(incognito_navigation);
  popup.views.emplace(*tab, view);
  popup.tab_id = *tab;
  popup.incognito_generation = generation;
  popup.incognito = true;
  popup_windows_.emplace(window_id, std::move(popup));
  pending_incognito_contexts_.erase(pending);
  pending_popup_windows_.push_back(window_id);
  CefWindow::CreateTopLevelWindow(this);
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

bool AlloyProductHostWin::OnRestoredBrowserReady() {
  CEF_REQUIRE_UI_THREAD();
  if (pending_restored_browsers_ == 0) return false;
  if (--pending_restored_browsers_ != 0) return true;
  if (!ActivateTab(tab_id_)) {
    FailSessionRestore();
    return true;
  }
  for (auto& [window_id, popup] : popup_windows_) {
    auto* popup_controller =
        coordinator_ ? coordinator_->controller(window_id) : nullptr;
    const auto browser = popup.browsers.find(popup.tab_id);
    if (!popup_controller || !popup_controller->Activate(popup.tab_id) ||
        browser == popup.browsers.end()) {
      FailSessionRestore();
      return true;
    }
    popup.view = popup.views.at(popup.tab_id);
    popup.browser = browser->second;
  }
  restoring_window_ids_.clear();
  PostSyncChrome();
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
  ScheduleSessionCheckpoint();
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
    if (bookmarks_) {
      static_cast<void>(bookmarks_->RefreshForUrl(snapshot->url));
    }
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
  callbacks.open_incognito = [this] {
    return profile_settings_ &&
           profile_settings_->OpenIncognito() ==
               window::AlloyProfileSettingsResult::kSuccess;
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
  ShutdownDailyState();
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
  return ControllerForBrowser(browser) != nullptr;
}

std::optional<std::string> AlloyProductHostWin::OwnerWindowIdForView(
    CefRefPtr<CefBrowserView> view) const {
  CEF_REQUIRE_UI_THREAD();
  if (!view || !coordinator_) return std::nullopt;
  if (auto* primary = controller(); primary && primary->OwnsView(view)) {
    return std::string(kPrimaryWindowId);
  }
  for (const auto& [window_id, popup] : popup_windows_) {
    auto* popup_controller = coordinator_->controller(window_id);
    if (popup_controller && popup_controller->OwnsView(view)) return window_id;
  }
  for (const auto& window_id : restoring_window_ids_) {
    auto* restoring_controller = coordinator_->controller(window_id);
    if (restoring_controller && restoring_controller->OwnsView(view)) {
      return window_id;
    }
  }
  return std::nullopt;
}

std::optional<std::string> AlloyProductHostWin::OwnerWindowIdForBrowser(
    CefRefPtr<CefBrowser> browser) const {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || !coordinator_) return std::nullopt;
  if (auto* primary = controller(); primary && primary->OwnsBrowser(browser)) {
    return std::string(kPrimaryWindowId);
  }
  for (const auto& [window_id, popup] : popup_windows_) {
    auto* popup_controller = coordinator_->controller(window_id);
    if (popup_controller && popup_controller->OwnsBrowser(browser)) {
      return window_id;
    }
  }
  for (const auto& window_id : restoring_window_ids_) {
    auto* restoring_controller = coordinator_->controller(window_id);
    if (restoring_controller && restoring_controller->OwnsBrowser(browser)) {
      return window_id;
    }
  }
  return std::nullopt;
}

window::AlloyTabController* AlloyProductHostWin::ControllerForBrowser(
    CefRefPtr<CefBrowser> browser) const {
  const auto owner = OwnerWindowIdForBrowser(browser);
  return owner && coordinator_ ? coordinator_->controller(*owner) : nullptr;
}

CefRefPtr<CefRequestContext> AlloyProductHostWin::ContextForWindow(
    const std::string& window_id) const {
  CEF_REQUIRE_UI_THREAD();
  if (window_id == kPrimaryWindowId) return dependencies_.request_context;
  const auto found = popup_windows_.find(window_id);
  return found != popup_windows_.end() ? found->second.request_context
                                       : nullptr;
}

void AlloyProductHostWin::ReleaseClosingView(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const auto owner = OwnerWindowIdForBrowser(browser);
  auto* tab_controller = owner && coordinator_
                             ? coordinator_->controller(*owner)
                             : nullptr;
  if (owner && *owner != kPrimaryWindowId) {
    if (!tab_controller || !tab_controller->ReleaseAfterDoClose(browser)) {
      return;
    }
    auto popup = popup_windows_.find(*owner);
    if (popup != popup_windows_.end()) {
      for (auto found = popup->second.browsers.begin();
           found != popup->second.browsers.end(); ++found) {
        if (!found->second || !browser || !found->second->IsSame(browser)) {
          continue;
        }
        const auto view = popup->second.views.find(found->first);
        if (view != popup->second.views.end()) {
          popup->second.views.erase(view);
        }
        popup->second.browsers.erase(found);
        break;
      }
      if (popup->second.browser && browser &&
          popup->second.browser->IsSame(browser)) {
        popup->second.view = nullptr;
        popup->second.browser = nullptr;
      }
    }
    return;
  }
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
  ShutdownDailyState();
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
  primary_closing_ = false;
  view_ = nullptr;
  browser_ = nullptr;
  ShutdownDailyState();
  if (builtin_content_) {
    builtin_content_->Shutdown();
    builtin_content_ = nullptr;
  }
  if (page_markdown_) {
    page_markdown_->Shutdown();
    page_markdown_.reset();
  }
  if (profile_settings_) {
    profile_settings_->Shutdown();
    profile_settings_.reset();
  }
  pending_incognito_contexts_.clear();
  dependencies_.request_context = nullptr;
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
