#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_bookmarks/bookmark_codec.h"
#include "crayon/browser_bookmarks/bookmark_store.h"
#include "crayon/browser_bookmarks_view/bookmark_bar_state_machine.h"
#include "crayon/browser_engine/types.h"

namespace crayon::browser::cef_shell::window {

enum class BookmarkOpenTarget { kCurrentTab = 0, kNewTab };

enum class AlloyBookmarkResult {
  kSuccess = 0,
  kInvalidInput,
  kNotFound,
  kFolderShown,
  kRejected,
  kInactive,
};

class AlloyBookmarks final {
public:
  struct Callbacks final {
    std::function<bool(const std::string &)> navigate_current;
    std::function<bool(const std::string &)> open_new_tab;
  };

  AlloyBookmarks(browser_engine::ProfileId profile_id, Callbacks callbacks);

  std::optional<std::uint64_t> AddCurrentPage(std::string title,
                                              std::string url);
  AlloyBookmarkResult Update(std::uint64_t node_id, std::string title,
                             std::string url);
  AlloyBookmarkResult Remove(std::uint64_t node_id);
  AlloyBookmarkResult Open(std::uint64_t node_id, BookmarkOpenTarget target);
  std::vector<std::uint64_t> Search(const std::string &query) const;
  bool Import(const std::string &document,
              browser_bookmarks::BookmarkCodecError *error = nullptr);
  std::string Export() const;
  bool LoadFromFile(
      const std::string &path,
      browser_bookmarks::BookmarkCodecError *error = nullptr);
  bool SaveToFile(
      const std::string &path,
      browser_bookmarks::BookmarkCodecError *error = nullptr) const;
  bool RefreshForUrl(const std::string &url);
  bool SetBarVisible(bool visible);
  bool Shutdown();

  const browser_engine::ProfileId &profile_id() const noexcept {
    return profile_id_;
  }
  const browser_bookmarks::BookmarkStore &store() const noexcept {
    return store_;
  }
  const browser_bookmarks_view::BookmarkBarStateMachine &bar() const noexcept {
    return bar_;
  }
  const std::vector<browser_bookmarks_view::BookmarkBarItem> &folder_items()
      const noexcept {
    return folder_items_;
  }
  bool active() const noexcept { return active_; }

private:
  bool RefreshProjection(const std::string &current_url);
  std::vector<browser_bookmarks_view::BookmarkBarItem>
  ProjectChildren(std::uint64_t parent) const;

  browser_engine::ProfileId profile_id_;
  Callbacks callbacks_;
  browser_bookmarks::BookmarkStore store_;
  browser_bookmarks_view::BookmarkBarStateMachine bar_;
  std::vector<browser_bookmarks_view::BookmarkBarItem> folder_items_;
  std::string current_url_;
  bool active_ = true;
};

} // namespace crayon::browser::cef_shell::window
