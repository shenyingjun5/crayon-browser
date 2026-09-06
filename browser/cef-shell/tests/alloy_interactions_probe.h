#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyInteractionsProbeResult final {
  bool menu_passed = false;
  bool command_passed = false;
  bool drag_passed = false;
  bool context_menu_passed = false;
  bool activity_passed = false;
  bool lifecycle_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyInteractionsProbe(
    std::shared_ptr<AlloyInteractionsProbeResult> result);
