#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyCastOverlayMacProbeResult final {
  bool placement_passed = false;
  bool click_passed = false;
  bool expiry_passed = false;
  bool unsupported_passed = false;
  bool duplicate_passed = false;
  bool picker_passed = false;
  bool detach_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyCastOverlayMacProbe(
    std::shared_ptr<AlloyCastOverlayMacProbeResult> result);
