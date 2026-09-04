#include "cast_toolbar_host_probe.h"

#include <iostream>
#include <utility>

#include "browser/window/chrome_location_bar.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

constexpr int kWideWidth = 1040;
constexpr int kNarrowWidth = 720;
constexpr int kWindowHeight = 640;
constexpr int kButtonWidth = 72;
constexpr int kToolbarHeight = 48;
constexpr int kMinimumLocationWidth = 160;
constexpr int kPollMilliseconds = 20;
constexpr int kMaxChecks = 500;
constexpr int kCloseTimeoutMilliseconds = 10000;
constexpr char kFixtureUrl[] = "about:blank";

class ToolbarProbe final : public CefApp,
                           public CefBrowserProcessHandler,
                           public CefClient,
                           public CefLifeSpanHandler,
                           public CefLoadHandler,
                           public CefDisplayHandler,
                           public CefJSDialogHandler,
                           public CefButtonDelegate,
                           public CefBrowserViewDelegate,
                           public CefWindowDelegate {
public:
  ToolbarProbe(std::shared_ptr<CastToolbarHostProbeResult> result,
               bool verify_close_cancellation)
      : result_(std::move(result)),
        verify_close_cancellation_(verify_close_cancellation) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  // Geometry fixture only: clicking never grants or sends a cast command.
  void OnButtonPressed(CefRefPtr<CefButton>) override {}
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_CHROME;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_CHROME;
  }
  ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>) override {
    return CEF_CTT_LOCATION;
  }

  void
  OnBeforeCommandLineProcessing(const CefString &,
                                CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("use-mock-keychain");
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
  }

  void OnContextInitialized() override {
    CefBrowserSettings settings;
    browser_view_ = CefBrowserView::CreateBrowserView(
        this, kFixtureUrl, settings, nullptr, nullptr, this);
    CefWindow::CreateTopLevelWindow(this);
    ScheduleCheck();
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
  }

  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                 int) override {
    if (frame->IsMain())
      loaded_ = true;
  }

  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString &title) override {
    if (!verify_close_cancellation_ || !finished_)
      return;
    if (title == "close-fixture-ready") {
      // Test document only: never serves as a production media input proof.
      browser_->GetHost()->SetFocus(true);
      CefMouseEvent event;
      event.x = 20;
      event.y = 20;
      browser_->GetHost()->SendMouseClickEvent(event, MBT_LEFT, false, 1);
      browser_->GetHost()->SendMouseClickEvent(event, MBT_LEFT, true, 1);
    } else if (title == "close-fixture-armed") {
      CefPostTask(TID_UI,
                  base::BindOnce(&ToolbarProbe::RequestClose, CefRefPtr(this)));
    }
  }

  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString &, bool,
                            CefRefPtr<CefJSDialogCallback> callback) override {
    ++close_dialogs_;
    callback->Continue(close_dialogs_ > 1, CefString());
    if (close_dialogs_ == 1)
      CefPostTask(TID_UI, base::BindOnce(&ToolbarProbe::VerifyCancelledClose,
                                         CefRefPtr(this)));
    return true;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings layout;
    layout.horizontal = false;
    auto box = window_->SetToBoxLayout(layout);
    window_->AddChildView(browser_view_);
    box->SetFlexForView(browser_view_, 1);
    window_->SetSize(CefSize(kWideWidth, kWindowHeight));
    window_->Show();
  }

  void OnWindowChanged(CefRefPtr<CefView> view, bool added) override {
    if (!browser_view_ || !view->IsSame(browser_view_))
      return;
    if (!added) {
      location_bar_.Detach();
      button_ = nullptr;
      return;
    }
    if (location_bar_.location_attached() || !window_)
      return;
    button_ = CefLabelButton::CreateLabelButton(this, "Cast");
    button_->SetMinimumSize(CefSize(kButtonWidth, kToolbarHeight));
    button_->SetEnabled(false);
    if (!location_bar_.Attach(window_, browser_view_, button_))
      button_ = nullptr;
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    location_bar_.SuspendLocation();
    const bool ready = !browser_ || browser_->GetHost()->TryCloseBrowser();
    if (ready) {
      original_location_ = nullptr;
      location_bar_.Detach();
    } else if (!location_bar_.RestoreLocation()) {
      result_->layout_passed = false;
    }
    return ready;
  }

  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    result_->browser_closed = true;
    browser_ = nullptr;
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    location_bar_.Detach();
    window_ = nullptr;
    browser_view_ = nullptr;
    original_location_ = nullptr;
    button_ = nullptr;
    overlay_ = nullptr;
    CefQuitMessageLoop();
  }

private:
  void ScheduleCheck() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&ToolbarProbe::Check, CefRefPtr(this)),
                       kPollMilliseconds);
  }

  bool CheckLayout() {
    window_->Layout();
    const auto location = location_bar_.LocationBoundsInScreen();
    const auto button = location_bar_.ActionBoundsInScreen();
    const bool adjacent = location.width >= kMinimumLocationWidth &&
                          button.width >= kButtonWidth &&
                          button.x == location.x + location.width &&
                          button.y == location.y &&
                          button.height == location.height;
    std::cout << "cast_toolbar_host_probe layout=" << stage_
              << " adjacent=" << adjacent
              << " location_width=" << location.width
              << " button_width=" << button.width << std::endl;
    return adjacent;
  }

  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaxChecks) {
      std::cout << "cast_toolbar_host_probe readiness window="
                << static_cast<bool>(window_)
                << " browser=" << static_cast<bool>(browser_)
                << " location=" << location_bar_.location_attached()
                << std::endl;
      Finish(false, "timeout");
      return;
    }
    if (!window_ || !browser_ || !location_bar_.location_attached() ||
        !button_ || (verify_close_cancellation_ && !loaded_)) {
      ScheduleCheck();
      return;
    }
    if (!CheckLayout()) {
      Finish(false, "layout");
      return;
    }
    if (stage_ == 0) {
      if (button_->IsEnabled() || !CheckComponentContracts()) {
        Finish(false, "disabled_state_or_lifecycle");
        return;
      }
      button_->SetEnabled(true);
      window_->SetSize(CefSize(kNarrowWidth, kWindowHeight));
      ++stage_;
      ScheduleCheck();
      return;
    }
    auto overlay_button = CefLabelButton::CreateLabelButton(this, "Cast video");
    overlay_ =
        window_->AddOverlayView(overlay_button, CEF_DOCKING_MODE_CUSTOM, false);
    if (!overlay_ || !button_->IsEnabled()) {
      Finish(false, "overlay_creation");
      return;
    }
    const auto content = browser_view_->GetBounds();
    overlay_->SetBounds(
        CefRect(content.x, content.y, kButtonWidth, kToolbarHeight));
    overlay_->SetVisible(true);
    const bool visible = overlay_->IsVisible();
    overlay_->SetVisible(false);
    const bool hidden = !overlay_->IsVisible();
    overlay_button = nullptr;
    overlay_->Destroy();
    overlay_ = nullptr;
    // Close on a later UI task, after overlay destruction has unwound.
    CefPostTask(TID_UI,
                base::BindOnce(&ToolbarProbe::Finish, CefRefPtr(this),
                               visible && hidden, "location_and_overlay"));
  }

  void Finish(bool passed, const char *detail) {
    if (finished_)
      return;
    finished_ = true;
    result_->layout_passed = passed;
    std::cout << "cast_toolbar_host_probe passed=" << passed
              << " detail=" << detail << " mock_keychain=1" << std::endl;
    if (window_) {
      if (passed && verify_close_cancellation_) {
        CefPostDelayedTask(
            TID_UI,
            base::BindOnce(&ToolbarProbe::CloseTimeout, CefRefPtr(this)),
            kCloseTimeoutMilliseconds);
        original_location_ = browser_view_->GetChromeToolbar();
        browser_->GetMainFrame()->ExecuteJavaScript(
            "document.body.innerHTML='<button "
            "style=\"position:fixed;left:0;top:0;"
            "width:200px;height:120px\">Close fixture</button>';"
            "document.querySelector('button').onclick=()=>{"
            "window.onbeforeunload=e=>{e.preventDefault();e.returnValue='';};"
            "document.title='close-fixture-armed';};"
            "document.title='close-fixture-ready';",
            kFixtureUrl, 1);
      } else {
        window_->Close();
      }
    } else {
      CefQuitMessageLoop();
    }
  }

  void RequestClose() {
    if (window_)
      window_->Close();
  }

  bool CheckComponentContracts() {
    using crayon::browser::cef_shell::window::ChromeLocationBar;
    ChromeLocationBar empty;
    if (empty.RestoreLocation() ||
        empty.Attach(nullptr, browser_view_, button_) ||
        location_bar_.Attach(window_, browser_view_, button_))
      return false;
    auto original = browser_view_->GetChromeToolbar();
    location_bar_.SuspendLocation();
    location_bar_.SuspendLocation();
    if (location_bar_.location_attached() || !location_bar_.RestoreLocation() ||
        !location_bar_.RestoreLocation() ||
        !original->IsSame(browser_view_->GetChromeToolbar()))
      return false;
    location_bar_.Detach();
    location_bar_.Detach();
    return !location_bar_.RestoreLocation() &&
           location_bar_.Attach(window_, browser_view_, button_) &&
           CheckLayout();
  }

  void CloseTimeout() {
    if (!window_)
      return;
    result_->layout_passed = false;
    std::cout << "cast_toolbar_close_probe timeout dialogs=" << close_dialogs_
              << std::endl;
    original_location_ = nullptr;
    location_bar_.Detach();
    if (browser_)
      browser_->GetHost()->CloseBrowser(true);
    else
      window_->Close();
  }

  void VerifyCancelledClose() {
    const bool preserved =
        window_ && browser_ && !browser_->GetHost()->IsReadyToBeClosed() &&
        location_bar_.location_attached() && button_ && button_->IsEnabled() &&
        original_location_ &&
        original_location_->IsSame(browser_view_->GetChromeToolbar()) &&
        CheckLayout();
    result_->cancellation_verified = preserved;
    std::cout << "cast_toolbar_close_probe cancelled_preserved=" << preserved
              << " dialogs=" << close_dialogs_ << std::endl;
    original_location_ = nullptr;
    CefPostTask(TID_UI,
                base::BindOnce(&ToolbarProbe::RequestClose, CefRefPtr(this)));
  }

  std::shared_ptr<CastToolbarHostProbeResult> result_;
  CefRefPtr<CefBrowserView> browser_view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  crayon::browser::cef_shell::window::ChromeLocationBar location_bar_;
  CefRefPtr<CefView> original_location_;
  CefRefPtr<CefLabelButton> button_;
  CefRefPtr<CefOverlayController> overlay_;
  int checks_ = 0;
  int stage_ = 0;
  bool finished_ = false;
  bool loaded_ = false;
  bool verify_close_cancellation_ = false;
  int close_dialogs_ = 0;

  IMPLEMENT_REFCOUNTING(ToolbarProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateCastToolbarHostProbe(std::shared_ptr<CastToolbarHostProbeResult> result,
                           bool verify_close_cancellation) {
  return new ToolbarProbe(std::move(result), verify_close_cancellation);
}
