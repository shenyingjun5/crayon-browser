#include "alloy_omnibox_probe.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "browser/window/alloy_omnibox.h"
#include "browser/window/alloy_search_engines.h"
#include "crayon/browser_privacy/privacy_defaults.h"
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button.h"
#include "include/views/cef_display.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace {

using crayon::browser::cef_shell::window::AlloyOmnibox;
using crayon::browser::cef_shell::window::DefaultSearchProviders;
using crayon::browser::cef_shell::window::kDefaultSearchEngine;
using crayon::browser::cef_shell::window::kSearchEngineOrder;
using crayon::browser::cef_shell::window::OmniboxSubmission;
using crayon::browser::cef_shell::window::OmniboxSubmissionKind;
using crayon::browser::cef_shell::window::SearchEngine;
using crayon::browser::cef_shell::window::SearchProviderFor;
using crayon::browser_omnibox::OmniboxSuggestion;
using crayon::browser_omnibox::SuggestionSource;
using crayon::browser_omnibox_provider::SearchProvider;
using crayon::browser_omnibox_provider::SearchProviderSet;
using crayon::browser_omnibox_provider::ValidateProvider;
using crayon::browser_privacy::DefaultPrivacyDefaults;

constexpr int kPollMilliseconds = 25;
constexpr int kMaximumChecks = 320;
constexpr int kKeyDown = 0x28;
constexpr int kKeyEnter = 0x0d;
constexpr int kKeyEscape = 0x1b;
constexpr int kKeyPeriod = 0xbe;
constexpr int kKeySpace = 0x20;

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
#if defined(__APPLE__)
    command->AppendSwitch("use-mock-keychain");
#endif
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
        AlloyOmnibox::Strings{"Search or enter address", "Address",
                              "No search provider", "Blocked", "Load failed"},
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
#if defined(_WIN32)
    const HWND handle = window_->GetWindowHandle();
    return handle && SetForegroundWindow(handle);
#else
    return window_->IsActive();
#endif
  }

  bool SendKey(int key) {
    if (!Foreground())
      return false;
#if defined(_WIN32)
    INPUT input[2]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = static_cast<WORD>(key);
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = static_cast<WORD>(key);
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, input, sizeof(INPUT)) == 2;
#else
    window_->SendKeyPress(key, EVENTFLAG_NONE);
    return true;
#endif
  }

  bool SendUnicodeText(const std::wstring &text) {
    if (!Foreground())
      return false;
#if defined(_WIN32)
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
#else
    // This native smoke covers the ASCII fixture; IME is a separate gate.
    for (const wchar_t character : text) {
      if (character >= L'a' && character <= L'z') {
        window_->SendKeyPress(static_cast<int>(character - L'a' + L'A'), EVENTFLAG_NONE);
      } else if (character == L'.') {
        window_->SendKeyPress(kKeyPeriod, EVENTFLAG_NONE);
      } else {
        return false;
      }
    }
    return true;
#endif
  }

  CefRefPtr<CefButton> FirstSuggestionButton(CefRect *bounds) const {
    if (!omnibox_->panel() || omnibox_->panel()->GetChildViewCount() < 2)
      return nullptr;
    auto suggestion_panel = omnibox_->panel()->GetChildViewAt(1)->AsPanel();
    if (!suggestion_panel || suggestion_panel->GetChildViewCount() == 0)
      return nullptr;
    auto button = suggestion_panel->GetChildViewAt(0)->AsButton();
    if (!button)
      return nullptr;
    if (bounds)
      *bounds = button->GetBoundsInScreen();
    return button;
  }

  bool ActivateFirstSuggestion() {
    CefRect bounds{};
    const auto button = FirstSuggestionButton(&bounds);
    if (!Foreground() || !button || !button->IsDrawn() ||
        bounds.width <= 0 || bounds.height <= 0) {
      return false;
    }
#if defined(_WIN32)
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
#else
    button->RequestFocus();
    if (!button->HasFocus())
      return false;
    window_->SendKeyPress(kKeySpace, EVENTFLAG_NONE);
    return true;
#endif
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

  // PLT-SHELL-24M2FIX-C8: the product catalogue decides, with no network, where
  // input that is not a URL is sent. Asserted here because the rest of this
  // probe tests the omnibox with a provider it configures itself, which would
  // leave the product's own default untested.
  bool SearchEngineCatalogOk() {
    const auto providers = DefaultSearchProviders();
    const auto *primary = providers.Primary();
    if (!primary || primary->name != "Baidu" ||
        primary->url_template.find("https://www.baidu.com/s?wd=") != 0) {
      return false;
    }
    const auto url = providers.BuildSearchUrl("chromium");
    if (!url || url->find("https://www.baidu.com/s?wd=chromium") != 0) {
      return false;
    }
    if (kSearchEngineOrder.size() != 2 ||
        kSearchEngineOrder[0] != kDefaultSearchEngine) {
      return false;
    }
    for (const SearchEngine engine : kSearchEngineOrder) {
      if (ValidateProvider(SearchProviderFor(engine))) {
        return false;
      }
    }
    return SearchProviderFor(SearchEngine::kBaidu).url_template !=
           SearchProviderFor(SearchEngine::kGoogle).url_template;
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
      std::cout << "alloy_omnibox_windows timeout stage=" << stage_
                << std::endl;
      Finish(false, "timeout");
      return;
    }
    if (logged_stage_ != stage_) {
      logged_stage_ = stage_;
      std::cout << "alloy_omnibox_windows stage=" << stage_ << std::endl;
    }
    // PLT-SHELL-24M2FIX-B: the notice paths below reach no network and need no
    // synthetic key delivery, so they are asserted before the input stages. The
    // input stages require an Accessibility/Foreground-trusted session and are
    // known to degrade in a non-foreground GUI session; gating the notices
    // behind them would let that environment limit mask this coverage.
    if (!local_scenario_checked_) {
      local_scenario_checked_ = true;
      const bool local_ok = RunLocalScenario() && SearchEngineCatalogOk();
      std::cout << "alloy_omnibox_windows local_notices=" << (local_ok ? 1 : 0)
                << std::endl;
      if (!local_ok) {
        Finish(false, "local-notices");
        return;
      }
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
          !omnibox_->Edit("") || !omnibox_->Focus()) {
        Finish(false, "display-or-input");
        return;
      }
      if (!SendUnicodeText(L"example.test")) {
        // Reported apart from the checks above: synthetic key delivery needs an
        // Accessibility-trusted session whose window can actually be activated,
        // so this failure means "environment", not "display or input".
        Finish(false, "foreground-input");
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
          omnibox_->suggestion_count() != 8 || !SendKey(kKeyDown)) {
        Finish(false, "generation-or-down");
        return;
      }
      result_->generation_passed = true;
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 2) {
      if (omnibox_->selected_suggestion() != 0 || !SendKey(kKeyEnter)) {
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
      if (!ActivateFirstSuggestion()) {
        Finish(false, "suggestion-click");
        return;
      }
#if defined(_WIN32)
      std::cout << "alloy_omnibox_windows suggestion_activation=mouse"
                << std::endl;
#else
      std::cout << "alloy_omnibox_windows suggestion_activation=keyboard"
                << std::endl;
#endif
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

      // The fail-closed submission notices and the failed-load notice need no
      // synthetic key delivery, so they are asserted in RunLocalScenario()
      // before the input stages; see the local_scenario_ gate in Check().
      if (!omnibox_->OnNavigationFinished(true,
                                          "https://user:pass@例子.test/path") ||
          !omnibox_->Edit("draft remains") ||
          !omnibox_->SetAddress("https://new.test/") ||
          omnibox_->displayed_text() != "draft remains" || !omnibox_->Focus() ||
          !SendKey(kKeyEscape)) {
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

  // PLT-SHELL-24M2FIX-B: every submission that can reach no remote service, and
  // every failed main-frame navigation, must surface a notice instead of
  // leaving the address bar (and a blank page area) looking idle. None of these
  // paths touch the network or need synthetic input.
  bool RunLocalScenario() {
    CEF_REQUIRE_UI_THREAD();
    std::vector<OmniboxSubmission> isolated;
    empty_provider_ = std::make_unique<AlloyOmnibox>(
        AlloyOmnibox::Strings{"Input", "Address", "No search provider",
                              "Blocked", "Load failed"},
        AlloyOmnibox::Callbacks{
            {},
            [&isolated](const OmniboxSubmission &submission) {
              isolated.push_back(submission);
            },
            {}},
        DefaultPrivacyDefaults());
    const bool notices_ok =
        empty_provider_->notice_text().empty() &&
        empty_provider_->Edit("plain query") && empty_provider_->Submit() &&
        isolated.size() == 1 &&
        isolated[0].kind == OmniboxSubmissionKind::kNoSearchProvider &&
        empty_provider_->notice_text() == "No search provider" &&
        empty_provider_->Edit("JaVaScRiPt:alert(1)") &&
        empty_provider_->Submit() && isolated.size() == 2 &&
        isolated[1].kind == OmniboxSubmissionKind::kBlocked &&
        empty_provider_->notice_text() == "Blocked" &&
        empty_provider_->Edit("https://user:pass@example.test/") &&
        empty_provider_->Submit() && isolated.size() == 3 &&
        isolated[2].kind == OmniboxSubmissionKind::kBlocked &&
        empty_provider_->notice_text() == "Blocked" &&
        empty_provider_->Shutdown();
    empty_provider_.reset();
    if (!notices_ok) {
      return false;
    }

    std::vector<OmniboxSubmission> failed;
    auto failing = std::make_unique<AlloyOmnibox>(
        AlloyOmnibox::Strings{"Input", "Address", "No search provider",
                              "Blocked", "Load failed"},
        AlloyOmnibox::Callbacks{
            {},
            [&failed](const OmniboxSubmission &submission) {
              failed.push_back(submission);
            },
            {}},
        DefaultPrivacyDefaults());
    const bool load_failure_ok =
        failing->notice_text().empty() && failing->Edit("https://fail.test/") &&
        failing->notice_text().empty() && failing->Submit() &&
        failed.size() == 1 &&
        failed[0].kind == OmniboxSubmissionKind::kNavigateUrl &&
        // Still loading: no notice yet, only a completed failure explains.
        failing->notice_text().empty() &&
        failing->OnNavigationFinished(false, "https://fail.test/") &&
        failing->notice_text() == "Load failed" &&
        // A fresh edit supersedes the stale failure notice.
        failing->Edit("second attempt") && failing->notice_text().empty() &&
        // A page-driven failure (link, redirect, reload) never enters the
        // loading state, so OnNavigationFinished reports false and the caller
        // must drive the notice explicitly.
        !failing->OnNavigationFinished(false, "https://elsewhere.test/") &&
        failing->notice_text().empty() && failing->ShowLoadFailureNotice() &&
        failing->notice_text() == "Load failed" &&
        failing->Edit("third attempt") && failing->notice_text().empty() &&
        failing->Shutdown();
    return load_failure_ok;
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
  bool local_scenario_checked_ = false;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(AlloyOmniboxProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateAlloyOmniboxProbe(std::shared_ptr<AlloyOmniboxProbeResult> result) {
  return new AlloyOmniboxProbe(std::move(result));
}
