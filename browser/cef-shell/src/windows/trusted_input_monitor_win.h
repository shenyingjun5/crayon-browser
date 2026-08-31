#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <memory>

namespace crayon::browser::cef_shell::windows {

// Browser-UI-thread mouse adapter. The low-level hook reports mouse-down input
// only when the screen point belongs to a window in this process and leaves
// the event unchanged. Start/Stop and the callback run on the owning UI thread.
class TrustedInputMonitorWin final {
 public:
  using Callback = std::function<void()>;

  TrustedInputMonitorWin();
  ~TrustedInputMonitorWin();

  bool Start(Callback callback);
  void Stop();

  static bool IsTrustedMouseDown(WPARAM message, const MSLLHOOKSTRUCT& mouse,
                                 HWND target_window);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::windows
