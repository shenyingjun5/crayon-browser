#include "cast_entry_surface_probe.h"

#include <iostream>
#include <memory>
#include <vector>

#include "browser/media_host/cast_entry_surface.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_button.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {
using crayon::browser::cef_shell::CastEntrySurface;
using namespace crayon::browser_cast_view;
constexpr int kPollMs = 20, kMaxPolls = 750, kSpaceKey = 32;
constexpr int kWide = 1040, kNarrow = 720, kHeight = 720;

class EntryProbe final : public CefApp,
                         public CefBrowserProcessHandler,
                         public CefClient,
                         public CefLifeSpanHandler,
                         public CefBrowserViewDelegate,
                         public CefWindowDelegate {
public:
  explicit EntryProbe(std::shared_ptr<CastEntrySurfaceProbeResult> result)
      : result_(std::move(result)) {}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
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
    view_ = CefBrowserView::CreateBrowserView(this, "about:blank", settings,
                                              nullptr, nullptr, this);
    surface_ = std::make_unique<CastEntrySurface>(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        [this] { return now_; },
        [this](CastSelectionIntent i) { Ack(std::move(i)); });
    CefWindow::CreateTopLevelWindow(this);
    Schedule();
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings layout;
    layout.horizontal = false;
    auto box = window_->SetToBoxLayout(layout);
    window_->AddChildView(view_);
    box->SetFlexForView(view_, 1);
    window_->SetSize(CefSize(kWide, kHeight));
    window_->Show();
    window_->Activate();
  }
  void OnWindowChanged(CefRefPtr<CefView> view, bool added) override {
    if (added && view_ && view->IsSame(view_) && window_ && !attached_)
      attached_ = surface_->Attach(window_, view_);
  }
  bool OnKeyEvent(CefRefPtr<CefWindow>, const CefKeyEvent &event) override {
    return surface_ && surface_->HandleKeyEvent(event);
  }
  bool OnAccelerator(CefRefPtr<CefWindow>, int command_id) override {
    return surface_ && surface_->HandleAccelerator(command_id);
  }
  void OnLayoutChanged(CefRefPtr<CefView>, const CefRect &) override {
    if (surface_)
      surface_->LayoutChanged();
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    surface_->SuspendLocation();
    if (!browser_ || browser_->GetHost()->TryCloseBrowser()) {
      surface_->Detach();
      return true;
    }
    surface_->RestoreLocation();
    return false;
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    result_->browser_closed = true;
    browser_ = nullptr;
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    surface_->Detach();
    surface_.reset();
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    CefQuitMessageLoop();
  }

private:
  void Schedule() {
    if (finished_ || poll_scheduled_)
      return;
    poll_scheduled_ = true;
    CefPostDelayedTask(
        TID_UI, base::BindOnce(&EntryProbe::RunScheduledCheck, CefRefPtr(this)),
        kPollMs);
  }
  void RunScheduledCheck() {
    poll_scheduled_ = false;
    Check();
  }
  CefRefPtr<CefView> Find(int id) {
    auto owned = surface_->GetView(id);
    if (owned)
      return owned;
    auto found = window_->GetViewForID(id);
    if (found)
      return found;
    // Public Views traversal, including the focused overlay's own widget root.
    auto focused = window_->GetFocusedView();
    while (focused) {
      found = focused->GetViewForID(id);
      if (found)
        return found;
      focused = focused->GetParentView();
    }
    return nullptr;
  }
  bool Press(int id) {
    auto button = Find(id);
    if (!button || !button->IsEnabled())
      return false;
    button->RequestFocus();
    window_->SendKeyPress(kSpaceKey, 0);
    return true;
  }
  bool AwaitControl(int id) {
    auto control = Find(id);
    if (control && control->IsEnabled() && control->IsDrawn())
      return false;
    // Apply acknowledges the model before replacing stale-revision widgets.
    // Wait for the observable control, under the existing global deadline.
    Schedule();
    return true;
  }
  void Ack(CastSelectionIntent intent) {
    intents_.push_back(intent);
    // Test owner, not a production runtime: records intents and supplies
    // read-only DTOs.
    if (intent.kind == CastIntentKind::kCommit)
      return;
    ++snapshot_.view_revision;
    ++snapshot_.draft_revision;
    if (intent.kind == CastIntentKind::kOpen ||
        intent.kind == CastIntentKind::kOpenForMedia) {
      snapshot_.picker_open = true;
      ++snapshot_.draft_id;
      snapshot_.phase = CastDraftPhase::kChoosing;
    }
    if (intent.kind == CastIntentKind::kCancel)
      snapshot_.picker_open = false;
    if (intent.kind == CastIntentKind::kSelectMedia ||
        intent.kind == CastIntentKind::kOpenForMedia)
      for (const auto &m : snapshot_.media)
        if (intent.media && m.ref == *intent.media)
          snapshot_.selected_media = m;
    if (intent.kind == CastIntentKind::kSelectDevice)
      snapshot_.selected_device = snapshot_.devices[0];
    if (intent.kind == CastIntentKind::kPrepare) {
      snapshot_.phase = CastDraftPhase::kPrepared;
      snapshot_.route = CastSelectionRoute::kDirect;
      snapshot_.prepared_until_ms = now_ + 1000;
    }
    // Deterministically exercise a poll after the owner acknowledgement but
    // before its queued native Render. Intent receipt is not view readiness.
    if (intent.kind == CastIntentKind::kSelectMedia)
      CefPostTask(TID_UI, base::BindOnce(&EntryProbe::Check, CefRefPtr(this)));
    surface_->Apply(snapshot_);
  }
  std::size_t Count(CastIntentKind kind) const {
    std::size_t count = 0;
    for (const auto &i : intents_)
      if (i.kind == kind)
        ++count;
    return count;
  }
  void Finish(bool passed, const char *reason) {
    if (finished_)
      return;
    finished_ = true;
    result_->behavior_passed = passed;
    std::cout << "cast_entry_surface_probe stage=" << stage_
              << " result=" << reason << " intents=" << intents_.size()
              << " active=" << window_->IsActive() << std::endl;
    window_->Close();
  }
  void Check() {
    if (finished_)
      return;
    if (++polls_ > kMaxPolls) {
      Finish(false, "timeout");
      return;
    }
    if (!attached_ || !browser_) {
      Schedule();
      return;
    }
    surface_->Tick();
    auto entry = Find(CastEntrySurface::kEntryId);
    if (!entry) {
      Finish(false, "entry_missing");
      return;
    }
    switch (stage_) {
    case 0: {
      if (!snapshot_.view_revision && entry->IsEnabled()) {
        Finish(false, "must_start_disabled");
        return;
      }
      window_->Layout();
      const auto e = entry->GetBoundsInScreen(),
                 location = view_->GetChromeToolbar()->GetBoundsInScreen();
      if (e.x != location.x + location.width || e.y != location.y ||
          location.width < 160) {
        Finish(false, "not_adjacent");
        return;
      }
      if (!snapshot_.view_revision) {
        snapshot_.context = {1, "fixture", 2, 3, 4};
        snapshot_.view_revision = 1;
        snapshot_.compatible = true;
        snapshot_.draft_id = snapshot_.draft_revision = 1;
        snapshot_.media = {{{10, 1}, "视频 A", true},
                           {{20, 1}, "视频 B", true}};
        snapshot_.media_total = snapshot_.eligible_count = 2;
        snapshot_.devices = {{"fixture-device", "客厅", true}};
        snapshot_.device_total = 1;
        surface_->BindContext(snapshot_.context);
        surface_->Apply(snapshot_);
      }
      if (!entry->IsEnabled()) {
        Schedule();
        return;
      }
      if (!intents_.empty() || !Press(CastEntrySurface::kEntryId)) {
        Finish(false, "open");
        return;
      }
      break;
    }
    case 1:
      if (!Count(CastIntentKind::kOpen)) {
        Schedule();
        return;
      }
      if (AwaitControl(CastEntrySurface::kMediaFirstId + 1))
        return;
      if (!Find(CastEntrySurface::kCommitId) ||
          Find(CastEntrySurface::kCommitId)->IsEnabled() ||
          !Press(CastEntrySurface::kMediaFirstId + 1)) {
        Finish(false, "media_choices");
        return;
      }
      break;
    case 2:
      if (!Count(CastIntentKind::kSelectMedia)) {
        Schedule();
        return;
      }
      if (!snapshot_.selected_media ||
          snapshot_.selected_media->ref.instance_id != 20 ||
          Count(CastIntentKind::kCommit)) {
        Finish(false, "device_choices");
        return;
      }
      if (AwaitControl(CastEntrySurface::kDeviceFirstId))
        return;
      if (!Press(CastEntrySurface::kDeviceFirstId)) {
        Finish(false, "device_input");
        return;
      }
      break;
    case 3:
      if (!Count(CastIntentKind::kSelectDevice)) {
        Schedule();
        return;
      }
      if (AwaitControl(CastEntrySurface::kPrepareId))
        return;
      if (Count(CastIntentKind::kCommit) ||
          !Press(CastEntrySurface::kPrepareId)) {
        Finish(false, "prepare");
        return;
      }
      break;
    case 4:
      if (!Count(CastIntentKind::kPrepare)) {
        Schedule();
        return;
      }
      if (AwaitControl(CastEntrySurface::kCommitId))
        return;
      if (!Press(CastEntrySurface::kCommitId)) {
        Finish(false, "commit");
        return;
      }
      break;
    case 5:
      if (!Count(CastIntentKind::kCommit)) {
        Schedule();
        return;
      }
      if (AwaitControl(CastEntrySurface::kCancelId))
        return;
      if (Count(CastIntentKind::kCommit) != 1 ||
          Find(CastEntrySurface::kCommitId)->IsEnabled() ||
          !Press(CastEntrySurface::kCancelId)) {
        Finish(false, "double_commit_or_cancel");
        return;
      }
      break;
    case 6: {
      if (!Count(CastIntentKind::kCancel)) {
        Schedule();
        return;
      }
      window_->SetSize(CefSize(kNarrow, kHeight));
      window_->Layout();
      snapshot_.phase = CastDraftPhase::kChoosing;
      snapshot_.selected_media.reset();
      snapshot_.selected_device.reset();
      ++snapshot_.view_revision;
      ++snapshot_.draft_revision;
      surface_->Apply(snapshot_);
      break;
    }
    case 7: {
      CastVideoAnchor anchor{snapshot_.context,
                             snapshot_.view_revision,
                             snapshot_.media[0].ref,
                             now_ + 100,
                             true,
                             0,
                             0,
                             640,
                             360};
      if (!surface_->GetView(CastEntrySurface::kOverlayFirstId)) {
        // A queued resize/layout notification can invalidate the first anchor.
        // Supply fresh geometry when absent, without replacing a live widget.
        surface_->SetVideoAnchors({anchor});
      }
      if (AwaitControl(CastEntrySurface::kOverlayFirstId))
        return;
      auto overlay = surface_->GetView(CastEntrySurface::kOverlayFirstId);
      const auto viewport = view_->GetBoundsInScreen();
      if (!overlay ||
          overlay->GetBoundsInScreen().x !=
              viewport.x + 640 - kCastOverlayInsetDip - kCastOverlayWidthDip ||
          !Press(CastEntrySurface::kOverlayFirstId)) {
        Finish(false, "overlay_geometry_or_focus");
        return;
      }
      break;
    }
    case 8:
      if (!Count(CastIntentKind::kOpenForMedia)) {
        Schedule();
        return;
      }
      if (!snapshot_.selected_media ||
          snapshot_.selected_media->ref.instance_id != 10 ||
          Count(CastIntentKind::kCommit) != 1) {
        Finish(false, "overlay_must_only_preselect");
        return;
      }
      if (AwaitControl(CastEntrySurface::kCancelId))
        return;
      window_->SendKeyPress(27, 0);
      break;
    case 9: {
      if (Count(CastIntentKind::kCancel) < 2) {
        Schedule();
        return;
      }
      CastVideoAnchor anchor{snapshot_.context,
                             snapshot_.view_revision,
                             snapshot_.media[0].ref,
                             now_ + 100,
                             true,
                             0,
                             0,
                             640,
                             360};
      if (!surface_->GetView(CastEntrySurface::kOverlayFirstId)) {
        surface_->SetVideoAnchors({anchor});
      }
      if (AwaitControl(CastEntrySurface::kOverlayFirstId))
        return;
      now_ += 101;
      surface_->Tick();
      if (surface_->GetView(CastEntrySurface::kOverlayFirstId)) {
        Finish(false, "expired_overlay_still_visible");
        return;
      }
      break;
    }
    case 10:
      if (Count(CastIntentKind::kOpenForMedia) != 1) {
        Finish(false, "expired_overlay");
        return;
      }
      ++snapshot_.context.navigation_id;
      surface_->BindContext(snapshot_.context);
      if (entry->IsEnabled()) {
        Finish(false, "navigation_not_cleared");
        return;
      }
      surface_->Detach();
      surface_->Detach();
      Finish(true, "PASS");
      return;
    default:
      Finish(false, "invalid_stage");
      return;
    }
    ++stage_;
    Schedule();
  }
  std::shared_ptr<CastEntrySurfaceProbeResult> result_;
  std::unique_ptr<CastEntrySurface> surface_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CastSelectionSnapshot snapshot_;
  std::vector<CastSelectionIntent> intents_;
  std::uint64_t now_ = 100;
  int stage_ = 0, polls_ = 0;
  bool attached_ = false, finished_ = false;
  bool poll_scheduled_ = false;
  IMPLEMENT_REFCOUNTING(EntryProbe);
};
} // namespace
CefRefPtr<CefApp> CreateCastEntrySurfaceProbe(
    std::shared_ptr<CastEntrySurfaceProbeResult> result) {
  return new EntryProbe(std::move(result));
}
