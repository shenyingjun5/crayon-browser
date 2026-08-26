#include "macos/app.h"

#include <memory>
#include <utility>

#include "browser/new_tab/cef_new_tab_handler.h"
#include "browser/permission/permission_store.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kInitialUrl[] = "crayon://newtab";

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

}  // namespace

BrowserApp::BrowserApp(std::string product_name)
    : product_name_(std::move(product_name)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
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
  if (!tab_controller_->CreateMainWindow()) {
    CefQuitMessageLoop();
  }
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

}  // namespace crayon::browser::cef_shell
