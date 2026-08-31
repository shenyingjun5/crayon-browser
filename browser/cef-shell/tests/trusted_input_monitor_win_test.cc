#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "windows/trusted_input_monitor_win.h"

#include <windows.h>

#include <iostream>

namespace {

bool TrustedMouseLifecycle() {
  HWND window = CreateWindowExW(0, L"STATIC", L"Trusted Input Test",
                                WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window) return false;

  int inputs = 0;
  crayon::browser::cef_shell::windows::TrustedInputMonitorWin monitor;
  if (!monitor.Start([&inputs] { ++inputs; }) ||
      monitor.Start([&inputs] { ++inputs; })) {
    DestroyWindow(window);
    return false;
  }
  MSLLHOOKSTRUCT mouse{};
  const bool accepts_owned_mouse =
      decltype(monitor)::IsTrustedMouseDown(WM_LBUTTONDOWN, mouse, window);
  mouse.flags = LLMHF_INJECTED;
  const bool rejects_injected =
      !decltype(monitor)::IsTrustedMouseDown(WM_LBUTTONDOWN, mouse, window);
  mouse.flags = LLMHF_LOWER_IL_INJECTED;
  const bool rejects_lower_integrity =
      !decltype(monitor)::IsTrustedMouseDown(WM_LBUTTONDOWN, mouse, window);
  mouse.flags = 0;
  const bool rejects_mouse_up =
      !decltype(monitor)::IsTrustedMouseDown(WM_LBUTTONUP, mouse, window);
  const bool rejects_foreign_window = !decltype(monitor)::IsTrustedMouseDown(
      WM_LBUTTONDOWN, mouse, GetDesktopWindow());

  monitor.Stop();
  monitor.Stop();
  DestroyWindow(window);
  return accepts_owned_mouse && rejects_injected && rejects_lower_integrity &&
         rejects_mouse_up && rejects_foreign_window && inputs == 0;
}

}  // namespace

int main() {
  if (!TrustedMouseLifecycle()) return 1;
  std::cout << "trusted_input_monitor_win_test passed\n";
  return 0;
}
