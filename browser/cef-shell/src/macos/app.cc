#include "macos/app.h"

#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/mdv/cef_mdv_handler.h"
#include "browser/new_tab/cef_new_tab_handler.h"
#include "browser/permission/permission_store.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "macos/page_markdown_platform_mac.h"
#include "macos/trusted_input_monitor_mac.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "crayon://newtab";
constexpr std::size_t kContentHostStartupChecks = 500;
constexpr std::int64_t kContentHostTickMilliseconds = 20;

std::string Utf8(CFStringRef value) {
  if (!value)
    return {};
  const CFIndex length = CFStringGetLength(value);
  const CFIndex capacity =
      CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<std::size_t>(capacity));
  return CFStringGetCString(value, buffer.data(), capacity,
                            kCFStringEncodingUTF8)
             ? std::string(buffer.data())
             : std::string();
}

std::string HelperExecutablePath(const char* helper_name) {
  CFURLRef bundle_url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
  if (!bundle_url)
    return {};
  CFStringRef bundle_path =
      CFURLCopyFileSystemPath(bundle_url, kCFURLPOSIXPathStyle);
  CFRelease(bundle_url);
  const std::string path = Utf8(bundle_path);
  if (bundle_path)
    CFRelease(bundle_path);
  if (path.empty())
    return {};
  return (std::filesystem::path(path) / "Contents" / "Helpers" / helper_name)
      .string();
}

std::uint64_t MonotonicMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

::crayon::browser::product_strings::ProductStrings BuildProductStringsOrEmpty(
    const ::crayon::browser::localization::LocaleSnapshot& snapshot) {
  return ::crayon::browser::product_strings::BuildProductStrings(
             snapshot, browser_mdv::MdvShortcutPlatform::kMacOS)
      .value_or(::crayon::browser::product_strings::ProductStrings{});
}

page_markdown::PageMarkdownStrings BuildPageMarkdownStrings(
    const ::crayon::browser::product_strings::PageMarkdownStrings& strings) {
  return page_markdown::PageMarkdownStrings{
      strings.preview_command, strings.copy_command, strings.save_as_command,
      strings.copied_status, strings.copy_failed_status,
      strings.save_cancelled_status};
}

macos::CastChromeStrings BuildCastStrings(
    const ::crayon::browser::product_strings::CastStrings& strings) {
  return macos::CastChromeStrings{
      strings.button_select, strings.button_stop, strings.picker_title,
      strings.picker_empty, strings.picker_select, strings.picker_refresh,
      strings.picker_cancel};
}

}  // namespace

BrowserApp::BrowserApp(
    ::crayon::browser::localization::LocaleSnapshot locale_snapshot)
    : product_strings_(BuildProductStringsOrEmpty(locale_snapshot)),
      page_markdown_strings_(
          BuildPageMarkdownStrings(product_strings_.page_markdown)),
      cast_strings_(BuildCastStrings(product_strings_.cast)),
      mdv_runtime_(std::make_shared<mdv::MdvRuntimeState>()),
      mdv_entries_(std::make_shared<mdv::MdvEntryController>(mdv_runtime_,
                                                             product_strings_.mdv)),
      mdv_editing_(
          std::make_shared<mdv::MdvEditController>(mdv_runtime_,
                                                   product_strings_.mdv)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
      content_host_(std::make_unique<macos::ContentHostAdapter>()),
      media_host_(std::make_unique<media_host::MediaHostAdapter>(
          std::make_unique<macos::MediaHostProcess>())),
      cast_shell_(std::make_unique<media_host::CastShellController>(
          media_host::CastCommandPort{
          [this](media_host::media_host_ipc::DiscoveryAction action) {
            return media_host_->RequestDiscovery(action);
          },
          [this](std::optional<std::uint64_t> revision, std::uint16_t offset) {
            return media_host_->RequestDevicePage(revision, offset);
          },
          [this](std::uint64_t candidate, std::string device, bool handoff) {
            return media_host_->RequestStartCast(candidate, std::move(device),
                                                 handoff);
          },
          [this](std::uint64_t generation) {
            return media_host_->RequestStopCast(generation);
          }})),
      trusted_input_monitor_(std::make_unique<macos::TrustedInputMonitor>()),
      tab_controller_(new window::TabController(
          kInitialUrl,
          [this](CefRefPtr<CefBrowser> browser) {
            if (!cast_chrome_)
              return;
            active_browser_id_ = browser->GetIdentifier();
            static_cast<void>(cast_chrome_->AttachWindow(
                active_browser_id_, browser->GetHost()->GetWindowHandle()));
            cast_chrome_->SetActiveWindow(active_browser_id_);
            cast_chrome_->Render(cast_shell_->coordinator());
          },
          std::nullopt,
          permission_store_.get())) {}

BrowserApp::~BrowserApp() = default;

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  static_cast<void>(process_type);
  command_line->AppendSwitch("use-mock-keychain");
}

void BrowserApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {
  new_tab::RegisterCrayonCustomSchemes(registrar);
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  cast_chrome_ = std::make_unique<macos::CastChromeMac>(
      cast_strings_,
      macos::CastChromeCallbacks{
          [this] { return cast_shell_->ActivateCastButton(); },
          [this] { return cast_shell_->RefreshReceivers(); },
          [this] { cast_shell_->CancelReceiverPicker(); },
          [this](const std::string& device_id) {
            return cast_shell_->SelectReceiver(device_id);
          }});
  new_tab::RegisterNewTabSchemeHandlerFactory(
      browser_new_tab::BuildNewTabPageModel(
          browser_new_tab::NewTabProfileMode::kRegular, {}),
      product_strings_.new_tab);
  if (!mdv::RegisterMdvSchemeHandlerFactory(product_strings_.mdv,
                                             mdv_runtime_)) {
    CefQuitMessageLoop();
    return;
  }
  tab_controller_->SetLocalEntryCommandHandler(
      [entries = mdv_entries_, editing = mdv_editing_](
          CefRefPtr<CefBrowser> browser, int command_id) {
        if (entries->HandleChromeCommand(browser, command_id))
          return true;
        return editing->HandleSaveCommand(browser, command_id);
      });
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
                               CefRefPtr<CefDragData> drag_data,
                               CefDragHandler::DragOperationsMask mask) {
        return entries->HandleDragEnter(browser, drag_data, mask);
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
        if (entries->HandleContextMenuCommand(browser, command_id))
          return true;
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
          std::int64_t query_id, const CefString& request, bool persistent,
          CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
        return editing->OnPageQuery(browser, frame, query_id, request,
                                    persistent, std::move(callback));
      });
  tab_controller_->SetPageSnapshotObserver(content_host_.get());
  static_cast<void>(
      trusted_input_monitor_->Start([controller = tab_controller_] {
        controller->NoteTrustedUserInputForActiveTab();
      }));
  page_markdown_preview_ =
      std::make_unique<page_markdown::CefPageMarkdownPreviewController>(
          tab_controller_.get(), mdv_editing_,
          page_markdown_strings_,
          macos::CopyMarkdownToPasteboard);
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
          if (active)
            cast_shell_->OnPageClosed();
        } else {
          static_cast<void>(
              host->AdvanceNavigation(tab_id, navigation_id, generation));
          if (active)
            cast_shell_->OnNavigation();
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
        cast_chrome_->Render(cast_shell_->coordinator());
      });
  tab_controller_->SetBrowserClosingCallback(
      [this](CefRefPtr<CefBrowser> browser) {
        cast_chrome_->DetachWindow(browser->GetIdentifier());
        if (active_browser_id_ == browser->GetIdentifier())
          active_browser_id_ = 0;
      });
  tab_controller_->SetMediaObservationEventsReadyCallback(
      [this] { ConsumeMediaObservations(); });
  tab_controller_->SetBrowsersClosedCallback([this] {
    content_host_tick_active_ = false;
    trusted_input_monitor_->Stop();
    page_markdown_preview_->Stop();
    cast_shell_->Shutdown();
    cast_chrome_->Close();
    content_host_->Stop();
    media_host_->Stop();
  });
  if (!content_host_->Start(HelperExecutablePath("crayon-content-host")) ||
      !media_host_->Start(HelperExecutablePath("crayon-media-host"))) {
    content_host_->Stop();
    media_host_->Stop();
    CefQuitMessageLoop();
    return;
  }
  ContinueContentHostStartup();
}

void BrowserApp::ContinueContentHostStartup() {
  CEF_REQUIRE_UI_THREAD();
  if (content_host_->healthy() && media_host_->healthy()) {
    if (!tab_controller_->CreateMainWindow()) {
      content_host_->Stop();
      media_host_->Stop();
      CefQuitMessageLoop();
      return;
    }
    content_host_tick_active_ = true;
    ScheduleContentHostTick();
    return;
  }
  if (++content_host_start_checks_ >= kContentHostStartupChecks) {
    content_host_->Stop();
    media_host_->Stop();
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
  if (!content_host_tick_active_)
    return;
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
  cast_chrome_->Render(cast_shell_->coordinator());
  ScheduleContentHostTick();
}

void BrowserApp::ConsumeMediaObservations() {
  CEF_REQUIRE_UI_THREAD();
  std::vector<media_host::BrowserMediaFact> facts;
  for (auto& event : tab_controller_->DrainMediaObservations(16)) {
    auto page_url =
        tab_controller_->TrustedPageUrl(event.tab_id, event.navigation_id);
    if (!page_url)
      continue;
    if (event.source == ::crayon::cef_shell::gateway::EventSource::kMedia &&
        tab_controller_->model().active_tab() == event.tab_id) {
      cast_shell_->OnBrowserVerifiedMedia();
    }
    facts.push_back(media_host::BrowserMediaFact{
        std::move(event), std::move(*page_url), MonotonicMilliseconds()});
  }
  media_host_->Consume(std::move(facts));
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

bool BrowserApp::product_strings_valid() const {
  return ::crayon::browser::product_strings::ProductStringsAreComplete(
      product_strings_);
}

}  // namespace crayon::browser::cef_shell
