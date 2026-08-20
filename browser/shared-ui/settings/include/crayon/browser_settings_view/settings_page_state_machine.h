#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace crayon::browser_settings_view {

/// Closed settings sections, mirroring the browser-design-v1 settings IA.
enum class SettingsSection {
  kStartup = 0,
  kAppearance,
  kSearch,
  kDownloads,
  kPrivacy,
};

constexpr bool IsValid(SettingsSection section) noexcept {
  switch (section) {
    case SettingsSection::kStartup:
    case SettingsSection::kAppearance:
    case SettingsSection::kSearch:
    case SettingsSection::kDownloads:
    case SettingsSection::kPrivacy:
      return true;
  }
  return false;
}

/// Platform-neutral view model for the settings page.
///
/// Tracks the open section, unsaved-change (dirty) state and reset
/// confirmation; actual preference values live in the domain store.
/// Thread contract: single-threaded, UI thread only.
class SettingsPageStateMachine final {
 public:
  SettingsPageStateMachine() = default;

  /// Opens a section.  Unknown sections are rejected.
  bool OpenSection(SettingsSection section) noexcept;
  SettingsSection current_section() const noexcept { return current_; }

  /// Marks unapplied edits / clears them after apply.
  void MarkDirty() noexcept;
  void ClearDirty() noexcept;
  bool dirty() const noexcept { return dirty_; }

  /// Two-step reset: request, then confirm.  Expiry/cancellation drops the
  /// pending reset without side effects.
  bool RequestReset() noexcept;
  bool ConfirmReset() noexcept;
  void CancelReset() noexcept;
  bool reset_pending() const noexcept { return reset_pending_; }

  bool active() const noexcept { return active_; }

  /// Clears all state and rejects every subsequent command.
  void Shutdown() noexcept;

 private:
  SettingsSection current_ = SettingsSection::kStartup;
  bool dirty_ = false;
  bool reset_pending_ = false;
  bool active_ = true;
};

}  // namespace crayon::browser_settings_view
