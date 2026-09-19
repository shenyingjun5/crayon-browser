#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyCastToolbarMacProbeResult {
  bool passed = false;
  bool closed = false;
};

CefRefPtr<CefApp> CreateAlloyCastToolbarMacProbe(
    std::shared_ptr<AlloyCastToolbarMacProbeResult> result);
