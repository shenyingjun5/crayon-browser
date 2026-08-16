#include "windows/app.h"

#include <memory>
#include <utility>

#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"
#include "resource_ids.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "about:blank";
constexpr int kMainIconSize = 32;
constexpr int kSmallIconSize = 16;

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
      tab_controller_(new window::TabController(
          kInitialUrl,
          [window_icons = window_icons_](CefRefPtr<CefBrowser> browser) {
            window_icons->Apply(browser);
          })),
      shell_runtime_(std::make_shared<WindowsShellRuntime>(tab_controller_)) {}

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
  if (!tab_controller_->CreateMainWindow()) {
    shell_runtime_->Shutdown();
    CefQuitMessageLoop();
  }
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

}  // namespace crayon::browser::cef_shell
