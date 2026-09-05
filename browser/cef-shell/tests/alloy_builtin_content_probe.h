#pragma once

#include <memory>

#include "include/cef_app.h"

struct AlloyBuiltinContentProbeResult final {
  bool new_tab_passed = false;
  bool mdv_runtime_passed = false;
  bool edit_save_passed = false;
  bool conflict_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyBuiltinContentProbe(
    std::shared_ptr<AlloyBuiltinContentProbeResult> result);
