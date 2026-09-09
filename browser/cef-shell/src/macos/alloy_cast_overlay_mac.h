#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "crayon/browser_cast_view/cast_selection.h"

namespace crayon::browser::cef_shell::macos {

struct AlloyCastOverlayObservation final {
  browser_cast_view::CastVideoAnchor anchor;
  double viewport_width = 0;
  double viewport_height = 0;
};

// macOS Browser-owned overlay for ordinary Alloy video anchors (PLT-SHELL-
// 22M). Page geometry is used only for placement; clicks return an opaque
// CastMediaRef to the Browser controller for current-context revalidation.
// Placement, expiry and picker gating come from the shared
// CastSelectionPresentation; this class only renders native controls.
class AlloyCastOverlayMac final {
 public:
  using Clock = std::function<std::uint64_t()>;
  using Activate = std::function<bool(browser_cast_view::CastMediaRef)>;

  AlloyCastOverlayMac(std::string accessible_name, Clock clock,
                      Activate activate);
  ~AlloyCastOverlayMac();

  AlloyCastOverlayMac(const AlloyCastOverlayMac&) = delete;
  AlloyCastOverlayMac& operator=(const AlloyCastOverlayMac&) = delete;

  /// Attaches to a native NSWindow and the container view that hosts the
  /// browser content. Both must be alive for the overlay's lifetime.
  bool Attach(void* root_window, void* container_view);
  void BindContext(browser_cast_view::CastViewContext context);
  bool Apply(browser_cast_view::CastSelectionSnapshot snapshot);
  void SetObservations(std::vector<AlloyCastOverlayObservation> observations);
  void Tick();
  void Invalidate();
  void Detach();

  std::size_t visible_count() const noexcept;
  /// Test/diagnostic projection of one placed button (CSS-scaled window
  /// coordinates, top-left origin). Absent when hidden.
  std::optional<browser_cast_view::CastOverlayBounds> placed_frame(
      std::size_t index) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::macos
