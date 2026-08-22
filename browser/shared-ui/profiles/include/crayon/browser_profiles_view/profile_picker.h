#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace crayon::browser_profiles_view {

/// Maximum profiles listed in the picker (bounded).
inline constexpr std::size_t kMaxProfiles = 64;
/// Maximum profile id / display-name token length, in bytes.
inline constexpr std::size_t kMaxProfileFieldLen = 128;

/// Closed profile entry kinds surfaced by the picker.
enum class ProfileEntryKind { kRegular = 0, kGuest };

/// One picker entry; ids are closed tokens, display names are bounded.
struct ProfileEntry {
  std::string id;
  std::string display_name;
  ProfileEntryKind kind{ProfileEntryKind::kRegular};
};

/// Closed picker states.
enum class PickerState { kClosed = 0, kOpen };

/// Closed outcome of a switch attempt.
enum class SwitchOutcome { kSwitched = 0, kUnknownProfile, kAlreadyActive, kBusy };

/// Profile picker view model (UX-014: Profile 切换、无痕窗口、清理失败
/// 显式报告、跨 Profile 零污染).  Incognito windows opened here are
/// ephemeral by construction: the picker never hands them to the session
/// restore coordinator.  Thread contract: single-threaded, UI thread
/// only.
class ProfilePickerModel final {
 public:
  /// Validates a closed token id / display name.
  static bool IsValidToken(const std::string& value);

  ProfilePickerModel() = default;

  /// Adds a profile entry; duplicates and overflow are rejected.
  bool AddProfile(const std::string& id,
                  const std::string& display_name,
                  ProfileEntryKind kind);

  /// Opens/closes the picker.
  bool Open() noexcept;
  void Close() noexcept;
  PickerState state() const noexcept { return state_; }

  /// Lists entries (bounded, insertion order).
  const std::vector<ProfileEntry>& entries() const noexcept { return entries_; }

  /// Switches the active profile.  Switching away with a pending
  /// cleanup failure is blocked until the failure is acknowledged
  /// (explicit reporting, never silently swallowed).
  SwitchOutcome SwitchTo(const std::string& id);

  /// Requests an incognito window for the active profile; incognito
  /// windows never enter session restore.
  bool RequestIncognitoWindow();

  /// Reports a cleanup failure for a profile (surfaced, must be
  /// acknowledged).
  bool ReportCleanupFailure(const std::string& id, const std::string& detail_token);

  /// Clears the reported failure after the user acknowledged it.
  void AcknowledgeCleanupFailure() noexcept;

  bool cleanup_failure_pending() const noexcept { return cleanup_failure_pending_; }
  const std::string& cleanup_failure_profile() const noexcept { return cleanup_profile_; }
  const std::string& active_profile() const noexcept { return active_; }

 private:
  const ProfileEntry* Find(const std::string& id) const;

  PickerState state_{PickerState::kClosed};
  std::vector<ProfileEntry> entries_;
  std::string active_;
  bool cleanup_failure_pending_{false};
  std::string cleanup_profile_;
};

}  // namespace crayon::browser_profiles_view
