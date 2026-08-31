#include "windows/app.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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
constexpr std::size_t kResourceStringCapacity = 512;
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

std::string LoadUtf8String(HINSTANCE resource_module,
                           unsigned int resource_id) {
  std::array<wchar_t, kResourceStringCapacity> buffer{};
  const int length = LoadStringW(resource_module, resource_id, buffer.data(),
                                 static_cast<int>(buffer.size()));
  if (length <= 0 || static_cast<std::size_t>(length) >= buffer.size() - 1) {
    return {};
  }
  return WideToUtf8(
      std::wstring_view(buffer.data(), static_cast<std::size_t>(length)));
}

browser_new_tab::NewTabPageStrings LoadNewTabStrings(
    HINSTANCE resource_module) {
  return browser_new_tab::NewTabPageStrings{
      "zh-CN",
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_TITLE),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_REGULAR_HEADING),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_INCOGNITO_HEADING),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_REGULAR_DESCRIPTION),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_INCOGNITO_DESCRIPTION),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_OMNIBOX_HINT),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_SHORTCUTS_HEADING),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_EMPTY_SHORTCUTS),
      LoadUtf8String(resource_module, IDS_CRAYON_NEW_TAB_CONFIG_ERROR),
  };
}

browser_mdv::MdvPageStrings LoadMdvStrings(HINSTANCE resource_module) {
  return browser_mdv::MdvPageStrings{
      "zh-CN",
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TITLE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_VIEW_SOURCE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_VIEW_PREVIEW),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_VIEW_SPLIT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_STATUS_EMPTY),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_STATUS_TOO_LARGE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_STATUS_INVALID_UTF8),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_STATUS_RENDER_POLICY),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_STATUS_NOT_MARKDOWN),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_STATUS_SAVED),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_CONFIRM_TEXT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_LABEL_SAVE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_LABEL_DISCARD),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_LABEL_CANCEL),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_LABEL_OPEN_IN_VIEWER),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOLBAR_TITLE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_BOLD),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_ITALIC),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_STRIKE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_INLINE_CODE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_BULLET_LIST),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_ORDERED_LIST),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_TASK_LIST),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_QUOTE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_CODE_BLOCK),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_TABLE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_LINK),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_DIVIDER),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_HEADING1),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_HEADING2),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_HEADING3),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_STRUCTURE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_INDENT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_OUTDENT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_ALIGN_DEFAULT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_ALIGN_LEFT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_ALIGN_CENTER),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOL_ALIGN_RIGHT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOLTIP_VIEW),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOLTIP_MARKDOWN),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOLTIP_STRUCTURE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_TOOLTIP_TABLE_ALIGNMENT),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_MERMAID_FULLSCREEN),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_MERMAID_SOURCE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_MERMAID_CLOSE),
      LoadUtf8String(resource_module, IDS_CRAYON_MDV_MERMAID_ERROR),
      browser_mdv::MdvShortcutPlatform::kWindows,
  };
}

std::string ContentHostExecutablePath() {
  std::array<wchar_t, MAX_PATH> path{};
  const DWORD length =
      GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  return WideToUtf8(
      (std::filesystem::path(
           std::wstring(path.data(), static_cast<std::size_t>(length)))
           .parent_path() /
       L"crayon-content-host.exe")
          .wstring());
}

page_markdown::PageMarkdownStrings LoadPageMarkdownStrings(
    HINSTANCE resource_module) {
  return page_markdown::PageMarkdownStrings{
      LoadUtf8String(resource_module, IDS_CRAYON_PAGE_MARKDOWN_PREVIEW),
      LoadUtf8String(resource_module, IDS_CRAYON_PAGE_MARKDOWN_COPY),
      LoadUtf8String(resource_module, IDS_CRAYON_PAGE_MARKDOWN_SAVE_AS),
      LoadUtf8String(resource_module, IDS_CRAYON_PAGE_MARKDOWN_COPIED),
      LoadUtf8String(resource_module, IDS_CRAYON_PAGE_MARKDOWN_COPY_FAILED),
      LoadUtf8String(resource_module, IDS_CRAYON_PAGE_MARKDOWN_SAVE_CANCELLED)};
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

BrowserApp::BrowserApp(HINSTANCE resource_module, std::wstring product_name)
    : product_name_(std::move(product_name)),
      window_icons_(std::make_shared<WindowsWindowIcons>(resource_module)),
      new_tab_strings_(LoadNewTabStrings(resource_module)),
      mdv_strings_(LoadMdvStrings(resource_module)),
      page_markdown_strings_(LoadPageMarkdownStrings(resource_module)),
      mdv_runtime_(std::make_shared<mdv::MdvRuntimeState>()),
      mdv_entries_(std::make_shared<mdv::MdvEntryController>(mdv_runtime_,
                                                             mdv_strings_)),
      mdv_editing_(
          std::make_shared<mdv::MdvEditController>(mdv_runtime_, mdv_strings_)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
      content_host_(std::make_unique<windows::ContentHostAdapter>()),
      tab_controller_(new window::TabController(
          browser_new_tab::kNewTabUrl,
          [window_icons = window_icons_](CefRefPtr<CefBrowser> browser) {
            window_icons->Apply(browser);
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
  if (!new_tab::RegisterNewTabSchemeHandlerFactory(page_model,
                                                   new_tab_strings_)) {
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
    return;
  }
  if (!mdv::RegisterMdvSchemeHandlerFactory(mdv_strings_, mdv_runtime_)) {
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
  tab_controller_->SetBrowsersClosedCallback([this] {
    content_host_tick_active_ = false;
    page_markdown_preview_->Stop();
    content_host_->Stop();
    shell_runtime_->Shutdown();
  });
  if (!content_host_->Start(ContentHostExecutablePath())) {
    shell_runtime_->Shutdown();
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
      shell_runtime_->Shutdown();
      CefQuitMessageLoop();
      return;
    }
    content_host_tick_active_ = true;
    ScheduleContentHostTick();
    return;
  }
  if (++content_host_start_checks_ >= kContentHostStartupChecks) {
    content_host_->Stop();
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
  content_host_->Tick();
  page_markdown_preview_->Tick(content_host_->Drain(64),
                               content_host_->healthy());
  ScheduleContentHostTick();
}

bool BrowserApp::new_tab_strings_valid() const {
  return !new_tab_strings_.language.empty() &&
         !new_tab_strings_.document_title.empty() &&
         !new_tab_strings_.regular_heading.empty() &&
         !new_tab_strings_.incognito_heading.empty() &&
         !new_tab_strings_.regular_description.empty() &&
         !new_tab_strings_.incognito_description.empty() &&
         !new_tab_strings_.omnibox_hint.empty() &&
         !new_tab_strings_.shortcuts_heading.empty() &&
         !new_tab_strings_.empty_shortcuts.empty() &&
         !new_tab_strings_.config_error.empty();
}

bool BrowserApp::mdv_strings_valid() const {
  return !mdv_strings_.language.empty() &&
         !mdv_strings_.document_title.empty() &&
         !mdv_strings_.view_source.empty() &&
         !mdv_strings_.view_preview.empty() &&
         !mdv_strings_.view_split.empty() &&
         !mdv_strings_.status_empty.empty() &&
         !mdv_strings_.status_too_large.empty() &&
         !mdv_strings_.status_invalid_utf8.empty() &&
         !mdv_strings_.status_render_policy.empty() &&
         !mdv_strings_.status_not_markdown.empty() &&
         !mdv_strings_.status_saved.empty() &&
         !mdv_strings_.confirm_text.empty() &&
         !mdv_strings_.label_save.empty() &&
         !mdv_strings_.label_discard.empty() &&
         !mdv_strings_.label_cancel.empty() &&
         !mdv_strings_.label_open_in_viewer.empty() &&
         !mdv_strings_.toolbar_title.empty() &&
         !mdv_strings_.tool_bold.empty() && !mdv_strings_.tool_italic.empty() &&
         !mdv_strings_.tool_strike.empty() &&
         !mdv_strings_.tool_inline_code.empty() &&
         !mdv_strings_.tool_bullet_list.empty() &&
         !mdv_strings_.tool_ordered_list.empty() &&
         !mdv_strings_.tool_task_list.empty() &&
         !mdv_strings_.tool_quote.empty() &&
         !mdv_strings_.tool_code_block.empty() &&
         !mdv_strings_.tool_table.empty() && !mdv_strings_.tool_link.empty() &&
         !mdv_strings_.tool_divider.empty() &&
         !mdv_strings_.tool_heading1.empty() &&
         !mdv_strings_.tool_heading2.empty() &&
         !mdv_strings_.tool_heading3.empty() &&
         !mdv_strings_.tool_structure.empty() &&
         !mdv_strings_.tool_indent.empty() &&
         !mdv_strings_.tool_outdent.empty() &&
         !mdv_strings_.tool_align_default.empty() &&
         !mdv_strings_.tool_align_left.empty() &&
         !mdv_strings_.tool_align_center.empty() &&
         !mdv_strings_.tool_align_right.empty() &&
         !mdv_strings_.tooltip_view.empty() &&
         !mdv_strings_.tooltip_markdown.empty() &&
         !mdv_strings_.tooltip_structure.empty() &&
         !mdv_strings_.tooltip_table_alignment.empty();
}

bool BrowserApp::page_markdown_strings_valid() const {
  return !page_markdown_strings_.preview_command.empty() &&
         !page_markdown_strings_.copy_command.empty() &&
         !page_markdown_strings_.save_as_command.empty() &&
         !page_markdown_strings_.copied_status.empty() &&
         !page_markdown_strings_.copy_failed_status.empty() &&
         !page_markdown_strings_.save_cancelled_status.empty();
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

}  // namespace crayon::browser::cef_shell
