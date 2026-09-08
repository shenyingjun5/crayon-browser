#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "browser/window/alloy_bookmarks.h"

namespace {
using crayon::browser::cef_shell::window::AlloyBookmarkResult;
using crayon::browser::cef_shell::window::AlloyBookmarks;
using crayon::browser::cef_shell::window::BookmarkOpenTarget;
using crayon::browser_engine::ProfileId;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

ProfileId Profile(const char *value) {
  const auto result = ProfileId::TryCreate(value);
  if (!result) std::abort();
  return *result;
}

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    std::error_code error;
    const std::filesystem::path temporary_root =
        std::filesystem::temp_directory_path(error);
    if (error)
      return;
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
      const std::string suffix = std::to_string(random()) + "-" +
                                 std::to_string(random());
      const std::filesystem::path candidate =
          temporary_root /
          std::filesystem::u8path(std::string(u8"crayon-alloy-书签-") + suffix);
      error.clear();
      if (std::filesystem::create_directory(candidate, error)) {
        path_ = candidate;
        return;
      }
      if (error && error != std::errc::file_exists)
        return;
    }
  }
  ~TemporaryDirectory() {
    if (path_.empty())
      return;
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path_, error));
  }

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
  TemporaryDirectory(TemporaryDirectory &&) = delete;
  TemporaryDirectory &operator=(TemporaryDirectory &&) = delete;

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

bool AdapterContract() {
  std::string current_navigation;
  std::string new_tab_navigation;
  AlloyBookmarks bookmarks(
      Profile("profile-a"),
      {[&](const std::string &url) { current_navigation = url; return true; },
       [&](const std::string &url) { new_tab_navigation = url; return true; }});
  CHECK(bookmarks.RefreshForUrl("https://example.test/"));
  const auto id = bookmarks.AddCurrentPage("Example", "https://example.test/");
  CHECK(id && bookmarks.bar().current_page_starred());
  CHECK(bookmarks.SetBarVisible(true) && bookmarks.bar().bar_visible());
  CHECK(bookmarks.Open(*id, BookmarkOpenTarget::kCurrentTab) ==
        AlloyBookmarkResult::kSuccess);
  CHECK(current_navigation == "https://example.test/");
  CHECK(bookmarks.Open(*id, BookmarkOpenTarget::kNewTab) ==
        AlloyBookmarkResult::kSuccess);
  CHECK(new_tab_navigation == current_navigation);
  CHECK(bookmarks.Update(*id, "Updated", "https://updated.test/") ==
        AlloyBookmarkResult::kSuccess);
  CHECK(bookmarks.Search("UPDATED") == std::vector<std::uint64_t>{*id});
  const std::string exported = bookmarks.Export();
  const std::size_t before = bookmarks.store().node_count();
  crayon::browser_bookmarks::BookmarkCodecError error{};
  CHECK(!bookmarks.Import("corrupt", &error));
  CHECK(bookmarks.store().node_count() == before);
  CHECK(bookmarks.Import(exported, &error));
  CHECK(bookmarks.store().FindByUrl("https://updated.test/").size() == 1);
  const auto restored_id = bookmarks.store().FindByUrl("https://updated.test/")[0];
  CHECK(bookmarks.Remove(restored_id) == AlloyBookmarkResult::kSuccess);
  CHECK(bookmarks.Shutdown() && bookmarks.Shutdown());
  CHECK(!bookmarks.AddCurrentPage("late", "https://late.test/"));
  CHECK(bookmarks.Open(restored_id, BookmarkOpenTarget::kCurrentTab) ==
        AlloyBookmarkResult::kInactive);
  return true;
}

bool ProfileIsolationAndFolderProjection() {
  AlloyBookmarks first(Profile("profile-a"), {});
  AlloyBookmarks second(Profile("profile-b"), {});
  CHECK(first.Import("CRAYON-BOOKMARKS v1\nF 0 6\nFolder\n"
                     "B 1 4 18\nLeaf\nhttps://leaf.test/\n"));
  CHECK(second.store().node_count() == 1);
  const auto folder = first.Search("Folder");
  CHECK(folder.size() == 1);
  CHECK(first.Open(folder[0], BookmarkOpenTarget::kCurrentTab) ==
        AlloyBookmarkResult::kFolderShown);
  CHECK(first.folder_items().size() == 1 &&
        first.folder_items()[0].title == "Leaf");
  return true;
}

bool FileLoadIsAtomicAndPersists() {
  TemporaryDirectory directory;
  CHECK(!directory.path().empty());
  const auto path = (directory.path() / "bookmarks-v1.txt").u8string();
  AlloyBookmarks source(Profile("profile-a"), {});
  CHECK(source.AddCurrentPage("Saved", "https://saved.test/") &&
        source.SaveToFile(path));
  AlloyBookmarks restored(Profile("profile-a"), {});
  CHECK(restored.Import("CRAYON-BOOKMARKS v1\nF 0 6\nFolder\n"
                        "B 1 4 18\nLeaf\nhttps://leaf.test/\n"));
  const auto folder = restored.Search("Folder");
  CHECK(folder.size() == 1 &&
        restored.Open(folder[0], BookmarkOpenTarget::kCurrentTab) ==
            AlloyBookmarkResult::kFolderShown &&
        restored.folder_items().size() == 1);
  CHECK(restored.LoadFromFile(path));
  CHECK(restored.Search("saved").size() == 1 && restored.folder_items().empty());
  CHECK(restored.Import("CRAYON-BOOKMARKS v1\nF 0 6\nFolder\n"
                        "B 1 4 18\nLeaf\nhttps://leaf.test/\n"));
  const auto reopened_folder = restored.Search("Folder");
  CHECK(reopened_folder.size() == 1 &&
        restored.Open(reopened_folder[0], BookmarkOpenTarget::kCurrentTab) ==
            AlloyBookmarkResult::kFolderShown &&
        restored.folder_items().size() == 1 &&
        restored.folder_items()[0].title == "Leaf");
  const auto before = restored.Export();
  const auto folder_items_before = restored.folder_items();
  {
    std::ofstream corrupt(std::filesystem::u8path(path),
                          std::ios::binary | std::ios::trunc);
    corrupt << "corrupt";
  }
  CHECK(!restored.LoadFromFile(path));
  CHECK(restored.Export() == before && restored.folder_items().size() ==
                                         folder_items_before.size() &&
        !folder_items_before.empty() && !restored.folder_items().empty() &&
        restored.folder_items()[0].title == folder_items_before[0].title);
  return true;
}
} // namespace

int main() {
  return AdapterContract() && ProfileIsolationAndFolderProjection() &&
                 FileLoadIsAtomicAndPersists()
             ? EXIT_SUCCESS : EXIT_FAILURE;
}
