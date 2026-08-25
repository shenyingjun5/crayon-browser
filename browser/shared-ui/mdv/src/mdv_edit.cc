#include "crayon/browser_mdv/mdv_edit.h"

namespace crayon::browser_mdv_edit {

bool MdvEditModel::ApplyEdit(const std::string& content, std::uint64_t now_ms) {
  if (confirm_ == ConfirmState::kPending) {
    return false;  // no edits while the blocking dialog is open
  }
  if (!has_document_) {
    return false;
  }
  edit_buffer_ = content;
  dirty_ = true;
  // The viewer's debounce + revision fencing owns render scheduling.
  static_cast<void>(viewer_->RequestRender(now_ms));
  return true;
}

bool MdvEditModel::BeginBlockingTransition() {
  if (confirm_ == ConfirmState::kPending) {
    return true;  // already blocking
  }
  if (!dirty_) {
    confirm_ = ConfirmState::kNotNeeded;
    return false;  // nothing to protect: transition proceeds directly
  }
  confirm_ = ConfirmState::kPending;
  decision_ = DirtyDecision::kNone;
  return true;
}

bool MdvEditModel::ResolveTransition(DirtyDecision choice) {
  if (confirm_ != ConfirmState::kPending || choice == DirtyDecision::kNone) {
    return false;
  }
  decision_ = choice;
  confirm_ = ConfirmState::kResolved;
  switch (choice) {
    case DirtyDecision::kSaveAndContinue:
      // Keep buffer and dirty state; the save (MDV-06) clears dirty on
      // success.  The transition stays blocked until the save lands.
      confirm_ = ConfirmState::kPending;
      break;
    case DirtyDecision::kDiscard:
      dirty_ = false;
      edit_buffer_.clear();
      break;
    case DirtyDecision::kCancel:
      // Keep everything; the user stays in the editor.
      confirm_ = ConfirmState::kNotNeeded;
      break;
    case DirtyDecision::kNone:
      return false;
  }
  return true;
}

void MdvEditModel::NotifySaveSucceeded() {
  if (dirty_) {
    dirty_ = false;
  }
  if (confirm_ == ConfirmState::kPending && decision_ == DirtyDecision::kSaveAndContinue) {
    confirm_ = ConfirmState::kNotNeeded;
    decision_ = DirtyDecision::kNone;
  }
}

void MdvEditModel::LoadDocument(const std::string& content, bool utf8_valid,
                                std::uint64_t now_ms) {
  // Only callable once the transition is not blocked (clean, or the
  // user chose discard / completed save-and-continue).
  if (confirm_ == ConfirmState::kPending) {
    return;
  }
  edit_buffer_ = content;
  dirty_ = false;
  has_document_ = true;
  confirm_ = ConfirmState::kNotNeeded;
  static_cast<void>(viewer_->LoadContent(content, utf8_valid, now_ms));
}

}  // namespace crayon::browser_mdv_edit
