#include "alloy_page_markdown_probe.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/new_tab/cef_new_tab_handler.h"
#include "browser/window/alloy_builtin_content.h"
#include "browser/window/alloy_page_markdown.h"
#include "browser/window/alloy_tab_controller.h"
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
#include "windows/content_host_adapter_win.h"
#include "windows/content_host_process_win.h"

#ifndef CRAYON_CONTENT_HOST_TEST_PATH
#error "CRAYON_CONTENT_HOST_TEST_PATH must be defined"
#endif

namespace {

using crayon::browser::cef_shell::mdv::MdvEditController;
using crayon::browser::cef_shell::mdv::MdvEntryController;
using crayon::browser::cef_shell::mdv::MdvRuntimeState;
using crayon::browser::cef_shell::window::AlloyBuiltinContent;
using crayon::browser::cef_shell::window::AlloyBuiltinContentObserver;
using crayon::browser::cef_shell::window::AlloyPageMarkdown;
using crayon::browser::cef_shell::window::AlloyTabController;
using crayon::browser::cef_shell::window::RegisterAlloyBuiltinContentFactories;
using crayon::browser::cef_shell::windows::ContentHostAdapter;
using crayon::browser::cef_shell::windows::ContentHostProcess;
using namespace crayon::browser_engine;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumPolls = 600;
constexpr int kPreviewCommandId = MENU_ID_USER_FIRST + 1;
constexpr int kCopyCommandId = MENU_ID_USER_FIRST + 2;

template <typename T>
T Required(std::optional<T> value) {
  if (!value) std::abort();
  return std::move(*value);
}

std::string SiblingUrl(const std::string& url, const char* filename) {
  const std::size_t slash = url.rfind('/');
  return slash == std::string::npos ? std::string{}
                                    : url.substr(0, slash + 1) + filename;
}

class Probe final : public CefApp,
                    public CefBrowserProcessHandler,
                    public CefBrowserViewDelegate,
                    public CefWindowDelegate,
                    public AlloyBuiltinContentObserver {
 public:
  Probe(std::string fixture_url,
        std::shared_ptr<AlloyPageMarkdownProbeResult> result)
      : fixture_url_(std::move(fixture_url)),
        recovery_url_(SiblingUrl(fixture_url_, "recovery.html")),
        result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override {
    crayon::browser::cef_shell::new_tab::RegisterCrayonCustomSchemes(registrar);
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
    const auto product = crayon::browser::product_strings::BuildProductStrings(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        crayon::browser_mdv::MdvShortcutPlatform::kWindows);
    if (!product) {
      Finish(false, "strings");
      return;
    }
    state_ = std::make_shared<MdvRuntimeState>();
    entries_ = std::make_shared<MdvEntryController>(state_, product->mdv);
    editing_ = std::make_shared<MdvEditController>(state_, product->mdv);
    if (!RegisterAlloyBuiltinContentFactories(
            crayon::browser_new_tab::BuildNewTabPageModel(
                crayon::browser_new_tab::NewTabProfileMode::kRegular,
                crayon::browser_new_tab::ShortcutConfig{}),
            product->new_tab, product->mdv, state_)) {
      Finish(false, "factory-registration");
      return;
    }
    tabs_ = std::make_unique<AlloyTabController>(
        Required(ProfileId::TryCreate("alloy-page-markdown")), 1);
    page_markdown_ = std::make_unique<AlloyPageMarkdown>(
        tabs_.get(), editing_,
        crayon::browser::cef_shell::page_markdown::PageMarkdownStrings{
            product->page_markdown.preview_command,
            product->page_markdown.copy_command,
            product->page_markdown.save_as_command,
            product->page_markdown.copied_status,
            product->page_markdown.copy_failed_status,
            product->page_markdown.save_cancelled_status},
        [this](const std::string& markdown) {
          copied_markdown_ = markdown;
          return true;
        },
        this);
    client_ = new AlloyBuiltinContent(entries_, editing_, page_markdown_.get());
    content_host_ = std::make_unique<ContentHostAdapter>(
        std::make_unique<ContentHostProcess>());
    page_markdown_->SetSnapshotObserver(content_host_.get());
    page_markdown_->SetSnapshotAdmission(
        [this] { return content_host_ && content_host_->healthy(); });
    page_markdown_->SetEventsReadyCallback([this] { ConsumeSnapshotEvents(); });
    if (!content_host_->Start(CRAYON_CONTENT_HOST_TEST_PATH)) {
      Finish(false, "content-host-start");
      return;
    }
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(client_, fixture_url_, settings,
                                              nullptr, nullptr, this);
    tab_id_ = tabs_->BeginCreate(view_, ContentPurpose::kWeb,
                                 NavigationId::FromRaw(1));
    if (!view_ || !tab_id_) {
      Finish(false, "begin-create");
      return;
    }
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
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    window_->AddChildView(tabs_->container());
    layout->SetFlexForView(tabs_->container(), 1);
    window_->SetSize(CefSize(960, 720));
    window_->Layout();
    window_->Show();
    window_->Activate();
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView> view,
                        CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
    const auto capabilities = Required(ContentCapabilitySet::TryCreate(
        (1U << static_cast<unsigned>(ContentCapability::kNavigate)) |
        (1U << static_cast<unsigned>(ContentCapability::kSnapshot))));
    if (!tabs_->OnBrowserCreated(view, browser, capabilities) ||
        !tabs_->Activate(*tab_id_)) {
      Finish(false, "browser-created");
    }
  }

  void OnBuiltinLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                        int http_status_code) override {
    if (frame && frame->IsMain() && http_status_code == 200) loaded_url_ = frame->GetURL();
  }

  void OnBuiltinLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                          cef_errorcode_t error_code, const CefString&,
                          const CefString& failed_url) override {
    if (frame && frame->IsMain() && error_code != ERR_ABORTED) {
      std::cerr << "alloy_page_markdown_load_error=" << error_code
                << " url=" << failed_url.ToString() << std::endl;
      Finish(false, "load-error");
    }
  }

  bool OnBuiltinConsoleMessage(CefRefPtr<CefBrowser>,
                               cef_log_severity_t level,
                               const CefString& message,
                               const CefString& source, int line) override {
    if (level >= LOGSEVERITY_ERROR) {
      std::cerr << "alloy_page_markdown_console_error=" << message.ToString()
                << " source=" << source.ToString() << " line=" << line
                << std::endl;
      Finish(false, "console-error");
    }
    return true;
  }

  void OnBuiltinTitleChange(CefRefPtr<CefBrowser>,
                            const CefString& title) override {
    if (title == "alloy-page-markdown-pass") {
      result_->preview_passed = true;
    } else if (title == "alloy-page-markdown-fail") {
      Finish(false, "preview-dom");
    }
  }

  void OnBuiltinBrowserClosing(CefRefPtr<CefBrowser> browser) override {
    result_->lifecycle_passed = tabs_ && tabs_->OnBeforeClose(browser);
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    return !browser_ || browser_->GetHost()->TryCloseBrowser();
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    view_ = nullptr;
    if (window_) window_->Close();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    window_ = nullptr;
    if (page_markdown_) page_markdown_->Shutdown();
    if (content_host_) content_host_->Stop();
    if (client_) client_->Shutdown();
    client_ = nullptr;
    page_markdown_.reset();
    content_host_.reset();
    entries_.reset();
    editing_.reset();
    state_.reset();
    result_->window_closed = tabs_ && tabs_->pending_count() == 0 &&
                             tabs_->ReleaseAfterClosed();
    tabs_.reset();
    std::cout << "alloy_page_markdown_windows context="
              << result_->context_menu_passed
              << " cancellation=" << result_->cancellation_passed
              << " preview=" << result_->preview_passed
              << " export=" << result_->export_passed
              << " lifecycle=" << result_->lifecycle_passed << std::endl;
    CefQuitMessageLoop();
  }

 private:
  void Schedule() {
    CefPostDelayedTask(
        TID_UI,
        CefCreateClosureTask(
            base::BindOnce(&Probe::Tick, CefRefPtr<Probe>(this))),
        kPollMilliseconds);
  }

  bool ReadyAt(const std::string& url) const {
    if (!browser_ || loaded_url_ != url) return false;
    const auto* tab = tabs_->model().FindByBrowser(browser_->GetIdentifier());
    return tab && tab->url == url && !tab->loading &&
           tab->lifecycle ==
               crayon::browser::cef_shell::window::TabLifecycle::kReady;
  }

  void ConsumeSnapshotEvents() {
    if (page_markdown_ && content_host_) {
      content_host_->Consume(page_markdown_->DrainSnapshots(64));
    }
  }

  void Tick() {
    if (finished_) return;
    if (++polls_ > kMaximumPolls) {
      const auto* tab = browser_ && tabs_
                            ? tabs_->model().FindByBrowser(
                                  browser_->GetIdentifier())
                            : nullptr;
      std::cout << "alloy_page_markdown_timeout stage=" << stage_
                << " loaded=" << loaded_url_
                << " browser_url="
                << (browser_ && browser_->GetMainFrame()
                        ? browser_->GetMainFrame()->GetURL().ToString()
                        : std::string{})
                << " tab_url=" << (tab ? tab->url : std::string{})
                << " loading=" << (tab && tab->loading)
                << " lifecycle="
                << (tab ? static_cast<int>(tab->lifecycle) : -1)
                << " view_drawn=" << (view_ && view_->IsDrawn())
                << " view_bounds="
                << (view_ ? std::to_string(view_->GetBounds().width) + "x" +
                                std::to_string(view_->GetBounds().height)
                          : std::string{})
                << std::endl;
      Finish(false, "timeout");
      return;
    }
    ConsumeSnapshotEvents();
    content_host_->Tick();
    page_markdown_->Tick(content_host_->Drain(64), content_host_->healthy());

    if (stage_ == 0 && ReadyAt(fixture_url_) && content_host_->healthy()) {
      const bool first = client_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), nullptr, kPreviewCommandId,
          EVENTFLAG_NONE);
      const bool duplicate = client_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), nullptr, kPreviewCommandId,
          EVENTFLAG_NONE);
      result_->context_menu_passed = first && duplicate;
      browser_->GetMainFrame()->LoadURL(recovery_url_);
      stage_ = 2;
    } else if (stage_ == 2 && ReadyAt(recovery_url_)) {
      result_->cancellation_passed =
          browser_->GetMainFrame()->GetURL().ToString() == recovery_url_;
      browser_->GetMainFrame()->LoadURL(fixture_url_);
      stage_ = 3;
    } else if (stage_ == 3 && ReadyAt(fixture_url_)) {
      static_cast<void>(client_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), nullptr, kPreviewCommandId,
          EVENTFLAG_NONE));
      stage_ = 5;
    } else if (stage_ == 5 && ReadyAt("crayon://mdv/app.html")) {
      browser_->GetMainFrame()->ExecuteJavaScript(
          R"JS((function(){var p=0,t=setInterval(function(){var b=document.body,s=document.getElementById('md-source'),v=document.getElementById('md-preview'),x=document.querySelectorAll('.view-switch');if(s&&v&&x.length===3){clearInterval(t);x[0].click();x[2].click();var ok=b.getAttribute('data-view')==='split'&&s.value.indexOf('Visible fixture heading')>=0&&s.value.indexOf('hidden fixture secret')<0&&s.value.indexOf('| Name | Value |')>=0;s.value+='\n\nalloy-page-markdown-edited';s.dispatchEvent(new Event('input',{bubbles:true}));document.title=ok?'alloy-page-markdown-pass':'alloy-page-markdown-fail';}else if(++p>500){clearInterval(t);document.title='alloy-page-markdown-fail';}},20);})();)JS",
          "crayon://mdv/app.html", 1);
      stage_ = 6;
    } else if (stage_ == 6 && result_->preview_passed) {
      const bool copied = client_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), nullptr, kCopyCommandId,
          EVENTFLAG_NONE);
      result_->export_passed =
          copied && copied_markdown_.find("alloy-page-markdown-edited") !=
                        std::string::npos;
      Finish(result_->context_menu_passed && result_->cancellation_passed &&
                 result_->preview_passed && result_->export_passed,
             "complete");
      return;
    }
    Schedule();
  }

  void Finish(bool passed, const char* detail) {
    if (finished_) return;
    finished_ = true;
    if (!passed) result_->preview_passed = false;
    std::cout << "alloy_page_markdown_windows passed=" << passed
              << " detail=" << detail << std::endl;
    if (window_) {
      window_->Close();
    } else {
      if (page_markdown_) page_markdown_->Shutdown();
      if (content_host_) content_host_->Stop();
      CefQuitMessageLoop();
    }
  }

  const std::string fixture_url_;
  const std::string recovery_url_;
  std::shared_ptr<AlloyPageMarkdownProbeResult> result_;
  std::shared_ptr<MdvRuntimeState> state_;
  std::shared_ptr<MdvEntryController> entries_;
  std::shared_ptr<MdvEditController> editing_;
  std::unique_ptr<AlloyTabController> tabs_;
  std::unique_ptr<AlloyPageMarkdown> page_markdown_;
  std::unique_ptr<ContentHostAdapter> content_host_;
  CefRefPtr<AlloyBuiltinContent> client_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  std::optional<crayon::browser::cef_shell::window::TabId> tab_id_;
  std::string loaded_url_;
  std::string copied_markdown_;
  int stage_ = 0;
  int polls_ = 0;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(Probe);
  DISALLOW_COPY_AND_ASSIGN(Probe);
};

}  // namespace

CefRefPtr<CefApp> CreateAlloyPageMarkdownProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyPageMarkdownProbeResult> result) {
  return new Probe(std::move(fixture_url), std::move(result));
}
