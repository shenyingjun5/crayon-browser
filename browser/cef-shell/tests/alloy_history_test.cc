#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <system_error>

#include "browser/window/alloy_history.h"

namespace {
using crayon::browser::cef_shell::window::AlloyHistory;
using crayon::browser::cef_shell::window::AlloyHistoryResult;
using crayon::browser_engine::ProfileId;

#define CHECK(condition) do { if (!(condition)) { std::cerr << __FILE__ << ':' \
  << __LINE__ << " CHECK failed: " << #condition << '\n'; return false; } } while (false)

ProfileId Profile(const char *value) {
  auto profile = ProfileId::TryCreate(value);
  if (!profile) std::abort();
  return *profile;
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
          std::filesystem::u8path(std::string(u8"crayon-alloy-历史-") + suffix);
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

bool RegularContract() {
  std::string opened;
  bool accept_open = false;
  AlloyHistory history(Profile("profile-a"), false,
                       {[&](const std::string &url) {
                         opened = url;
                         return accept_open;
                       }});
  CHECK(history.BeginNavigation(1));
  CHECK(history.CommitNavigation(2, "https://stale.test/", "stale", 1) ==
        AlloyHistoryResult::kStaleNavigation);
  CHECK(history.CommitNavigation(1, "https://first.test/", "First", 10) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.CommitNavigation(1, "https://duplicate.test/", "duplicate", 11) ==
        AlloyHistoryResult::kStaleNavigation);
  CHECK(history.BeginNavigation(2));
  CHECK(history.CommitNavigation(2, "https://second.test/", "Second", 20) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.Search("test") && history.view().entries().size() == 2 &&
        history.view().entries()[0].visited_at == 20);
  CHECK(history.RecordClosedTab("https://closed.test/", "Closed", 30) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.RestoreRecentlyClosed() == AlloyHistoryResult::kRejected);
  CHECK(history.store().recently_closed_count() == 1);
  accept_open = true;
  CHECK(history.RestoreRecentlyClosed() == AlloyHistoryResult::kSuccess);
  CHECK(opened == "https://closed.test/" &&
        history.store().recently_closed_count() == 0);
  CHECK(history.DeleteRange(10, 10) == 1);
  CHECK(history.DeleteUrl("https://second.test/") == 1);
  CHECK(history.ClearAll() && history.view().entries().empty());

  CHECK(history.BeginNavigation(3));
  CHECK(history.CommitNavigation(3, "https://export.test/", "Export", 40) ==
        AlloyHistoryResult::kSuccess);
  const auto exported = history.Export();
  CHECK(!history.Import("corrupt"));
  CHECK(history.store().entries().size() == 1);
  CHECK(history.Import(exported));
  CHECK(history.store().entries().size() == 1);
  CHECK(history.Shutdown() && history.Shutdown());
  CHECK(!history.BeginNavigation(4));
  return true;
}

bool EphemeralAndProfileIsolation() {
  AlloyHistory regular(Profile("profile-a"), false, {});
  AlloyHistory ephemeral(Profile("profile-b"), true, {});
  CHECK(ephemeral.BeginNavigation(1));
  CHECK(ephemeral.CommitNavigation(1, "https://private.test/", "Private", 1) ==
        AlloyHistoryResult::kEphemeral);
  CHECK(ephemeral.RecordClosedTab("https://private.test/", "Private", 2) ==
        AlloyHistoryResult::kEphemeral);
  CHECK(ephemeral.Export().empty() && !ephemeral.Import("CRAYON-HISTORY v1\n"));
  CHECK(regular.store().entries().empty());
  return true;
}

bool QueryProjectionSurvivesMutations() {
  AlloyHistory history(Profile("profile-a"), false, {});
  CHECK(history.BeginNavigation(1));
  CHECK(history.CommitNavigation(1, "https://keep.test/old", "Keep old", 10) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.BeginNavigation(2));
  CHECK(history.CommitNavigation(2, "https://drop.test/old", "Drop old", 20) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.Search("keep") && history.view().entries().size() == 1 &&
        history.view().entries()[0].display_title == "Keep old");
  CHECK(history.BeginNavigation(3));
  CHECK(history.CommitNavigation(3, "https://drop.test/new", "Drop new", 30) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.view().entries().size() == 1 &&
        history.view().entries()[0].display_title == "Keep old");
  CHECK(history.BeginNavigation(4));
  CHECK(history.CommitNavigation(4, "https://keep.test/new", "Keep new", 40) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.view().entries().size() == 2 &&
        history.view().entries()[0].display_title == "Keep new");
  CHECK(history.DeleteUrl("https://drop.test/old") == 1 &&
        history.view().entries().size() == 2);
  CHECK(history.DeleteRange(20, 30) == 1 && history.view().entries().size() == 2 &&
        history.view().entries()[0].display_title == "Keep new");

  AlloyHistory imported(Profile("profile-a"), false, {});
  CHECK(imported.BeginNavigation(1));
  CHECK(imported.CommitNavigation(1, "https://keep.test/import", "Keep import", 50) ==
        AlloyHistoryResult::kSuccess);
  CHECK(imported.BeginNavigation(2));
  CHECK(imported.CommitNavigation(2, "https://drop.test/import", "Drop import", 60) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.Import(imported.Export()) && history.view().entries().size() == 1 &&
        history.view().entries()[0].display_title == "Keep import");
  CHECK(!history.Import("corrupt") && history.view().entries().size() == 1 &&
        history.view().entries()[0].display_title == "Keep import");

  TemporaryDirectory directory;
  CHECK(!directory.path().empty());
  const std::string path = (directory.path() / "history-v1.txt").u8string();
  AlloyHistory loaded(Profile("profile-a"), false, {});
  CHECK(loaded.BeginNavigation(1));
  CHECK(loaded.CommitNavigation(1, "https://keep.test/file", "Keep file", 70) ==
        AlloyHistoryResult::kSuccess);
  CHECK(loaded.BeginNavigation(2));
  CHECK(loaded.CommitNavigation(2, "https://drop.test/file", "Drop file", 80) ==
        AlloyHistoryResult::kSuccess);
  CHECK(loaded.SaveToFile(path));
  CHECK(history.LoadFromFile(path) && history.view().entries().size() == 1 &&
        history.view().entries()[0].display_title == "Keep file");
  {
    std::ofstream corrupt(std::filesystem::u8path(path),
                          std::ios::binary | std::ios::trunc);
    corrupt << "corrupt";
  }
  CHECK(!history.LoadFromFile(path) && history.view().entries().size() == 1 &&
        history.view().entries()[0].display_title == "Keep file");
  CHECK(history.Search("") &&
        history.view().entries().size() == history.store().entries().size());
  CHECK(history.ClearAll() && history.view().entries().empty() &&
        history.view().query().empty());
  return true;
}

bool FileLoadIsAtomicAndPersists() {
  TemporaryDirectory directory;
  CHECK(!directory.path().empty());
  const auto path = (directory.path() / "history-v1.txt").u8string();
  AlloyHistory source(Profile("profile-a"), false, {});
  CHECK(source.BeginNavigation(1));
  CHECK(source.CommitNavigation(1, "https://saved.test/", "Saved", 1) ==
        AlloyHistoryResult::kSuccess);
  CHECK(source.SaveToFile(path));
  AlloyHistory restored(Profile("profile-a"), false, {});
  CHECK(restored.LoadFromFile(path));
  CHECK(restored.store().entries().size() == 1);
  const auto before = restored.Export();
  {
    std::ofstream corrupt(std::filesystem::u8path(path),
                          std::ios::binary | std::ios::trunc);
    corrupt << "corrupt";
  }
  CHECK(!restored.LoadFromFile(path));
  CHECK(restored.Export() == before);
  return true;
}
} // namespace

int main() {
  return RegularContract() && EphemeralAndProfileIsolation() &&
                 QueryProjectionSurvivesMutations() &&
                 FileLoadIsAtomicAndPersists()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
