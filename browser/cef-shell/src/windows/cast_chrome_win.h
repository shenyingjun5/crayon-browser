#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "crayon/browser_cast_view/cast_ui_coordinator.h"

namespace crayon::browser::cef_shell::windows {

struct CastChromePresentation final {
  bool cast_code_pending = false;
  bool cast_code_failed = false;
  bool control_pending = false;
  bool control_failed = false;
  bool playback_paused = false;

  friend bool operator==(const CastChromePresentation& left,
                         const CastChromePresentation& right) {
    return left.cast_code_pending == right.cast_code_pending &&
           left.cast_code_failed == right.cast_code_failed &&
           left.control_pending == right.control_pending &&
           left.control_failed == right.control_failed &&
           left.playback_paused == right.playback_paused;
  }
  friend bool operator!=(const CastChromePresentation& left,
                         const CastChromePresentation& right) {
    return !(left == right);
  }
};

struct CastChromeStrings final {
  std::wstring button_select;
  std::wstring button_stop;
  std::wstring picker_title;
  std::wstring picker_empty;
  std::wstring picker_select;
  std::wstring picker_refresh;
  std::wstring picker_cancel;
  std::wstring cast_code_label;
  std::wstring cast_code_connect;
  std::wstring cast_code_failed;
  std::wstring playback_pause;
  std::wstring playback_resume;
  std::wstring playback_seek;
  std::wstring playback_seconds;
  std::wstring playback_failed;
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

// Win32 adapter for the browser-owned Cast surface. It overlays one native,
// accessible button in the Chrome window and owns a modeless receiver picker.
// Callbacks only return to the shared UI controller; this class performs no
// receiver protocol, media delivery or pipe I/O.
class CastChromeWin final {
 public:
  CastChromeWin(CastChromeStrings strings, CastChromeCallbacks callbacks);
  ~CastChromeWin();

  bool AttachWindow(int browser_id, void* native_window);
  void DetachWindow(int browser_id);
  void SetActiveWindow(int browser_id);
  void Render(const browser_cast_view::CastUiCoordinator& coordinator,
              CastChromePresentation presentation = {});
  void Close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::windows
