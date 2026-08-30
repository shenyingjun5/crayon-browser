#include "macos/app.h"

#include <CoreFoundation/CoreFoundation.h>

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

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "crayon://newtab";
constexpr std::size_t kContentHostStartupChecks = 500;
constexpr std::int64_t kContentHostTickMilliseconds = 20;

browser_new_tab::NewTabPageStrings DefaultNewTabStrings() {
  return browser_new_tab::NewTabPageStrings{
      .language = "zh-CN",
      .document_title = "蜡笔浏览器",
      .regular_heading = "开始干净的画布",
      .incognito_heading = "无痕浏览",
      .regular_description = "使用地址栏搜索或输入网址。",
      .incognito_description = "本页不显示跨会话快捷入口、历史或建议。",
      .omnibox_hint = "聚焦地址栏",
      .shortcuts_heading = "固定快捷入口",
      .empty_shortcuts = "暂无固定快捷入口",
      .config_error = "快捷入口配置已损坏并已安全忽略",
  };
}

std::string Utf8(CFStringRef value) {
  if (!value) return {};
  const CFIndex length = CFStringGetLength(value);
  const CFIndex capacity =
      CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<std::size_t>(capacity));
  return CFStringGetCString(value, buffer.data(), capacity,
                            kCFStringEncodingUTF8)
             ? std::string(buffer.data())
             : std::string();
}

std::string Localized(const char* key) {
  CFStringRef key_string = CFStringCreateWithCString(
      kCFAllocatorDefault, key, kCFStringEncodingUTF8);
  if (!key_string) return {};
  CFStringRef value = CFBundleCopyLocalizedString(
      CFBundleGetMainBundle(), key_string, key_string, CFSTR("Localizable"));
  const std::string result = Utf8(value);
  if (value) CFRelease(value);
  CFRelease(key_string);
  return result;
}

std::string PreferredLanguage() {
  CFArrayRef languages = CFLocaleCopyPreferredLanguages();
  std::string language = "en-US";
  if (languages && CFArrayGetCount(languages) > 0) {
    const auto first = static_cast<CFStringRef>(
        CFArrayGetValueAtIndex(languages, 0));
    const std::string tag = Utf8(first);
    if (tag.rfind("zh", 0) == 0) language = "zh-CN";
  }
  if (languages) CFRelease(languages);
  return language;
}

std::string ContentHostExecutablePath() {
  CFURLRef bundle_url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
  if (!bundle_url) return {};
  CFStringRef bundle_path =
      CFURLCopyFileSystemPath(bundle_url, kCFURLPOSIXPathStyle);
  CFRelease(bundle_url);
  const std::string path = Utf8(bundle_path);
  if (bundle_path) CFRelease(bundle_path);
  if (path.empty()) return {};
  return (std::filesystem::path(path) / "Contents" / "Helpers" /
          "crayon-content-host")
      .string();
}

browser_mdv::MdvPageStrings DefaultMdvStrings() {
  return browser_mdv::MdvPageStrings{
      PreferredLanguage(),
      Localized("mdv.title"),
      Localized("mdv.view_source"),
      Localized("mdv.view_preview"),
      Localized("mdv.view_split"),
      Localized("mdv.status_empty"),
      Localized("mdv.status_too_large"),
      Localized("mdv.status_invalid_utf8"),
      Localized("mdv.status_render_policy"),
      Localized("mdv.status_not_markdown"),
      Localized("mdv.status_saved"),
      Localized("mdv.confirm_text"),
      Localized("mdv.label_save"),
      Localized("mdv.label_discard"),
      Localized("mdv.label_cancel"),
      Localized("mdv.label_open_in_viewer"),
      Localized("mdv.toolbar.title"),
      Localized("mdv.tool.bold"),
      Localized("mdv.tool.italic"),
      Localized("mdv.tool.strike"),
      Localized("mdv.tool.inline_code"),
      Localized("mdv.tool.bullet_list"),
      Localized("mdv.tool.ordered_list"),
      Localized("mdv.tool.task_list"),
      Localized("mdv.tool.quote"),
      Localized("mdv.tool.code_block"),
      Localized("mdv.tool.table"),
      Localized("mdv.tool.link"),
      Localized("mdv.tool.divider"),
      Localized("mdv.tool.heading1"),
      Localized("mdv.tool.heading2"),
      Localized("mdv.tool.heading3"),
      Localized("mdv.tool.structure"),
      Localized("mdv.tool.indent"),
      Localized("mdv.tool.outdent"),
      Localized("mdv.tool.align_default"),
      Localized("mdv.tool.align_left"),
      Localized("mdv.tool.align_center"),
      Localized("mdv.tool.align_right"),
      Localized("mdv.tooltip.view"),
      Localized("mdv.tooltip.markdown"),
      Localized("mdv.tooltip.structure"),
      Localized("mdv.tooltip.table_alignment"),
      Localized("mdv.mermaid.fullscreen"),
      Localized("mdv.mermaid.source"),
      Localized("mdv.mermaid.close"),
      Localized("mdv.mermaid.error"),
      browser_mdv::MdvShortcutPlatform::kMacOS,
  };
}

}  // namespace

BrowserApp::BrowserApp(std::string product_name)
    : product_name_(std::move(product_name)),
      mdv_strings_(DefaultMdvStrings()),
      mdv_runtime_(std::make_shared<mdv::MdvRuntimeState>(
          mdv::BuildFixtureSnapshot())),
      mdv_entries_(std::make_shared<mdv::MdvEntryController>(
          mdv_runtime_, mdv_strings_)),
      mdv_editing_(std::make_shared<mdv::MdvEditController>(mdv_runtime_,
                                                            mdv_strings_)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
      content_host_(std::make_unique<macos::ContentHostAdapter>()),
      tab_controller_(new window::TabController(
          kInitialUrl, window::TabController::BrowserCreatedCallback{},
          std::nullopt, permission_store_.get())) {}

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
  new_tab::RegisterNewTabSchemeHandlerFactory(
      browser_new_tab::BuildNewTabPageModel(
          browser_new_tab::NewTabProfileMode::kRegular, {}),
      DefaultNewTabStrings());
  if (!mdv::RegisterMdvSchemeHandlerFactory(mdv_strings_, mdv_runtime_)) {
    CefQuitMessageLoop();
    return;
  }
  tab_controller_->SetLocalEntryCommandHandler(
      [entries = mdv_entries_, editing = mdv_editing_](
          CefRefPtr<CefBrowser> browser, int command_id) {
        if (entries->HandleChromeCommand(browser, command_id)) return true;
        return editing->HandleSaveCommand(browser, command_id);
      });
  mdv_entries_->SetDocumentLoadedCallback(
      [editing = mdv_editing_](CefRefPtr<CefBrowser> browser,
                               const std::string& path,
                               const std::string& normalized,
                               std::uint64_t size, std::uint64_t mtime) {
        editing->OnDocumentLoaded(browser, path, normalized, size, mtime);
      });
  tab_controller_->SetNavigationInterceptor(
      [editing = mdv_editing_, entries = mdv_entries_](
          CefRefPtr<CefBrowser> browser, const CefString& url,
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
      [entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefContextMenuParams> params,
                               CefRefPtr<CefMenuModel> model) {
        return entries->HandleContextMenuAugment(browser, params, model);
      });
  tab_controller_->SetContextMenuCommandHandler(
      [entries = mdv_entries_](CefRefPtr<CefBrowser> browser, int command_id) {
        return entries->HandleContextMenuCommand(browser, command_id);
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
  tab_controller_->SetPageSnapshotAdmission(
      [host = content_host_.get()] { return host->healthy(); });
  tab_controller_->SetPageSnapshotEventsReadyCallback([this] {
    content_host_->Consume(tab_controller_->DrainPageSnapshots(16));
  });
  tab_controller_->SetBrowsersClosedCallback([this] {
    content_host_tick_active_ = false;
    content_host_->Stop();
  });
  if (!content_host_->Start(ContentHostExecutablePath())) {
    CefQuitMessageLoop();
    return;
  }
  ContinueContentHostStartup();
}

void BrowserApp::ContinueContentHostStartup() {
  CEF_REQUIRE_UI_THREAD();
  if (content_host_->healthy()) {
    if (!tab_controller_->CreateMainWindow()) {
      content_host_->Stop();
      CefQuitMessageLoop();
      return;
    }
    content_host_tick_active_ = true;
    ScheduleContentHostTick();
    return;
  }
  if (++content_host_start_checks_ >= kContentHostStartupChecks) {
    content_host_->Stop();
    CefQuitMessageLoop();
    return;
  }
  CefPostDelayedTask(
      TID_UI,
      CefCreateClosureTask(base::BindOnce(
          &BrowserApp::ContinueContentHostStartup, CefRefPtr<BrowserApp>(this))),
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
  content_host_->Tick();
  ScheduleContentHostTick();
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

}  // namespace crayon::browser::cef_shell
