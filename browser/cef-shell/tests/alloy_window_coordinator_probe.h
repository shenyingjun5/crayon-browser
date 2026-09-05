#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloyWindowCoordinatorProbeResult final {
  bool behavior_passed = false;
  bool real_popup_passed = false;
  bool policy_passed = false;
  bool isolation_passed = false;
  bool advanced_transfer_passed = false;
  bool session_restore_passed = false;
  bool windows_closed = false;
};

CefRefPtr<CefApp> CreateAlloyWindowCoordinatorProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyWindowCoordinatorProbeResult> result);
