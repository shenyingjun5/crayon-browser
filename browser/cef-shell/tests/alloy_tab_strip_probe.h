#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyTabStripProbeResult final {
  bool behavior_passed = false;
  bool real_clicks_passed = false;
  bool capacity_passed = false;
  bool layout_passed = false;
  bool icons_passed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp>
CreateAlloyTabStripProbe(std::shared_ptr<AlloyTabStripProbeResult> result);
