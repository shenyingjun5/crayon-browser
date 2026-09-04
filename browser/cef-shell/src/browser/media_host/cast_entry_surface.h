#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "crayon/browser_cast_view/cast_selection.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"

namespace crayon::browser::cef_shell {

// UI-thread-only native surface. The Browser owner supplies a negotiated,
// verified projection and revalidates every intent. No MHV1/SDK fallback
// exists. Host requirements: LOCATION BrowserView, forward window keys/layout
// changes, call Tick at least every kTickIntervalMs while visible, Detach
// before close.
class CastEntrySurface final {
public:
  using Clock = std::function<std::uint64_t()>;
  using IntentSink =
      std::function<void(browser_cast_view::CastSelectionIntent)>;
  static constexpr int kTickIntervalMs = 50;
  // Stable native view IDs for host focus/accessibility routing.
  static constexpr int kEntryId = 0xca00;
  static constexpr int kCommitId = 0xca01;
  static constexpr int kCancelId = 0xca02;
  static constexpr int kPrepareId = 0xca05;
  static constexpr int kMediaFirstId = 0xca10;
  static constexpr int kDeviceFirstId = 0xca30;
  static constexpr int kOverlayFirstId = 0xca50;

  CastEntrySurface(localization::LocaleSnapshot locale, Clock clock,
                   IntentSink sink);
  ~CastEntrySurface();
  CastEntrySurface(const CastEntrySurface &) = delete;
  CastEntrySurface &operator=(const CastEntrySurface &) = delete;

  bool Attach(CefRefPtr<CefWindow> window,
              CefRefPtr<CefBrowserView> browser_view);
  void BindContext(browser_cast_view::CastViewContext context);
  bool Apply(browser_cast_view::CastSelectionSnapshot snapshot);
  // Replace geometry atomically; empty/invalid/over-capacity hides all
  // overlays.
  void SetVideoAnchors(std::vector<browser_cast_view::CastVideoAnchor> anchors);
  void InvalidateGeometry();
  void LayoutChanged();
  void Tick();
  bool HandleKeyEvent(const CefKeyEvent &event);
  // Forward CefWindowDelegate::OnAccelerator before other window commands.
  bool HandleAccelerator(int command_id);
  // Host accessibility/focus routing over this surface's own active controls.
  // Does not inspect the BrowserView/Chromium private view tree.
  CefRefPtr<CefView> GetView(int view_id) const;
  void SuspendLocation();
  bool RestoreLocation();
  void Detach();

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace crayon::browser::cef_shell
