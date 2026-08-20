#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_downloads_view/download_shelf_state_machine.h"

namespace {

using crayon::browser_downloads::DownloadState;
using crayon::browser_downloads_view::DownloadProjection;
using crayon::browser_downloads_view::DownloadShelfStateMachine;
using crayon::browser_downloads_view::kMaxVisibleDownloads;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

DownloadProjection MakeProjection(std::uint64_t id,
                                  DownloadState state,
                                  unsigned percent = 0) {
  DownloadProjection p;
  p.download_id = id;
  p.display_name = "file-" + std::to_string(id) + ".bin";
  p.state = state;
  p.percent = percent;
  return p;
}

bool StartOpensShelfAndAddsItem() {
  DownloadShelfStateMachine sm;
  CHECK(!sm.shelf_open());
  CHECK(sm.OnDownloadStarted(
      MakeProjection(1, DownloadState::kInProgress, 10)));
  CHECK(sm.shelf_open());
  CHECK(sm.items().size() == 1);
  CHECK(sm.active_count() == 1);
  return true;
}

bool InvalidProjectionsRejected() {
  DownloadShelfStateMachine sm;
  auto zero_id = MakeProjection(0, DownloadState::kInProgress);
  CHECK(!sm.OnDownloadStarted(zero_id));
  auto empty_name = MakeProjection(1, DownloadState::kInProgress);
  empty_name.display_name.clear();
  CHECK(!sm.OnDownloadStarted(empty_name));
  auto bad_percent = MakeProjection(2, DownloadState::kInProgress, 101);
  CHECK(!sm.OnDownloadStarted(bad_percent));
  CHECK(sm.items().empty());
  return true;
}

bool DuplicateIdsRejected() {
  DownloadShelfStateMachine sm;
  CHECK(sm.OnDownloadStarted(MakeProjection(1, DownloadState::kInProgress)));
  CHECK(!sm.OnDownloadStarted(
      MakeProjection(1, DownloadState::kInProgress)));
  CHECK(sm.items().size() == 1);
  return true;
}

bool CapacityEnforced() {
  DownloadShelfStateMachine sm;
  for (std::size_t i = 1; i <= kMaxVisibleDownloads; ++i) {
    CHECK(sm.OnDownloadStarted(
        MakeProjection(i, DownloadState::kCompleted)));
  }
  CHECK(!sm.OnDownloadStarted(
      MakeProjection(kMaxVisibleDownloads + 1, DownloadState::kCompleted)));
  return true;
}

bool UpdateReplacesProjection() {
  DownloadShelfStateMachine sm;
  sm.OnDownloadStarted(MakeProjection(1, DownloadState::kInProgress, 10));
  CHECK(sm.OnDownloadUpdated(
      MakeProjection(1, DownloadState::kInProgress, 60)));
  CHECK(sm.Find(1) != nullptr && sm.Find(1)->percent == 60);
  // Unknown IDs are ignored.
  CHECK(!sm.OnDownloadUpdated(
      MakeProjection(99, DownloadState::kInProgress, 60)));
  return true;
}

bool RemoveDropsProjection() {
  DownloadShelfStateMachine sm;
  sm.OnDownloadStarted(MakeProjection(1, DownloadState::kFailed));
  CHECK(sm.active_count() == 1);  // failed is not finished
  CHECK(sm.OnDownloadRemoved(1)); // UI-side release after failure
  CHECK(sm.items().empty());
  CHECK(!sm.OnDownloadRemoved(1));
  CHECK(!sm.OnDownloadRemoved(42));
  return true;
}

bool ClearCompletedKeepsActive() {
  DownloadShelfStateMachine sm;
  sm.OnDownloadStarted(MakeProjection(1, DownloadState::kCompleted, 100));
  sm.OnDownloadStarted(MakeProjection(2, DownloadState::kCancelled));
  sm.OnDownloadStarted(MakeProjection(3, DownloadState::kInProgress, 50));
  sm.OnDownloadStarted(MakeProjection(4, DownloadState::kPaused, 50));
  CHECK(sm.ClearCompleted() == 2);
  CHECK(sm.items().size() == 2);
  CHECK(sm.Find(1) == nullptr);
  CHECK(sm.Find(3) != nullptr);
  CHECK(sm.active_count() == 2);
  return true;
}

bool ShelfVisibilityControlled() {
  DownloadShelfStateMachine sm;
  sm.OpenShelf();
  CHECK(sm.shelf_open());
  sm.CloseShelf();
  CHECK(!sm.shelf_open());
  return true;
}

bool ShutdownRejectsEverything() {
  DownloadShelfStateMachine sm;
  sm.OnDownloadStarted(MakeProjection(1, DownloadState::kInProgress));
  sm.Shutdown();
  CHECK(!sm.active());
  CHECK(sm.items().empty());
  CHECK(!sm.shelf_open());
  CHECK(!sm.OnDownloadStarted(
      MakeProjection(2, DownloadState::kInProgress)));
  CHECK(!sm.OnDownloadUpdated(
      MakeProjection(1, DownloadState::kCompleted, 100)));
  CHECK(!sm.OnDownloadRemoved(1));
  CHECK(sm.Find(1) == nullptr);
  sm.OpenShelf();
  CHECK(!sm.shelf_open());
  return true;
}

}  // namespace

int main() {
  if (!StartOpensShelfAndAddsItem() || !InvalidProjectionsRejected() ||
      !DuplicateIdsRejected() || !CapacityEnforced() ||
      !UpdateReplacesProjection() || !RemoveDropsProjection() ||
      !ClearCompletedKeepsActive() || !ShelfVisibilityControlled() ||
      !ShutdownRejectsEverything()) {
    return 1;
  }
  return 0;
}
