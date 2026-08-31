#pragma once

#include <functional>
#include <memory>
#include <string>

#include "crayon/browser_cast_view/cast_ui_coordinator.h"

namespace crayon::browser::cef_shell::macos {

struct CastChromeStrings final {
  std::string button_select;
  std::string button_stop;
  std::string picker_title;
  std::string picker_empty;
  std::string picker_select;
  std::string picker_refresh;
  std::string picker_cancel;
};

struct CastChromeCallbacks final {
  std::function<bool()> activate;
  std::function<bool()> refresh;
  std::function<void()> cancel;
  std::function<bool(const std::string&)> select;
};

// AppKit adapter for the browser-owned Cast surface. The titlebar accessory
// is outside the page viewport and all sheet callbacks return to the CEF UI
// thread; this class never calls a receiver protocol stack, Chromium
// MediaRouter or pipe I/O.
class CastChromeMac final {
 public:
  CastChromeMac(CastChromeStrings strings, CastChromeCallbacks callbacks);
  ~CastChromeMac();

  bool AttachWindow(int browser_id, void* native_view);
  void DetachWindow(int browser_id);
  void SetActiveWindow(int browser_id);
  void Render(const browser_cast_view::CastUiCoordinator& coordinator);
  void Close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::macos
