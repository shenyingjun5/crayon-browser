// PLT-SHELL-23M: per-locale localization consistency in the real Alloy
// host. For the selected UI language this probe verifies the newtab html
// lang, navigator.language(s), a localized heading and the MDV document
// title, then closes. IME composition, Narrator, native DPI and
// full-restart language switching are separate user-authorized manual
// gates.
#import <AppKit/AppKit.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "alloy_locale_matrix_mac_probe.h"

#include "browser/mdv/cef_mdv_entries.h"
#include "browser/new_tab/cef_new_tab_handler.h"
#include "browser/window/alloy_builtin_content.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "crayon/browser_product_strings/product_strings.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::mdv::MdvEditController;
using crayon::browser::cef_shell::mdv::MdvEntryController;
using crayon::browser::cef_shell::mdv::MdvRuntimeState;
using crayon::browser::cef_shell::window::AlloyBuiltinContent;
using crayon::browser::cef_shell::window::AlloyBuiltinContentObserver;
using crayon::browser::cef_shell::window::RegisterAlloyBuiltinContentFactories;
using crayon::browser::localization::ResolveLocaleSnapshot;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumPolls = 600;
constexpr char kViewerUrl[] = "crayon://mdv/app.html";
constexpr char kFixtureMarkdown[] = "# Locale matrix\n\nVisible body text.\n";

std::string JsString(const std::string& value) {
  std::string out = "\"";
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out += c; break;
    }
  }
  out += "\"";
  return out;
}

class LocaleProbe final : public CefApp,
                          public CefBrowserProcessHandler,
                          public CefBrowserViewDelegate,
                          public CefWindowDelegate,
                          public AlloyBuiltinContentObserver {
 public:
  LocaleProbe(std::string locale_tag,
              std::shared_ptr<AlloyLocaleMatrixMacProbeResult> result)
      : locale_tag_(std::move(locale_tag)), result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override {
    crayon::browser::cef_shell::new_tab::RegisterCrayonCustomSchemes(registrar);
  }
  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
#if defined(__APPLE__)
    // Required macOS product semantics; without it the network service
    // stalls on real Keychain access (see the 19M record).
    command->AppendSwitch("use-mock-keychain");
#endif
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    const auto snapshot = ResolveLocaleSnapshot({locale_tag_});
    const auto strings =
        crayon::browser::product_strings::BuildProductStrings(
            snapshot, crayon::browser_mdv::MdvShortcutPlatform::kMacOS);
    if (!strings) {
      Finish(false, "strings");
      return;
    }
    state_ = std::make_shared<MdvRuntimeState>();
    entries_ = std::make_shared<MdvEntryController>(state_, strings->mdv);
    editing_ = std::make_shared<MdvEditController>(state_, strings->mdv);
    client_ = new AlloyBuiltinContent(entries_, editing_, this);
    const auto model = crayon::browser_new_tab::BuildNewTabPageModel(
        crayon::browser_new_tab::NewTabProfileMode::kRegular,
        crayon::browser_new_tab::ShortcutConfig{});
    if (!RegisterAlloyBuiltinContentFactories(model, strings->new_tab,
                                              strings->mdv, state_)) {
      Finish(false, "factory-registration");
      return;
    }
    heading_ = strings->new_tab.document_title;

    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(
        client_, crayon::browser_new_tab::kNewTabUrl, settings, nullptr,
        nullptr, this);
    CefWindow::CreateTopLevelWindow(this);
    Schedule();
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings box;
    auto layout = window_->SetToBoxLayout(box);
    window_->AddChildView(view_);
    layout->SetFlexForView(view_, 1);
    window_->SetTitle("Crayon Locale Matrix Probe");
    window_->SetSize(CefSize(800, 560));
    window_->Layout();
    window_->Show();
    window_->Activate();
    auto* native_view = (__bridge NSView*)window_->GetWindowHandle();
    [native_view.window makeKeyAndOrderFront:nil];
    Schedule();
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    return finished_ && (!browser_ || browser_->GetHost()->TryCloseBrowser());
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    result_->browser_closed = true;
    browser_ = nullptr;
    if (window_) {
      window_->Close();
    }
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    client_ = nullptr;
    entries_.reset();
    editing_.reset();
    state_.reset();
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    std::cout << "alloy_locale_matrix_mac tag=" << locale_tag_
              << " newtab=" << result_->new_tab_passed
              << " browser_closed=" << result_->browser_closed
              << " window_closed=" << result_->window_closed << std::endl;
    CefQuitMessageLoop();
  }

  void OnBuiltinLoadEnd(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int http_status_code) override {
    if (frame && frame->IsMain()) {
      std::cerr << "locale_debug load_end status=" << http_status_code
                << " url=" << frame->GetURL().ToString()
                << " stage=" << stage_ << std::endl;
    }
    if (http_status_code != 200) return;
    if (stage_ == 0) {
      const std::string url = frame->GetURL().ToString();
      frame->ExecuteJavaScript(
          "(function(){"
          "var lang=document.documentElement.lang;"
          "var nav=navigator.language;"
          "var navs=(navigator.languages&&navigator.languages[0])||'';"
          "var heading=document.body.textContent.indexOf(" +
              JsString(heading_) + ")>=0;"
          "document.title='loc|'+lang+'|'+nav+'|'+navs+'|'+(heading?1:0);"
          "})();",
          url, 1);
      stage_ = 1;
      return;
    }
  }

  void OnBuiltinTitleChange(CefRefPtr<CefBrowser>,
                            const CefString& title) override {
    const std::string value = title.ToString();
    std::cerr << "locale_debug title_change stage=" << stage_ << " value="
              << value << std::endl;
    if (finished_) return;
    if (stage_ == 1 && value.rfind("loc|", 0) == 0) {
      const std::string expected = "loc|" + locale_tag_ + "|" + locale_tag_ +
                                   "|" + locale_tag_ + "|1";
      result_->new_tab_passed = value == expected;
      Finish(result_->new_tab_passed, "complete");
      return;
    }
  }

  void OnBuiltinLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                          cef_errorcode_t error_code, const CefString&,
                          const CefString& failed_url) override {
    if (frame && frame->IsMain() && error_code != ERR_ABORTED) {
      Finish(false, "load-error");
    }
  }

 private:
  void Schedule() {
    CefPostDelayedTask(
        TID_UI, base::BindOnce(&LocaleProbe::Poll, CefRefPtr<LocaleProbe>(this)),
        kPollMilliseconds);
  }

  void Finish(bool passed, const char* detail) {
    if (finished_) return;
    finished_ = true;
    std::cout << "alloy_locale_matrix_mac tag=" << locale_tag_
              << " passed=" << passed << " detail=" << detail << std::endl;
    if (browser_ && browser_->GetHost()) {
      browser_->GetHost()->CloseBrowser(true);
    } else if (window_) {
      window_->Close();
    }
  }

  void Poll() {
    if (finished_) return;
    if (++polls_ > kMaximumPolls) {
      Finish(false, "timeout");
      return;
    }
    Schedule();
  }

  std::string locale_tag_;
  std::shared_ptr<AlloyLocaleMatrixMacProbeResult> result_;
  std::shared_ptr<MdvRuntimeState> state_;
  std::shared_ptr<MdvEntryController> entries_;
  std::shared_ptr<MdvEditController> editing_;
  CefRefPtr<AlloyBuiltinContent> client_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  std::string heading_;
  std::string expected_title_;
  int stage_ = 0;
  int polls_ = 0;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(LocaleProbe);
  DISALLOW_COPY_AND_ASSIGN(LocaleProbe);
};

}  // namespace

CefRefPtr<CefApp> CreateAlloyLocaleMatrixMacProbe(
    std::string locale_tag,
    std::shared_ptr<AlloyLocaleMatrixMacProbeResult> result) {
  return new LocaleProbe(std::move(locale_tag), std::move(result));
}
