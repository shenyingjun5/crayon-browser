// PLT-SHELL-22M: real-CEF macOS probe for the Browser-owned cast overlay.
// Drives the shared CastSelectionPresentation through AlloyCastOverlayMac
// inside a real Alloy window: supported anchor placement and click routing,
// expiry, unsupported/duplicate hiding, picker gating, key-window focus and
// detach cleanup. No network, no Cast-SDK transport.
#import <AppKit/AppKit.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "alloy_cast_overlay_mac_probe.h"

#include "crayon/browser_cast_view/cast_selection.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_task.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "macos/alloy_cast_overlay_mac.h"

namespace {

namespace cast_view = ::crayon::browser_cast_view;
using ::crayon::browser::cef_shell::macos::AlloyCastOverlayMac;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumPolls = 600;
constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 720;
// The overlay places on a 960x540 CSS viewport inside a 960x720 window, so
// the vertical scale differs from the horizontal one on purpose.
constexpr double kViewportWidth = 960;
constexpr double kViewportHeight = 540;

class Probe final : public CefApp,
                    public CefBrowserProcessHandler,
                    public CefClient,
                    public CefBrowserViewDelegate,
                    public CefWindowDelegate {
 public:
  explicit Probe(std::shared_ptr<AlloyCastOverlayMacProbeResult> result)
      : result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    overlay_ = std::make_unique<AlloyCastOverlayMac>(
        "投屏此视频",
        [this] { return now_ms_; },
        [this](cast_view::CastMediaRef ref) {
          activated_ = ref;
          ++activate_count_;
          return true;
        });
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(this, "about:blank", settings,
                                              nullptr, nullptr, this);
    CefWindow::CreateTopLevelWindow(this);
    Schedule();
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    window_->AddChildView(view_);
    layout->SetFlexForView(view_, 1);
    window_->SetTitle("Crayon Alloy Cast Overlay Probe");
    window_->SetSize(CefSize(kWindowWidth, kWindowHeight));
    window_->Layout();
    window_->Show();
    window_->Activate();
    AttachOverlay();
    Schedule();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return finished_; }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    overlay_->Detach();
    overlay_.reset();
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    std::cout << "alloy_cast_overlay_mac placement="
              << result_->placement_passed
              << " click=" << result_->click_passed
              << " expiry=" << result_->expiry_passed
              << " unsupported=" << result_->unsupported_passed
              << " duplicate=" << result_->duplicate_passed
              << " picker=" << result_->picker_passed
              << " detach=" << result_->detach_passed
              << " browser_closed=" << result_->browser_closed << std::endl;
    CefQuitMessageLoop();
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    result_->browser_closed = true;
    browser_ = nullptr;
    view_ = nullptr;
    if (window_) {
      window_->Close();
    }
  }

 private:
  static cast_view::CastViewContext Context() {
    return cast_view::CastViewContext{1, "probe", 1, 1, 1};
  }

  cast_view::CastVideoAnchor Anchor(cast_view::CastMediaRef media,
                                    std::uint64_t expires_at,
                                    bool supported) {
    return cast_view::CastVideoAnchor{Context(), revision_, media, expires_at,
                                      supported, 100, 80, 480, 300};
  }

  cast_view::CastSelectionSnapshot Snapshot(bool picker_open) {
    cast_view::CastSelectionSnapshot snapshot;
    snapshot.context = Context();
    snapshot.view_revision = ++revision_;
    snapshot.compatible = true;
    snapshot.picker_open = picker_open;
    snapshot.media = {
        {cast_view::CastMediaRef{1, 10}, "视频一", true},
        {cast_view::CastMediaRef{2, 20}, "视频二", true}};
    snapshot.eligible_count = 2;
    snapshot.media_total = 2;
    return snapshot;
  }

  void AttachOverlay() {
    // CefWindow's platform handle is the window's root NSView on macOS.
    auto* native_view = (__bridge NSView*)window_->GetWindowHandle();
    NSWindow* native_window = native_view.window;
    attached_ = overlay_->Attach((__bridge void*)native_window,
                                 (__bridge void*)native_view);
    std::cerr << "overlay_debug attach=" << attached_ << " key="
              << (native_window.keyWindow ? 1 : 0) << " visible="
              << (native_window.visible ? 1 : 0) << " appkey="
              << (NSApp.keyWindow != nil) << " appkeytitle="
              << (NSApp.keyWindow.title ? NSApp.keyWindow.title.UTF8String
                                        : "(nil)")
              << std::endl;
    [native_window makeKeyAndOrderFront:nil];
    if (attached_) {
      overlay_->BindContext(Context());
      cast_view::CastSelectionSnapshot snapshot = Snapshot(false);
      revision_ = snapshot.view_revision;
      overlay_->Apply(snapshot);
    }
  }

  void Schedule() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&Probe::Tick, CefRefPtr<Probe>(this)),
                       kPollMilliseconds);
  }

  NSButton* FindVisibleButton() const {
    auto* container = (__bridge NSView*)window_->GetWindowHandle();
    NSButton* found = nil;
    for (NSView* sub in container.subviews) {
      for (NSView* inner in sub.subviews) {
        if ([inner isKindOfClass:[NSButton class]] && !inner.hidden) {
          found = static_cast<NSButton*>(inner);
        }
      }
    }
    return found;
  }

  void Finish(bool passed, const char* detail) {
    if (finished_) return;
    finished_ = true;
    std::cout << "alloy_cast_overlay_mac passed=" << passed
              << " detail=" << detail << std::endl;
    if (browser_ && browser_->GetHost()) {
      browser_->GetHost()->CloseBrowser(true);
    } else if (window_) {
      window_->Close();
    }
  }

  void Tick() {
    if (finished_) return;
    if (++polls_ > kMaximumPolls) {
      const std::size_t visible = overlay_ ? overlay_->visible_count() : 0;
      std::cout << "alloy_cast_overlay_mac_timeout stage=" << stage_
                << " attached=" << attached_ << " visible=" << visible
                << std::endl;
      Finish(false, "timeout");
      return;
    }
    if (!attached_) {
      if (window_ && browser_ && view_ && view_->IsDrawn()) {
        AttachOverlay();
        if (!attached_) {
          Finish(false, "attach");
          return;
        }
      } else {
        Schedule();
        return;
      }
    }
    switch (stage_) {
      case 0: {
        // Supported main-frame anchor places through the shared engine with
        // the expected viewport scaling.
        const bool applied = overlay_->Apply(Snapshot(false));
        overlay_->SetObservations(
            {{Anchor(cast_view::CastMediaRef{1, 10}, now_ms_ + 500, true),
              kViewportWidth, kViewportHeight}});
        overlay_->Tick();
        const auto frame = overlay_->placed_frame(0);
        std::cerr << "overlay_debug stage0 applied=" << applied
                  << " visible=" << overlay_->visible_count()
                  << " frame="
                  << (frame ? std::to_string(frame->x) + "," +
                                  std::to_string(frame->y) + "," +
                                  std::to_string(frame->width) + "," +
                                  std::to_string(frame->height)
                            : std::string("none"))
                  << std::endl;
        auto* nv = (__bridge NSView*)window_->GetWindowHandle();
        std::cerr << "overlay_debug stage0 key="
                  << (nv.window.keyWindow ? 1 : 0) << " main="
                  << (nv.window.mainWindow ? 1 : 0) << std::endl;
        result_->placement_passed =
            overlay_->visible_count() == 1 && frame.has_value() &&
            frame->width == cast_view::kCastOverlayWidthDip &&
            frame->x == 476 && frame->y >= 0 && frame->height > 0;
        stage_ = 1;
        Schedule();
        return;
      }
      case 1: {
        // A real click on the visible native button routes the opaque ref
        // for controller revalidation.
        NSButton* visible_button = FindVisibleButton();
        if (visible_button) {
          [visible_button performClick:nil];
        }
        result_->click_passed =
            activated_.has_value() && activated_->instance_id == 1 &&
            activated_->source_revision == 10;
        stage_ = 2;
        Schedule();
        return;
      }
      case 2: {
        // Stale geometry: past the 500 ms geometry lifetime the shared
        // engine refuses placement.
        now_ms_ += 501;
        overlay_->SetObservations(
            {{Anchor(cast_view::CastMediaRef{1, 10}, now_ms_ - 1, true),
              kViewportWidth, kViewportHeight}});
        overlay_->Tick();
        result_->expiry_passed = overlay_->visible_count() == 0;
        stage_ = 3;
        Schedule();
        return;
      }
      case 3: {
        // Unsupported surfaces (iframe/Shadow DOM/fullscreen/PiP) never
        // reach the overlay: supported=false must not be drawn.
        overlay_->SetObservations(
            {{Anchor(cast_view::CastMediaRef{2, 20}, now_ms_ + 500, false),
              kViewportWidth, kViewportHeight}});
        overlay_->Tick();
        result_->unsupported_passed = overlay_->visible_count() == 0;
        stage_ = 4;
        Schedule();
        return;
      }
      case 4: {
        // Duplicate media refs in one batch hide the whole overlay.
        overlay_->SetObservations(
            {{Anchor(cast_view::CastMediaRef{1, 10}, now_ms_ + 500, true),
              kViewportWidth, kViewportHeight},
             {Anchor(cast_view::CastMediaRef{1, 10}, now_ms_ + 500, true),
              kViewportWidth, kViewportHeight}});
        overlay_->Tick();
        result_->duplicate_passed = overlay_->visible_count() == 0;
        stage_ = 5;
        Schedule();
        return;
      }
      case 5: {
        // Picker visible hides the overlay; closing it restores rendering.
        const auto picker_on = Snapshot(true);
        revision_ = picker_on.view_revision;
        overlay_->Apply(picker_on);
        overlay_->SetObservations(
            {{Anchor(cast_view::CastMediaRef{1, 10}, now_ms_ + 500, true),
              kViewportWidth, kViewportHeight}});
        overlay_->Tick();
        const bool hidden_with_picker = overlay_->visible_count() == 0;
        const auto picker_off = Snapshot(false);
        revision_ = picker_off.view_revision;
        overlay_->Apply(picker_off);
        overlay_->SetObservations(
            {{Anchor(cast_view::CastMediaRef{1, 10}, now_ms_ + 500, true),
              kViewportWidth, kViewportHeight}});
        overlay_->Tick();
        result_->picker_passed =
            hidden_with_picker && overlay_->visible_count() == 1;
        stage_ = 6;
        Schedule();
        return;
      }
      case 6: {
        // Detach removes the overlay surface completely and is idempotent.
        overlay_->Detach();
        overlay_->Detach();
        result_->detach_passed = overlay_->visible_count() == 0;
        Finish(result_->placement_passed && result_->click_passed &&
                   result_->expiry_passed && result_->unsupported_passed &&
                   result_->duplicate_passed && result_->picker_passed &&
                   result_->detach_passed,
               "complete");
        return;
      }
      default:
        Finish(false, "stage");
        return;
    }
  }

  std::shared_ptr<AlloyCastOverlayMacProbeResult> result_;
  std::unique_ptr<AlloyCastOverlayMac> overlay_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  std::optional<cast_view::CastMediaRef> activated_;
  int polls_ = 0;
  int stage_ = 0;
  int activate_count_ = 0;
  std::uint64_t now_ms_ = 1000;
  std::uint64_t revision_ = 1;
  bool attached_ = false;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(Probe);
  DISALLOW_COPY_AND_ASSIGN(Probe);
};

}  // namespace

CefRefPtr<CefApp> CreateAlloyCastOverlayMacProbe(
    std::shared_ptr<AlloyCastOverlayMacProbeResult> result) {
  return new Probe(std::move(result));
}
