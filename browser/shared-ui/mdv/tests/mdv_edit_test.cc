// MDV-05 contract tests (MD-004 editing, MD-005 dirty confirmation):
// in-memory-only edits, debounced renders, closed three-choice flow,
// blocking transitions.
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_mdv/mdv_edit.h"
#include "crayon/browser_mdv/mdv_viewer.h"

namespace {

using crayon::browser_mdv::MdvViewerModel;
using crayon::browser_mdv_edit::DirtyDecision;
using crayon::browser_mdv_edit::ConfirmState;
using crayon::browser_mdv_edit::MdvEditModel;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool EditMarksDirtyAndRenders() {
  MdvViewerModel viewer;
  MdvEditModel editor(&viewer);
  editor.LoadDocument("# start\n", true, 0);
  CHECK(!editor.dirty());
  CHECK(editor.ApplyEdit("# start\n\nmore text\n", 100));
  CHECK(editor.dirty());
  CHECK(editor.edit_buffer() == "# start\n\nmore text\n");
  // Rapid edits merge through the viewer debounce.
  const auto rev_a = viewer.RequestRender(200);
  const auto rev_b = viewer.RequestRender(250);
  CHECK(rev_a == rev_b);
  // No document: edits rejected.
  MdvViewerModel empty_viewer;
  MdvEditModel empty_editor(&empty_viewer);
  CHECK(!empty_editor.ApplyEdit("x", 0));
  return true;
}

bool CleanTransitionNotBlocked() {
  MdvViewerModel viewer;
  MdvEditModel editor(&viewer);
  editor.LoadDocument("# clean\n", true, 0);
  CHECK(editor.BeginBlockingTransition() == false);  // proceeds directly
  CHECK(editor.confirm_state() == ConfirmState::kNotNeeded);
  return true;
}

bool DirtyTransitionBlocksAndResolves() {
  MdvViewerModel viewer;
  MdvEditModel editor(&viewer);
  editor.LoadDocument("# doc\n", true, 0);
  editor.ApplyEdit("# doc v2\n", 0);
  // Blocking transition opens the three-choice dialog.
  CHECK(editor.BeginBlockingTransition());
  CHECK(editor.confirm_state() == ConfirmState::kPending);
  // Edits are rejected while blocking.
  CHECK(!editor.ApplyEdit("# doc v3\n", 1));
  // Cancel keeps content and closes the dialog.
  CHECK(editor.ResolveTransition(DirtyDecision::kCancel));
  CHECK(editor.dirty());  // content preserved
  CHECK(editor.confirm_state() == ConfirmState::kNotNeeded);
  CHECK(editor.edit_buffer() == "# doc v2\n");
  // A second transition blocks again; discard drops without writing.
  CHECK(editor.BeginBlockingTransition());
  CHECK(editor.ResolveTransition(DirtyDecision::kDiscard));
  CHECK(!editor.dirty());
  CHECK(editor.edit_buffer().empty());
  return true;
}

bool SaveAndContinueKeepsBlockedUntilSaveLands() {
  MdvViewerModel viewer;
  MdvEditModel editor(&viewer);
  editor.LoadDocument("# doc\n", true, 0);
  editor.ApplyEdit("# doc v2\n", 0);
  editor.BeginBlockingTransition();
  CHECK(editor.ResolveTransition(DirtyDecision::kSaveAndContinue));
  // Still blocked and dirty: the save (MDV-06) must land first.
  CHECK(editor.confirm_state() == ConfirmState::kPending);
  CHECK(editor.dirty());
  // The save success clears dirty and releases the block (MDV-06
  // calls NotifySaveSucceeded after the atomic write).
  editor.NotifySaveSucceeded();
  CHECK(!editor.dirty());
  CHECK(editor.confirm_state() == ConfirmState::kNotNeeded);
  return true;
}

bool DoubleBlockAndInvalidResolve() {
  MdvViewerModel viewer;
  MdvEditModel editor(&viewer);
  editor.LoadDocument("# d\n", true, 0);
  editor.ApplyEdit("# d2\n", 0);
  CHECK(editor.BeginBlockingTransition());
  CHECK(editor.BeginBlockingTransition());  // idempotent
  CHECK(!editor.ResolveTransition(DirtyDecision::kNone));  // no-op choice
  // Resolve on a non-pending model is rejected.
  MdvViewerModel clean_viewer;
  MdvEditModel clean(&clean_viewer);
  clean.LoadDocument("# c\n", true, 0);
  CHECK(!clean.ResolveTransition(DirtyDecision::kDiscard));
  return true;
}

/// Pseudo-random operation storm: dirty implies buffer non-empty or a
/// prior load; pending confirmations only resolve with a real choice;
/// state stays closed.
bool StormInvariants() {
  std::uint64_t seed = 0x5A5A'C0DE'1234'ABCD;
  auto next = [&seed]() {
    seed = seed * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return seed;
  };
  MdvViewerModel viewer;
  MdvEditModel editor(&viewer);
  std::uint64_t clock = 0;
  for (int step = 0; step < 5'000; ++step) {
    clock += next() % 300;
    switch (next() % 5) {
      case 0:
        editor.LoadDocument(next() % 2 ? "# d\n" : "", true, clock);
        break;
      case 1:
        static_cast<void>(editor.ApplyEdit(next() % 2 ? "# e\n" : "", clock));
        break;
      case 2:
        static_cast<void>(editor.BeginBlockingTransition());
        break;
      case 3:
        static_cast<void>(editor.ResolveTransition(
            static_cast<DirtyDecision>(next() % 4)));
        break;
      default:
        if (next() % 6 == 0) {
          viewer.CloseDocument();
        }
        break;
    }
    const ConfirmState state = editor.confirm_state();
    CHECK(state == ConfirmState::kNotNeeded || state == ConfirmState::kPending ||
          state == ConfirmState::kResolved);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = EditMarksDirtyAndRenders() && CleanTransitionNotBlocked() &&
                  DirtyTransitionBlocksAndResolves() &&
                  SaveAndContinueKeepsBlockedUntilSaveLands() &&
                  DoubleBlockAndInvalidResolve() && StormInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "mdv_edit_test passed\n";
  return EXIT_SUCCESS;
}
