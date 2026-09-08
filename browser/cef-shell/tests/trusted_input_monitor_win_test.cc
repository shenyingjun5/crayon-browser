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

bool ExternalProtocolGenerationHandoff() {
  using crayon::browser::cef_shell::windows::TrustedExternalProtocolInput;
  constexpr std::string_view kOrigin = "http://127.0.0.1:8766";
  constexpr std::string_view kTarget = "mailto:alloy-security@example.test";
  TrustedExternalProtocolInput input(2'000);
  if (!input.Note(7, 11, std::string(kOrigin), 100) ||
      !input.Arm(7, 11, kOrigin, std::string(kTarget), true, 150)) {
    return false;
  }

  // OnProtocolExecution may run after the tab has begun the failed external
  // navigation. Consumption must return the bound source generation instead
  // of comparing it with that newer tab generation.
  const auto source_generation = input.Consume(7, kOrigin, kTarget, 200);
  const bool generation_handoff = source_generation == 11;
  const bool one_shot = !input.Consume(7, kOrigin, kTarget, 201);

  const bool programmatic_rejected =
      input.Note(7, 12, std::string(kOrigin), 300) &&
      !input.Arm(7, 12, kOrigin, std::string(kTarget), false, 301) &&
      !input.Consume(7, kOrigin, kTarget, 302);
  const bool stale_rejected =
      input.Note(7, 13, std::string(kOrigin), 400) &&
      !input.Arm(7, 13, kOrigin, std::string(kTarget), true, 2'401);
  const bool mismatch_consumes =
      input.Note(7, 14, std::string(kOrigin), 500) &&
      input.Arm(7, 14, kOrigin, std::string(kTarget), true, 501) &&
      !input.Consume(8, kOrigin, kTarget, 502) &&
      !input.Consume(7, kOrigin, kTarget, 503);
  const bool unsupported_rejected =
      input.Note(7, 15, std::string(kOrigin), 600) &&
      !input.Arm(7, 15, kOrigin, "file:///tmp/blocked", true, 601);
  return generation_handoff && one_shot && programmatic_rejected &&
         stale_rejected && mismatch_consumes && unsupported_rejected;
}

}  // namespace

int main() {
  if (!TrustedMouseLifecycle() || !ExternalProtocolGenerationHandoff())
    return 1;
  std::cout << "trusted_input_monitor_win_test passed\n";
  return 0;
}
