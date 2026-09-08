#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

// Converts a physical Browser-process mouse-down plus CEF's main-frame
// user-gesture signal into a bounded, one-shot external-protocol intent. The
// intent retains the source navigation generation because CEF may advance the
// tab generation before delivering OnProtocolExecution.
class TrustedExternalProtocolInput final {
 public:
  explicit TrustedExternalProtocolInput(std::uint64_t lifetime_ms);

  bool Note(std::uint32_t tab_id, std::uint64_t navigation_generation,
            std::string canonical_origin, std::uint64_t now_ms);
  bool Arm(std::uint32_t tab_id, std::uint64_t navigation_generation,
           std::string_view canonical_origin, std::string target_url,
           bool browser_user_gesture, std::uint64_t now_ms);
  std::optional<std::uint64_t>
  Consume(std::uint32_t tab_id, std::string_view canonical_origin,
          std::string_view target_url, std::uint64_t now_ms);
  void Reset();

 private:
  struct Input final {
    std::uint32_t tab_id = 0;
    std::uint64_t navigation_generation = 0;
    std::string canonical_origin;
    std::uint64_t at_ms = 0;
  };
  struct Intent final {
    Input source;
    std::string target_url;
  };

  bool IsFresh(std::uint64_t at_ms, std::uint64_t now_ms) const noexcept;
  static bool IsSupportedTarget(std::string_view target_url) noexcept;

  std::uint64_t lifetime_ms_ = 0;
  std::optional<Input> input_;
  std::optional<Intent> intent_;
};

}  // namespace crayon::browser::cef_shell::windows
