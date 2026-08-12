#include "macos/app.h"

#include <algorithm>
#include <utility>

#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "about:blank";
constexpr int kInitialWindowWidth = 1100;
constexpr int kInitialWindowHeight = 760;

class BrowserViewDelegate final : public CefBrowserViewDelegate {
 public:
  BrowserViewDelegate() = default;

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  IMPLEMENT_REFCOUNTING(BrowserViewDelegate);
  DISALLOW_COPY_AND_ASSIGN(BrowserViewDelegate);
};

class BrowserWindowDelegate final : public CefWindowDelegate {
 public:
  BrowserWindowDelegate(CefRefPtr<CefBrowserView> browser_view,
                        std::string product_name)
      : browser_view_(browser_view), product_name_(std::move(product_name)) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(browser_view_);
    window->SetTitle(product_name_);
    window->Show();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    static_cast<void>(window);
    browser_view_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    static_cast<void>(window);
    if (browser_view_) {
      CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
      if (browser) {
        return browser->GetHost()->TryCloseBrowser();
      }
    }
    return true;
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    static_cast<void>(view);
    return CefSize(kInitialWindowWidth, kInitialWindowHeight);
  }

 private:
  CefRefPtr<CefBrowserView> browser_view_;
  const std::string product_name_;

  IMPLEMENT_REFCOUNTING(BrowserWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(BrowserWindowDelegate);
};

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
  if (browsers_.size() == 1U) {
    is_closing_ = true;
  }
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

void BrowserClient::CloseAllBrowsers(bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  if (browsers_.empty()) {
    CefQuitMessageLoop();
    return;
  }
  for (const auto& browser : browsers_) {
    browser->GetHost()->CloseBrowser(force_close);
  }
}

void BrowserClient::ShowMainWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (browsers_.empty() || is_closing_) {
    return;
  }
  CefRefPtr<CefBrowserView> browser_view =
      CefBrowserView::GetForBrowser(browsers_.front());
  if (browser_view) {
    CefRefPtr<CefWindow> window = browser_view->GetWindow();
    if (window) {
      window->Show();
    }
  }
}

BrowserApp::BrowserApp(std::string product_name)
    : product_name_(std::move(product_name)),
      browser_client_(new BrowserClient) {}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  CefBrowserSettings browser_settings;
  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
      browser_client_, kInitialUrl, browser_settings, nullptr, nullptr,
      new BrowserViewDelegate);
  if (!browser_view) {
    CefQuitMessageLoop();
    return;
  }
  if (!CefWindow::CreateTopLevelWindow(
          new BrowserWindowDelegate(browser_view, product_name_))) {
    CefQuitMessageLoop();
  }
}

}  // namespace crayon::browser::cef_shell
