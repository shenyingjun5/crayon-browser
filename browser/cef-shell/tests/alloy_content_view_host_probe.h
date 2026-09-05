#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyContentViewHostProbeResult final {
  bool behavior_passed = false;
  bool browsers_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyContentViewHostProbe(
    std::shared_ptr<AlloyContentViewHostProbeResult> result);
