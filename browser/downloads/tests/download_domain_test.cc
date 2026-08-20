#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "crayon/browser_downloads/download_item.h"
#include "crayon/browser_downloads/download_path.h"

namespace {

using crayon::browser_downloads::ClassifyDownloadDanger;
using crayon::browser_downloads::DownloadDanger;
using crayon::browser_downloads::DownloadItem;
using crayon::browser_downloads::DownloadState;
using crayon::browser_downloads::IsValid;
using crayon::browser_downloads::kMaxDedupeIndex;
using crayon::browser_downloads::kMaxFileNameLength;
using crayon::browser_downloads::ResolveUniqueDownloadPath;
using crayon::browser_downloads::SanitizeDownloadFileName;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

// ---------- Danger classification ----------

bool DangerClassificationMatrix() {
  CHECK(ClassifyDownloadDanger("setup.exe") == DownloadDanger::kDangerous);
  CHECK(ClassifyDownloadDanger("Setup.EXE") == DownloadDanger::kDangerous);
  CHECK(ClassifyDownloadDanger("run.bat") == DownloadDanger::kDangerous);
  CHECK(ClassifyDownloadDanger("script.ps1") == DownloadDanger::kDangerous);
  CHECK(ClassifyDownloadDanger("archive.jar") == DownloadDanger::kDangerous);
  CHECK(ClassifyDownloadDanger("notes.txt") == DownloadDanger::kSafe);
  CHECK(ClassifyDownloadDanger("photo.png") == DownloadDanger::kSafe);
  CHECK(ClassifyDownloadDanger("no_extension") == DownloadDanger::kSafe);
  CHECK(ClassifyDownloadDanger(".hiddenfile") == DownloadDanger::kSafe);
  CHECK(ClassifyDownloadDanger("trailingdot.") == DownloadDanger::kSafe);
  CHECK(ClassifyDownloadDanger("") == DownloadDanger::kSafe);
  // Extension is taken from the base name, not from directories.
  CHECK(ClassifyDownloadDanger("dir.exe/notes.txt") == DownloadDanger::kSafe);
  CHECK(ClassifyDownloadDanger("dir/setup.exe") == DownloadDanger::kDangerous);
  return true;
}

// ---------- Item state machine ----------

bool SafeItemStartsInProgress() {
  auto item = DownloadItem::Create(1, "report.pdf");
  CHECK(item.state() == DownloadState::kInProgress);
  CHECK(item.download_id() == 1);
  CHECK(!item.terminal());
  return true;
}

bool DangerousItemRequiresConfirmation() {
  auto item = DownloadItem::Create(2, "setup.exe");
  CHECK(item.state() == DownloadState::kPendingDangerConfirm);
  // Active-state commands are rejected while pending.
  CHECK(!item.Pause());
  CHECK(!item.Complete());
  CHECK(!item.MarkFailed());
  CHECK(item.ConfirmDangerous());
  CHECK(item.state() == DownloadState::kInProgress);
  CHECK(!item.ConfirmDangerous());  // already confirmed
  return true;
}

bool DangerousItemCanBeDiscarded() {
  auto item = DownloadItem::Create(3, "evil.scr");
  CHECK(item.DiscardDangerous());
  CHECK(item.state() == DownloadState::kCancelled);
  CHECK(item.terminal());
  CHECK(!item.DiscardDangerous());
  return true;
}

bool ProgressIsBounded() {
  auto item = DownloadItem::Create(4, "file.zip");
  CHECK(item.OnProgress(50, 100));
  CHECK(item.received_bytes() == 50);
  CHECK(!item.OnProgress(101, 100));  // over total
  CHECK(item.received_bytes() == 50); // unchanged after rejection
  CHECK(!item.Complete());            // not fully received
  CHECK(item.OnProgress(100, 100));
  CHECK(item.Complete());
  CHECK(item.state() == DownloadState::kCompleted);
  return true;
}

bool UnknownTotalCompletes() {
  auto item = DownloadItem::Create(5, "stream.bin");
  CHECK(item.OnProgress(10, 0));  // total unknown
  CHECK(item.Complete());
  CHECK(item.terminal());
  return true;
}

bool PauseResumeCycle() {
  auto item = DownloadItem::Create(6, "movie.mkv");
  CHECK(item.Pause());
  CHECK(item.state() == DownloadState::kPaused);
  CHECK(!item.Pause());            // already paused
  CHECK(!item.OnProgress(1, 10));  // paused items take no progress
  CHECK(item.Resume());
  CHECK(item.state() == DownloadState::kInProgress);
  CHECK(!item.Resume());
  return true;
}

bool CancelIsTerminal() {
  auto item = DownloadItem::Create(7, "data.csv");
  item.Pause();
  CHECK(item.Cancel());
  CHECK(item.state() == DownloadState::kCancelled);
  CHECK(!item.Cancel());
  CHECK(!item.Resume());
  CHECK(!item.Retry());
  return true;
}

bool FailureAndRetry() {
  auto item = DownloadItem::Create(8, "image.jpg");
  CHECK(item.OnProgress(40, 100));
  CHECK(item.MarkFailed());
  CHECK(item.state() == DownloadState::kFailed);
  CHECK(!item.OnProgress(50, 100));
  CHECK(item.Retry());
  CHECK(item.state() == DownloadState::kInProgress);
  CHECK(item.received_bytes() == 0);
  return true;
}

bool FailedCanBeCancelled() {
  auto item = DownloadItem::Create(9, "log.txt");
  item.MarkFailed();
  CHECK(item.Cancel());
  CHECK(item.terminal());
  return true;
}

bool OpenActionsRequireCompletion() {
  auto item = DownloadItem::Create(10, "doc.docx");
  CHECK(!item.CanOpenItem());
  CHECK(!item.CanOpenLocation());
  item.Complete();
  CHECK(item.CanOpenItem());
  CHECK(item.CanOpenLocation());
  CHECK(!item.Cancel());  // completed is terminal
  return true;
}

bool StateEnumClosure() {
  CHECK(IsValid(DownloadState::kPendingDangerConfirm));
  CHECK(IsValid(DownloadState::kInProgress));
  CHECK(IsValid(DownloadState::kPaused));
  CHECK(IsValid(DownloadState::kCompleted));
  CHECK(IsValid(DownloadState::kFailed));
  CHECK(IsValid(DownloadState::kCancelled));
  CHECK(!IsValid(static_cast<DownloadState>(42)));
  return true;
}

// ---------- Path sanitizing ----------

bool SanitizeStripsSeparatorsAndControls() {
  const auto clean = SanitizeDownloadFileName("../etc/passwd\x01.txt");
  CHECK(clean.has_value());
  CHECK(*clean == "..etcpasswd.txt");
  CHECK(SanitizeDownloadFileName("a/b\\c.txt") == std::optional<std::string>("abc.txt"));
  return true;
}

bool SanitizeRejectsEmptyAndDotOnly() {
  CHECK(!SanitizeDownloadFileName("").has_value());
  CHECK(!SanitizeDownloadFileName("/").has_value());
  CHECK(!SanitizeDownloadFileName("...").has_value());
  CHECK(!SanitizeDownloadFileName("..").has_value());
  return true;
}

bool SanitizeTrimsTrailingDotsAndSpaces() {
  CHECK(SanitizeDownloadFileName("name.txt...  ") ==
        std::optional<std::string>("name.txt"));
  return true;
}

bool SanitizeEnforcesLengthBound() {
  const std::string ok(kMaxFileNameLength, 'a');
  const std::string over = ok + "b";
  CHECK(SanitizeDownloadFileName(ok).has_value());
  CHECK(!SanitizeDownloadFileName(over).has_value());
  return true;
}

// ---------- Unique path resolution ----------

bool g_exists_result = false;
bool FakePathExists(const std::string&) { return g_exists_result; }

bool UniquePathResolvesWithoutCollision() {
  g_exists_result = false;
  const auto path =
      ResolveUniqueDownloadPath("/downloads", "report.pdf", &FakePathExists);
  CHECK(path == std::optional<std::string>("/downloads/report.pdf"));
  return true;
}

bool UniquePathDedupesCollisions() {
  g_exists_result = true;  // everything exists -> eventually exhausts
  CHECK(!ResolveUniqueDownloadPath("/downloads", "report.pdf",
                                   &FakePathExists)
             .has_value());
  return true;
}

bool UniquePathRejectsInvalidInput() {
  g_exists_result = false;
  CHECK(!ResolveUniqueDownloadPath("", "a.txt", &FakePathExists).has_value());
  CHECK(!ResolveUniqueDownloadPath("/d", "", &FakePathExists).has_value());
  CHECK(!ResolveUniqueDownloadPath("/d", "a.txt", nullptr).has_value());
  return true;
}

}  // namespace

int main() {
  if (!DangerClassificationMatrix() || !SafeItemStartsInProgress() ||
      !DangerousItemRequiresConfirmation() || !DangerousItemCanBeDiscarded() ||
      !ProgressIsBounded() || !UnknownTotalCompletes() || !PauseResumeCycle() ||
      !CancelIsTerminal() || !FailureAndRetry() || !FailedCanBeCancelled() ||
      !OpenActionsRequireCompletion() || !StateEnumClosure() ||
      !SanitizeStripsSeparatorsAndControls() ||
      !SanitizeRejectsEmptyAndDotOnly() ||
      !SanitizeTrimsTrailingDotsAndSpaces() || !SanitizeEnforcesLengthBound() ||
      !UniquePathResolvesWithoutCollision() || !UniquePathDedupesCollisions() ||
      !UniquePathRejectsInvalidInput()) {
    return 1;
  }
  return 0;
}
