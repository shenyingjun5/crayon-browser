#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloyProfileContextProbeResult final {
  bool context_isolation_passed = false;
  bool cookie_isolation_passed = false;
  bool browsers_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyProfileContextProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyProfileContextProbeResult> result);
