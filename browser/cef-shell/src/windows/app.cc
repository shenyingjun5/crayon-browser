#include "windows/app.h"

#include <algorithm>
#include <utility>

#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "about:blank";

}  // namespace

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const auto existing = std::find_if(
      browsers_.begin(), browsers_.end(),
      [&browser](const auto& candidate) { return candidate->IsSame(browser); });
  if (existing == browsers_.end()) {
    browsers_.push_back(browser);
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
      [&browser](const auto& candidate) { return candidate->IsSame(browser); });
  if (existing == browsers_.end()) {
    return;
  }
  browsers_.erase(existing);
  if (browsers_.empty()) {
    CefQuitMessageLoop();
  }
}

BrowserApp::BrowserApp(std::wstring product_name)
    : product_name_(std::move(product_name)), client_(new BrowserClient) {}

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

}  // namespace crayon::browser::cef_shell
