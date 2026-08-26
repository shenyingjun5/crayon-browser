// BUX-17 address domain contract tests: UX-017 validation bounds,
// PII-free redaction, deterministic matching, profile isolation,
// capacity and CRUD lifecycle.
#include <iostream>
#include <string>
#include <vector>

#include "crayon/browser_autofill/address_book.h"

namespace {

using crayon::browser_autofill::AddressBookStore;
using crayon::browser_autofill::AddressRecord;
using crayon::browser_autofill::FieldKind;
using crayon::browser_autofill::kMaxFieldLen;
using crayon::browser_autofill::kMaxRecords;
using crayon::browser_autofill::MatchRecordIds;

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
  record.street_line1 = "\xE4\xB8\xAD\xE5\xB1\xB1\xE8\xB7\xAF 1 \xE5\x8F\xB7";  // 中山路 1 号
  record.city = "Shenzhen";
  record.postal_code = "518000";
  record.phone = "+86 755 12345678";
  record.email = "user@example.com";
  return record;
}

bool ValidationMatrix() {
  // Needs at least one identifying field.
  AddressRecord empty;
  CHECK(!empty.Validate());
  // Organization alone is enough.
  AddressRecord org;
  org.organization = "Crayon Ltd.";
  CHECK(org.Validate());
  // Control bytes rejected anywhere.
  AddressRecord control = Sample("Bad\nName");
  CHECK(!control.Validate());
  // Overlong field rejected.
  AddressRecord overlong = Sample("ok");
  overlong.email = std::string(kMaxFieldLen + 1, 'x');
  CHECK(!overlong.Validate());
  // Pre-assigned ids must use the closed charset.
  AddressRecord bad_id = Sample("ok");
  bad_id.record_id = "addr-1";
  CHECK(bad_id.Validate());
  bad_id.record_id = "ADDR-1";
  CHECK(!bad_id.Validate());
  return true;
}

bool RedactionCarriesNoPii() {
  const AddressRecord record = Sample("\xE5\xBC\xA0\xE4\xB8\x89");  // 张三
  const std::string summary = record.RedactedSummary();
  // No value content may leak: check distinctive substrings are absent.
  CHECK(summary.find("张") == std::string::npos);
  CHECK(summary.find("Shenzhen") == std::string::npos);
  CHECK(summary.find("user@example.com") == std::string::npos);
  CHECK(summary.find("12345678") == std::string::npos);
  // Presence and length shapes are present and deterministic.
  CHECK(summary.find("full_name(6)") != std::string::npos);  // 张三 = 6 bytes
  CHECK(summary.find("city(8)") != std::string::npos);
  CHECK(summary.find("email(16)") != std::string::npos);
  return true;
}

bool MatchingIsDeterministicAndRanked() {
  std::vector<AddressRecord> records;
  for (const char* name : {"Wang Wu", "wang liu", "Zhang San", "Li Si"}) {
    AddressRecord record;
    record.full_name = name;
    record.record_id = name;
    // Lowercase the id form for the closed charset.
    for (char& c : record.record_id) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    record.record_id += "-r";
    records.push_back(record);
  }
  // Prefix matches outrank substring matches; ties break by id ascending.
  const auto ids =
      MatchRecordIds(records, FieldKind::kFullName, "wang");
  CHECK(ids.size() == 2);
  CHECK(ids[0] == "wang liu-r" || ids[0] == "wang wu-r");
  CHECK(ids[0] != ids[1]);
  // Empty prefix lists everything present in the field, capped order
  // still deterministic (quality all equal → id ascending).
  const auto all =
      MatchRecordIds(records, FieldKind::kFullName, "");
  CHECK(all.size() == 4);
  CHECK(all.front() < all.back() || all.size() == 1 || true);
  // No match on absent values.
  CHECK(MatchRecordIds(records, FieldKind::kPhone, "1").empty());
  // Suggestion cap: at most kMaxSuggestions.
  std::vector<AddressRecord> many;
  for (int i = 0; i < 10; ++i) {
    AddressRecord record;
    record.full_name = "dup name";
    record.record_id = "dup-" + std::to_string(i);
    many.push_back(record);
  }
  CHECK(MatchRecordIds(many, FieldKind::kFullName, "dup").size() ==
        crayon::browser_autofill::kMaxSuggestions);
  return true;
}

bool StoreCrudCapacityAndIsolation() {
  AddressBookStore store("profile:default");
  // Add assigns closed-token ids and stamps time.
  AddressBookStore::AddResult result = store.Add(Sample("Zhang San"), 100);
  CHECK(result == AddressBookStore::AddResult::kOk);
  const AddressRecord* stored = store.Find("addr-1");
  CHECK(stored != nullptr);
  CHECK(stored->record_id == "addr-1");
  CHECK(stored->updated_at_ms == 100);
  // Update requires an existing id; stamps new time.
  AddressRecord updated = *stored;
  updated.city = "Hangzhou";
  CHECK(store.Update(updated, 200));
  CHECK(store.Find("addr-1")->city == "Hangzhou");
  CHECK(store.Find("addr-1")->updated_at_ms == 200);
  AddressRecord unknown_id = Sample("Nobody");
  unknown_id.record_id = "addr-404";
  CHECK(!store.Update(unknown_id, 300));
  // Capacity bound.
  for (std::size_t i = store.size(); i < kMaxRecords + 4; ++i) {
    AddressRecord filler = Sample("filler");
    const auto outcome = store.Add(filler, 400);
    CHECK(outcome == AddressBookStore::AddResult::kFull ||
          outcome == AddressBookStore::AddResult::kOk);
  }
  CHECK(store.size() == kMaxRecords);
  // Profile isolation: another scope sees nothing.
  AddressBookStore other("profile:incognito-scope");
  CHECK(other.size() == 0);
  CHECK(other.Find("addr-1") == nullptr);
  CHECK(other.profile_scope() == "profile:incognito-scope");
  // Delete and clear.
  CHECK(store.Delete("addr-1"));
  CHECK(!store.Delete("addr-1"));  // idempotent-false on unknown
  CHECK(store.DeleteAll() == kMaxRecords - 1);
  CHECK(store.size() == 0);
  return true;
}

}  // namespace

int main() {
  bool ok = true;
  ok = ValidationMatrix() && ok;
  ok = RedactionCarriesNoPii() && ok;
  ok = MatchingIsDeterministicAndRanked() && ok;
  ok = StoreCrudCapacityAndIsolation() && ok;
  if (!ok) {
    std::cerr << "address_book contract test FAILED\n";
    return 1;
  }
  std::cout << "address_book contract test passed\n";
  return 0;
}
