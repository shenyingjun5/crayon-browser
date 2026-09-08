#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

#include "macos/alloy_session_restore_mac.h"

namespace {

using crayon::browser::cef_shell::macos::AlloySessionRestoreMac;
using crayon::browser::cef_shell::window::AlloySessionFileResult;
using crayon::browser_session::SessionProfileSnapshot;
using crayon::browser_session::SessionTabSnapshot;
using crayon::browser_session::SessionWindowSnapshot;
using crayon::browser_session::WindowKind;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #condition \
                << '\n';                                                      \
      return false;                                                             \
    }                                                                           \
  } while (false)

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    std::error_code error;
    const std::filesystem::path temporary_root =
        std::filesystem::temp_directory_path(error);
    if (error)
      return;
    std::string pattern =
        (temporary_root / "crayon-session-restore-XXXXXX").string();
    if (char *created = mkdtemp(pattern.data()))
      path_ = created;
  }
  ~TemporaryDirectory() {
    if (path_.empty())
      return;
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path_, error));
  }

  const std::string &path() const { return path_; }

private:
  std::string path_;
};

SessionProfileSnapshot Fixture(std::string profile_id = "p1") {
  return {std::move(profile_id),
          {SessionWindowSnapshot{
              "w1",
              {SessionTabSnapshot{"https://example.test/a", true, false,
                                  std::string("group-a")},
               SessionTabSnapshot{"crayon://newtab/", false, true, std::nullopt}},
              1}}};
}

bool WriteFile(const std::string &path, const std::string &contents) {
  const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                        0600);
  if (file < 0)
    return false;
  std::size_t offset = 0;
  int interrupted = 0;
  while (offset < contents.size()) {
    const ssize_t written = write(file, contents.data() + offset,
                                  contents.size() - offset);
    if (written < 0 && errno == EINTR && ++interrupted <= 64)
      continue;
    if (written <= 0) {
      static_cast<void>(close(file));
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return close(file) == 0;
}

bool IsDirectory(const std::string &path) {
  struct stat metadata {};
  return stat(path.c_str(), &metadata) == 0 && S_ISDIR(metadata.st_mode);
}

bool HasTemporarySibling(const std::string &directory) {
  std::error_code error;
  std::filesystem::directory_iterator entries(directory, error);
  if (error)
    return true;
  const std::string prefix = ".crayon-session-tmp-";
  for (const std::filesystem::directory_entry &entry : entries) {
    if (entry.path().filename().string().rfind(prefix, 0) == 0)
      return true;
  }
  return false;
}

bool RoundTripUtf8Replace() {
  TemporaryDirectory root;
  CHECK(!root.path().empty());
  const std::string parent = root.path() + "/会话";
  CHECK(mkdir(parent.c_str(), 0700) == 0);
  const std::string path = parent + "/" + std::string(240, 'a');
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(path, Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kSuccess);
  CHECK(!HasTemporarySibling(parent));
  struct stat metadata {};
  CHECK(stat(path.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0600);
  AlloySessionFileResult result{};
  auto restored = AlloySessionRestoreMac::LoadCheckpoint(path, "p1", &result);
  CHECK(restored && result == AlloySessionFileResult::kSuccess &&
        restored->windows.front().tabs.size() == 2);
  auto replacement = Fixture();
  replacement.windows.front().tabs.front().url = "https://example.test/replaced";
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(path, replacement, WindowKind::kRegular) ==
        AlloySessionFileResult::kSuccess);
  CHECK(!HasTemporarySibling(parent));
  restored = AlloySessionRestoreMac::LoadCheckpoint(path, "p1", &result);
  CHECK(restored && restored->windows.front().tabs.front().url ==
                        "https://example.test/replaced");
  CHECK(unlink(path.c_str()) == 0 && rmdir(parent.c_str()) == 0);
  return true;
}

bool LoadFailures() {
  TemporaryDirectory root;
  CHECK(!root.path().empty());
  const std::string path = root.path() + "/checkpoint";
  AlloySessionFileResult result{};
  CHECK(!AlloySessionRestoreMac::LoadCheckpoint(path, "p1", &result) &&
        result == AlloySessionFileResult::kNotFound);
  const std::string missing_parent = root.path() + "/missing/checkpoint";
  CHECK(!AlloySessionRestoreMac::LoadCheckpoint(missing_parent, "p1", &result) &&
        result == AlloySessionFileResult::kNotFound);
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(missing_parent, Fixture(),
                                               WindowKind::kRegular) ==
        AlloySessionFileResult::kIoError);
  CHECK(access((root.path() + "/missing").c_str(), F_OK) != 0 && errno == ENOENT);
  CHECK(WriteFile(path, "corrupt"));
  CHECK(!AlloySessionRestoreMac::LoadCheckpoint(path, "p1", &result) &&
        result == AlloySessionFileResult::kCorrupt);
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(path, Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kSuccess);
  CHECK(!AlloySessionRestoreMac::LoadCheckpoint(path, "p2", &result) &&
        result == AlloySessionFileResult::kProfileMismatch);
  CHECK(WriteFile(path, std::string(crayon::browser_session::kMaxSessionBytes + 1, 'x')));
  CHECK(!AlloySessionRestoreMac::LoadCheckpoint(path, "p1", &result) &&
        result == AlloySessionFileResult::kTooLarge);
  return true;
}

bool IncognitoAndInvalidPaths() {
  TemporaryDirectory root;
  CHECK(!root.path().empty());
  const std::string missing_parent = root.path() + "/missing/checkpoint";
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(missing_parent, Fixture(),
                                               WindowKind::kIncognito) ==
        AlloySessionFileResult::kSkippedIncognito);
  CHECK(access(missing_parent.c_str(), F_OK) != 0 && errno == ENOENT);
  CHECK(AlloySessionRestoreMac::SaveCheckpoint("relative", Fixture(),
                                               WindowKind::kIncognito) ==
        AlloySessionFileResult::kSkippedIncognito);
  std::string nul_path = root.path() + "/checkpoint";
  nul_path.push_back('\0');
  nul_path += "suffix";
  CHECK(AlloySessionRestoreMac::SaveCheckpoint("", Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kInvalidPath);
  CHECK(AlloySessionRestoreMac::SaveCheckpoint("relative", Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kInvalidPath);
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(nul_path, Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kInvalidPath);
  CHECK(AlloySessionRestoreMac::SaveCheckpoint("/" + std::string(PATH_MAX, 'a'),
                                               Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kInvalidPath);
  return true;
}

bool SymlinkFifoAndDirectoryFailures() {
  TemporaryDirectory root;
  CHECK(!root.path().empty());
  const std::string target = root.path() + "/target";
  const std::string link = root.path() + "/link";
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(target, Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kSuccess);
  CHECK(symlink("target", link.c_str()) == 0);
  AlloySessionFileResult result{};
  CHECK(!AlloySessionRestoreMac::LoadCheckpoint(link, "p1", &result) &&
        result == AlloySessionFileResult::kIoError);
  CHECK(AlloySessionRestoreMac::LoadCheckpoint(target, "p1", &result));
  const std::string fifo = root.path() + "/fifo";
  CHECK(mkfifo(fifo.c_str(), 0600) == 0);
  CHECK(!AlloySessionRestoreMac::LoadCheckpoint(fifo, "p1", &result) &&
        result == AlloySessionFileResult::kIoError);
  const std::string directory = root.path() + "/directory";
  CHECK(mkdir(directory.c_str(), 0700) == 0);
  CHECK(AlloySessionRestoreMac::SaveCheckpoint(directory, Fixture(), WindowKind::kRegular) ==
        AlloySessionFileResult::kIoError);
  CHECK(IsDirectory(directory) && !HasTemporarySibling(root.path()));
  CHECK(unlink(link.c_str()) == 0 && unlink(target.c_str()) == 0 &&
        unlink(fifo.c_str()) == 0 && rmdir(directory.c_str()) == 0);
  return true;
}

} // namespace

int main() {
  const bool passed = RoundTripUtf8Replace() && LoadFailures() &&
                      IncognitoAndInvalidPaths() && SymlinkFifoAndDirectoryFailures();
  std::cout << "alloy_session_restore_mac_test " << (passed ? "passed" : "failed")
            << '\n';
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
