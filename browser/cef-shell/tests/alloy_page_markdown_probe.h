#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloyPageMarkdownProbeResult final {
  bool context_menu_passed = false;
  bool cancellation_passed = false;
  bool preview_passed = false;
  bool export_passed = false;
  bool lifecycle_passed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyPageMarkdownProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyPageMarkdownProbeResult> result);
