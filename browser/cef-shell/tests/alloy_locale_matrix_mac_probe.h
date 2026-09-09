#pragma once

#include <memory>
#include <string>

#include "include/cef_app.h"

// PLT-SHELL-23M: per-locale localization consistency probe. One process per
// locale; the scenario name is "alloy-loc-<TAG>" with TAG in {zh-CN, en-US,
// zh-TW}.
struct AlloyLocaleMatrixMacProbeResult final {
  bool new_tab_passed = false;
  bool browser_closed = false;
  bool window_closed = false;
};

CefRefPtr<CefApp> CreateAlloyLocaleMatrixMacProbe(
    std::string locale_tag,
    std::shared_ptr<AlloyLocaleMatrixMacProbeResult> result);
