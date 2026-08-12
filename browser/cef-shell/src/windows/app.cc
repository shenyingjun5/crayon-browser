#include "windows/app.h"

#include <algorithm>
#include <utility>

#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"
#include "resource_ids.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "about:blank";
constexpr int kMainIconSize = 32;
constexpr int kSmallIconSize = 16;

} // namespace

BrowserClient::BrowserClient(HINSTANCE resource_module)
    : main_icon_(static_cast<HICON>(LoadImageW(
          resource_module, MAKEINTRESOURCEW(IDI_CRAYON_APP), IMAGE_ICON,
          kMainIconSize, kMainIconSize, LR_DEFAULTCOLOR))),
      small_icon_(static_cast<HICON>(LoadImageW(
          resource_module, MAKEINTRESOURCEW(IDI_CRAYON_APP_SMALL), IMAGE_ICON,
          kSmallIconSize, kSmallIconSize, LR_DEFAULTCOLOR))) {}

BrowserClient::~BrowserClient() {
  if (main_icon_) {
    DestroyIcon(main_icon_);
  }
  if (small_icon_) {
    DestroyIcon(small_icon_);
  }
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const auto existing = std::find_if(
      browsers_.begin(), browsers_.end(),
      [&browser](const auto &candidate) { return candidate->IsSame(browser); });
  if (existing == browsers_.end()) {
    browsers_.push_back(browser);
  }
  HWND window = browser->GetHost()->GetWindowHandle();
  if (window) {
    SendMessageW(window, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(main_icon_));
    SendMessageW(window, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(small_icon_));
  }
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const auto existing = std::find_if(
      browsers_.begin(), browsers_.end(),
      [&browser](const auto &candidate) { return candidate->IsSame(browser); });
  if (existing == browsers_.end()) {
    return;
  }
  browsers_.erase(existing);
  if (browsers_.empty()) {
    CefQuitMessageLoop();
  }
}

BrowserApp::BrowserApp(HINSTANCE resource_module, std::wstring product_name)
    : product_name_(std::move(product_name)),
      client_(new BrowserClient(resource_module)) {}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefWindowInfo window_info;
  window_info.SetAsPopup(nullptr, product_name_);
  window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

  CefBrowserSettings browser_settings;
  if (!CefBrowserHost::CreateBrowser(window_info, client_, kInitialUrl,
                                     browser_settings, nullptr, nullptr)) {
    CefQuitMessageLoop();
  }
}

} // namespace crayon::browser::cef_shell
