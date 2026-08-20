#pragma once

#include <cstdint>
#include <string>

namespace crayon::browser_downloads {

/// Lifecycle state of a single download item.
enum class DownloadState {
  kPendingDangerConfirm = 0,  // Dangerous target awaiting user decision.
  kInProgress,
  kPaused,
  kCompleted,
  kFailed,
  kCancelled,  // Terminal.
};

constexpr bool IsValid(DownloadState state) noexcept {
  switch (state) {
    case DownloadState::kPendingDangerConfirm:
    case DownloadState::kInProgress:
    case DownloadState::kPaused:
    case DownloadState::kCompleted:
    case DownloadState::kFailed:
    case DownloadState::kCancelled:
      return true;
  }
  return false;
}

/// Danger classification of a download target file name.
enum class DownloadDanger {
  kSafe = 0,
  kDangerous,  // Executable/script-like extension; needs confirmation.
};

/// Classifies a file name by a closed set of executable/script extensions.
/// The check is case-insensitive and operates on the final extension only.
DownloadDanger ClassifyDownloadDanger(const std::string& file_name) noexcept;

/// Platform-neutral state machine for one download item.
///
/// Owns only state transitions and progress bounds; the CEF download
/// handler adapter drives it and performs actual file/network work.
/// Thread contract: single-threaded, engine UI thread only.
class DownloadItem final {
 public:
  /// Creates an item.  Dangerous file names start in
  /// `kPendingDangerConfirm`, safe ones in `kInProgress`.
  /// `target_file_name` must already be sanitized with
  /// `SanitizeDownloadFileName`; this class does not re-validate it.
  static DownloadItem Create(std::uint64_t download_id,
                             std::string target_file_name);

  std::uint64_t download_id() const noexcept { return download_id_; }
  DownloadState state() const noexcept { return state_; }
  const std::string& target_file_name() const { return target_file_name_; }
  std::uint64_t received_bytes() const noexcept { return received_bytes_; }
  std::uint64_t total_bytes() const noexcept { return total_bytes_; }
  bool terminal() const noexcept {
    return state_ == DownloadState::kCompleted ||
           state_ == DownloadState::kCancelled;
  }

  /// User confirms a dangerous download: pending -> in progress.
  bool ConfirmDangerous() noexcept;

  /// User discards a dangerous download: pending -> cancelled.
  bool DiscardDangerous() noexcept;

  /// Records progress.  Rejects non-in-progress states and out-of-bounds
  /// byte counts (received > total when total is known).
  bool OnProgress(std::uint64_t received_bytes,
                  std::uint64_t total_bytes) noexcept;

  bool Pause() noexcept;    // in progress -> paused
  bool Resume() noexcept;   // paused -> in progress
  bool Cancel() noexcept;   // active states -> cancelled (terminal)
  bool MarkFailed() noexcept;  // in progress/paused -> failed
  bool Retry() noexcept;    // failed -> in progress (progress reset)

  /// Completes the download.  Requires in progress and, when a total is
  /// known, received == total.
  bool Complete() noexcept;

  /// User-facing post-completion actions; only valid once completed.
  bool CanOpenItem() const noexcept;
  bool CanOpenLocation() const noexcept;

 private:
  DownloadItem(std::uint64_t download_id, std::string target_file_name,
               DownloadState initial_state);

  std::uint64_t download_id_;
  std::string target_file_name_;
  DownloadState state_;
  std::uint64_t received_bytes_ = 0;
  std::uint64_t total_bytes_ = 0;  // 0 means total unknown.
};

}  // namespace crayon::browser_downloads
