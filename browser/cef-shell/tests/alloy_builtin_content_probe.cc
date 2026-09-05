#include "alloy_builtin_content_probe.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <windows.h>

#include "browser/mdv/cef_mdv_editing.h"
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

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 720;
constexpr int kPollMilliseconds = 25;
constexpr int kMaximumPolls = 600;
constexpr char kViewerUrl[] = "crayon://mdv/app.html";

bool WriteUtf8(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  return output.good();
}

std::string ReadUtf8(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

class BuiltinProbe final : public CefApp,
                           public CefBrowserProcessHandler,
                           public CefBrowserViewDelegate,
                           public CefWindowDelegate,
                           public AlloyBuiltinContentObserver {
 public:
  explicit BuiltinProbe(std::shared_ptr<AlloyBuiltinContentProbeResult> result)
      : result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override {
    crayon::browser::cef_shell::new_tab::RegisterCrayonCustomSchemes(
        registrar);
  }

  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-default-apps");
    command->AppendSwitch("disable-sync");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    const auto strings = crayon::browser::product_strings::BuildProductStrings(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        crayon::browser_mdv::MdvShortcutPlatform::kWindows);
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
    markdown_path_ = std::filesystem::temp_directory_path() /
                     (L"crayon-alloy-builtins-" +
                      std::to_wstring(GetCurrentProcessId()) + L".md");
    const std::string markdown =
        "# Alloy 内置页\n\n```cpp\nint main() { return 0; }\n```\n\n"
        "$x^2$\n\n```mermaid\ngraph TD; A-->B;\n```\n\n"
        "<script>window.__alloyPwned=1</script>\n";
    if (!WriteUtf8(markdown_path_, markdown)) {
      Finish(false, "fixture-write");
      return;
    }
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(
        client_, crayon::browser_new_tab::kNewTabUrl, settings, nullptr,
        nullptr, this);
    if (!view_) {
      Finish(false, "browser-view");
      return;
    }
    CefWindow::CreateTopLevelWindow(this);
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    window_->AddChildView(view_);
    layout->SetFlexForView(view_, 1);
    window_->SetSize(CefSize(kWindowWidth, kWindowHeight));
    window_->Layout();
    window_->Show();
  }

  void OnBuiltinBrowserCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
    if (!browser_ || browser_->GetHost()->GetRuntimeStyle() !=
                         CEF_RUNTIME_STYLE_ALLOY) {
      Finish(false, "runtime-style");
    }
  }

  void OnBuiltinLoadEnd(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int http_status_code) override {
    if (!frame || !frame->IsMain() || http_status_code != 200) {
      return;
    }
    const std::string url = frame->GetURL().ToString();
    if (stage_ == 0 && url == crayon::browser_new_tab::kNewTabUrl) {
      frame->ExecuteJavaScript(
          "document.title=(document.documentElement.lang==='zh-CN'&&"
          "document.documentElement.getAttribute('data-profile-mode')==='regular'"
          "&&!window.__alloyPwned)"
          "?'alloy-newtab-pass':'alloy-newtab-fail';",
          url, 1);
    } else if (stage_ == 1 && url == kViewerUrl) {
      frame->ExecuteJavaScript(
          R"JS((function() {
            var polls = 0;
            var timer = setInterval(function() {
              polls++;
              var body = document.body;
              var preview = document.getElementById('md-preview');
              var source = document.getElementById('md-source');
              var highlighted = preview && preview.querySelector(
                  'code[data-mdv-highlighted=true].hljs');
              var math = preview && preview.querySelector(
                  '[data-mdv-math-rendered=true]');
              var mermaid = preview && preview.querySelector(
                  'code[data-mdv-mermaid-rendered=true]');
              if (body && source && highlighted && math && mermaid) {
                clearInterval(timer);
                var buttons = document.querySelectorAll('.view-switch');
                buttons[0].click();
                buttons[2].click();
                var modes = body.getAttribute('data-view') === 'split';
                buttons[1].click();
                var css = '';
                for (var i = 0; i < document.styleSheets.length; i++) {
                  try {
                    var rules = document.styleSheets[i].cssRules;
                    for (var j = 0; j < rules.length; j++) {
                      css += rules[j].cssText;
                    }
                  } catch (ignored) {}
                }
                var safe = !window.__alloyPwned &&
                    preview.textContent.indexOf('window.__alloyPwned=1') >= 0;
                var themes = css.indexOf('prefers-color-scheme: dark') >= 0;
                if (!modes || !safe || !themes) {
                  document.title = 'alloy-mdv-fail';
                  return;
                }
                source.value += '\n\nalloy-edited';
                source.dispatchEvent(new Event('input', {bubbles: true}));
                var dirtyTimer = setInterval(function() {
                  if (body.getAttribute('data-dirty') === 'true') {
                    clearInterval(dirtyTimer);
                    document.title = 'alloy-mdv-ready';
                  }
                }, 20);
              } else if (polls > 1400) {
                clearInterval(timer);
                document.title = 'alloy-runtime-timeout-' +
                    Number(!!highlighted) + Number(!!math) +
                    Number(!!mermaid) + '-' + Number(!!(preview &&
                      preview.querySelector('[data-mdv-mermaid-error=true]')));
              }
            }, 25);
          })();)JS",
          url, 1);
    }
  }

  void OnBuiltinLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                          cef_errorcode_t error_code, const CefString&,
                          const CefString& failed_url) override {
    if (frame && frame->IsMain() && error_code != ERR_ABORTED) {
      std::cerr << "alloy_builtin_load_error=" << error_code
                << " url=" << failed_url.ToString() << std::endl;
      Finish(false, "load-error");
    }
  }

  bool OnBuiltinConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t level,
                               const CefString& message, const CefString& source,
                               int line) override {
    if (level >= LOGSEVERITY_ERROR) {
      std::cerr << "alloy_builtin_console_error=" << message.ToString()
                << " source=" << source.ToString() << " line=" << line
                << std::endl;
      Finish(false, "console-error");
    }
    return true;
  }

  void OnBuiltinTitleChange(CefRefPtr<CefBrowser> browser,
                            const CefString& title_value) override {
    const std::string title = title_value.ToString();
    if (title == "alloy-newtab-fail" || title == "alloy-mdv-fail" ||
        title.rfind("alloy-runtime-timeout", 0) == 0) {
      Finish(false, title.c_str());
      return;
    }
    if (stage_ == 0 && title == "alloy-newtab-pass") {
      result_->new_tab_passed = true;
      stage_ = 1;
      entries_->LoadAndShow(browser, markdown_path_.string(),
                            crayon::browser_mdv::EntrySource::kUserCommand);
      return;
    }
    if (stage_ == 1 && title == "alloy-mdv-ready") {
      result_->mdv_runtime_passed = true;
      if (!editing_->SaveWriteBack(browser) ||
          ReadUtf8(markdown_path_).find("alloy-edited") == std::string::npos) {
        Finish(false, "write-back");
        return;
      }
      result_->edit_save_passed = true;
      if (!WriteUtf8(markdown_path_, "# external modification\n")) {
        Finish(false, "external-write");
        return;
      }
      stage_ = 2;
      browser->GetMainFrame()->ExecuteJavaScript(
          "setTimeout(function(){var s=document.getElementById('md-source');"
          "s.value+=' conflict';s.dispatchEvent(new Event('input',{bubbles:"
          "true}));},150);",
          kViewerUrl, 1);
      ScheduleEditCheck();
      return;
    }
    if (stage_ == 3 && title == "alloy-conflict-pass") {
      const auto snapshot = state_->snapshot();
      result_->conflict_passed = snapshot.dirty && snapshot.confirm_visible &&
                                 !snapshot.save_ok;
      Finish(result_->conflict_passed, "complete");
    }
  }

  void OnBuiltinBrowserClosing(CefRefPtr<CefBrowser>) override {
    result_->browser_closed = true;
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    return !browser_ || browser_->GetHost()->TryCloseBrowser();
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    view_ = nullptr;
    if (window_) {
      window_->Close();
    }
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    window_ = nullptr;
    if (client_) {
      client_->Shutdown();
    }
    client_ = nullptr;
    entries_.reset();
    editing_.reset();
    state_.reset();
    std::error_code ignored;
    std::filesystem::remove(markdown_path_, ignored);
    std::cout << "alloy_builtin_content_windows newtab="
              << result_->new_tab_passed
              << " runtime=" << result_->mdv_runtime_passed
              << " save=" << result_->edit_save_passed
              << " conflict=" << result_->conflict_passed
              << " browser_closed=" << result_->browser_closed << std::endl;
    CefQuitMessageLoop();
  }

 private:
  void ScheduleEditCheck() {
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&BuiltinProbe::CheckSecondEdit,
                       CefRefPtr<BuiltinProbe>(this)),
        kPollMilliseconds);
  }

  void CheckSecondEdit() {
    if (finished_ || stage_ != 2 || !browser_) {
      return;
    }
    const auto current = editing_->CurrentMarkdown(browser_);
    if (!current || current->find("alloy-edited conflict") ==
                        std::string::npos) {
      if (++edit_polls_ > kMaximumPolls) {
        Finish(false, "second-edit-timeout");
      } else {
        ScheduleEditCheck();
      }
      return;
    }
    if (!editing_->SaveWriteBack(browser_)) {
      Finish(false, "conflict-save-command");
      return;
    }
    stage_ = 3;
    browser_->GetMainFrame()->ExecuteJavaScript(
        "var q=setInterval(function(){var c=document.getElementById('md-"
        "confirm');if(c&&c.getAttribute('data-show')==='true'&&document.body."
        "getAttribute('data-dirty')==='true'){clearInterval(q);document.title="
        "'alloy-conflict-pass';}},20);",
        kViewerUrl, 1);
  }

  void Finish(bool passed, const char* detail) {
    if (finished_) {
      return;
    }
    finished_ = true;
    std::cout << "alloy_builtin_content_windows detail=" << detail
              << " passed=" << passed << std::endl;
    if (!passed) {
      result_->conflict_passed = false;
    }
    if (window_) {
      window_->Close();
    } else {
      std::error_code ignored;
      std::filesystem::remove(markdown_path_, ignored);
      CefQuitMessageLoop();
    }
  }

  std::shared_ptr<AlloyBuiltinContentProbeResult> result_;
  std::shared_ptr<MdvRuntimeState> state_;
  std::shared_ptr<MdvEntryController> entries_;
  std::shared_ptr<MdvEditController> editing_;
  CefRefPtr<AlloyBuiltinContent> client_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  std::filesystem::path markdown_path_;
  int stage_ = 0;
  int edit_polls_ = 0;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(BuiltinProbe);
  DISALLOW_COPY_AND_ASSIGN(BuiltinProbe);
};

}  // namespace

CefRefPtr<CefApp> CreateAlloyBuiltinContentProbe(
    std::shared_ptr<AlloyBuiltinContentProbeResult> result) {
  return new BuiltinProbe(std::move(result));
}
