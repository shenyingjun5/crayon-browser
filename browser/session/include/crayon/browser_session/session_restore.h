#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace crayon::browser_session {

/// Maximum persisted windows per profile (bounded store).
inline constexpr std::size_t kMaxWindowsPerProfile = 32;
/// Maximum tabs recorded for one window.
inline constexpr std::size_t kMaxTabsPerWindow = 64;
/// Maximum profiles with retained records.
inline constexpr std::size_t kMaxProfiles = 16;
/// Maximum profile/window id token length, in bytes.
inline constexpr std::size_t kMaxIdLen = 128;

/// Closed window kinds; incognito windows are never recorded or
/// restored (UX-014: 无痕不恢复).
enum class WindowKind { kRegular = 0, kIncognito };

/// Closed startup policies, mirroring the preference store values.
enum class StartupPolicy { kNewTab = 0, kRestore };

/// Closed restore outcome for one profile.
enum class RestoreDecision { kNewTabOnly = 0, kRestoreRecorded, kRestoreAfterCrash };

/// One recorded window of a regular browsing session.
struct RecordedWindow {
  std::string window_id;
  std::size_t tab_count;
};

/// Records and plans per-profile session restore (UX-014: 按策略恢复、
/// 无痕不恢复、旧 session 拒绝、跨 Profile 零污染).  This is the
/// platform-neutral view model; disk persistence belongs to the engine
/// adapter.  Thread contract: single-threaded, UI thread only.
class SessionRestoreCoordinator final {
 public:
  /// Validates an id token: non-empty, closed charset `[A-Za-z0-9._-]`,
  /// bounded.
  static bool IsValidId(const std::string& id);

  SessionRestoreCoordinator() = default;

  /// Records a window for a profile.  Incognito windows are refused
  /// (they must never enter the restore set); overflow fails closed.
  bool RecordWindow(const std::string& profile_id,
                    const std::string& window_id,
                    std::size_t tab_count,
                    WindowKind kind);

  /// Marks a checkpoint: everything recorded so far is clean and may be
  /// restored after a crash; windows recorded after the last checkpoint
  /// are dropped on crash recovery.
  void Checkpoint(const std::string& profile_id);

  /// Marks the profile's last exit as a crash.
  void MarkCrashedLastExit(const std::string& profile_id);

  /// Decides the restore plan for a profile under `policy`.
  RestoreDecision PlanRestore(const std::string& profile_id, StartupPolicy policy) const;

  /// Returns the restorable window set: under crash recovery only
  /// checkpointed windows survive, and the dropped count is reported.
  std::vector<RecordedWindow> RestorableWindows(const std::string& profile_id,
                                                bool after_crash,
                                                std::size_t* dropped) const;

  /// Session epochs: bumped at every restore; results delivered for a
  /// stale epoch must be rejected by the caller (旧 session).
  std::uint64_t AdvanceEpoch(const std::string& profile_id);
  bool IsCurrentEpoch(const std::string& profile_id, std::uint64_t epoch) const;

  /// Drops all records for one profile (profile destroyed / cleanup).
  std::size_t ClearProfile(const std::string& profile_id);

  std::size_t recorded_window_count(const std::string& profile_id) const;

 private:
  struct ProfileRecord {
    std::vector<RecordedWindow> checkpointed;
    std::vector<RecordedWindow> tail;
    bool crashed_last_exit{false};
    std::uint64_t epoch{0};
  };

  ProfileRecord* Find(const std::string& profile_id);
  const ProfileRecord* Find(const std::string& profile_id) const;

  std::unordered_map<std::string, ProfileRecord> records_;
};

}  // namespace crayon::browser_session
