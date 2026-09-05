#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyOmniboxProbeResult final {
  bool behavior_passed = false;
  bool real_input_passed = false;
  bool generation_passed = false;
  bool display_safety_passed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp>
CreateAlloyOmniboxProbe(std::shared_ptr<AlloyOmniboxProbeResult> result);
