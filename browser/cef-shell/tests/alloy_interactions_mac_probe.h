#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyInteractionsMacProbeResult final {
  bool attach_passed = false;
  bool native_menu_passed = false;
  bool context_passed = false;
  bool drag_passed = false;
  bool commands_passed = false;
  bool fencing_passed = false;
  bool shutdown_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyInteractionsMacProbe(
    std::shared_ptr<AlloyInteractionsMacProbeResult> result);
