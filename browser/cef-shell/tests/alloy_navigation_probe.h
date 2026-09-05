#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloyNavigationProbeResult final {
  bool behavior_passed = false;
  bool real_navigation_passed = false;
  bool identity_passed = false;
  bool fencing_passed = false;
  bool bookmark_passed = false;
  bool history_passed = false;
  bool download_passed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp>
CreateAlloyNavigationProbe(std::string fixture_url,
                           std::shared_ptr<AlloyNavigationProbeResult> result);
