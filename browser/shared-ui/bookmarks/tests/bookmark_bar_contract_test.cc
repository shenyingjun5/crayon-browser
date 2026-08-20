#include <cstdlib>
#include <iostream>
#include <vector>

#include "crayon/browser_bookmarks_view/bookmark_bar_state_machine.h"

namespace {

using crayon::browser_bookmarks_view::BookmarkBarItem;
using crayon::browser_bookmarks_view::BookmarkBarStateMachine;
using crayon::browser_bookmarks_view::kMaxBarItems;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

BookmarkBarItem MakeItem(std::uint64_t id, bool folder = false) {
  return BookmarkBarItem{id, "item-" + std::to_string(id), folder};
}

bool VisibilityToggle() {
  BookmarkBarStateMachine bar;
  CHECK(!bar.bar_visible());
  bar.ShowBar();
  CHECK(bar.bar_visible());
  bar.HideBar();
  CHECK(!bar.bar_visible());
  return true;
}

bool SetItemsValidates() {
  BookmarkBarStateMachine bar;
  CHECK(bar.SetItems({MakeItem(1), MakeItem(2, true)}));
  CHECK(bar.items().size() == 2);
  // Zero ID or empty title rejected without side effects.
  CHECK(!bar.SetItems({BookmarkBarItem{0, "bad", false}}));
  CHECK(bar.items().size() == 2);
  CHECK(!bar.SetItems({BookmarkBarItem{7, "", false}}));
  CHECK(bar.items().size() == 2);
  return true;
}

bool CapacityEnforced() {
  BookmarkBarStateMachine bar;
  std::vector<BookmarkBarItem> items;
  for (std::size_t i = 1; i <= kMaxBarItems; ++i) {
    items.push_back(MakeItem(i));
  }
  CHECK(bar.SetItems(items));
  items.push_back(MakeItem(kMaxBarItems + 1));
  CHECK(!bar.SetItems(items));
  CHECK(bar.items().size() == kMaxBarItems);
  return true;
}

bool StarredStateTracksCurrentPage() {
  BookmarkBarStateMachine bar;
  CHECK(!bar.current_page_starred());
  bar.SetCurrentPageBookmark(42);
  CHECK(bar.current_page_starred());
  CHECK(bar.current_page_bookmark() == std::optional<std::uint64_t>(42));
  bar.SetCurrentPageBookmark(std::nullopt);
  CHECK(!bar.current_page_starred());
  return true;
}

bool ShutdownRejectsEverything() {
  BookmarkBarStateMachine bar;
  bar.SetItems({MakeItem(1)});
  bar.SetCurrentPageBookmark(1);
  bar.ShowBar();
  bar.Shutdown();
  CHECK(!bar.active());
  CHECK(bar.items().empty());
  CHECK(!bar.bar_visible());
  CHECK(!bar.current_page_starred());
  CHECK(!bar.SetItems({MakeItem(2)}));
  bar.ShowBar();
  CHECK(!bar.bar_visible());
  bar.SetCurrentPageBookmark(9);
  CHECK(!bar.current_page_starred());
  return true;
}

}  // namespace

int main() {
  if (!VisibilityToggle() || !SetItemsValidates() || !CapacityEnforced() ||
      !StarredStateTracksCurrentPage() || !ShutdownRejectsEverything()) {
    return 1;
  }
  return 0;
}
