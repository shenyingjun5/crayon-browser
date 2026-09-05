#include <cstdlib>
#include <iostream>
#include <string>

#include "browser/window/alloy_downloads.h"

namespace {
using crayon::browser::cef_shell::permission::DownloadStartKind;
using crayon::browser::cef_shell::permission::DownloadUpdate;
using crayon::browser::cef_shell::window::AlloyDownloads;
using crayon::browser_downloads::DownloadState;

#ifdef CHECK
#undef CHECK
#endif
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

bool NeverExists(const std::string &) { return false; }

bool LifecycleContract() {
  bool confirmed = false;
  bool discarded = false;
  bool paused = false;
  bool resumed = false;
  bool cancelled = false;
  std::string opened;
  AlloyDownloads downloads("C:/verified-downloads", NeverExists,
                           {[&](std::uint64_t, const std::string &path) {
                              confirmed =
                                  path.find("danger.exe") != std::string::npos;
                              return confirmed;
                            },
                            [&](std::uint64_t) {
                              discarded = true;
                              return true;
                            },
                            [&](std::uint64_t) {
                              paused = true;
                              return true;
                            },
                            [&](std::uint64_t) {
                              resumed = true;
                              return true;
                            },
                            [&](std::uint64_t) {
                              cancelled = true;
                              return true;
                            },
                            [&](const std::string &path) {
                              opened = path;
                              return true;
                            }});
  const auto safe = downloads.OnDownloadStarting(
      1, "safe.txt", "https://example.test/safe.txt");
  CHECK(safe.kind == DownloadStartKind::kAccept &&
        safe.target_path.find("safe.txt") != std::string::npos &&
        safe.generation != 0);
  downloads.OnDownloadProgress(
      {1, safe.generation + 1, 99, 100, false, false, true});
  CHECK(downloads.shelf().Find(1)->percent == 0);
  downloads.OnDownloadProgress(
      {1, safe.generation, 50, 100, false, false, true});
  CHECK(downloads.shelf().Find(1)->percent == 50);
  CHECK(downloads.Pause(1) && paused &&
        downloads.shelf().Find(1)->state == DownloadState::kPaused);
  downloads.OnDownloadProgress(
      {1, safe.generation, 60, 100, false, false, true});
  CHECK(downloads.shelf().Find(1)->state == DownloadState::kPaused);
  CHECK(downloads.Resume(1) && resumed);
  downloads.OnDownloadProgress(
      {1, safe.generation, 100, 100, true, false, false});
  CHECK(downloads.shelf().Find(1)->state == DownloadState::kCompleted);
  CHECK(downloads.OpenLocation(1) && opened == safe.target_path);

  const auto danger = downloads.OnDownloadStarting(
      2, "../danger.exe", "https://example.test/danger.exe");
  CHECK(danger.kind == DownloadStartKind::kPending);
  CHECK(downloads.ConfirmDangerous(2) && confirmed);
  CHECK(downloads.Cancel(2) && cancelled);
  CHECK(!downloads.OpenLocation(2));
  const auto discarded_start = downloads.OnDownloadStarting(
      3, "script.cmd", "https://example.test/script.cmd");
  CHECK(discarded_start.kind == DownloadStartKind::kPending);
  CHECK(downloads.DiscardDangerous(3) && discarded);
  CHECK(downloads.shelf().Find(3)->state == DownloadState::kCancelled);
  CHECK(downloads.OnDownloadStarting(4, "..", "https://example.test/").kind ==
        DownloadStartKind::kReject);
  CHECK(downloads.Shutdown() && downloads.Shutdown());
  CHECK(downloads.OnDownloadStarting(5, "late.txt", "https://example.test/")
            .kind == DownloadStartKind::kReject);
  return true;
}

bool FailureAndCapacityContract() {
  AlloyDownloads downloads("C:/verified-downloads", NeverExists, {});
  for (std::uint64_t id = 1;
       id <= crayon::browser_downloads_view::kMaxVisibleDownloads; ++id) {
    CHECK(downloads
              .OnDownloadStarting(id, "file-" + std::to_string(id) + ".txt",
                                  "https://example.test/file")
              .kind == DownloadStartKind::kAccept);
  }
  CHECK(downloads
            .OnDownloadStarting(100, "overflow.txt",
                                "https://example.test/overflow")
            .kind == DownloadStartKind::kReject);
  const auto first = downloads.shelf().Find(1);
  CHECK(first != nullptr);
  downloads.OnDownloadProgress({1, 1, 1, 2, false, false, false});
  CHECK(downloads.shelf().Find(1)->state == DownloadState::kFailed);
  downloads.OnDownloadProgress({2, 2, 3, 2, false, false, true});
  CHECK(downloads.shelf().Find(2)->state == DownloadState::kFailed);
  downloads.OnDownloadProgress({3, 3, 1, 2, true, false, false});
  CHECK(downloads.shelf().Find(3)->state == DownloadState::kFailed);
  CHECK(!downloads.Pause(1));
  return true;
}
} // namespace

int main() {
  return LifecycleContract() && FailureAndCapacityContract() ? EXIT_SUCCESS
                                                             : EXIT_FAILURE;
}
