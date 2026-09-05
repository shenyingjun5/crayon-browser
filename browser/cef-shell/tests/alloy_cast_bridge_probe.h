#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyCastBridgeProbeResult {
  bool selection_passed = false;
  bool connection_passed = false;
  bool reason_passed = false;
  bool session_passed = false;
  bool accessibility_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyCastBridgeProbe(
    std::shared_ptr<AlloyCastBridgeProbeResult> result);
