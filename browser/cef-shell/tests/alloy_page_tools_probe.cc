#include "alloy_page_tools_probe.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
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
constexpr int kMaximumDirectoryCandidates = 32;
constexpr double kExpectedTwoHundredPercentLevel = 3.8017840169239308;

std::filesystem::path PathFromUtf8(const std::string& value) {
#if defined(_WIN32)
  return std::filesystem::path(CefString(value).ToWString());
#else
  return std::filesystem::path(value);
#endif
}

std::wstring PathForCef(const std::filesystem::path& path) {
#if defined(_WIN32)
  return path.native();
#else
  return CefString(path.native()).ToWString();
#endif
}

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
#if defined(__APPLE__)
    command->AppendSwitch("use-mock-keychain");
#endif
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
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    if (tools_)
      tools_->Shutdown();
    tools_ = nullptr;
    if (!test_directory_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(test_directory_, error);
    }
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
    if (!CreateTestDirectory()) {
      Finish(false, "pdf-directory");
      return;
    }
    pdf_path_ = test_directory_ / PathFromUtf8("页面输出.pdf");
    const std::filesystem::path text_path =
        test_directory_ / PathFromUtf8("页面输出.txt");
    std::string nul_filename = "页面输出";
    nul_filename.push_back('\0');
    nul_filename += ".pdf";
    const std::filesystem::path nul_path =
        test_directory_ / PathFromUtf8(nul_filename);
    if (tools_->PrintToPdf(1, CefString("relative.pdf").ToWString(),
                           "relative.pdf", [](bool) {})) {
      Finish(false, "pdf-relative-accepted");
      return;
    }
    if (tools_->PrintToPdf(1, PathForCef(text_path), "not-pdf.txt",
                           [](bool) {})) {
      Finish(false, "pdf-text-accepted");
      return;
    }
    if (tools_->PrintToPdf(1, PathForCef(nul_path), "nul.pdf", [](bool) {})) {
      Finish(false, "pdf-nul-accepted");
      return;
    }
    if (!tools_->PrintToPdf(
            1, PathForCef(pdf_path_), "alloy-page-tools.pdf", [this](bool ok) {
              std::error_code error;
              const bool exists = std::filesystem::exists(pdf_path_, error);
              const std::uintmax_t size =
                  exists && !error
                      ? std::filesystem::file_size(pdf_path_, error)
                      : 0;
              char header[5]{};
              std::ifstream pdf(pdf_path_, std::ios::binary);
              pdf.read(header, sizeof(header));
              result_->pdf_passed =
                  ok && !error && exists && size > 0 &&
                  pdf.gcount() ==
                      static_cast<std::streamsize>(sizeof(header)) &&
                  std::string(header, sizeof(header)) == "%PDF-";
              if (!result_->pdf_passed) {
                Finish(false, "pdf-output");
                return;
              }
              tools_->AcknowledgeOutput();
              fenced_path_ =
                  test_directory_ / PathFromUtf8("页面输出-fenced.pdf");
              if (!tools_->PrintToPdf(
                      1, PathForCef(fenced_path_),
                      "alloy-page-tools-fenced.pdf", [this](bool allowed) {
                        result_->pdf_fencing_passed =
                            !allowed && !tools_->OnNavigation(1);
                      }) ||
                  !tools_->OnNavigation(2)) {
                Finish(false, "pdf-fence");
              }
            })) {
      Finish(false, "print-pdf");
    }
  }

  bool CreateTestDirectory() {
    if (!test_directory_.empty())
      return true;
    std::error_code error;
    const std::filesystem::path temporary_root =
        std::filesystem::temp_directory_path(error);
    if (error)
      return false;
#if defined(_WIN32)
    const std::string process_id = std::to_string(GetCurrentProcessId());
#else
    const std::string process_id = std::to_string(getpid());
#endif
    for (int candidate = 0; candidate < kMaximumDirectoryCandidates;
         ++candidate) {
      const std::filesystem::path directory =
          temporary_root / ("crayon-alloy-page-tools-" + process_id + "-" +
                            std::to_string(candidate));
      error.clear();
      if (!std::filesystem::create_directory(directory, error)) {
        if (error && error != std::errc::file_exists)
          return false;
        continue;
      }
      test_directory_ = std::filesystem::canonical(directory, error);
      if (!error)
        return true;
      std::error_code cleanup_error;
      std::filesystem::remove_all(directory, cleanup_error);
      test_directory_.clear();
      return false;
    }
    return false;
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
    if (!pdf_started_ && result_->find_passed && result_->zoom_passed &&
        result_->fullscreen_passed) {
      pdf_started_ = true;
      StartPdf();
      if (finished_)
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
    std::cout << "alloy_page_tools passed=" << passed
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
  std::filesystem::path test_directory_;
  std::filesystem::path pdf_path_;
  std::filesystem::path fenced_path_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<AlloyPageTools> tools_;
  int checks_ = 0;
  bool started_ = false;
  bool find_completed_ = false;
  bool pdf_started_ = false;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(AlloyPageToolsProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateAlloyPageToolsProbe(std::string fixture_url,
                          std::shared_ptr<AlloyPageToolsProbeResult> result) {
  return new AlloyPageToolsProbe(std::move(fixture_url), std::move(result));
}
