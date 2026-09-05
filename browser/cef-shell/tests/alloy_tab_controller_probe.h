#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloyTabControllerProbeResult final {
  bool behavior_passed = false;
  bool close_cancelled = false;
  bool late_create_closed = false;
  bool renderer_crash_closed = false;
  bool advanced_state_passed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyTabControllerProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyTabControllerProbeResult> result);
