// BUX-17 address autofill UI view model implementation.
#include "crayon/browser_autofill_ui/autofill_ui.h"

namespace crayon::browser_autofill_ui {

// --- SaveAddressPromptModel ---

bool SaveAddressPromptModel::Present(const AddressRecord& candidate,
                                     AutofillProfile profile) {
  if (profile == AutofillProfile::kIncognito) {
    // Incognito never saves; the prompt is not even offered.
    pending_ = false;
    candidate_ = AddressRecord{};
    return false;
  }
  if (!candidate.Validate() || !candidate.record_id.empty()) {
    return false;
  }
  pending_ = true;
  candidate_ = candidate;
  return true;
}

bool SaveAddressPromptModel::Accept(AddressBookStore* store,
                                    std::uint64_t now_ms) {
  if (!pending_ || store == nullptr) {
    return false;
  }
  const bool stored =
      store->Add(candidate_, now_ms) ==
      AddressBookStore::AddResult::kOk;
  pending_ = false;
  candidate_ = AddressRecord{};
  return stored;
}

bool SaveAddressPromptModel::Decline() {
  if (!pending_) {
    return false;
  }
  pending_ = false;
  candidate_ = AddressRecord{};
  return true;
}

// --- AddressSuggestionModel ---

void AddressSuggestionModel::Open(AutofillProfile profile,
                                  const AddressBookStore& store,
                                  FieldKind field,
                                  const std::string& typed_prefix) {
  suggestion_ids_.clear();
  if (profile == AutofillProfile::kIncognito) {
    // Incognito neither suggests nor reads the store.
    return;
  }
  suggestion_ids_ = crayon::browser_autofill::MatchRecordIds(
      store.AllSorted(), field, typed_prefix);
}

const AddressRecord* AddressSuggestionModel::Pick(
    const AddressBookStore& store, const std::string& record_id) const {
  for (const std::string& id : suggestion_ids_) {
    if (id == record_id) {
      return store.Find(record_id);
    }
  }
  return nullptr;
}

// --- AddressEditorModel ---

bool AddressEditorModel::BeginEdit(const AddressRecord& record) {
  if (state_ != State::kNone) {
    return false;
  }
  // Create-new drafts start empty and are validated field-by-field via
  // SetField plus a final Commit check; editing an existing record
  // requires it to be valid as-is.
  if (!record.record_id.empty() && !record.Validate()) {
    return false;
  }
  if (crayon::browser_autofill::IsValidFieldValue(record.record_id) ==
      false) {
    return false;
  }
  draft_ = record;
  state_ = State::kEditing;
  return true;
}

bool AddressEditorModel::SetField(FieldKind kind, const std::string& value) {
  if (state_ != State::kEditing) {
    return false;
  }
  if (!crayon::browser_autofill::IsValidFieldValue(value)) {
    return false;
  }
  switch (kind) {
    case FieldKind::kFullName:
      draft_.full_name = value;
      break;
    case FieldKind::kOrganization:
      draft_.organization = value;
      break;
    case FieldKind::kStreetLine1:
      draft_.street_line1 = value;
      break;
    case FieldKind::kStreetLine2:
      draft_.street_line2 = value;
      break;
    case FieldKind::kCity:
      draft_.city = value;
      break;
    case FieldKind::kState:
      draft_.state = value;
      break;
    case FieldKind::kPostalCode:
      draft_.postal_code = value;
      break;
    case FieldKind::kCountryRegion:
      draft_.country_region = value;
      break;
    case FieldKind::kPhone:
      draft_.phone = value;
      break;
    case FieldKind::kEmail:
      draft_.email = value;
      break;
  }
  return true;
}

bool AddressEditorModel::Commit(AddressBookStore* store,
                                std::uint64_t now_ms) {
  if (state_ != State::kEditing || store == nullptr || !draft_.Validate()) {
    return false;
  }
  const bool ok = draft_.record_id.empty()
                      ? store->Add(draft_, now_ms) ==
                            AddressBookStore::AddResult::kOk
                      : store->Update(draft_, now_ms);
  if (ok) {
    draft_ = AddressRecord{};
    state_ = State::kNone;
  }
  return ok;
}

bool AddressEditorModel::BeginDelete(const AddressBookStore& store,
                                     const std::string& record_id) {
  if (state_ != State::kNone || store.Find(record_id) == nullptr) {
    return false;
  }
  delete_target_id_ = record_id;
  state_ = State::kDeleting;
  return true;
}

bool AddressEditorModel::ConfirmDelete(AddressBookStore* store) {
  if (state_ != State::kDeleting || store == nullptr) {
    return false;
  }
  const bool deleted = store->Delete(delete_target_id_);
  delete_target_id_.clear();
  state_ = State::kNone;
  return deleted;
}

void AddressEditorModel::Cancel() {
  if (state_ != State::kNone) {
    draft_ = AddressRecord{};
    delete_target_id_.clear();
    state_ = State::kNone;
  }
}

}  // namespace crayon::browser_autofill_ui
