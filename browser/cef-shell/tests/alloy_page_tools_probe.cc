#include "alloy_page_tools_probe.h"

#include <windows.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "browser/window/alloy_page_tools.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::window::AlloyPageTools;
using crayon::browser_page_tools::FullscreenState;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 600;
constexpr double kExpectedTwoHundredPercentLevel = 3.8017840169239308;

class AlloyPageToolsProbe final : public CefApp,
                                  public CefBrowserProcessHandler,
                                  public CefClient,
                                  public CefFindHandler,
                                  public CefLoadHandler,
                                  public CefDisplayHandler,
                                  public CefBrowserViewDelegate,
                                  public CefWindowDelegate {
public:
  AlloyPageToolsProbe(std::string fixture_url,
                      std::shared_ptr<AlloyPageToolsProbeResult> result)
      : fixture_url_(std::move(fixture_url)), result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }

  void
  OnBeforeCommandLineProcessing(const CefString &,
                                CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-default-apps");
    command->AppendSwitch("disable-sync");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(this, fixture_url_, settings,
                                              nullptr, nullptr, this);
    if (!view_) {
      Finish(false, "create-view");
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

  void OnBrowserCreated(CefRefPtr<CefBrowserView>,
                        CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    result_->browser_closed = true;
    if (window_)
      window_->Close();
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window->SetToBoxLayout(settings);
    window->AddChildView(view_);
    layout->SetFlexForView(view_, 1);
    window->SetSize(CefSize(720, 480));
    window->Layout();
    window->Show();
    if (!browser_) {
      Finish(false, "browser-not-created");
      return;
    }
    tools_ = new AlloyPageTools(browser_, window_, "alloy-page-tools");
    const auto &capabilities = tools_->capabilities();
    result_->capability_passed =
        capabilities.find && capabilities.zoom && capabilities.fullscreen &&
        capabilities.system_print && capabilities.print_to_pdf &&
        !capabilities.picture_in_picture;
    ScheduleCheck();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return finished_; }

  void OnWindowFullscreenTransition(CefRefPtr<CefWindow>,
                                    bool is_completed) override {
    if (!tools_)
      return;
    tools_->OnWindowFullscreenTransition(is_completed);
    if (is_completed &&
        tools_->fullscreen().state() == FullscreenState::kFullscreen) {
      CefPostTask(TID_UI, base::BindOnce(&AlloyPageToolsProbe::ExitFullscreen,
                                         CefRefPtr<AlloyPageToolsProbe>(this)));
    } else if (is_completed &&
               tools_->fullscreen().state() == FullscreenState::kWindowed) {
      result_->fullscreen_passed = true;
    }
  }

  void OnFullscreenModeChange(CefRefPtr<CefBrowser>, bool fullscreen) override {
    if (tools_)
      static_cast<void>(tools_->OnContentFullscreenChange(fullscreen));
  }

  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (!frame->IsMain() || http_status_code != 200 || started_ || !tools_)
      return;
    started_ = true;
    if (!tools_->OnNavigation(1) ||
        !tools_->StartFind(1, "alloyneedle", false) ||
        !tools_->SetZoom(1, 200)) {
      Finish(false, "start-tools");
      return;
    }
    result_->zoom_passed = std::abs(browser_->GetHost()->GetZoomLevel() -
                                    kExpectedTwoHundredPercentLevel) < 0.01 &&
                           tools_->ResetZoom(1) &&
                           std::abs(browser_->GetHost()->GetZoomLevel()) < 0.01;
    if (!tools_->EnterFullscreen(1)) {
      Finish(false, "enter-fullscreen");
      return;
    }
  }

  void OnFindResult(CefRefPtr<CefBrowser> browser, int identifier, int count,
                    const CefRect &selection_rect, int active_match_ordinal,
                    bool final_update) override {
    if (!tools_)
      return;
    tools_->OnFindResult(browser, identifier, count, selection_rect,
                         active_match_ordinal, final_update);
    if (!final_update || count != 3 || find_completed_)
      return;
    find_completed_ = true;
    const bool next = tools_->FindNext(1);
    const bool previous = tools_->FindPrevious(1);
    const bool end = tools_->EndFind(1);
    result_->find_passed = next && previous && end && !tools_->find().active();
    StartPdf();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    if (tools_)
      tools_->Shutdown();
    tools_ = nullptr;
    std::error_code error;
    std::filesystem::remove(pdf_path_, error);
    std::filesystem::remove(fenced_path_, error);
    view_ = nullptr;
    window_ = nullptr;
    CefQuitMessageLoop();
  }

private:
  void ExitFullscreen() {
    if (!tools_->ExitFullscreen(1))
      Finish(false, "exit-fullscreen");
  }

  void StartPdf() {
    wchar_t temp_path[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp_path) == 0) {
      Finish(false, "temp-path");
      return;
    }
    pdf_path_ = std::filesystem::path(temp_path) /
                (L"crayon-alloy-page-tools-" +
                 std::to_wstring(GetCurrentProcessId()) + L".pdf");
    if (tools_->PrintToPdf(1, L"relative.pdf", "relative.pdf",
                           [](bool) {}) ||
        tools_->PrintToPdf(1, pdf_path_.replace_extension(L".txt").wstring(),
                           "not-pdf.txt", [](bool) {})) {
      Finish(false, "pdf-path-guard");
      return;
    }
    pdf_path_.replace_extension(L".pdf");
    if (!tools_->PrintToPdf(
            1, pdf_path_.wstring(), "alloy-page-tools.pdf", [this](bool ok) {
              result_->pdf_passed = ok && std::filesystem::exists(pdf_path_) &&
                                    std::filesystem::file_size(pdf_path_) > 0;
              tools_->AcknowledgeOutput();
              fenced_path_ = pdf_path_.parent_path() /
                             L"crayon-alloy-page-tools-fenced.pdf";
              if (!tools_->PrintToPdf(1, fenced_path_.wstring(),
                                      "alloy-page-tools-fenced.pdf",
                                      [this](bool allowed) {
                                        result_->pdf_fencing_passed =
                                            !allowed &&
                                            !tools_->OnNavigation(1);
                                      }) ||
                  !tools_->OnNavigation(2)) {
                Finish(false, "pdf-fence");
              }
            })) {
      Finish(false, "print-pdf");
    }
  }

  void ScheduleCheck() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&AlloyPageToolsProbe::Check,
                                      CefRefPtr<AlloyPageToolsProbe>(this)),
                       kPollMilliseconds);
  }

  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (result_->find_passed && result_->zoom_passed &&
        result_->fullscreen_passed && result_->pdf_passed &&
        result_->pdf_fencing_passed && result_->capability_passed) {
      Finish(true, "complete");
      return;
    }
    ScheduleCheck();
  }

  void Finish(bool passed, const char *detail) {
    if (finished_)
      return;
    finished_ = true;
    std::cout << "alloy_page_tools_windows passed=" << passed
              << " detail=" << detail << " find=" << result_->find_passed
              << " zoom=" << result_->zoom_passed
              << " fullscreen=" << result_->fullscreen_passed
              << " pdf=" << result_->pdf_passed
              << " pdf_fence=" << result_->pdf_fencing_passed
              << " capability=" << result_->capability_passed << std::endl;
    if (browser_)
      browser_->GetHost()->CloseBrowser(true);
    else if (window_)
      window_->Close();
    else
      CefQuitMessageLoop();
  }

  const std::string fixture_url_;
  std::shared_ptr<AlloyPageToolsProbeResult> result_;
  std::filesystem::path pdf_path_;
  std::filesystem::path fenced_path_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<AlloyPageTools> tools_;
  int checks_ = 0;
  bool started_ = false;
  bool find_completed_ = false;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(AlloyPageToolsProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateAlloyPageToolsProbe(std::string fixture_url,
                          std::shared_ptr<AlloyPageToolsProbeResult> result) {
  return new AlloyPageToolsProbe(std::move(fixture_url), std::move(result));
}
