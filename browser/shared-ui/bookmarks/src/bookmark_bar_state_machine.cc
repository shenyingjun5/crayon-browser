#include "crayon/browser_bookmarks_view/bookmark_bar_state_machine.h"

#include <algorithm>

namespace crayon::browser_bookmarks_view {

void BookmarkBarStateMachine::ShowBar() noexcept {
  if (active_) {
    visible_ = true;
  }
}

void BookmarkBarStateMachine::HideBar() noexcept {
  visible_ = false;
}

bool BookmarkBarStateMachine::SetItems(std::vector<BookmarkBarItem> items) {
  if (!active_ || items.size() > kMaxBarItems) {
    return false;
  }
  const bool valid = std::all_of(
      items.begin(), items.end(),
      [](const BookmarkBarItem& item) {
        return item.node_id != 0 && !item.title.empty();
      });
  if (!valid) {
    return false;
  }
  items_ = std::move(items);
  return true;
}

void BookmarkBarStateMachine::SetCurrentPageBookmark(
    std::optional<std::uint64_t> node_id) noexcept {
  if (active_) {
    current_page_bookmark_ = node_id;
  }
}

void BookmarkBarStateMachine::Shutdown() noexcept {
  active_ = false;
  items_.clear();
  current_page_bookmark_.reset();
  visible_ = false;
}

}  // namespace crayon::browser_bookmarks_view
