#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloyCastOverlayProbeResult final {
  bool geometry_passed = false;
  bool native_surface_passed = false;
  bool keyboard_intent_passed = false;
  bool stale_and_focus_passed = false;
  bool occlusion_and_navigation_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyCastOverlayProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyCastOverlayProbeResult> result);
