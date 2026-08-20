#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crayon::browser_bookmarks_view {

/// Maximum number of items projected onto the bookmark bar.
inline constexpr std::size_t kMaxBarItems = 128;

/// Read-only projection of one bar item (folder or bookmark).
struct BookmarkBarItem final {
  std::uint64_t node_id = 0;
  std::string title;
  bool is_folder = false;
};

/// Platform-neutral view model for the bookmark bar.
///
/// Holds only bounded (id, title, kind) projections; URLs, file paths and
/// page data stay in the domain layer.  Thread contract: single-threaded,
/// UI thread only.
class BookmarkBarStateMachine final {
 public:
  BookmarkBarStateMachine() = default;

  // --- Visibility ---
  void ShowBar() noexcept;
  void HideBar() noexcept;
  bool bar_visible() const noexcept { return visible_; }

  // --- Projection events (forwarded from the domain layer) ---

  /// Replaces the whole projection.  Rejects oversize projections and
  /// invalid entries (zero ID or empty title) without side effects.
  bool SetItems(std::vector<BookmarkBarItem> items);

  const std::vector<BookmarkBarItem>& items() const noexcept {
    return items_;
  }

  // --- Current-page starred state ---

  /// Records which bookmark (if any) the current page is saved as.
  void SetCurrentPageBookmark(std::optional<std::uint64_t> node_id) noexcept;
  std::optional<std::uint64_t> current_page_bookmark() const noexcept {
    return current_page_bookmark_;
  }
  bool current_page_starred() const noexcept {
    return current_page_bookmark_.has_value();
  }

  /// Clears all state and rejects every subsequent event.
  void Shutdown() noexcept;

  bool active() const noexcept { return active_; }

 private:
  std::vector<BookmarkBarItem> items_;
  std::optional<std::uint64_t> current_page_bookmark_;
  bool visible_ = false;
  bool active_ = true;
};

}  // namespace crayon::browser_bookmarks_view
