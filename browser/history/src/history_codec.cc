#include "crayon/browser_history/history_codec.h"

#include <cstdio>
#include <fstream>
#include <string_view>

namespace crayon::browser_history {

namespace {

constexpr std::string_view kHeader = "CRAYON-HISTORY v1\n";
constexpr std::size_t kMaxNumberDigits = 20;  // u64 timestamps.

void SetError(HistoryCodecError* error, HistoryCodecError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

class Parser final {
 public:
  explicit Parser(std::string_view document) : document_(document) {}

  bool ConsumeHeader() {
    if (document_.substr(0, kHeader.size()) != kHeader) {
      return false;
    }
    position_ = kHeader.size();
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
        return false;  // Overflow.
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

}  // namespace

std::string SerializeHistory(const HistoryStore& store) {
  std::string out(kHeader);
  for (const HistoryEntry& entry : store.entries()) {
    out += "V " + std::to_string(entry.visited_at) + " " +
           std::to_string(entry.title.size()) + " " +
           std::to_string(entry.url.size()) + "\n" + entry.title + "\n" +
           entry.url + "\n";
  }
  return out;
}

std::optional<HistoryStore> DeserializeHistory(
    const std::string& document,
    HistoryCodecError* error) {
  if (document.size() > kMaxHistoryFileBytes) {
    SetError(error, HistoryCodecError::kLengthOverflow);
    return std::nullopt;
  }
  Parser parser(document);
  if (!parser.ConsumeHeader()) {
    SetError(error, HistoryCodecError::kBadHeader);
    return std::nullopt;
  }
  HistoryStore store;
  while (!parser.AtEnd()) {
    char kind = '\0';
    if (!parser.ReadRecordKind(&kind)) {
      SetError(error, HistoryCodecError::kTruncated);
      return std::nullopt;
    }
    if (kind != 'V') {
      SetError(error, HistoryCodecError::kUnknownRecordType);
      return std::nullopt;
    }
    std::uint64_t visited_at = 0;
    std::uint64_t title_len = 0;
    std::uint64_t url_len = 0;
    if (!parser.ReadNumber(&visited_at) || !parser.ReadNumber(&title_len) ||
        !parser.ReadNumber(&url_len) || title_len > kMaxTitleBytes ||
        url_len > kMaxUrlBytes) {
      SetError(error, HistoryCodecError::kLengthOverflow);
      return std::nullopt;
    }
    std::string title;
    std::string url;
    if (!parser.ReadPayload(title_len, &title) ||
        !parser.ReadPayload(url_len, &url)) {
      SetError(error, HistoryCodecError::kTruncated);
      return std::nullopt;
    }
    if (store.RecordVisit(std::move(url), std::move(title), visited_at) == 0) {
      SetError(error, HistoryCodecError::kContentRejected);
      return std::nullopt;
    }
  }
  return store;
}

bool SaveHistoryToFile(const HistoryStore& store,
                       const std::string& path,
                       HistoryCodecError* error) {
  if (store.ephemeral()) {
    SetError(error, HistoryCodecError::kEphemeralRefused);
    return false;
  }
  const std::string staging = path + ".tmp";
  {
    std::ofstream out(staging, std::ios::binary | std::ios::trunc);
    if (!out) {
      SetError(error, HistoryCodecError::kIoFailure);
      return false;
    }
    out << SerializeHistory(store);
    if (!out.good()) {
      SetError(error, HistoryCodecError::kIoFailure);
      return false;
    }
  }
  if (std::rename(staging.c_str(), path.c_str()) != 0) {
    std::remove(staging.c_str());
    SetError(error, HistoryCodecError::kIoFailure);
    return false;
  }
  return true;
}

std::optional<HistoryStore> LoadHistoryFromFile(
    const std::string& path,
    HistoryCodecError* error) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    SetError(error, HistoryCodecError::kIoFailure);
    return std::nullopt;
  }
  const std::streamsize size = in.tellg();
  if (size < 0 ||
      static_cast<std::uintmax_t>(size) > kMaxHistoryFileBytes) {
    SetError(error, HistoryCodecError::kLengthOverflow);
    return std::nullopt;
  }
  in.seekg(0);
  std::string document(static_cast<std::size_t>(size), '\0');
  if (size > 0 && !in.read(document.data(), size)) {
    SetError(error, HistoryCodecError::kIoFailure);
    return std::nullopt;
  }
  return DeserializeHistory(document, error);
}

}  // namespace crayon::browser_history
