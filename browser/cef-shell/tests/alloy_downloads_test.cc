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

#ifdef _WIN32
constexpr char kVerifiedDownloadsDirectory[] = "C:/verified-downloads";
#else
constexpr char kVerifiedDownloadsDirectory[] = "/verified-downloads";
#endif

bool LifecycleContract() {
  bool confirmed = false;
  bool discarded = false;
  bool paused = false;
  bool resumed = false;
  bool cancelled = false;
  std::string opened;
  AlloyDownloads downloads(kVerifiedDownloadsDirectory, NeverExists,
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
  AlloyDownloads downloads(kVerifiedDownloadsDirectory, NeverExists, {});
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

bool InvalidControlsDoNotInvokeCallbacks() {
  unsigned callbacks = 0;
  AlloyDownloads downloads(
      kVerifiedDownloadsDirectory, NeverExists,
      {[&](std::uint64_t, const std::string &) { ++callbacks; return true; },
       [&](std::uint64_t) { ++callbacks; return true; },
       [&](std::uint64_t) { ++callbacks; return true; },
       [&](std::uint64_t) { ++callbacks; return true; },
       [&](std::uint64_t) { ++callbacks; return true; }, {}});
  const auto complete = downloads.OnDownloadStarting(
      1, "complete.txt", "https://example.test/complete.txt");
  CHECK(complete.kind == DownloadStartKind::kAccept);
  downloads.OnDownloadProgress({1, complete.generation, 1, 1, true, false, false});
  CHECK(!downloads.Pause(1) && !downloads.Resume(1) && !downloads.Cancel(1) &&
        !downloads.DiscardDangerous(1) && callbacks == 0);
  const auto progress = downloads.OnDownloadStarting(
      2, "progress.txt", "https://example.test/progress.txt");
  CHECK(progress.kind == DownloadStartKind::kAccept);
  CHECK(!downloads.DiscardDangerous(2) && !downloads.Resume(2) && callbacks == 0);
  const auto pending = downloads.OnDownloadStarting(
      3, "danger.exe", "https://example.test/danger.exe");
  CHECK(pending.kind == DownloadStartKind::kPending);
  CHECK(!downloads.Pause(3) && !downloads.Resume(3) && callbacks == 0);
  return true;
}

bool ShutdownFromEveryControlCallbackIsSafe() {
  {
    AlloyDownloads *self = nullptr;
    AlloyDownloads downloads(kVerifiedDownloadsDirectory, NeverExists,
                            {[&](std::uint64_t, const std::string &) {
                               self->Shutdown();
                               return true;
                             }, {}, {}, {}, {}, {}});
    self = &downloads;
    CHECK(downloads.OnDownloadStarting(1, "danger.exe", "https://example.test/").kind ==
          DownloadStartKind::kPending);
    CHECK(!downloads.ConfirmDangerous(1) && downloads.shelf().Find(1) == nullptr);
  }
  {
    AlloyDownloads *self = nullptr;
    AlloyDownloads downloads(kVerifiedDownloadsDirectory, NeverExists,
                            {{}, [&](std::uint64_t) { self->Shutdown(); return true; },
                             {}, {}, {}, {}});
    self = &downloads;
    CHECK(downloads.OnDownloadStarting(1, "danger.exe", "https://example.test/").kind ==
          DownloadStartKind::kPending);
    CHECK(!downloads.DiscardDangerous(1) && downloads.shelf().Find(1) == nullptr);
  }
  {
    AlloyDownloads *self = nullptr;
    AlloyDownloads downloads(kVerifiedDownloadsDirectory, NeverExists,
                            {{}, {}, [&](std::uint64_t) { self->Shutdown(); return true; },
                             {}, {}, {}});
    self = &downloads;
    CHECK(downloads.OnDownloadStarting(1, "safe.txt", "https://example.test/").kind ==
          DownloadStartKind::kAccept);
    CHECK(!downloads.Pause(1) && downloads.shelf().Find(1) == nullptr);
  }
  {
    AlloyDownloads *self = nullptr;
    AlloyDownloads downloads(kVerifiedDownloadsDirectory, NeverExists,
                            {{}, {}, [](std::uint64_t) { return true; },
                             [&](std::uint64_t) { self->Shutdown(); return true; }, {}, {}});
    self = &downloads;
    CHECK(downloads.OnDownloadStarting(1, "safe.txt", "https://example.test/").kind ==
          DownloadStartKind::kAccept);
    CHECK(downloads.Pause(1));
    CHECK(!downloads.Resume(1) && downloads.shelf().Find(1) == nullptr);
  }
  {
    AlloyDownloads *self = nullptr;
    AlloyDownloads downloads(kVerifiedDownloadsDirectory, NeverExists,
                            {{}, {}, {}, {},
                             [&](std::uint64_t) { self->Shutdown(); return true; }, {}});
    self = &downloads;
    CHECK(downloads.OnDownloadStarting(1, "safe.txt", "https://example.test/").kind ==
          DownloadStartKind::kAccept);
    CHECK(!downloads.Cancel(1) && downloads.shelf().Find(1) == nullptr);
  }
  return true;
}

bool ReentrantCallbacksPreserveDomainState() {
  AlloyDownloads *nested_self = nullptr;
  bool nested_control_rejected = false;
  unsigned cancel_callbacks = 0;
  AlloyDownloads nested(
      kVerifiedDownloadsDirectory, NeverExists,
      {{}, {},
       [&](std::uint64_t) {
         nested_control_rejected = !nested_self->Cancel(1);
         return true;
       },
       {},
       [&](std::uint64_t) { ++cancel_callbacks; return true; }, {}});
  nested_self = &nested;
  CHECK(nested.OnDownloadStarting(1, "nested.txt", "https://example.test/").kind ==
        DownloadStartKind::kAccept);
  CHECK(nested.Pause(1) && nested_control_rejected && cancel_callbacks == 0 &&
        nested.shelf().Find(1)->state == DownloadState::kPaused);

  AlloyDownloads *self = nullptr;
  std::uint64_t generation = 0;
  AlloyDownloads downloads(
      kVerifiedDownloadsDirectory, NeverExists,
      {{}, {},
       [&](std::uint64_t) {
         self->OnDownloadProgress({1, generation, 50, 100, false, false, true});
         return true;
       },
       {}, {}, {}});
  self = &downloads;
  const auto started = downloads.OnDownloadStarting(
      1, "safe.txt", "https://example.test/safe.txt");
  CHECK(started.kind == DownloadStartKind::kAccept);
  generation = started.generation;
  CHECK(downloads.Pause(1));
  CHECK(downloads.shelf().Find(1)->state == DownloadState::kPaused &&
        downloads.shelf().Find(1)->percent == 50);

  AlloyDownloads *terminal_self = nullptr;
  std::uint64_t terminal_generation = 0;
  AlloyDownloads terminal(
      kVerifiedDownloadsDirectory, NeverExists,
      {{}, {},
       [&](std::uint64_t) {
         terminal_self->OnDownloadProgress(
             {1, terminal_generation, 1, 1, true, false, false});
         return true;
       },
       {}, {}, {}});
  terminal_self = &terminal;
  const auto terminal_started = terminal.OnDownloadStarting(
      1, "terminal.txt", "https://example.test/");
  CHECK(terminal_started.kind == DownloadStartKind::kAccept);
  terminal_generation = terminal_started.generation;
  CHECK(!terminal.Pause(1) &&
        terminal.shelf().Find(1)->state == DownloadState::kCompleted);

  AlloyDownloads rejected(kVerifiedDownloadsDirectory, NeverExists,
                          {[&](std::uint64_t, const std::string &) { return false; },
                           {}, {}, {}, {}, {}});
  CHECK(rejected.OnDownloadStarting(1, "danger.exe", "https://example.test/").kind ==
        DownloadStartKind::kPending);
  CHECK(!rejected.ConfirmDangerous(1) &&
        rejected.shelf().Find(1)->state == DownloadState::kPendingDangerConfirm);

  AlloyDownloads *open_self = nullptr;
  std::string opened_path;
  AlloyDownloads opener(
      kVerifiedDownloadsDirectory, NeverExists,
      {{}, {}, {}, {}, {},
       [&](const std::string &path) {
         open_self->Shutdown();
         opened_path = path;
         return true;
       }});
  open_self = &opener;
  const auto completed = opener.OnDownloadStarting(
      1, "open.txt", "https://example.test/open.txt");
  CHECK(completed.kind == DownloadStartKind::kAccept);
  opener.OnDownloadProgress({1, completed.generation, 1, 1, true, false, false});
  CHECK(opener.OpenLocation(1) && opened_path == completed.target_path &&
        opener.shelf().Find(1) == nullptr);
  return true;
}
} // namespace

int main() {
  return LifecycleContract() && FailureAndCapacityContract() &&
                 InvalidControlsDoNotInvokeCallbacks() &&
                 ShutdownFromEveryControlCallbackIsSafe() &&
                 ReentrantCallbacksPreserveDomainState()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
