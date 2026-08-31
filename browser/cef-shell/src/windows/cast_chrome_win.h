#pragma once

#include <functional>
#include <memory>
#include <string>

#include "crayon/browser_cast_view/cast_ui_coordinator.h"

namespace crayon::browser::cef_shell::windows {

struct CastChromeStrings final {
  std::wstring button_select;
  std::wstring button_stop;
  std::wstring picker_title;
  std::wstring picker_empty;
  std::wstring picker_select;
  std::wstring picker_refresh;
  std::wstring picker_cancel;
};

struct CastChromeCallbacks final {
  std::function<bool()> activate;
  std::function<bool()> refresh;
  std::function<void()> cancel;
  std::function<bool(const std::string&)> select;
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
  void Render(const browser_cast_view::CastUiCoordinator& coordinator);
  void Close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::windows
