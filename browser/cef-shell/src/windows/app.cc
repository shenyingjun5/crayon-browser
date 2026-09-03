#include "windows/app.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/mdv/cef_mdv_handler.h"
#include "browser/new_tab/cef_new_tab_handler.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "resource_ids.h"
#include "windows/markdown_file_dialog_win.h"
#include "windows/page_markdown_platform_win.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr int kMainIconSize = 32;
constexpr int kSmallIconSize = 16;
constexpr std::size_t kContentHostStartupChecks = 500;
constexpr std::int64_t kContentHostTickMilliseconds = 20;

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int value_length = static_cast<int>(value.size());
  const int utf8_length =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          value_length, nullptr, 0, nullptr, nullptr);
  if (utf8_length <= 0) {
    return {};
  }
  std::string utf8(static_cast<std::size_t>(utf8_length), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          value_length, utf8.data(), utf8_length, nullptr,
                          nullptr) != utf8_length) {
    return {};
  }
  return utf8;
}

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int value_length = static_cast<int>(value.size());
  const int wide_length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), value_length, nullptr, 0);
  if (wide_length <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          value_length, wide.data(), wide_length) !=
      wide_length) {
    return {};
  }
  return wide;
}

::crayon::browser::product_strings::ProductStrings BuildProductStringsOrEmpty(
    const ::crayon::browser::localization::LocaleSnapshot& snapshot) {
  return ::crayon::browser::product_strings::BuildProductStrings(
             snapshot, browser_mdv::MdvShortcutPlatform::kWindows)
      .value_or(::crayon::browser::product_strings::ProductStrings{});
}

std::string HelperExecutablePath(std::wstring_view name) {
  std::array<wchar_t, MAX_PATH> path{};
  const DWORD length =
      GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  return WideToUtf8(
      (std::filesystem::path(
           std::wstring(path.data(), static_cast<std::size_t>(length)))
           .parent_path() /
       name)
          .wstring());
}

page_markdown::PageMarkdownStrings BuildPageMarkdownStrings(
    const ::crayon::browser::product_strings::PageMarkdownStrings& strings) {
  return page_markdown::PageMarkdownStrings{
      strings.preview_command, strings.copy_command, strings.save_as_command,
      strings.copied_status, strings.copy_failed_status,
      strings.save_cancelled_status};
}

windows::CastChromeStrings BuildCastStrings(
    const ::crayon::browser::product_strings::CastStrings& strings) {
  return windows::CastChromeStrings{
      Utf8ToWide(strings.button_select), Utf8ToWide(strings.button_stop),
      Utf8ToWide(strings.picker_title), Utf8ToWide(strings.picker_empty),
      Utf8ToWide(strings.picker_select), Utf8ToWide(strings.picker_refresh),
      Utf8ToWide(strings.picker_cancel), Utf8ToWide(strings.cast_code_label),
      Utf8ToWide(strings.cast_code_connect),
      Utf8ToWide(strings.cast_code_failed),
      Utf8ToWide(strings.playback_pause), Utf8ToWide(strings.playback_resume),
      Utf8ToWide(strings.playback_seek), Utf8ToWide(strings.playback_seconds),
      Utf8ToWide(strings.playback_failed)};
}

windows::CastChromePresentation CastChromePresentation(
    media_host::CastShellPresentation presentation) {
  return windows::CastChromePresentation{
      presentation.cast_code_pending, presentation.cast_code_failed,
      presentation.control_pending, presentation.control_failed,
      presentation.playback_paused};
}

}  // namespace

WindowsWindowIcons::WindowsWindowIcons(HINSTANCE resource_module)
    : main_icon_(static_cast<HICON>(LoadImageW(
          resource_module, MAKEINTRESOURCEW(IDI_CRAYON_APP), IMAGE_ICON,
          kMainIconSize, kMainIconSize, LR_DEFAULTCOLOR))),
      small_icon_(static_cast<HICON>(LoadImageW(
          resource_module, MAKEINTRESOURCEW(IDI_CRAYON_APP_SMALL), IMAGE_ICON,
          kSmallIconSize, kSmallIconSize, LR_DEFAULTCOLOR))) {}

WindowsWindowIcons::~WindowsWindowIcons() {
  if (main_icon_) {
    DestroyIcon(main_icon_);
  }
  if (small_icon_) {
    DestroyIcon(small_icon_);
  }
}

void WindowsWindowIcons::Apply(CefRefPtr<CefBrowser> browser) const {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) {
    return;
  }
  HWND window = browser->GetHost()->GetWindowHandle();
  if (window) {
    SendMessageW(window, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(main_icon_));
    SendMessageW(window, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(small_icon_));
  }
}

BrowserApp::BrowserApp(
    HINSTANCE resource_module,
    ::crayon::browser::localization::LocaleSnapshot locale_snapshot)
    : window_icons_(std::make_shared<WindowsWindowIcons>(resource_module)),
      about_resources_(
          new branding::AboutBrowserResources(locale_snapshot.locale)),
      product_strings_(BuildProductStringsOrEmpty(locale_snapshot)),
      page_markdown_strings_(
          BuildPageMarkdownStrings(product_strings_.page_markdown)),
      cast_strings_(BuildCastStrings(product_strings_.cast)),
      mdv_runtime_(std::make_shared<mdv::MdvRuntimeState>()),
      mdv_entries_(std::make_shared<mdv::MdvEntryController>(
          mdv_runtime_, product_strings_.mdv)),
      mdv_editing_(
          std::make_shared<mdv::MdvEditController>(mdv_runtime_,
                                                   product_strings_.mdv)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
      content_host_(std::make_unique<windows::ContentHostAdapter>()),
      media_host_(std::make_unique<media_host::MediaHostAdapter>(
          std::make_unique<windows::MediaHostProcess>())),
      cast_shell_(std::make_unique<media_host::CastShellController>(
          media_host::CastCommandPort{
              [this](media_host::media_host_ipc::DiscoveryAction action) {
                return media_host_->RequestDiscovery(action);
              },
              [this](std::optional<std::uint64_t> revision,
                     std::uint16_t offset) {
                return media_host_->RequestDevicePage(revision, offset);
              },
              [this](std::uint64_t candidate, std::string device,
                     bool handoff) {
                return media_host_->RequestStartCast(
                    candidate, std::move(device), handoff);
              },
              [this](std::uint64_t generation) {
                return media_host_->RequestStopCast(generation);
              },
              [this](std::string cast_code) {
                return media_host_->RequestResolveCastCode(
                    std::move(cast_code));
              },
              [this](std::uint64_t generation,
                     media_host::media_host_ipc::CastControlAction action,
                     std::optional<std::uint64_t> position) {
                return media_host_->RequestControlCast(generation, action,
                                                       position);
              }})),
      trusted_input_monitor_(
          std::make_unique<windows::TrustedInputMonitorWin>()),
      tab_controller_(new window::TabController(
          browser_new_tab::kNewTabUrl,
          [this](CefRefPtr<CefBrowser> browser) {
            window_icons_->Apply(browser);
            if (!cast_chrome_) return;
            active_browser_id_ = browser->GetIdentifier();
            static_cast<void>(cast_chrome_->AttachWindow(
                active_browser_id_, browser->GetHost()->GetWindowHandle()));
            cast_chrome_->SetActiveWindow(active_browser_id_);
            cast_chrome_->Render(
                cast_shell_->coordinator(),
                CastChromePresentation(cast_shell_->presentation()));
          },
          browser_new_tab::kNewTabUrl, permission_store_.get())),
      shell_runtime_(std::make_shared<WindowsShellRuntime>(tab_controller_)) {}

BrowserApp::~BrowserApp() = default;

void BrowserApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {
  new_tab::RegisterCrayonCustomSchemes(registrar);
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  cast_chrome_ = std::make_unique<windows::CastChromeWin>(
      cast_strings_,
      windows::CastChromeCallbacks{
          [this] { return cast_shell_->ActivateCastButton(); },
          [this] { return cast_shell_->RefreshReceivers(); },
          [this] { cast_shell_->CancelReceiverPicker(); },
          [this](const std::string& device_id) {
            return cast_shell_->SelectReceiver(device_id);
          },
          [this](std::string cast_code) {
            return cast_shell_->ConnectCastCode(std::move(cast_code));
          },
          [this](bool paused) { return cast_shell_->SetPaused(paused); },
          [this](std::uint64_t seconds) {
            return cast_shell_->SeekSession(seconds);
          }});
  std::weak_ptr<WindowsShellRuntime> shell_runtime = shell_runtime_;
  tab_controller_->SetChromeCommandCallback([shell_runtime](int command_id) {
    if (const auto runtime = shell_runtime.lock()) {
      runtime->ObserveChromeCommand(command_id);
    }
  });
  tab_controller_->SetBrowsersClosedCallback([shell_runtime]() {
    if (const auto runtime = shell_runtime.lock()) {
      runtime->Shutdown();
    }
  });
  const auto page_model = browser_new_tab::BuildNewTabPageModel(
      browser_new_tab::NewTabProfileMode::kRegular,
      browser_new_tab::ShortcutConfig{});
  if (!new_tab::RegisterNewTabSchemeHandlerFactory(
          page_model, product_strings_.new_tab)) {
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
    return;
  }
  if (!mdv::RegisterMdvSchemeHandlerFactory(product_strings_.mdv,
                                             mdv_runtime_)) {
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
    return;
  }
  tab_controller_->SetLocalEntryCommandHandler(
      [entries = mdv_entries_, editing = mdv_editing_](
          CefRefPtr<CefBrowser> browser, int command_id) {
        if (entries->HandleChromeCommand(browser, command_id)) {
          return true;
        }
        return editing->HandleSaveCommand(browser, command_id);
      });
  tab_controller_->SetFileDialogHandler(windows::HandleMarkdownFileDialog);
  mdv_entries_->SetDocumentLoadedCallback(
      [editing = mdv_editing_](CefRefPtr<CefBrowser> browser,
                               const std::string& path,
                               const std::string& normalized,
                               std::uint64_t size, std::uint64_t mtime) {
        editing->OnDocumentLoaded(browser, path, normalized, size, mtime);
      });
  tab_controller_->SetNavigationInterceptor([editing = mdv_editing_,
                                             entries = mdv_entries_](
                                                CefRefPtr<CefBrowser> browser,
                                                const CefString& url,
                                                bool user_gesture) {
    if (editing->InterceptWhileDirty(browser, url.ToString(), user_gesture)) {
      return true;
    }
    return entries->InterceptNavigation(browser, url, user_gesture);
  });
  tab_controller_->SetLocalEntryDragHandler(
      [entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefDragData> dragData,
                               CefDragHandler::DragOperationsMask mask) {
        return entries->HandleDragEnter(browser, dragData, mask);
      });
  tab_controller_->SetContextMenuAugmenter(
      [this, entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefContextMenuParams> params,
                                     CefRefPtr<CefMenuModel> model) {
        const bool mdv =
            entries->HandleContextMenuAugment(browser, params, model);
        const bool page_markdown =
            page_markdown_preview_->HandleContextMenuAugment(browser, params,
                                                             model);
        return mdv || page_markdown;
      });
  tab_controller_->SetContextMenuCommandHandler(
      [this, entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                                     int command_id) {
        if (entries->HandleContextMenuCommand(browser, command_id)) return true;
        tab_controller_->NoteTrustedUserInputForActiveTab();
        return page_markdown_preview_->HandleContextMenuCommand(browser,
                                                                command_id);
      });
  tab_controller_->SetSaveCommandHandler(
      [editing = mdv_editing_](CefRefPtr<CefBrowser> browser) {
        return editing->SaveWriteBack(browser);
      });
  tab_controller_->SetPageQueryHandler(
      [editing = mdv_editing_](
          CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
          std::uint64_t query_id, const CefString& request, bool persistent,
          CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
        return editing->OnPageQuery(browser, frame, query_id, request,
                                    persistent, std::move(callback));
      });
  page_markdown_preview_ =
      std::make_unique<page_markdown::CefPageMarkdownPreviewController>(
          tab_controller_.get(), mdv_editing_, page_markdown_strings_,
          windows::CopyMarkdownToClipboard);
  tab_controller_->SetPageSnapshotObserver(content_host_.get());
  tab_controller_->SetPageSnapshotAdmission(
      [host = content_host_.get()] { return host->healthy(); });
  tab_controller_->SetPageSnapshotEventsReadyCallback([this] {
    content_host_->Consume(tab_controller_->DrainPageSnapshots(16));
  });
  tab_controller_->SetMediaObservationLifecycleCallback(
      [this, host = media_host_.get()](std::uint32_t tab_id,
                                       std::uint64_t navigation_id,
                                       std::uint32_t generation, bool closed) {
        const bool active = tab_controller_->model().active_tab() == tab_id;
        if (closed) {
          static_cast<void>(host->CloseTab(tab_id, generation));
          if (active) cast_shell_->OnPageClosed();
        } else {
          static_cast<void>(
              host->AdvanceNavigation(tab_id, navigation_id, generation));
          if (active) cast_shell_->OnNavigation();
        }
      });
  tab_controller_->SetBrowserFocusedCallback(
      [this](CefRefPtr<CefBrowser> browser) {
        if (active_browser_id_ != 0 &&
            active_browser_id_ != browser->GetIdentifier()) {
          cast_shell_->OnNavigation();
        }
        active_browser_id_ = browser->GetIdentifier();
        static_cast<void>(cast_chrome_->AttachWindow(
            active_browser_id_, browser->GetHost()->GetWindowHandle()));
        cast_chrome_->SetActiveWindow(active_browser_id_);
        cast_chrome_->Render(
            cast_shell_->coordinator(),
            CastChromePresentation(cast_shell_->presentation()));
      });
  tab_controller_->SetBrowserClosingCallback(
      [this](CefRefPtr<CefBrowser> browser) {
        cast_chrome_->DetachWindow(browser->GetIdentifier());
        if (active_browser_id_ == browser->GetIdentifier())
          active_browser_id_ = 0;
      });
  tab_controller_->SetMediaObservationEventsReadyCallback(
      [this] { ConsumeMediaObservations(); });
  if (!trusted_input_monitor_->Start([controller = tab_controller_] {
        controller->NoteTrustedUserInputForActiveTab();
      })) {
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
    return;
  }
  tab_controller_->SetBrowsersClosedCallback([this] {
    content_host_tick_active_ = false;
    trusted_input_monitor_->Stop();
    page_markdown_preview_->Stop();
    cast_shell_->Shutdown();
    cast_chrome_->Close();
    content_host_->Stop();
    media_host_->Stop();
    shell_runtime_->Shutdown();
  });
  if (!content_host_->Start(HelperExecutablePath(L"crayon-content-host.exe")) ||
      !media_host_->Start(HelperExecutablePath(L"crayon-media-host.exe"))) {
    trusted_input_monitor_->Stop();
    content_host_->Stop();
    media_host_->Stop();
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
    return;
  }
  ContinueContentHostStartup();
}

void BrowserApp::ContinueContentHostStartup() {
  CEF_REQUIRE_UI_THREAD();
  if (content_host_->healthy() && media_host_->healthy()) {
    if (!tab_controller_->CreateMainWindow()) {
      trusted_input_monitor_->Stop();
      content_host_->Stop();
      media_host_->Stop();
      shell_runtime_->Shutdown();
      CefQuitMessageLoop();
      return;
    }
    content_host_tick_active_ = true;
    ScheduleContentHostTick();
    return;
  }
  if (++content_host_start_checks_ >= kContentHostStartupChecks) {
    trusted_input_monitor_->Stop();
    content_host_->Stop();
    media_host_->Stop();
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
    return;
  }
  CefPostDelayedTask(TID_UI,
                     CefCreateClosureTask(
                         base::BindOnce(&BrowserApp::ContinueContentHostStartup,
                                        CefRefPtr<BrowserApp>(this))),
                     kContentHostTickMilliseconds);
}

void BrowserApp::ScheduleContentHostTick() {
  CefPostDelayedTask(
      TID_UI,
      CefCreateClosureTask(base::BindOnce(&BrowserApp::ContentHostTick,
                                          CefRefPtr<BrowserApp>(this))),
      kContentHostTickMilliseconds);
}

void BrowserApp::ContentHostTick() {
  CEF_REQUIRE_UI_THREAD();
  if (!content_host_tick_active_) return;
  content_host_->Consume(tab_controller_->DrainPageSnapshots(16));
  ConsumeMediaObservations();
  content_host_->Tick();
  media_host_->Tick();
  page_markdown_preview_->Tick(content_host_->Drain(64),
                               content_host_->healthy());
  static_cast<void>(media_host_->Drain(64));
  cast_shell_->ConsumePlanning(media_host_->DrainPlanning(64));
  cast_shell_->ConsumeCast(media_host_->DrainCast(64));
  const bool media_healthy = media_host_->healthy();
  const std::uint64_t cast_epoch = media_host_->cast_state_epoch();
  if ((!media_healthy && media_host_was_healthy_) ||
      (media_host_cast_epoch_ != 0 && cast_epoch != media_host_cast_epoch_)) {
    cast_shell_->OnHostUnavailable();
  }
  media_host_was_healthy_ = media_healthy;
  media_host_cast_epoch_ = cast_epoch;
  if (CefRefPtr<CefBrowser> active_browser = tab_controller_->ActiveBrowser()) {
    active_browser_id_ = active_browser->GetIdentifier();
    static_cast<void>(cast_chrome_->AttachWindow(
        active_browser_id_, active_browser->GetHost()->GetWindowHandle()));
    cast_chrome_->SetActiveWindow(active_browser_id_);
  }
  cast_chrome_->Render(cast_shell_->coordinator(),
                       CastChromePresentation(cast_shell_->presentation()));
  ScheduleContentHostTick();
}

void BrowserApp::ConsumeMediaObservations() {
  CEF_REQUIRE_UI_THREAD();
  std::vector<media_host::BrowserMediaFact> facts;
  for (auto& event : tab_controller_->DrainMediaObservations(16)) {
    auto page_url =
        tab_controller_->TrustedPageUrl(event.tab_id, event.navigation_id);
    if (!page_url) continue;
    if (event.source == ::crayon::cef_shell::gateway::EventSource::kMedia &&
        tab_controller_->model().active_tab() == event.tab_id) {
      cast_shell_->OnBrowserVerifiedMedia();
    }
    const auto observed_at = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    facts.push_back(media_host::BrowserMediaFact{
        std::move(event), std::move(*page_url), observed_at});
  }
  media_host_->Consume(std::move(facts));
}

bool BrowserApp::new_tab_strings_valid() const {
  return ::crayon::browser::product_strings::ProductStringsAreComplete(
      product_strings_);
}

bool BrowserApp::mdv_strings_valid() const {
  return ::crayon::browser::product_strings::ProductStringsAreComplete(
      product_strings_);
}

bool BrowserApp::page_markdown_strings_valid() const {
  return ::crayon::browser::product_strings::ProductStringsAreComplete(
      product_strings_);
}

bool BrowserApp::cast_strings_valid() const {
  return !cast_strings_.button_select.empty() &&
         !cast_strings_.button_stop.empty() &&
         !cast_strings_.picker_title.empty() &&
         !cast_strings_.picker_empty.empty() &&
         !cast_strings_.picker_select.empty() &&
         !cast_strings_.picker_refresh.empty() &&
         !cast_strings_.picker_cancel.empty() &&
         !cast_strings_.cast_code_label.empty() &&
         !cast_strings_.cast_code_connect.empty() &&
         !cast_strings_.cast_code_failed.empty() &&
         !cast_strings_.playback_pause.empty() &&
         !cast_strings_.playback_resume.empty() &&
         !cast_strings_.playback_seek.empty() &&
         !cast_strings_.playback_seconds.empty() &&
         !cast_strings_.playback_failed.empty();
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

}  // namespace crayon::browser::cef_shell
