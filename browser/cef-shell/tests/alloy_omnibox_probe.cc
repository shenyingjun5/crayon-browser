#include "alloy_omnibox_probe.h"

#include <windows.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "browser/window/alloy_omnibox.h"
#include "crayon/browser_privacy/privacy_defaults.h"
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_display.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::window::AlloyOmnibox;
using crayon::browser::cef_shell::window::OmniboxSubmission;
using crayon::browser::cef_shell::window::OmniboxSubmissionKind;
using crayon::browser_omnibox::OmniboxSuggestion;
using crayon::browser_omnibox::SuggestionSource;
using crayon::browser_omnibox_provider::SearchProvider;
using crayon::browser_omnibox_provider::SearchProviderSet;
using crayon::browser_privacy::DefaultPrivacyDefaults;

constexpr int kPollMilliseconds = 25;
constexpr int kMaximumChecks = 320;

class AlloyOmniboxProbe final : public CefApp,
                                public CefBrowserProcessHandler,
                                public CefWindowDelegate {
public:
  explicit AlloyOmniboxProbe(std::shared_ptr<AlloyOmniboxProbeResult> result)
      : result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
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
    SearchProviderSet providers;
    if (!providers.Add(SearchProvider{
            "Fixture", "https://search.test/?q={searchTerms}"})) {
      Finish(false, "provider");
      return;
    }
    omnibox_ = std::make_unique<AlloyOmnibox>(
        AlloyOmnibox::Strings{"Search or enter address", "Address"},
        AlloyOmnibox::Callbacks{
            [this](std::uint64_t generation, const std::string &text) {
              requests_.push_back({generation, text});
            },
            [this](const OmniboxSubmission &submission) {
              submissions_.push_back(submission);
            },
            [this]() { ++cancel_events_; }},
        DefaultPrivacyDefaults(), std::move(providers));
    CefWindow::CreateTopLevelWindow(this);
  }

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    mounted_panel_ = omnibox_->panel();
    window_->AddChildView(mounted_panel_);
    layout->SetFlexForView(mounted_panel_, 1);
    window_->SetTitle("Crayon Alloy Omnibox Probe");
    window_->SetSize(CefSize(900, 360));
    window_->Layout();
    window_->Show();
    window_->Activate();
    ScheduleCheck();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return finished_; }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    result_->behavior_passed = passed_ && result_->real_input_passed &&
                               result_->generation_passed &&
                               result_->display_safety_passed;
    std::cout << "alloy_omnibox_windows passed=" << result_->behavior_passed
              << " real_input=" << result_->real_input_passed
              << " generation=" << result_->generation_passed
              << " display_safety=" << result_->display_safety_passed
              << std::endl;
    mounted_panel_ = nullptr;
    window_ = nullptr;
    omnibox_.reset();
    CefQuitMessageLoop();
  }

private:
  struct Request final {
    std::uint64_t generation;
    std::string text;
  };

  bool Foreground() {
    if (!window_)
      return false;
    window_->Activate();
    const HWND handle = window_->GetWindowHandle();
    return handle && SetForegroundWindow(handle);
  }

  bool SendKey(WORD key) {
    if (!Foreground())
      return false;
    INPUT input[2]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = key;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = key;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, input, sizeof(INPUT)) == 2;
  }

  bool SendUnicodeText(const std::wstring &text) {
    if (!Foreground())
      return false;
    std::vector<INPUT> input;
    input.reserve(text.size() * 2);
    for (wchar_t character : text) {
      INPUT down{};
      down.type = INPUT_KEYBOARD;
      down.ki.wScan = character;
      down.ki.dwFlags = KEYEVENTF_UNICODE;
      input.push_back(down);
      INPUT up = down;
      up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      input.push_back(up);
    }
    return SendInput(static_cast<UINT>(input.size()), input.data(),
                     sizeof(INPUT)) == input.size();
  }

  bool ClickFirstSuggestion() {
    if (!Foreground() || !omnibox_->panel() ||
        omnibox_->panel()->GetChildViewCount() < 2) {
      return false;
    }
    auto suggestion_panel = omnibox_->panel()->GetChildViewAt(1)->AsPanel();
    if (!suggestion_panel || suggestion_panel->GetChildViewCount() == 0) {
      return false;
    }
    const auto button = suggestion_panel->GetChildViewAt(0);
    const CefRect bounds = button->GetBoundsInScreen();
    if (!button->IsDrawn() || bounds.width <= 0 || bounds.height <= 0) {
      return false;
    }
    const CefPoint point = CefDisplay::ConvertScreenPointToPixels(
        CefPoint(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2));
    if (!SetCursorPos(point.x, point.y))
      return false;
    INPUT input[2]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(2, input, sizeof(INPUT)) == 2;
  }

  std::vector<OmniboxSuggestion> Suggestions(std::size_t count) {
    std::vector<OmniboxSuggestion> result;
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(OmniboxSuggestion{"Suggestion " + std::to_string(index),
                                         "https://suggestion" +
                                             std::to_string(index) + ".test/",
                                         SuggestionSource::kHistory});
    }
    return result;
  }

  void ScheduleCheck() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&AlloyOmniboxProbe::Check,
                                      CefRefPtr<AlloyOmniboxProbe>(this)),
                       kPollMilliseconds);
  }

  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (logged_stage_ != stage_) {
      logged_stage_ = stage_;
      std::cout << "alloy_omnibox_windows stage=" << stage_ << std::endl;
    }
    if (stage_ == 0) {
      const auto punycode = AlloyOmnibox::SafeDisplayText(
          "https://user:pass@xn--fsqu00a.test/path");
      const auto unicode =
          AlloyOmnibox::SafeDisplayText("https://例子.test/path");
      result_->display_safety_passed =
          punycode && *punycode == "https://xn--fsqu00a.test/path" && unicode &&
          unicode->find("例子") == std::string::npos &&
          unicode->find('%') != std::string::npos &&
          !AlloyOmnibox::SafeDisplayText("https://x.test/a\nInjected") &&
          !AlloyOmnibox::SafeDisplayText(std::string(2049, 'a'));
      if (!result_->display_safety_passed ||
          window_->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY ||
          !omnibox_->SetAddress("https://user:pass@xn--fsqu00a.test/path") ||
          omnibox_->displayed_text() != "https://xn--fsqu00a.test/path" ||
          !omnibox_->Edit("") || !omnibox_->Focus() ||
          !SendUnicodeText(L"example.test")) {
        Finish(false, "display-or-input");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 1) {
      if (requests_.empty() || requests_.back().text != "example.test") {
        ScheduleCheck();
        return;
      }
      const auto generation = requests_.back().generation;
      if (generation <= 1 ||
          omnibox_->ApplySuggestions(generation - 1, Suggestions(1)) ||
          !omnibox_->ApplySuggestions(generation, Suggestions(10)) ||
          omnibox_->ApplySuggestions(generation, Suggestions(1)) ||
          omnibox_->suggestion_count() != 8 || !SendKey(VK_DOWN)) {
        Finish(false, "generation-or-down");
        return;
      }
      result_->generation_passed = true;
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 2) {
      if (omnibox_->selected_suggestion() != 0 || !SendKey(VK_RETURN)) {
        Finish(false, "selection-or-enter");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 3) {
      if (submissions_.empty()) {
        ScheduleCheck();
        return;
      }
      if (submissions_.back().kind != OmniboxSubmissionKind::kNavigateUrl ||
          submissions_.back().value != "https://suggestion0.test/" ||
          !omnibox_->OnNavigationFinished(true, submissions_.back().value) ||
          !omnibox_->Edit("click") ||
          !omnibox_->ApplySuggestions(omnibox_->edit_generation(),
                                      Suggestions(1))) {
        Finish(false, "keyboard-submit");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 4) {
      window_->Layout();
      if (!ClickFirstSuggestion()) {
        Finish(false, "suggestion-click");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 5) {
      if (submissions_.size() < 2) {
        ScheduleCheck();
        return;
      }
      if (submissions_.back().kind != OmniboxSubmissionKind::kNavigateUrl ||
          !omnibox_->OnNavigationFinished(true, submissions_.back().value) ||
          !omnibox_->Edit("蜡笔 browser&more") || !omnibox_->Submit() ||
          submissions_.back().kind != OmniboxSubmissionKind::kSearchUrl ||
          submissions_.back().value !=
              "https://search.test/?q=%E8%9C%A1%E7%AC%94%20browser%26more") {
        Finish(false, "click-or-search");
        return;
      }

      std::vector<OmniboxSubmission> isolated;
      empty_provider_ = std::make_unique<AlloyOmnibox>(
          AlloyOmnibox::Strings{"Input", "Address"},
          AlloyOmnibox::Callbacks{
              {},
              [&isolated](const OmniboxSubmission &submission) {
                isolated.push_back(submission);
              },
              {}},
          DefaultPrivacyDefaults());
      if (!empty_provider_->Edit("plain query") || !empty_provider_->Submit() ||
          isolated.size() != 1 ||
          isolated[0].kind != OmniboxSubmissionKind::kNoSearchProvider ||
          !empty_provider_->Edit("JaVaScRiPt:alert(1)") ||
          !empty_provider_->Submit() || isolated.size() != 2 ||
          isolated[1].kind != OmniboxSubmissionKind::kBlocked ||
          !empty_provider_->Edit("https://user:pass@example.test/") ||
          !empty_provider_->Submit() || isolated.size() != 3 ||
          isolated[2].kind != OmniboxSubmissionKind::kBlocked ||
          !empty_provider_->Shutdown()) {
        Finish(false, "fail-closed-submissions");
        return;
      }
      empty_provider_.reset();

      if (!omnibox_->OnNavigationFinished(true,
                                          "https://user:pass@例子.test/path") ||
          !omnibox_->Edit("draft remains") ||
          !omnibox_->SetAddress("https://new.test/") ||
          omnibox_->displayed_text() != "draft remains" || !omnibox_->Focus() ||
          !SendKey(VK_ESCAPE)) {
        Finish(false, "editing-protection");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (cancel_events_ == 0) {
      ScheduleCheck();
      return;
    }
    window_->SetSize(CefSize(280, 160));
    window_->Layout();
    const CefRect narrow = mounted_panel_->GetBoundsInScreen();
    window_->SetSize(CefSize(1000, 300));
    window_->Layout();
    const CefRect wide = mounted_panel_->GetBoundsInScreen();
    result_->real_input_passed =
        cancel_events_ == 1 && narrow.width > 0 && wide.width > narrow.width &&
        omnibox_->displayed_text() == "https://new.test/";
    Finish(result_->real_input_passed, "complete");
  }

  void Finish(bool passed, const char *detail) {
    if (finished_)
      return;
    finished_ = true;
    passed_ = passed;
    std::cout << "alloy_omnibox_windows detail=" << detail << std::endl;
    if (empty_provider_) {
      empty_provider_->Shutdown();
      empty_provider_.reset();
    }
    if (omnibox_ && (!omnibox_->Shutdown() || !omnibox_->Shutdown())) {
      passed_ = false;
    }
    if (window_) {
      if (mounted_panel_)
        window_->RemoveChildView(mounted_panel_);
      window_->Close();
    } else {
      CefQuitMessageLoop();
    }
  }

  std::shared_ptr<AlloyOmniboxProbeResult> result_;
  std::unique_ptr<AlloyOmnibox> omnibox_;
  std::unique_ptr<AlloyOmnibox> empty_provider_;
  CefRefPtr<CefPanel> mounted_panel_;
  CefRefPtr<CefWindow> window_;
  std::vector<Request> requests_;
  std::vector<OmniboxSubmission> submissions_;
  int checks_ = 0;
  int stage_ = 0;
  int logged_stage_ = -1;
  int cancel_events_ = 0;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(AlloyOmniboxProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateAlloyOmniboxProbe(std::shared_ptr<AlloyOmniboxProbeResult> result) {
  return new AlloyOmniboxProbe(std::move(result));
}
