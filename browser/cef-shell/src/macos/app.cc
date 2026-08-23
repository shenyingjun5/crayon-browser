#include "macos/app.h"

#include <memory>
#include <utility>

#include "browser/permission/permission_store.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "about:blank";

}  // namespace

BrowserApp::BrowserApp(std::string product_name)
    : product_name_(std::move(product_name)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
      tab_controller_(new window::TabController(
          kInitialUrl, window::TabController::BrowserCreatedCallback{},
          std::nullopt, permission_store_.get())) {}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  if (!tab_controller_->CreateMainWindow()) {
    CefQuitMessageLoop();
  }
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

}  // namespace crayon::browser::cef_shell
