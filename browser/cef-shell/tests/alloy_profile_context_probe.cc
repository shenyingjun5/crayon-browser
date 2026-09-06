#include "alloy_profile_context_probe.h"

#include <windows.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "browser/context/profile_context_factory.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::context::ProfileContextFactory;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 600;
constexpr std::size_t kViewCount = 4;
constexpr std::size_t kUniqueContextCount = 4;

class AlloyProfileContextProbe;
CefRefPtr<CefClient> CreateProfileBrowserClient(AlloyProfileContextProbe *owner,
                                                std::size_t index);
CefRefPtr<CefBrowserViewDelegate>
CreateProfileBrowserViewDelegate(AlloyProfileContextProbe *owner,
                                 std::size_t index);
CefRefPtr<CefWindowDelegate>
CreateProfileWindowDelegate(AlloyProfileContextProbe *owner, std::size_t index);

std::string Origin(const std::string &url) {
  const std::size_t scheme = url.find("://");
  const std::size_t slash = scheme == std::string::npos
                                ? std::string::npos
                                : url.find('/', scheme + 3);
  return slash == std::string::npos ? url : url.substr(0, slash);
}

class InitializationObserver final : public CefRequestContextHandler {
public:
  explicit InitializationObserver(bool *initialized)
      : initialized_(initialized) {}

  void OnRequestContextInitialized(
      CefRefPtr<CefRequestContext> request_context) override {
    if (initialized_ && request_context)
      *initialized_ = true;
  }

private:
  bool *initialized_;

  IMPLEMENT_REFCOUNTING(InitializationObserver);
};

class AlloyProfileContextProbe final : public CefApp,
                                       public CefBrowserProcessHandler,
                                       public CefClient,
                                       public CefLifeSpanHandler,
                                       public CefDisplayHandler {
public:
  AlloyProfileContextProbe(
      std::string fixture_url,
      std::shared_ptr<AlloyProfileContextProbeResult> result)
      : origin_(Origin(fixture_url)), result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

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
    const std::filesystem::path profile_root =
        std::filesystem::temp_directory_path() /
        ("crayon-page-snapshot-integration-" +
         std::to_string(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(profile_root, error);
    if (error) {
      Finish(false, "profile-root");
      return;
    }
    factory_ = std::make_unique<ProfileContextFactory>(profile_root.string());
    contexts_[0] = CefRequestContext::GetGlobalContext();
    contexts_initialized_[0] = true;
    if (!factory_->AdoptGlobalContext("profile-a", contexts_[0])) {
      Finish(false, "adopt-global-context");
      return;
    }
    for (std::size_t i = 1; i < kUniqueContextCount; ++i) {
      initialization_observers_[i] =
          new InitializationObserver(&contexts_initialized_[i]);
    }
    profile_a_again_ = factory_->GetPersistentContext("profile-a");
    contexts_[1] = factory_->GetPersistentContext("profile-b",
                                                  initialization_observers_[1]);
    contexts_[2] =
        factory_->CreateTemporaryContext(initialization_observers_[2]);
    contexts_[3] =
        factory_->CreateTemporaryContext(initialization_observers_[3]);
    for (const auto &context : contexts_) {
      if (!context) {
        Finish(false, "create-context");
        return;
      }
    }
    if (factory_->GetPersistentContext("bad/profile")) {
      Finish(false, "context-isolation");
      return;
    }

    ScheduleContextInitializationCheck();
  }

  void RecordWindowCreated(std::size_t index, CefRefPtr<CefWindow> window) {
    if (index >= kViewCount || !window) {
      Finish(false, "create-window");
      return;
    }
    windows_[index] = window;
    CefBoxLayoutSettings settings;
    auto layout = window->SetToBoxLayout(settings);
    window->AddChildView(views_[index]);
    layout->SetFlexForView(views_[index], 1);
    window->SetTitle("Crayon Alloy Profile Context Probe");
    window->SetSize(CefSize(480, 320));
    window->Layout();
    window->Show();
    ++created_windows_;
  }

  bool CanCloseWindow() const { return finished_; }

  void RecordWindowDestroyed(std::size_t index) {
    if (index >= kViewCount || !windows_[index])
      return;
    windows_[index] = nullptr;
    if (++destroyed_windows_ != expected_windows_)
      return;
    result_->window_closed = true;
    if (factory_)
      factory_->Shutdown();
    factory_.reset();
    for (auto &context : contexts_)
      context = nullptr;
    profile_a_again_ = nullptr;
    for (auto &view : views_)
      view = nullptr;
    for (auto &client : browser_clients_)
      client = nullptr;
    for (auto &delegate : browser_view_delegates_)
      delegate = nullptr;
    for (auto &delegate : window_delegates_)
      delegate = nullptr;
    CefQuitMessageLoop();
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString &title) override {
    for (std::size_t i = 0; i < kViewCount; ++i) {
      if (browsers_[i] && browser &&
          browser->GetIdentifier() == browsers_[i]->GetIdentifier()) {
        titles_[i] = title.ToString();
        return;
      }
    }
  }

  void RecordTitle(std::size_t index, const CefString &title) {
    if (index < kViewCount)
      titles_[index] = title.ToString();
  }

  void RecordBrowserCreated(std::size_t index, CefRefPtr<CefBrowser> browser) {
    if (index >= kViewCount || !browser) {
      Finish(false, "unknown-browser");
      return;
    }
    browsers_[index] = browser;
  }

  void RecordBrowserDestroyed(std::size_t index,
                              CefRefPtr<CefBrowser> browser) {
    if (index < kViewCount && browsers_[index] && browser &&
        browser->GetIdentifier() == browsers_[index]->GetIdentifier()) {
      browsers_[index] = nullptr;
      ++closed_browsers_;
    }
    if (finished_ && index < kViewCount && windows_[index]) {
      if (closed_browsers_ == kViewCount)
        result_->browsers_closed = true;
      windows_[index]->Close();
    }
  }

private:
  void ScheduleContextInitializationCheck() {
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(
            &AlloyProfileContextProbe::CreateViewsWhenContextsInitialized,
            CefRefPtr<AlloyProfileContextProbe>(this)),
        kPollMilliseconds);
  }

  void CreateViewsWhenContextsInitialized() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      Finish(false, "context-initialization-timeout");
      return;
    }
    for (bool initialized : contexts_initialized_) {
      if (!initialized) {
        ScheduleContextInitializationCheck();
        return;
      }
    }

    CefBrowserSettings settings;
    for (std::size_t i = 0; i < kViewCount; ++i) {
      browser_clients_[i] = CreateProfileBrowserClient(this, i);
      browser_view_delegates_[i] = CreateProfileBrowserViewDelegate(this, i);
      views_[i] = CefBrowserView::CreateBrowserView(
          browser_clients_[i], "about:blank", settings, nullptr, contexts_[i],
          browser_view_delegates_[i]);
      if (!views_[i]) {
        Finish(false, "create-view");
        return;
      }
    }
    expected_windows_ = kViewCount;
    for (std::size_t i = 0; i < kViewCount; ++i) {
      window_delegates_[i] = CreateProfileWindowDelegate(this, i);
      CefWindow::CreateTopLevelWindow(window_delegates_[i]);
    }
    ScheduleCheck();
  }

  void ScheduleCheck() {
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&AlloyProfileContextProbe::Check,
                       CefRefPtr<AlloyProfileContextProbe>(this)),
        kPollMilliseconds);
  }

  bool AllBrowsersCreated() const {
    for (const auto &browser : browsers_) {
      if (!browser)
        return false;
    }
    return true;
  }

  void Load(std::size_t index, const std::string &path) {
    browsers_[index]->GetMainFrame()->LoadURL(origin_ + path);
  }

  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (created_windows_ != kViewCount || !AllBrowsersCreated()) {
      ScheduleCheck();
      return;
    }
    if (stage_ == 0) {
      result_->context_isolation_passed =
          contexts_[0]->IsSame(profile_a_again_) &&
          !contexts_[0]->IsSame(contexts_[1]) &&
          !contexts_[2]->IsSame(contexts_[3]) &&
          !contexts_[0]->GetCachePath().empty() &&
          !contexts_[1]->GetCachePath().empty() &&
          contexts_[0]->GetCachePath() != contexts_[1]->GetCachePath() &&
          contexts_[2]->GetCachePath().empty() &&
          contexts_[3]->GetCachePath().empty();
      if (!result_->context_isolation_passed) {
        Finish(false, "context-isolation");
        return;
      }
      Load(0, "/profile-cookie-set-a");
    } else if (stage_ == 1 && titles_[0] == "set:profile_cookie=a") {
      Load(1, "/profile-cookie-read");
    } else if (stage_ == 2 && titles_[1] == "read:") {
      Load(1, "/profile-cookie-set-b");
    } else if (stage_ == 3 && titles_[1] == "set:profile_cookie=b") {
      Load(0, "/profile-cookie-read");
    } else if (stage_ == 4 && titles_[0] == "read:profile_cookie=a") {
      Load(2, "/profile-cookie-set-private");
    } else if (stage_ == 5 && titles_[2] == "set:profile_cookie=private") {
      Load(3, "/profile-cookie-read");
    } else if (stage_ == 6 && titles_[3] == "read:") {
      result_->cookie_isolation_passed = true;
      Finish(true, "complete");
      return;
    } else {
      ScheduleCheck();
      return;
    }
    ++stage_;
    ScheduleCheck();
  }

  void Finish(bool passed, const char *detail) {
    if (finished_)
      return;
    finished_ = true;
    std::cout << "alloy_profile_context_windows passed=" << passed
              << " detail=" << detail << std::endl;
    if (expected_windows_ == 0) {
      if (factory_)
        factory_->Shutdown();
      factory_.reset();
      result_->browsers_closed = true;
      result_->window_closed = true;
      CefQuitMessageLoop();
      return;
    }
    for (const auto &browser : browsers_) {
      if (browser)
        browser->GetHost()->CloseBrowser(true);
    }
    if (closed_browsers_ == kViewCount) {
      result_->browsers_closed = true;
      for (const auto &window : windows_) {
        if (window)
          window->Close();
      }
    }
  }

  const std::string origin_;
  std::shared_ptr<AlloyProfileContextProbeResult> result_;
  std::unique_ptr<ProfileContextFactory> factory_;
  std::array<CefRefPtr<CefRequestContext>, kViewCount> contexts_;
  CefRefPtr<CefRequestContext> profile_a_again_;
  std::array<CefRefPtr<InitializationObserver>, kUniqueContextCount>
      initialization_observers_;
  std::array<bool, kUniqueContextCount> contexts_initialized_{};
  std::array<CefRefPtr<CefBrowserView>, kViewCount> views_;
  std::array<CefRefPtr<CefClient>, kViewCount> browser_clients_;
  std::array<CefRefPtr<CefBrowserViewDelegate>, kViewCount>
      browser_view_delegates_;
  std::array<CefRefPtr<CefWindowDelegate>, kViewCount> window_delegates_;
  std::array<CefRefPtr<CefBrowser>, kViewCount> browsers_;
  std::array<std::string, kViewCount> titles_;
  std::array<CefRefPtr<CefWindow>, kViewCount> windows_;
  std::size_t created_windows_ = 0;
  std::size_t expected_windows_ = 0;
  std::size_t destroyed_windows_ = 0;
  std::size_t closed_browsers_ = 0;
  int stage_ = 0;
  int checks_ = 0;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(AlloyProfileContextProbe);
};

class ProfileBrowserClient final : public CefClient, public CefDisplayHandler {
public:
  ProfileBrowserClient(CefRefPtr<AlloyProfileContextProbe> owner,
                       std::size_t index)
      : owner_(std::move(owner)), index_(index) {}

  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString &title) override {
    if (owner_)
      owner_->RecordTitle(index_, title);
  }

private:
  CefRefPtr<AlloyProfileContextProbe> owner_;
  const std::size_t index_;

  IMPLEMENT_REFCOUNTING(ProfileBrowserClient);
};

CefRefPtr<CefClient> CreateProfileBrowserClient(AlloyProfileContextProbe *owner,
                                                std::size_t index) {
  return new ProfileBrowserClient(owner, index);
}

class ProfileBrowserViewDelegate final : public CefBrowserViewDelegate {
public:
  ProfileBrowserViewDelegate(CefRefPtr<AlloyProfileContextProbe> owner,
                             std::size_t index)
      : owner_(std::move(owner)), index_(index) {}

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView>,
                        CefRefPtr<CefBrowser> browser) override {
    if (owner_)
      owner_->RecordBrowserCreated(index_, browser);
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser> browser) override {
    if (owner_)
      owner_->RecordBrowserDestroyed(index_, browser);
  }

private:
  CefRefPtr<AlloyProfileContextProbe> owner_;
  const std::size_t index_;

  IMPLEMENT_REFCOUNTING(ProfileBrowserViewDelegate);
};

CefRefPtr<CefBrowserViewDelegate>
CreateProfileBrowserViewDelegate(AlloyProfileContextProbe *owner,
                                 std::size_t index) {
  return new ProfileBrowserViewDelegate(owner, index);
}

class ProfileWindowDelegate final : public CefWindowDelegate {
public:
  ProfileWindowDelegate(CefRefPtr<AlloyProfileContextProbe> owner,
                        std::size_t index)
      : owner_(std::move(owner)), index_(index) {}

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    if (owner_)
      owner_->RecordWindowCreated(index_, window);
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    return owner_ && owner_->CanCloseWindow();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    if (owner_)
      owner_->RecordWindowDestroyed(index_);
  }

private:
  CefRefPtr<AlloyProfileContextProbe> owner_;
  const std::size_t index_;

  IMPLEMENT_REFCOUNTING(ProfileWindowDelegate);
};

CefRefPtr<CefWindowDelegate>
CreateProfileWindowDelegate(AlloyProfileContextProbe *owner,
                            std::size_t index) {
  return new ProfileWindowDelegate(owner, index);
}

} // namespace

CefRefPtr<CefApp> CreateAlloyProfileContextProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyProfileContextProbeResult> result) {
  return new AlloyProfileContextProbe(std::move(fixture_url),
                                      std::move(result));
}
