#pragma once
#include "include/cef_app.h"
#include <memory>

struct CastEntrySurfaceProbeResult {
  bool behavior_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};
// Independent fixture only; never linked into the product or used as playback
// proof.
CefRefPtr<CefApp> CreateCastEntrySurfaceProbe(
    std::shared_ptr<CastEntrySurfaceProbeResult> result);
