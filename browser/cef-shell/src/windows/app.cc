#include "windows/app.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "browser/mdv/cef_mdv_entries.h"
#include "browser/mdv/cef_mdv_handler.h"
#include "browser/new_tab/cef_new_tab_handler.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"
#include "resource_ids.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr int kMainIconSize = 32;
constexpr int kSmallIconSize = 16;
constexpr std::size_t kResourceStringCapacity = 512;

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
  };
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
      mdv_runtime_(std::make_shared<mdv::MdvRuntimeState>(
          mdv::BuildFixtureSnapshot())),
      mdv_entries_(std::make_shared<mdv::MdvEntryController>(
          mdv_runtime_, mdv_strings_)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
      tab_controller_(new window::TabController(
          browser_new_tab::kNewTabUrl,
          [window_icons = window_icons_](CefRefPtr<CefBrowser> browser) {
            window_icons->Apply(browser);
          },
          browser_new_tab::kNewTabUrl, permission_store_.get())),
      shell_runtime_(std::make_shared<WindowsShellRuntime>(tab_controller_)) {}

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
      [entries = mdv_entries_](CefRefPtr<CefBrowser> browser, int command_id) {
        return entries->HandleChromeCommand(browser, command_id);
      });
  tab_controller_->SetNavigationInterceptor(
      [entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                               const CefString& url, bool user_gesture) {
        return entries->InterceptNavigation(browser, url, user_gesture);
      });
  if (!tab_controller_->CreateMainWindow()) {
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
  }
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
         !mdv_strings_.status_not_markdown.empty();
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

}  // namespace crayon::browser::cef_shell
