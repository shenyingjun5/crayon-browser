// BUX-17 address autofill domain implementation.
#include "crayon/browser_autofill/address_book.h"

#include <algorithm>

namespace crayon::browser_autofill {
namespace {

bool HasControlByte(const std::string& value) {
  for (unsigned char byte : value) {
    if (byte < 0x20 || byte == 0x7F) {
      return true;
    }
  }
  return false;
}

char AsciiLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string AsciiLowerCopy(const std::string& value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (char c : value) {
    lowered.push_back(AsciiLower(c));
  }
  return lowered;
}

bool IsValidId(const std::string& value) {
  if (value.empty() || value.size() > kMaxRecordIdLen) {
    return false;
  }
  for (unsigned char byte : value) {
    const bool ok = (byte >= 'a' && byte <= 'z') ||
                    (byte >= '0' && byte <= '9') || byte == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

}  // namespace

const char* FieldKindWireName(FieldKind kind) {
  switch (kind) {
    case FieldKind::kFullName:
      return "full_name";
    case FieldKind::kOrganization:
      return "organization";
    case FieldKind::kStreetLine1:
      return "street_line1";
    case FieldKind::kStreetLine2:
      return "street_line2";
    case FieldKind::kCity:
      return "city";
    case FieldKind::kState:
      return "state";
    case FieldKind::kPostalCode:
      return "postal_code";
    case FieldKind::kCountryRegion:
      return "country_region";
    case FieldKind::kPhone:
      return "phone";
    case FieldKind::kEmail:
      return "email";
  }
  return "unknown";
}

bool IsValidFieldValue(const std::string& value) {
  return value.size() <= kMaxFieldLen && !HasControlByte(value);
}

const std::string& AddressRecord::FieldValue(FieldKind kind) const {
  switch (kind) {
    case FieldKind::kFullName:
      return full_name;
    case FieldKind::kOrganization:
      return organization;
    case FieldKind::kStreetLine1:
      return street_line1;
    case FieldKind::kStreetLine2:
      return street_line2;
    case FieldKind::kCity:
      return city;
    case FieldKind::kState:
      return state;
    case FieldKind::kPostalCode:
      return postal_code;
    case FieldKind::kCountryRegion:
      return country_region;
    case FieldKind::kPhone:
      return phone;
    case FieldKind::kEmail:
      return email;
  }
  static const std::string kEmpty;
  return kEmpty;
}

bool AddressRecord::Validate() const {
  // At least one identifying field must be present.
  if (full_name.empty() && organization.empty()) {
    return false;
  }
  const std::string* fields[] = {
      &record_id,   &full_name,  &organization, &street_line1,
      &street_line2, &city,      &state,        &postal_code,
      &country_region, &phone,   &email,
  };
  for (const std::string* field : fields) {
    if (!IsValidFieldValue(*field)) {
      return false;
    }
  }
  if (!record_id.empty() && !IsValidId(record_id)) {
    return false;
  }
  return true;
}

std::string AddressRecord::RedactedSummary() const {
  // PII-free: presence and length only, never content.
  std::string summary = "address";
  if (updated_at_ms != 0) {
    summary += "@";
    summary += std::to_string(updated_at_ms);
  }
  const auto append = [&summary](const char* name, const std::string& value) {
    if (!value.empty()) {
      summary += " ";
      summary += name;
      summary += "(";
      summary += std::to_string(value.size());
      summary += ")";
    }
  };
  append("id", record_id);
  append(FieldKindWireName(FieldKind::kFullName), full_name);
  append(FieldKindWireName(FieldKind::kOrganization), organization);
  append(FieldKindWireName(FieldKind::kStreetLine1), street_line1);
  append(FieldKindWireName(FieldKind::kStreetLine2), street_line2);
  append(FieldKindWireName(FieldKind::kCity), city);
  append(FieldKindWireName(FieldKind::kState), state);
  append(FieldKindWireName(FieldKind::kPostalCode), postal_code);
  append(FieldKindWireName(FieldKind::kCountryRegion), country_region);
  append(FieldKindWireName(FieldKind::kPhone), phone);
  append(FieldKindWireName(FieldKind::kEmail), email);
  return summary;
}

std::vector<std::string> MatchRecordIds(
    const std::vector<AddressRecord>& records, FieldKind field,
    const std::string& typed_prefix) {
  // ASCII case-insensitive comparison keeps matching predictable for
  // mixed-case names; non-ASCII bytes compare as-is (deterministic).
  const std::string prefix = AsciiLowerCopy(typed_prefix);
  struct Ranked {
    int quality;          // 2 = prefix match, 1 = substring, 0 = present
    const AddressRecord* record;
  };
  std::vector<Ranked> ranked;
  for (const AddressRecord& record : records) {
    const std::string lowered = AsciiLowerCopy(record.FieldValue(field));
    if (lowered.empty()) {
      continue;
    }
    if (!prefix.empty()) {
      const bool is_prefix = lowered.rfind(prefix, 0) == 0;
      const bool contains =
          !is_prefix && lowered.find(prefix) != std::string::npos;
      if (!is_prefix && !contains) {
        continue;
      }
      ranked.push_back(Ranked{is_prefix ? 2 : 1, &record});
    } else {
      ranked.push_back(Ranked{0, &record});
    }
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const Ranked& a, const Ranked& b) {
              if (a.quality != b.quality) {
                return a.quality > b.quality;
              }
              return a.record->record_id < b.record->record_id;
            });
  std::vector<std::string> ids;
  ids.reserve(ranked.size());
  for (const Ranked& entry : ranked) {
    if (ids.size() >= kMaxSuggestions) {
      break;
    }
    ids.push_back(entry.record->record_id);
  }
  return ids;
}

AddressBookStore::AddressBookStore(std::string profile_scope)
    : profile_scope_(std::move(profile_scope)) {}

AddressBookStore::AddResult AddressBookStore::Add(AddressRecord record,
                                                  std::uint64_t now_ms) {
  if (records_.size() >= kMaxRecords) {
    return AddResult::kFull;
  }
  if (!record.Validate() || !record.record_id.empty()) {
    return AddResult::kInvalid;
  }
  record.record_id = "addr-" + std::to_string(next_id_++);
  record.updated_at_ms = now_ms;
  records_.push_back(std::move(record));
  return AddResult::kOk;
}

bool AddressBookStore::Update(AddressRecord record, std::uint64_t now_ms) {
  if (!record.Validate() || !IsValidId(record.record_id)) {
    return false;
  }
  for (AddressRecord& existing : records_) {
    if (existing.record_id == record.record_id) {
      record.updated_at_ms = now_ms;
      existing = std::move(record);
      return true;
    }
  }
  return false;
}

bool AddressBookStore::Delete(const std::string& record_id) {
  for (auto it = records_.begin(); it != records_.end(); ++it) {
    if (it->record_id == record_id) {
      records_.erase(it);
      return true;
    }
  }
  return false;
}

std::size_t AddressBookStore::DeleteAll() {
  const std::size_t removed = records_.size();
  records_.clear();
  return removed;
}

const AddressRecord* AddressBookStore::Find(
    const std::string& record_id) const {
  for (const AddressRecord& record : records_) {
    if (record.record_id == record_id) {
      return &record;
    }
  }
  return nullptr;
}

std::vector<AddressRecord> AddressBookStore::AllSorted() const {
  std::vector<AddressRecord> sorted = records_;
  std::sort(sorted.begin(), sorted.end(),
            [](const AddressRecord& a, const AddressRecord& b) {
              if (a.updated_at_ms != b.updated_at_ms) {
                return a.updated_at_ms > b.updated_at_ms;
              }
              return a.record_id < b.record_id;
            });
  return sorted;
}

}  // namespace crayon::browser_autofill
