// BUX-17: local address autofill domain model (UX-017).
//
// Closed address/contact vocabulary only — nine field kinds covering
// postal and contact data.  Passwords, payment cards and government IDs
// are NOT expressible in these types, ever.
//
// Privacy contract:
// - `RedactedSummary()` emits only "field(len)" shapes; no PII value can
//   reach logs or diagnostics through it.
// - Records live in a profile-scoped in-memory store; different scopes
//   never see each other.  Incognito windows use neither the prompt nor
//   suggestions (enforced in the UI layer).
// - Nothing here is reachable from CAAP tools or the page data plane;
//   this module is not registered anywhere agent-visible.
//
// Persistence is a later platform wiring task on top of the PLT secure
// store; the store interface is designed to be swappable.
//
// Thread contract: single-threaded, UI thread only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crayon::browser_autofill {

/// Maximum length of one field value, in bytes.
inline constexpr std::size_t kMaxFieldLen = 256;
/// Maximum record id length, in bytes.
inline constexpr std::size_t kMaxRecordIdLen = 64;
/// Maximum records per profile scope.
inline constexpr std::size_t kMaxRecords = 64;
/// Maximum suggestions returned per query.
inline constexpr std::size_t kMaxSuggestions = 6;

/// Closed fillable field kinds.  The list is complete by design.
enum class FieldKind {
  kFullName = 0,
  kOrganization,
  kStreetLine1,
  kStreetLine2,
  kCity,
  kState,
  kPostalCode,
  kCountryRegion,
  kPhone,
  kEmail,
};

/// Stable wire name for snapshots, matching keys and diagnostics.
const char* FieldKindWireName(FieldKind kind);

/// Reports whether `value` fits the closed text rule: non-empty not
/// required here (fields are optional), bounded, no control bytes.
bool IsValidFieldValue(const std::string& value);

/// One address/contact record.  All fields are optional except that at
/// least one of full name / organization must be present.
struct AddressRecord {
  std::string record_id;    // empty until assigned by the store
  std::string full_name;
  std::string organization;
  std::string street_line1;
  std::string street_line2;
  std::string city;
  std::string state;
  std::string postal_code;
  std::string country_region;
  std::string phone;
  std::string email;
  std::uint64_t updated_at_ms = 0;

  /// Closed validation: bounds, control characters, and at least one
  /// identifying field.  `record_id` may be empty pre-insertion.
  [[nodiscard]] bool Validate() const;

  /// Returns the value of one field kind (empty for unknown input).
  [[nodiscard]] const std::string& FieldValue(FieldKind kind) const;

  /// PII-free diagnostics form: `field(len)` pairs only.
  [[nodiscard]] std::string RedactedSummary() const;
};

/// Deterministic suggestion ordering for one typed field query:
/// prefix matches first, then substring matches, ties broken by record
/// id ascending.  At most `kMaxSuggestions` ids are returned.
std::vector<std::string> MatchRecordIds(
    const std::vector<AddressRecord>& records, FieldKind field,
    const std::string& typed_prefix);

/// In-memory, profile-scoped address book.  Scopes are opaque strings
/// supplied by the shell (e.g. "profile:<id>"); records never cross
/// scopes because each store instance holds exactly one.
class AddressBookStore final {
 public:
  explicit AddressBookStore(std::string profile_scope);

  /// Insertion outcomes.
  enum class AddResult { kOk = 0, kInvalid, kFull };

  /// Validates and inserts; assigns `addr-<n>` id (monotonic per store)
  /// and stamps `updated_at_ms`.  The caller supplies the clock value.
  AddResult Add(AddressRecord record, std::uint64_t now_ms);

  /// Replaces an existing record (same non-empty id), revalidating.
  bool Update(AddressRecord record, std::uint64_t now_ms);

  /// Deletes one record; returns false when the id is unknown.
  bool Delete(const std::string& record_id);

  /// Deletes everything; returns the number removed.
  std::size_t DeleteAll();

  [[nodiscard]] const AddressRecord* Find(
      const std::string& record_id) const;

  /// Deterministic listing: `updated_at_ms` descending, then id ascending.
  [[nodiscard]] std::vector<AddressRecord> AllSorted() const;

  [[nodiscard]] std::size_t size() const { return records_.size(); }

  [[nodiscard]] const std::string& profile_scope() const {
    return profile_scope_;
  }

 private:
  std::string profile_scope_;
  std::vector<AddressRecord> records_;
  std::uint64_t next_id_ = 1;
};

}  // namespace crayon::browser_autofill
