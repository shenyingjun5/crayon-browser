#include "windows/trusted_input_monitor_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
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

TrustedExternalProtocolInput::TrustedExternalProtocolInput(
    std::uint64_t lifetime_ms)
    : lifetime_ms_(lifetime_ms) {}

bool TrustedExternalProtocolInput::Note(
    std::uint32_t tab_id, std::uint64_t navigation_generation,
    std::string canonical_origin, std::uint64_t now_ms) {
  input_.reset();
  intent_.reset();
  if (lifetime_ms_ == 0 || tab_id == 0 || navigation_generation == 0 ||
      canonical_origin.empty()) {
    return false;
  }
  input_ = Input{tab_id, navigation_generation, std::move(canonical_origin),
                 now_ms};
  return true;
}

bool TrustedExternalProtocolInput::Arm(
    std::uint32_t tab_id, std::uint64_t navigation_generation,
    std::string_view canonical_origin, std::string target_url,
    bool browser_user_gesture, std::uint64_t now_ms) {
  intent_.reset();
  auto input = std::move(input_);
  input_.reset();
  if (!input || !browser_user_gesture || tab_id != input->tab_id ||
      navigation_generation != input->navigation_generation ||
      canonical_origin != input->canonical_origin ||
      !IsSupportedTarget(target_url) || !IsFresh(input->at_ms, now_ms)) {
    return false;
  }
  intent_ = Intent{std::move(*input), std::move(target_url)};
  return true;
}

std::optional<std::uint64_t> TrustedExternalProtocolInput::Consume(
    std::uint32_t tab_id, std::string_view canonical_origin,
    std::string_view target_url, std::uint64_t now_ms) {
  auto intent = std::move(intent_);
  intent_.reset();
  if (!intent || tab_id != intent->source.tab_id ||
      canonical_origin != intent->source.canonical_origin ||
      target_url != intent->target_url ||
      !IsFresh(intent->source.at_ms, now_ms)) {
    return std::nullopt;
  }
  return intent->source.navigation_generation;
}

void TrustedExternalProtocolInput::Reset() {
  input_.reset();
  intent_.reset();
}

bool TrustedExternalProtocolInput::IsFresh(std::uint64_t at_ms,
                                           std::uint64_t now_ms) const
    noexcept {
  return at_ms <= now_ms && now_ms - at_ms <= lifetime_ms_;
}

bool TrustedExternalProtocolInput::IsSupportedTarget(
    std::string_view target_url) noexcept {
  constexpr std::size_t kMaximumTargetBytes = 512;
  constexpr std::array<std::string_view, 3> kPrefixes = {"mailto:", "tel:",
                                                         "sms:"};
  if (target_url.empty() || target_url.size() > kMaximumTargetBytes) {
    return false;
  }
  for (const auto prefix : kPrefixes) {
    if (target_url.rfind(prefix, 0) == 0) return true;
  }
  return false;
}

}  // namespace crayon::browser::cef_shell::windows
