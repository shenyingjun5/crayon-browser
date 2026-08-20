#include "crayon/browser_preferences/preference_codec.h"

#include <cstdio>
#include <fstream>
#include <string_view>

namespace crayon::browser_preferences {

namespace {

constexpr std::string_view kHeaderPrefix = "CRAYON-PREFERENCES v";
constexpr std::size_t kMaxNumberDigits = 20;

void SetError(PreferenceCodecError* error,
              PreferenceCodecError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

class Parser final {
 public:
  explicit Parser(std::string_view document) : document_(document) {}

  /// Reads the header and returns its schema version.
  bool ConsumeHeader(std::uint32_t* version) {
    if (document_.substr(0, kHeaderPrefix.size()) != kHeaderPrefix) {
      return false;
    }
    position_ = kHeaderPrefix.size();
    std::uint64_t parsed = 0;
    if (!ReadNumber(&parsed) || parsed > 99) {
      return false;
    }
    *version = static_cast<std::uint32_t>(parsed);
    return true;
  }

  bool AtEnd() const { return position_ == document_.size(); }

  bool ReadNumber(std::uint64_t* value) {
    std::uint64_t parsed = 0;
    std::size_t digits = 0;
    while (position_ < document_.size() && digits < kMaxNumberDigits) {
      const char c = document_[position_];
      if (c < '0' || c > '9') {
        break;
      }
      const std::uint64_t next =
          parsed * 10 + static_cast<std::uint64_t>(c - '0');
      if (next < parsed) {
        return false;
      }
      parsed = next;
      ++position_;
      ++digits;
    }
    if (digits == 0 || position_ >= document_.size()) {
      return false;
    }
    const char terminator = document_[position_];
    if (terminator != ' ' && terminator != '\n') {
      return false;
    }
    ++position_;
    *value = parsed;
    return true;
  }

  bool ReadRecordKind(char* kind) {
    if (position_ + 2 > document_.size()) {
      return false;
    }
    *kind = document_[position_];
    if (document_[position_ + 1] != ' ') {
      return false;
    }
    position_ += 2;
    return true;
  }

  bool ReadPayload(std::uint64_t length, std::string* out) {
    if (position_ >= document_.size()) {
      return false;
    }
    const std::size_t remaining = document_.size() - position_;
    if (length + 1 > remaining ||
        document_[position_ + length] != '\n') {
      return false;
    }
    *out = std::string(document_.substr(position_, length));
    position_ += length + 1;
    return true;
  }

 private:
  std::string_view document_;
  std::size_t position_ = 0;
};

std::string SerializeRecord(char kind,
                            const std::string& key,
                            const std::string& payload) {
  std::string out;
  out += kind;
  out += ' ';
  out += std::to_string(key.size());
  out += '\n';
  out += key;
  out += '\n';
  if (kind == 'S') {
    // String payloads carry an explicit length line; numeric kinds are
    // parsed directly as a number line.
    out += std::to_string(payload.size());
    out += '\n';
  }
  out += payload;
  out += '\n';
  return out;
}

}  // namespace

std::string SerializePreferences(const PreferenceStore& store) {
  std::string out(kHeaderPrefix);
  out += std::to_string(kPreferenceSchemaVersion);
  out += '\n';
  for (const std::string& key : PreferenceStore::RegisteredKeys()) {
    if (!store.IsModified(key)) {
      continue;
    }
    const PreferenceValue& value = store.Get(key);
    if (const auto* flag = std::get_if<bool>(&value)) {
      out += SerializeRecord('B', key, *flag ? "1" : "0");
    } else if (const auto* number = std::get_if<std::int64_t>(&value)) {
      out += SerializeRecord('I', key, std::to_string(*number));
    } else {
      out += SerializeRecord('S', key, std::get<std::string>(value));
    }
  }
  return out;
}

std::optional<PreferenceStore> DeserializePreferences(
    const std::string& document,
    PreferenceCodecError* error) {
  if (document.size() > kMaxPreferenceFileBytes) {
    SetError(error, PreferenceCodecError::kLengthOverflow);
    return std::nullopt;
  }
  Parser parser(document);
  std::uint32_t version = 0;
  if (!parser.ConsumeHeader(&version)) {
    SetError(error, PreferenceCodecError::kBadHeader);
    return std::nullopt;
  }
  if (version > kPreferenceSchemaVersion) {
    SetError(error, PreferenceCodecError::kUnsupportedVersion);
    return std::nullopt;
  }
  const bool strict = version == kPreferenceSchemaVersion;

  PreferenceStore store;
  while (!parser.AtEnd()) {
    char kind = '\0';
    if (!parser.ReadRecordKind(&kind)) {
      SetError(error, PreferenceCodecError::kTruncated);
      return std::nullopt;
    }
    std::uint64_t key_len = 0;
    if (!parser.ReadNumber(&key_len) || key_len > kMaxPreferenceStringBytes) {
      SetError(error, PreferenceCodecError::kLengthOverflow);
      return std::nullopt;
    }
    std::string key;
    if (!parser.ReadPayload(key_len, &key)) {
      SetError(error, PreferenceCodecError::kTruncated);
      return std::nullopt;
    }
    PreferenceValue value;
    bool semantically_invalid = false;
    if (kind == 'B' || kind == 'I') {
      std::uint64_t number = 0;
      if (!parser.ReadNumber(&number)) {
        SetError(error, PreferenceCodecError::kTruncated);
        return std::nullopt;
      }
      if (kind == 'B') {
        if (number > 1) {
          semantically_invalid = true;
          value = false;
        } else {
          value = number == 1;
        }
      } else {
        value = static_cast<std::int64_t>(number);
      }
    } else if (kind == 'S') {
      std::uint64_t payload_len = 0;
      if (!parser.ReadNumber(&payload_len) ||
          payload_len > kMaxPreferenceStringBytes) {
        SetError(error, PreferenceCodecError::kLengthOverflow);
        return std::nullopt;
      }
      std::string payload;
      if (!parser.ReadPayload(payload_len, &payload)) {
        SetError(error, PreferenceCodecError::kTruncated);
        return std::nullopt;
      }
      value = std::move(payload);
    } else {
      SetError(error, PreferenceCodecError::kUnknownRecordType);
      return std::nullopt;
    }

    if (semantically_invalid || !store.Set(key, std::move(value))) {
      if (strict) {
        SetError(error, PreferenceCodecError::kContentRejected);
        return std::nullopt;
      }
      // Migration from v0: unknown keys and invalid values are dropped.
    }
  }
  return store;
}

bool SavePreferencesToFile(const PreferenceStore& store,
                           const std::string& path,
                           PreferenceCodecError* error) {
  const std::string staging = path + ".tmp";
  {
    std::ofstream out(staging, std::ios::binary | std::ios::trunc);
    if (!out) {
      SetError(error, PreferenceCodecError::kIoFailure);
      return false;
    }
    out << SerializePreferences(store);
    if (!out.good()) {
      SetError(error, PreferenceCodecError::kIoFailure);
      return false;
    }
  }
  if (std::rename(staging.c_str(), path.c_str()) != 0) {
    std::remove(staging.c_str());
    SetError(error, PreferenceCodecError::kIoFailure);
    return false;
  }
  return true;
}

std::optional<PreferenceStore> LoadPreferencesFromFile(
    const std::string& path,
    PreferenceCodecError* error) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    SetError(error, PreferenceCodecError::kIoFailure);
    return std::nullopt;
  }
  const std::streamsize size = in.tellg();
  if (size < 0 ||
      static_cast<std::uintmax_t>(size) > kMaxPreferenceFileBytes) {
    SetError(error, PreferenceCodecError::kLengthOverflow);
    return std::nullopt;
  }
  in.seekg(0);
  std::string document(static_cast<std::size_t>(size), '\0');
  if (size > 0 && !in.read(document.data(), size)) {
    SetError(error, PreferenceCodecError::kIoFailure);
    return std::nullopt;
  }
  return DeserializePreferences(document, error);
}

}  // namespace crayon::browser_preferences
