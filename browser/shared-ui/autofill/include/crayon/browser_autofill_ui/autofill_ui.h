// BUX-17: address autofill UI view models (UX-017).
//
// Three user flows over the address domain model:
// - save confirmation: a visible prompt before anything is stored;
//   refused outright in incognito windows;
// - field suggestions: deterministic matches surfaced one field at a
//   time; unavailable in incognito windows;
// - editor/delete: bounded editing and an explicit two-step delete.
//
// The models never persist anything themselves; they drive the
// profile-scoped `AddressBookStore`.  Values shown in the UI are the
// user's own data on the user's own screen — diagnostics must use
// `RedactedSummary()` instead.  Nothing here is agent-visible.
//
// Thread contract: single-threaded, UI thread only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "crayon/browser_autofill/address_book.h"

namespace crayon::browser_autofill_ui {

using crayon::browser_autofill::AddressBookStore;
using crayon::browser_autofill::AddressRecord;
using crayon::browser_autofill::FieldKind;

/// Window profile class driving the incognito rules.
enum class AutofillProfile { kNormal = 0, kIncognito };

/// Save confirmation flow: Present -> Accept/Decline.
class SaveAddressPromptModel final {
 public:
  /// Presents the candidate for confirmation.  Fails in incognito
  /// windows (nothing may be saved from incognito).
  bool Present(const AddressRecord& candidate, AutofillProfile profile);

  /// Inserts into `store`; fails unless presented.  Returns false when
  /// the store refuses (invalid/full).  Either terminal call clears the
  /// pending candidate.
  bool Accept(AddressBookStore* store, std::uint64_t now_ms);

  bool Decline();

  [[nodiscard]] bool pending() const { return pending_; }

  [[nodiscard]] const AddressRecord* candidate() const {
    return pending_ ? &candidate_ : nullptr;
  }

 private:
  bool pending_ = false;
  AddressRecord candidate_;
};

/// Field suggestion flow: Open -> Pick.
class AddressSuggestionModel final {
 public:
  /// Computes suggestions for one typed field.  Incognito yields an
  /// empty list without touching the store; otherwise the store's
  /// records are matched deterministically.
  void Open(AutofillProfile profile, const AddressBookStore& store,
            FieldKind field, const std::string& typed_prefix);

  /// Resolves one suggested id to its record for filling the field;
  /// nullptr when unknown or not opened.
  [[nodiscard]] const AddressRecord* Pick(
      const AddressBookStore& store, const std::string& record_id) const;

  [[nodiscard]] const std::vector<std::string>& suggestion_ids() const {
    return suggestion_ids_;
  }

 private:
  std::vector<std::string> suggestion_ids_;
};

/// Edit / delete flow: BeginEdit -> SetField* -> Commit, or
/// BeginDelete -> ConfirmDelete (two-step, explicit).
class AddressEditorModel final {
 public:
  enum class State { kNone = 0, kEditing, kDeleting };

  /// Starts editing a copy of `record` (empty id means "create new").
  bool BeginEdit(const AddressRecord& record);

  /// Sets one field; validated immediately against the closed rules.
  bool SetField(FieldKind kind, const std::string& value);

  /// Commits to the store: insert when the id is empty, update
  /// otherwise.  Terminal; leaves the edit state.
  bool Commit(AddressBookStore* store, std::uint64_t now_ms);

  /// Starts deleting `record_id`; requires an existing record.
  bool BeginDelete(const AddressBookStore& store,
                   const std::string& record_id);

  /// Performs the deletion; returns false when nothing was deleted.
  bool ConfirmDelete(AddressBookStore* store);

  void Cancel();

  [[nodiscard]] State state() const { return state_; }
  [[nodiscard]] const AddressRecord* draft() const {
    return state_ == State::kEditing ? &draft_ : nullptr;
  }
  [[nodiscard]] const std::string& delete_target_id() const {
    return delete_target_id_;
  }

 private:
  State state_ = State::kNone;
  AddressRecord draft_;
  std::string delete_target_id_;
};

}  // namespace crayon::browser_autofill_ui
