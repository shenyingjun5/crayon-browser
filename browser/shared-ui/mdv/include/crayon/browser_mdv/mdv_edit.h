// MDV-05: split-pane editing state machine (MDV-01 §7–§8).
//
// Editing produces in-memory dirty state only — nothing ever writes
// to disk from this model (MDV-06 owns save semantics; this model
// only exposes the save-request hook).  Every edit funnels through
// the MDV-03 render debounce with revision fencing, and closing,
// switching or navigating with dirty state forces the closed
// three-choice confirmation (save-and-continue / discard / cancel);
// cancel keeps content, discard never writes (MD-005).
//
// Thread contract: single-threaded, UI thread only.
#pragma once

#include <cstdint>
#include <string>

#include "crayon/browser_mdv/mdv_viewer.h"

namespace crayon::browser_mdv_edit {

/// Closed dirty-confirmation choices.
enum class DirtyDecision { kNone = 0, kSaveAndContinue, kDiscard, kCancel };

/// Closed confirmation flow states.
enum class ConfirmState {
  kNotNeeded = 0,  // clean or no document
  kPending,        // three-choice dialog open
  kResolved,       // decision applied; model left the blocking state
};

/// Split-pane editing model over the read-only viewer.
class MdvEditModel final {
 public:
  explicit MdvEditModel(crayon::browser_mdv::MdvViewerModel* viewer)
      : viewer_(viewer) {}

  /// Applies one edit burst (keystroke batch / paste).  Updates the
  /// in-memory buffer, marks dirty and returns the render revision the
  /// editor should render (debounced by the viewer).  Edits are
  /// rejected while a confirmation is pending.
  bool ApplyEdit(const std::string& content, std::uint64_t now_ms);

  /// Begins a blocking transition (close tab / switch file / navigate).
  /// Returns false when there is nothing dirty (transition may proceed
  /// immediately); otherwise the dialog opens.
  bool BeginBlockingTransition();

  /// Applies the user's choice.  kSaveAndContinue requests the save
  /// (MDV-06 hook) and keeps editing; kDiscard drops the buffer;
  /// kCancel keeps everything and closes the dialog.  Returns false
  /// when no confirmation is pending.
  bool ResolveTransition(DirtyDecision decision);

  /// The MDV-06 save flow calls this after the atomic write succeeds:
  /// dirty clears and a save-and-continue transition is released.
  void NotifySaveSucceeded();

  /// Loads new content after a non-blocked (or confirmed) transition.
  void LoadDocument(const std::string& content, bool utf8_valid, std::uint64_t now_ms);

  bool dirty() const { return dirty_; }
  ConfirmState confirm_state() const { return confirm_; }
  const std::string& edit_buffer() const { return edit_buffer_; }
  DirtyDecision decision() const { return decision_; }

 private:
  crayon::browser_mdv::MdvViewerModel* viewer_;
  bool dirty_ = false;
  bool has_document_ = false;
  ConfirmState confirm_ = ConfirmState::kNotNeeded;
  DirtyDecision decision_ = DirtyDecision::kNone;
  std::string edit_buffer_;
};

}  // namespace crayon::browser_mdv_edit
