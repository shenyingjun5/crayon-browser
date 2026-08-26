// BUX-17 autofill UI contract tests: save confirmation, incognito
// rules, suggestions, editor/delete lifecycle, locale parity, storm
// invariants.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "crayon/browser_autofill_ui/autofill_ui.h"

namespace {

using crayon::browser_autofill::AddressBookStore;
using crayon::browser_autofill::AddressRecord;
using crayon::browser_autofill::FieldKind;
using crayon::browser_autofill_ui::AddressEditorModel;
using crayon::browser_autofill_ui::AddressSuggestionModel;
using crayon::browser_autofill_ui::AutofillProfile;
using crayon::browser_autofill_ui::SaveAddressPromptModel;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

AddressRecord Sample(const std::string& name) {
  AddressRecord record;
  record.full_name = name;
  record.city = "Shenzhen";
  return record;
}

bool SavePromptFlow() {
  AddressBookStore store("profile:default");
  SaveAddressPromptModel model;
  CHECK(!model.pending());
  // Incognito refuses to even present.
  CHECK(!model.Present(Sample("Zhang San"), AutofillProfile::kIncognito));
  CHECK(!model.pending());
  // Invalid candidates are refused.
  AddressRecord invalid;
  invalid.city = "no name";
  CHECK(!model.Present(invalid, AutofillProfile::kNormal));
  // Normal flow: present -> accept stores exactly one record.
  CHECK(model.Present(Sample("Zhang San"), AutofillProfile::kNormal));
  CHECK(model.candidate() != nullptr);
  CHECK(model.Accept(&store, 100));
  CHECK(store.size() == 1);
  CHECK(!model.pending());
  // Decline path stores nothing.
  CHECK(model.Present(Sample("Li Si"), AutofillProfile::kNormal));
  CHECK(model.Decline());
  CHECK(!model.pending());
  CHECK(store.size() == 1);
  // Accept without Present fails.
  SaveAddressPromptModel idle;
  CHECK(!idle.Accept(&store, 200));
  return true;
}

bool SuggestionsAndIncognito() {
  AddressBookStore store("profile:default");
  store.Add(Sample("Wang Wu"), 10);
  store.Add(Sample("wang liu"), 20);
  AddressSuggestionModel model;
  // Incognito: empty suggestions and no store read at all.
  model.Open(AutofillProfile::kIncognito, store, FieldKind::kFullName, "");
  CHECK(model.suggestion_ids().empty());
  // Prefix ranking.
  model.Open(AutofillProfile::kNormal, store, FieldKind::kFullName, "wang");
  CHECK(model.suggestion_ids().size() == 2);
  const AddressRecord* picked = model.Pick(store, model.suggestion_ids()[0]);
  CHECK(picked != nullptr);
  CHECK(picked->city == "Shenzhen");
  // Picking an id that was never suggested is refused even if it exists.
  AddressRecord ghost = Sample("Ghost");
  store.Add(ghost, 30);
  CHECK(model.Pick(store, "addr-3") == nullptr);
  return true;
}

bool EditorLifecycle() {
  AddressBookStore store("profile:default");
  AddressEditorModel editor;
  // Commit before BeginEdit fails.
  CHECK(!editor.Commit(&store, 1));
  // Create-new flow with field-wise editing.
  CHECK(editor.BeginEdit(AddressRecord{}));
  CHECK(editor.SetField(FieldKind::kFullName, "Zhao Liu"));
  CHECK(editor.SetField(FieldKind::kPhone, "+86 13800000000"));
  // Control bytes refused per-field.
  CHECK(!editor.SetField(FieldKind::kCity, "bad\ncity"));
  CHECK(editor.Commit(&store, 500));
  CHECK(store.size() == 1);
  CHECK(editor.state() == AddressEditorModel::State::kNone);
  // Edit-existing flow keeps the id.
  const AddressRecord* existing = store.Find("addr-1");
  CHECK(editor.BeginEdit(*existing));
  CHECK(editor.SetField(FieldKind::kCity, "Hangzhou"));
  CHECK(editor.Commit(&store, 600));
  CHECK(store.Find("addr-1")->city == "Hangzhou");
  // Delete flow requires the two explicit steps.
  CHECK(editor.BeginDelete(store, "addr-1"));
  CHECK(editor.ConfirmDelete(&store));
  CHECK(store.size() == 0);
  CHECK(!editor.BeginDelete(store, "missing"));
  // Cancel clears any in-flight state.
  const AddressRecord draft_record = Sample("Draft");
  CHECK(editor.BeginEdit(draft_record));
  editor.Cancel();
  CHECK(editor.state() == AddressEditorModel::State::kNone);
  return true;
}

bool LocaleParity() {
  const char* repo_root = std::getenv("CRAYON_REPO_ROOT");
  CHECK(repo_root != nullptr);
  auto extract = [](const std::string& path, std::set<std::string>* keys,
                    bool* ok) {
    std::ifstream input(path);
    if (!input) {
      *ok = false;
      return;
    }
    std::string line;
    while (std::getline(input, line)) {
      const auto q1 = line.find('"');
      if (q1 == std::string::npos) continue;
      const auto q2 = line.find('"', q1 + 1);
      if (q2 == std::string::npos) continue;
      if (line.find(':', q2 + 1) == std::string::npos) continue;
      keys->insert(line.substr(q1 + 1, q2 - q1 - 1));
    }
    *ok = true;
  };
  bool ok_en = false;
  bool ok_zh = false;
  std::set<std::string> en;
  std::set<std::string> zh;
  extract(std::string(repo_root) + "/browser/shared-ui/locales/en-US.json",
          &en, &ok_en);
  extract(std::string(repo_root) + "/browser/shared-ui/locales/zh-CN.json",
          &zh, &ok_zh);
  CHECK(ok_en);
  CHECK(ok_zh);
  CHECK(en == zh);
  static const char* kRequired[] = {
      "autofill.save.title",
      "autofill.save.body",
      "autofill.save.accept",
      "autofill.save.decline",
      "autofill.suggest.title",
      "autofill.edit.title",
      "autofill.edit.delete",
      "autofill.delete.confirm_title",
      "autofill.delete.confirm_body",
      "autofill.incognito.notice",
      "autofill.field.full_name",
      "autofill.field.organization",
      "autofill.field.street_line1",
      "autofill.field.street_line2",
      "autofill.field.city",
      "autofill.field.state",
      "autofill.field.postal_code",
      "autofill.field.country_region",
      "autofill.field.phone",
      "autofill.field.email",
  };
  for (const char* key : kRequired) {
    CHECK(en.count(key) == 1);
    CHECK(zh.count(key) == 1);
  }
  return true;
}

bool StormInvariants() {
  AddressBookStore store("profile:default");
  SaveAddressPromptModel prompt;
  AddressSuggestionModel suggestions;
  unsigned long long state = 0xD1B54A32D192ED03ULL;
  auto next = [&state]() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
  };
  for (int step = 0; step < 3000; ++step) {
    switch (next() % 5) {
      case 0:
        prompt.Present(Sample("storm" + std::to_string(next() % 50)),
                       next() % 2 == 0 ? AutofillProfile::kNormal
                                       : AutofillProfile::kIncognito);
        break;
      case 1:
        prompt.Decline();
        break;
      case 2:
        prompt.Accept(&store, static_cast<unsigned long long>(next()));
        break;
      case 3:
        suggestions.Open(AutofillProfile::kNormal, store,
                         FieldKind::kFullName, "s");
        for (const std::string& id : suggestions.suggestion_ids()) {
          CHECK(suggestions.Pick(store, id) != nullptr);
        }
        break;
      default:
        if (store.size() > 0) {
          store.Delete(store.AllSorted().front().record_id);
        }
        break;
    }
    CHECK(store.size() <= crayon::browser_autofill::kMaxRecords);
  }
  return true;
}

}  // namespace

int main() {
  bool ok = true;
  ok = SavePromptFlow() && ok;
  ok = SuggestionsAndIncognito() && ok;
  ok = EditorLifecycle() && ok;
  ok = LocaleParity() && ok;
  ok = StormInvariants() && ok;
  if (!ok) {
    std::cerr << "autofill_ui contract test FAILED\n";
    return 1;
  }
  std::cout << "autofill_ui contract test passed\n";
  return 0;
}
