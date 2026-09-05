#include "browser/window/alloy_bookmarks.h"

#include <algorithm>
#include <utility>

namespace crayon::browser::cef_shell::window {

AlloyBookmarks::AlloyBookmarks(browser_engine::ProfileId profile_id,
                               Callbacks callbacks)
    : profile_id_(std::move(profile_id)), callbacks_(std::move(callbacks)) {}

std::optional<std::uint64_t>
AlloyBookmarks::AddCurrentPage(std::string title, std::string url) {
  if (!active_) {
    return std::nullopt;
  }
  const std::uint64_t id = store_.AddBookmark(
      browser_bookmarks::BookmarkStore::kRootId, std::move(title),
      std::move(url));
  if (id == 0 || !RefreshProjection(current_url_)) {
    return std::nullopt;
  }
  return id;
}

AlloyBookmarkResult AlloyBookmarks::Update(std::uint64_t node_id,
                                           std::string title,
                                           std::string url) {
  if (!active_) {
    return AlloyBookmarkResult::kInactive;
  }
  if (!store_.Find(node_id)) {
    return AlloyBookmarkResult::kNotFound;
  }
  if (!store_.Update(node_id, std::move(title), std::move(url))) {
    return AlloyBookmarkResult::kInvalidInput;
  }
  return RefreshProjection(current_url_) ? AlloyBookmarkResult::kSuccess
                                         : AlloyBookmarkResult::kRejected;
}

AlloyBookmarkResult AlloyBookmarks::Remove(std::uint64_t node_id) {
  if (!active_) {
    return AlloyBookmarkResult::kInactive;
  }
  if (!store_.Remove(node_id)) {
    return AlloyBookmarkResult::kNotFound;
  }
  folder_items_.clear();
  return RefreshProjection(current_url_) ? AlloyBookmarkResult::kSuccess
                                         : AlloyBookmarkResult::kRejected;
}

AlloyBookmarkResult AlloyBookmarks::Open(std::uint64_t node_id,
                                         BookmarkOpenTarget target) {
  if (!active_) {
    return AlloyBookmarkResult::kInactive;
  }
  const auto *node = store_.Find(node_id);
  if (!node) {
    return AlloyBookmarkResult::kNotFound;
  }
  if (node->kind == browser_bookmarks::BookmarkKind::kFolder) {
    folder_items_ = ProjectChildren(node_id);
    return AlloyBookmarkResult::kFolderShown;
  }
  const bool accepted =
      target == BookmarkOpenTarget::kCurrentTab
          ? callbacks_.navigate_current && callbacks_.navigate_current(node->url)
          : callbacks_.open_new_tab && callbacks_.open_new_tab(node->url);
  return accepted ? AlloyBookmarkResult::kSuccess
                  : AlloyBookmarkResult::kRejected;
}

std::vector<std::uint64_t>
AlloyBookmarks::Search(const std::string &query) const {
  return active_ ? store_.Search(query) : std::vector<std::uint64_t>{};
}

bool AlloyBookmarks::Import(
    const std::string &document,
    browser_bookmarks::BookmarkCodecError *error) {
  if (!active_) {
    return false;
  }
  auto candidate = browser_bookmarks::DeserializeBookmarks(document, error);
  if (!candidate) {
    return false;
  }
  auto previous = std::move(store_);
  store_ = std::move(*candidate);
  folder_items_.clear();
  if (RefreshProjection(current_url_)) {
    return true;
  }
  store_ = std::move(previous);
  static_cast<void>(RefreshProjection(current_url_));
  return false;
}

std::string AlloyBookmarks::Export() const {
  return active_ ? browser_bookmarks::SerializeBookmarks(store_) : std::string{};
}

bool AlloyBookmarks::RefreshForUrl(const std::string &url) {
  if (!active_) {
    return false;
  }
  current_url_ = url;
  return RefreshProjection(current_url_);
}

bool AlloyBookmarks::SetBarVisible(bool visible) {
  if (!active_) {
    return false;
  }
  visible ? bar_.ShowBar() : bar_.HideBar();
  return bar_.bar_visible() == visible;
}

bool AlloyBookmarks::Shutdown() {
  if (!active_) {
    return true;
  }
  active_ = false;
  callbacks_ = {};
  current_url_.clear();
  folder_items_.clear();
  bar_.Shutdown();
  return true;
}

bool AlloyBookmarks::RefreshProjection(const std::string &current_url) {
  auto items = ProjectChildren(browser_bookmarks::BookmarkStore::kRootId);
  if (!bar_.SetItems(std::move(items))) {
    return false;
  }
  const auto matches = store_.FindByUrl(current_url);
  bar_.SetCurrentPageBookmark(matches.empty()
                                  ? std::nullopt
                                  : std::optional<std::uint64_t>(matches[0]));
  return true;
}

std::vector<browser_bookmarks_view::BookmarkBarItem>
AlloyBookmarks::ProjectChildren(std::uint64_t parent) const {
  std::vector<browser_bookmarks_view::BookmarkBarItem> items;
  for (std::uint64_t id : store_.ChildrenOf(parent)) {
    if (items.size() >= browser_bookmarks_view::kMaxBarItems) {
      break;
    }
    const auto *node = store_.Find(id);
    if (node) {
      items.push_back({id, node->title,
                       node->kind == browser_bookmarks::BookmarkKind::kFolder});
    }
  }
  return items;
}

} // namespace crayon::browser::cef_shell::window
