#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "crayon/browser_cast_view/cast_selection.h"

namespace crayon::browser::cef_shell::windows {

struct AlloyCastOverlayObservation final {
  browser_cast_view::CastVideoAnchor anchor;
  double viewport_width = 0;
  double viewport_height = 0;
};

// Windows-only Browser-owned surface for ordinary Alloy video anchors. Page
// geometry is used only for placement; activation returns an opaque player
// reference to the Browser controller for current-context revalidation.
class AlloyCastOverlayWin final {
 public:
  using Clock = std::function<std::uint64_t()>;
  using Activate = std::function<bool(browser_cast_view::CastMediaRef)>;

  static constexpr int kFirstControlId = 0x5d00;

  AlloyCastOverlayWin(std::wstring accessible_name, Clock clock,
                      Activate activate);
  ~AlloyCastOverlayWin();

  AlloyCastOverlayWin(const AlloyCastOverlayWin&) = delete;
  AlloyCastOverlayWin& operator=(const AlloyCastOverlayWin&) = delete;

  bool Attach(void* root_window, void* browser_window);
  void BindContext(browser_cast_view::CastViewContext context);
  bool Apply(browser_cast_view::CastSelectionSnapshot snapshot);
  void SetObservations(std::vector<AlloyCastOverlayObservation> observations);
  void Tick();
  void Invalidate();
  void Detach();
  std::size_t visible_count() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::windows
