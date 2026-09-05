#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloySecurityProbeResult final {
  bool certificate_deny_passed = false;
  bool certificate_once_passed = false;
  bool permission_prompt_passed = false;
  bool external_protocol_blocked = false;
  bool external_protocol_denied = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp>
CreateAlloySecurityProbe(std::string fixture_url,
                         std::shared_ptr<AlloySecurityProbeResult> result);
