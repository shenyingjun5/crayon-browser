#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include "crayon/browser_bookmarks/bookmark_codec.h"
#include "crayon/browser_history/history_codec.h"
#include "crayon/browser_preferences/preference_codec.h"

namespace {

using crayon::browser_bookmarks::BookmarkCodecError;
using crayon::browser_bookmarks::BookmarkStore;
using crayon::browser_bookmarks::LoadBookmarksFromFile;
using crayon::browser_bookmarks::SaveBookmarksToFile;
using crayon::browser_bookmarks::SerializeBookmarks;
using crayon::browser_history::HistoryCodecError;
using crayon::browser_history::HistoryStore;
using crayon::browser_history::LoadHistoryFromFile;
using crayon::browser_history::SaveHistoryToFile;
using crayon::browser_history::SerializeHistory;
using crayon::browser_preferences::LoadPreferencesFromFile;
using crayon::browser_preferences::PreferenceCodecError;
using crayon::browser_preferences::PreferenceStore;
using crayon::browser_preferences::PreferenceValue;
using crayon::browser_preferences::SavePreferencesToFile;
using crayon::browser_preferences::SerializePreferences;

constexpr rlim_t kFileSizeLimit = 16;
constexpr std::size_t kMaximumBufferedPayload = 1024;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: "         \
                << #condition << '\n';                                      \
      return false;                                                          \
    }                                                                        \
  } while (false)

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(error);
    if (error) {
      return;
    }
    std::string template_path =
        (root / std::filesystem::u8path(
                    "crayon-profile-persistence-测试-XXXXXX"))
            .u8string();
    std::vector<char> mutable_template(template_path.begin(),
                                       template_path.end());
    mutable_template.push_back('\0');
    if (char* created = mkdtemp(mutable_template.data())) {
      path_ = std::filesystem::u8path(created);
    }
  }

  ~TemporaryDirectory() {
    if (!path_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

bool ReadFile(const std::filesystem::path& path, std::string* contents) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  *contents = std::string(std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>());
  return !input.bad();
}

template <typename Attempt>
bool FailsUnderFileSizeLimit(Attempt&& attempt) {
  const pid_t child = fork();
  if (child < 0) {
    return false;
  }
  if (child == 0) {
    struct rlimit limit {};
    if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR ||
        getrlimit(RLIMIT_FSIZE, &limit) != 0 ||
        limit.rlim_max < kFileSizeLimit) {
      _exit(2);
    }
    limit.rlim_cur = kFileSizeLimit;
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
      _exit(2);
    }
    _exit(attempt() ? 0 : 3);
  }

  int status = 0;
  pid_t waited = 0;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited != child) {
    std::cerr << "file-size child wait failed\n";
    return false;
  }
  if (WIFSIGNALED(status)) {
    std::cerr << "file-size child terminated by signal " << WTERMSIG(status)
              << '\n';
    return false;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::cerr << "file-size child exited "
              << (WIFEXITED(status) ? WEXITSTATUS(status) : -1) << '\n';
    return false;
  }
  return true;
}

bool BookmarkWriteFailurePreservesTarget() {
  TemporaryDirectory directory;
  CHECK(!directory.path().empty());
  const std::string path = (directory.path() / "书签.txt").u8string();
  BookmarkStore old_store;
  const std::uint64_t old_id = old_store.AddBookmark(
      BookmarkStore::kRootId, "旧书签", "https://old.test/");
  CHECK(old_id != 0);
  BookmarkCodecError error = BookmarkCodecError::kBadHeader;
  CHECK(SaveBookmarksToFile(old_store, path, &error));
  const auto loaded = LoadBookmarksFromFile(path, &error);
  CHECK(loaded && loaded->Find(old_id) &&
        loaded->Find(old_id)->title == "旧书签");
  std::string old_bytes;
  CHECK(ReadFile(path, &old_bytes));

  BookmarkStore replacement;
  CHECK(replacement.AddBookmark(BookmarkStore::kRootId, std::string(64, 'b'),
                                "https://replacement.test/") != 0);
  const std::string replacement_bytes = SerializeBookmarks(replacement);
  CHECK(replacement_bytes.size() > kFileSizeLimit);
  CHECK(replacement_bytes.size() < kMaximumBufferedPayload);
  CHECK(FailsUnderFileSizeLimit([&] {
    BookmarkCodecError child_error = BookmarkCodecError::kBadHeader;
    const bool saved = SaveBookmarksToFile(replacement, path, &child_error);
    if (saved) {
      std::cerr << "bookmarks save unexpectedly succeeded\n";
      return false;
    }
    if (child_error != BookmarkCodecError::kIoFailure) {
      std::cerr << "bookmarks save unexpected error\n";
      return false;
    }
    return true;
  }));
  std::string current_bytes;
  CHECK(ReadFile(path, &current_bytes));
  CHECK(current_bytes == old_bytes);
  CHECK(!std::filesystem::exists(std::filesystem::u8path(path + ".tmp")));
  return true;
}

bool HistoryWriteFailurePreservesTarget() {
  TemporaryDirectory directory;
  CHECK(!directory.path().empty());
  const std::string path = (directory.path() / "历史.txt").u8string();
  HistoryStore old_store;
  CHECK(old_store.RecordVisit("https://old.test/", "旧历史", 1) != 0);
  HistoryCodecError error = HistoryCodecError::kBadHeader;
  CHECK(SaveHistoryToFile(old_store, path, &error));
  const auto loaded = LoadHistoryFromFile(path, &error);
  CHECK(loaded && loaded->entries().size() == 1 &&
        loaded->entries().front().title == "旧历史");
  std::string old_bytes;
  CHECK(ReadFile(path, &old_bytes));

  HistoryStore replacement;
  CHECK(replacement.RecordVisit("https://replacement.test/",
                                std::string(64, 'h'), 2) != 0);
  const std::string replacement_bytes = SerializeHistory(replacement);
  CHECK(replacement_bytes.size() > kFileSizeLimit);
  CHECK(replacement_bytes.size() < kMaximumBufferedPayload);
  CHECK(FailsUnderFileSizeLimit([&] {
    HistoryCodecError child_error = HistoryCodecError::kBadHeader;
    const bool saved = SaveHistoryToFile(replacement, path, &child_error);
    if (saved) {
      std::cerr << "history save unexpectedly succeeded\n";
      return false;
    }
    if (child_error != HistoryCodecError::kIoFailure) {
      std::cerr << "history save unexpected error\n";
      return false;
    }
    return true;
  }));
  std::string current_bytes;
  CHECK(ReadFile(path, &current_bytes));
  CHECK(current_bytes == old_bytes);
  CHECK(!std::filesystem::exists(std::filesystem::u8path(path + ".tmp")));
  return true;
}

bool PreferenceWriteFailurePreservesTarget() {
  TemporaryDirectory directory;
  CHECK(!directory.path().empty());
  const std::string path = (directory.path() / "偏好.txt").u8string();
  PreferenceStore old_store;
  CHECK(old_store.Set(PreferenceStore::kSearchProvider,
                      PreferenceValue{std::string("旧搜索")}));
  PreferenceCodecError error = PreferenceCodecError::kBadHeader;
  CHECK(SavePreferencesToFile(old_store, path, &error));
  const auto loaded = LoadPreferencesFromFile(path, &error);
  CHECK(loaded &&
        std::get<std::string>(loaded->Get(PreferenceStore::kSearchProvider)) ==
            "旧搜索");
  std::string old_bytes;
  CHECK(ReadFile(path, &old_bytes));

  PreferenceStore replacement;
  CHECK(replacement.Set(PreferenceStore::kTheme,
                        PreferenceValue{PreferenceStore::kThemeDark}));
  const std::string replacement_bytes = SerializePreferences(replacement);
  CHECK(replacement_bytes.size() > kFileSizeLimit);
  CHECK(replacement_bytes.size() < kMaximumBufferedPayload);
  CHECK(FailsUnderFileSizeLimit([&] {
    PreferenceCodecError child_error = PreferenceCodecError::kBadHeader;
    const bool saved = SavePreferencesToFile(replacement, path, &child_error);
    if (saved) {
      std::cerr << "preferences save unexpectedly succeeded\n";
      return false;
    }
    if (child_error != PreferenceCodecError::kIoFailure) {
      std::cerr << "preferences save unexpected error\n";
      return false;
    }
    return true;
  }));
  std::string current_bytes;
  CHECK(ReadFile(path, &current_bytes));
  CHECK(current_bytes == old_bytes);
  CHECK(!std::filesystem::exists(std::filesystem::u8path(path + ".tmp")));
  return true;
}

}  // namespace

int main() {
  if (!BookmarkWriteFailurePreservesTarget()) {
    return 1;
  }
  if (!HistoryWriteFailurePreservesTarget()) {
    return 1;
  }
  if (!PreferenceWriteFailurePreservesTarget()) {
    return 1;
  }
  return 0;
}
