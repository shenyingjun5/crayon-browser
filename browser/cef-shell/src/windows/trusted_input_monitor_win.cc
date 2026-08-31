#include "windows/trusted_input_monitor_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <utility>

namespace crayon::browser::cef_shell::windows {
namespace {

thread_local TrustedInputMonitorWin::Callback* active_callback = nullptr;

LRESULT CALLBACK MouseHook(int code, WPARAM wparam, LPARAM lparam) {
  if (code == HC_ACTION && lparam && active_callback && *active_callback) {
    const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
    const HWND target = WindowFromPoint(mouse->pt);
    if (TrustedInputMonitorWin::IsTrustedMouseDown(wparam, *mouse, target)) {
      (*active_callback)();
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

}  // namespace

struct TrustedInputMonitorWin::Impl final {
  HHOOK hook = nullptr;
  Callback callback;
};

TrustedInputMonitorWin::TrustedInputMonitorWin()
    : impl_(std::make_unique<Impl>()) {}

TrustedInputMonitorWin::~TrustedInputMonitorWin() { Stop(); }

bool TrustedInputMonitorWin::Start(Callback callback) {
  if (impl_->hook || active_callback || !callback) return false;
  impl_->callback = std::move(callback);
  active_callback = &impl_->callback;
  impl_->hook =
      SetWindowsHookExW(WH_MOUSE_LL, MouseHook, GetModuleHandleW(nullptr), 0);
  if (!impl_->hook) {
    active_callback = nullptr;
    impl_->callback = {};
    return false;
  }
  return true;
}

void TrustedInputMonitorWin::Stop() {
  if (impl_->hook) {
    UnhookWindowsHookEx(impl_->hook);
    impl_->hook = nullptr;
  }
  if (active_callback == &impl_->callback) active_callback = nullptr;
  impl_->callback = {};
}

bool TrustedInputMonitorWin::IsTrustedMouseDown(WPARAM message,
                                                const MSLLHOOKSTRUCT& mouse,
                                                HWND target_window) {
  const bool is_mouse_down =
      message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
      message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
  if (!is_mouse_down ||
      (mouse.flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) != 0 ||
      !target_window) {
    return false;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(target_window, &process_id);
  return process_id == GetCurrentProcessId();
}

}  // namespace crayon::browser::cef_shell::windows
