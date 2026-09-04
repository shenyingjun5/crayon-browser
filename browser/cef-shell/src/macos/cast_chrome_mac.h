#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "crayon/browser_cast_view/cast_ui_coordinator.h"

namespace crayon::browser::cef_shell::macos {

struct CastChromePresentation final {
  bool cast_code_pending = false;
  bool cast_code_failed = false;
  bool control_pending = false;
  bool control_failed = false;
  bool playback_paused = false;
};

struct CastChromeStrings final {
  std::string button_select;
  std::string button_stop;
  std::string picker_title;
  std::string picker_empty;
  std::string picker_select;
  std::string picker_refresh;
  std::string picker_cancel;
  std::string cast_code_label;
  std::string cast_code_connect;
  std::string cast_code_failed;
  std::string playback_pause;
  std::string playback_resume;
  std::string playback_seek;
  std::string playback_seconds;
  std::string playback_failed;
  std::string rejected;
  std::string rejected_no_route;
  std::string rejected_drm;
  std::string retry;
};

struct CastChromeCallbacks final {
  std::function<bool()> activate;
  std::function<bool()> refresh;
  std::function<void()> cancel;
  std::function<bool(const std::string&)> select;
  std::function<bool(std::string)> connect_cast_code;
  std::function<bool(bool)> set_paused;
  std::function<bool(std::uint64_t)> seek;
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
  void Render(const browser_cast_view::CastUiCoordinator& coordinator,
              CastChromePresentation presentation = {});
  void Close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::macos
