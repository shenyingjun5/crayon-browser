#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyTabStripProbeResult final {
  bool behavior_passed = false;
  bool real_clicks_passed = false;
  bool capacity_passed = false;
  bool layout_passed = false;
  bool icons_passed = false;
  // PLT-SHELL-24M2FIX-C4: the native chrome decoration projection (tab rects,
  // indicator slots, active/loading flags) matches the strip's own views.
  bool decoration_passed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp>
CreateAlloyTabStripProbe(std::shared_ptr<AlloyTabStripProbeResult> result);
