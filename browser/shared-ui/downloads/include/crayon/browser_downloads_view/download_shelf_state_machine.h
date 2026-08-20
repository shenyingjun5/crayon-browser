#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_downloads/download_item.h"

namespace crayon::browser_downloads_view {

/// Maximum number of download projections kept for the shelf/page UI.
inline constexpr std::size_t kMaxVisibleDownloads = 64;

/// Read-only projection of one download for the shelf/page UI.
struct DownloadProjection final {
  std::uint64_t download_id = 0;
  std::string display_name;
  crayon::browser_downloads::DownloadState state =
      crayon::browser_downloads::DownloadState::kInProgress;
  unsigned percent = 0;  // 0..100; 0 when total is unknown.
};

/// Platform-neutral view model backing the download shelf and downloads
/// page.
///
/// It only consumes projections forwarded from the domain layer; it holds
/// no file paths, engine handles or network state.  Thread contract:
/// single-threaded, engine UI thread only.
class DownloadShelfStateMachine final {
 public:
  DownloadShelfStateMachine() = default;

  // --- Shelf visibility ---
  void OpenShelf() noexcept {
    if (active_) {
      shelf_open_ = true;
    }
  }
  void CloseShelf() noexcept { shelf_open_ = false; }
  bool shelf_open() const noexcept { return shelf_open_; }

  // --- Projection events (forwarded from the domain layer) ---

  /// Adds a projection.  Rejects duplicate IDs and capacity overflow;
  /// adding an item implicitly opens the shelf.
  bool OnDownloadStarted(const DownloadProjection& projection);

  /// Replaces the projection for `download_id`; unknown IDs are ignored.
  bool OnDownloadUpdated(const DownloadProjection& projection);

  /// Drops the projection for `download_id`; unknown IDs are ignored.
  /// Removing an in-progress entry is the UI-side release after failure
  /// or cancellation handled by the domain layer.
  bool OnDownloadRemoved(std::uint64_t download_id) noexcept;

  /// Removes all completed/cancelled projections.  Returns the count.
  std::size_t ClearCompleted() noexcept;

  // --- Queries ---
  const std::vector<DownloadProjection>& items() const noexcept {
    return items_;
  }
  const DownloadProjection* Find(std::uint64_t download_id) const noexcept;
  std::size_t active_count() const noexcept;
  bool active() const noexcept { return active_; }

  /// Clears all state and rejects every subsequent event.
  void Shutdown() noexcept;

 private:
  static bool IsFinished(
      crayon::browser_downloads::DownloadState state) noexcept;

  std::vector<DownloadProjection> items_;
  bool shelf_open_ = false;
  bool active_ = true;
};

}  // namespace crayon::browser_downloads_view
