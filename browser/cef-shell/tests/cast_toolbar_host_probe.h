#pragma once

#include <memory>

#include "include/cef_app.h"

struct CastToolbarHostProbeResult {
  bool layout_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
  bool cancellation_verified = false;
};

// Test-target only. Creates no product cast command or media playback proof.
CefRefPtr<CefApp>
CreateCastToolbarHostProbe(std::shared_ptr<CastToolbarHostProbeResult> result,
                           bool verify_close_cancellation = false);
