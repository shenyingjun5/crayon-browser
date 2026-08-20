#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "crayon/browser_windows/popup_policy.h"

namespace crayon::browser_windows {

/// Maximum number of simultaneously open windows (normal + popup).
inline constexpr std::size_t kMaxWindows = 8;

/// Maximum length of a window identifier in bytes.
inline constexpr std::size_t kMaxWindowIdLength = 64;

/// Kind of a managed window.
enum class WindowKind {
  kNormal = 0,
  kPopup,
};

constexpr bool IsValid(WindowKind kind) noexcept {
  switch (kind) {
    case WindowKind::kNormal:
    case WindowKind::kPopup:
      return true;
  }
  return false;
}

/// Display mode of a window.  Fullscreen and picture-in-picture are mutually
/// exclusive; entering a new mode requires exiting the current one first.
enum class WindowMode {
  kNormal = 0,
  kFullscreen,
  kPictureInPicture,
};

constexpr bool IsValid(WindowMode mode) noexcept {
  switch (mode) {
    case WindowMode::kNormal:
    case WindowMode::kFullscreen:
    case WindowMode::kPictureInPicture:
      return true;
  }
  return false;
}

/// Platform-neutral owner of the browser window set.
///
/// Tracks window creation, focus recency, popup association, fullscreen and
/// picture-in-picture modes.  Actual platform windows are created by the
/// CEF/Win32/AppKit adapter which consumes this machine's decisions; this
/// class holds no platform handles and performs no I/O.
///
/// Thread contract: single-threaded, called from the engine UI thread only
/// (same contract as the other shared-ui state machines).
class WindowStateMachine final {
 public:
  WindowStateMachine() = default;

  // --- Lifecycle commands ---

  /// Creates a normal window.  Rejects invalid IDs, duplicates and capacity
  /// overflow.  The new window becomes focused.
  bool CreateWindow(const std::string& window_id);

  /// Requests a popup window owned by `opener_window_id`.
  ///
  /// The request is classified by `PopupPolicy`; only allowed requests create
  /// a window (kind `kPopup`) and the created popup becomes focused.
  /// The decision is always observable via the return value.
  PopupDecision RequestPopup(const std::string& opener_window_id,
                             const std::string& popup_window_id,
                             PopupSource source);

  /// Closes a window.  Unknown or already-closed IDs are rejected without
  /// side effects.  Closing a window clears its mode state; closing the
  /// focused window moves focus to the most recently used normal window.
  /// Closing an opener does not cascade to its popups.
  bool CloseWindow(const std::string& window_id) noexcept;

  /// Moves focus to an existing window.
  bool FocusWindow(const std::string& window_id);

  // --- Mode commands ---

  /// Enters fullscreen.  Popup windows and windows already in another mode
  /// are rejected.
  bool EnterFullscreen(const std::string& window_id);

  /// Exits fullscreen back to `kNormal`.  Rejected when not fullscreen.
  bool ExitFullscreen(const std::string& window_id);

  /// Enters picture-in-picture.  Popup windows and windows already in
  /// another mode are rejected.  `media_active` asserts the caller observed
  /// active media in the originating tab.
  bool EnterPictureInPicture(const std::string& window_id,
                             bool media_active);

  /// Exits picture-in-picture back to `kNormal`.  Rejected when not in PiP.
  bool ExitPictureInPicture(const std::string& window_id);

  // --- Queries ---

  bool HasWindow(const std::string& window_id) const;
  bool IsPopup(const std::string& window_id) const;
  WindowMode ModeOf(const std::string& window_id) const;
  std::optional<std::string> OpenerOf(
      const std::string& window_id) const;
  std::optional<std::string> focused_window_id() const;
  std::size_t window_count() const noexcept { return windows_.size(); }
  bool has_windows() const noexcept { return !windows_.empty(); }
  bool active() const noexcept { return active_; }

  /// Removes all windows and rejects every subsequent command.
  void Shutdown() noexcept;

 private:
  struct WindowState final {
    WindowKind kind = WindowKind::kNormal;
    WindowMode mode = WindowMode::kNormal;
    std::string opener_id;  // Empty for normal windows.
  };

  WindowState* FindMutable(const std::string& window_id) noexcept;
  const WindowState* Find(const std::string& window_id) const noexcept;
  static bool IsValidWindowId(const std::string& window_id) noexcept;
  std::size_t PopupCountOf(const std::string& opener_id) const noexcept;
  void TouchFocus(const std::string& window_id);
  void RestoreFocusAfterClose(const std::string& closed_id) noexcept;
  bool InsertWindow(std::string window_id,
                    WindowKind kind,
                    std::string opener_id);

  std::unordered_map<std::string, WindowState> windows_;
  std::vector<std::string> focus_recency_;  // Most recently focused last.
  bool active_ = true;
};

}  // namespace crayon::browser_windows
