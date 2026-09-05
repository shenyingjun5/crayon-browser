#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

struct AlloyPageToolsProbeResult final {
  bool find_passed = false;
  bool zoom_passed = false;
  bool fullscreen_passed = false;
  bool pdf_passed = false;
  bool pdf_fencing_passed = false;
  bool capability_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp>
CreateAlloyPageToolsProbe(std::string fixture_url,
                          std::shared_ptr<AlloyPageToolsProbeResult> result);
