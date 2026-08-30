#include "crayon/cef_shell_ipc/content_host_codec.h"

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>

namespace crayon::cef_shell::ipc::content_host {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'C', 'H', 'V', '1'};
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kMaxIdBytes = 128;
constexpr std::size_t kMaxUrlBytes = 2048;
constexpr std::size_t kMaxTitleBytes = 512;
constexpr std::size_t kMaxFactTextBytes = 32 * 1024;
constexpr std::size_t kMaxLanguageBytes = 32;
constexpr std::size_t kMaxTableCells = 256 * 32;
constexpr std::size_t kMaxTableCellBytes = 1024;

enum class Kind : std::uint8_t {
  kBegin = 1,
  kFactBatch,
  kTerminal,
  kCancel,
  kNavigation,
  kCloseTab,
  kShutdown,
  kMarkdownChunk,
  kErrorReply,
};

void SetError(CodecError value, CodecError* error) {
  if (error != nullptr) *error = value;
}

bool IsValidUtf8(const std::string& value) {
  const auto* data = reinterpret_cast<const unsigned char*>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const unsigned char first = data[index++];
    std::uint32_t code = 0;
    std::size_t remaining = 0;
    if (first < 0x80) {
      code = first;
    } else if ((first & 0xE0) == 0xC0) {
      code = first & 0x1F;
      remaining = 1;
      if (code == 0) return false;
    } else if ((first & 0xF0) == 0xE0) {
      code = first & 0x0F;
      remaining = 2;
    } else if ((first & 0xF8) == 0xF0) {
      code = first & 0x07;
      remaining = 3;
    } else {
      return false;
    }
    if (remaining > value.size() - index) return false;
    for (std::size_t i = 0; i < remaining; ++i) {
      const unsigned char next = data[index++];
      if ((next & 0xC0) != 0x80) return false;
      code = (code << 6) | (next & 0x3F);
    }
    if ((remaining == 1 && code < 0x80) || (remaining == 2 && code < 0x800) ||
        (remaining == 3 && code < 0x10000) || code > 0x10FFFF ||
        (code >= 0xD800 && code <= 0xDFFF))
      return false;
    if ((code < 0x20 && code != '\n' && code != '\t') ||
        (code >= 0x7F && code <= 0x9F))
      return false;
  }
  return true;
}

bool ValidLanguage(const std::string& value) {
  return !value.empty() && value.size() <= kMaxLanguageBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '+' || c == '-';
         });
}

bool ValidId(const std::string& value) {
  return !value.empty() && value.size() <= kMaxIdBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                  c == ':';
         });
}

bool ValidTabId(const std::string& value) {
  return !value.empty() && value.size() <= kMaxIdBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
         });
}

bool ValidFact(const Fact& fact) {
  if (!IsValidUtf8(fact.text) || fact.text.size() > kMaxFactTextBytes ||
      (fact.url && (!IsValidUtf8(*fact.url) || fact.url->empty() ||
                    fact.url->size() > kMaxUrlBytes)) ||
      (fact.language && !ValidLanguage(*fact.language)) ||
      fact.table_cells.size() > kMaxTableCells ||
      std::any_of(fact.table_cells.begin(), fact.table_cells.end(),
                  [](const std::string& value) {
                    return value.size() > kMaxTableCellBytes ||
                           !IsValidUtf8(value);
                  }))
    return false;
  const bool base = !fact.url && !fact.language && fact.level == 0 &&
                    fact.depth == 0 && !fact.ordered && !fact.ordinal &&
                    fact.table_columns == 0 && fact.table_cells.empty();
  const bool text = !fact.text.empty();
  switch (fact.kind) {
    case FactKind::kHeading:
      return text && fact.level >= 1 && fact.level <= 6 && !fact.url &&
             !fact.language && fact.depth == 0 && !fact.ordered &&
             !fact.ordinal && fact.table_columns == 0 &&
             fact.table_cells.empty();
    case FactKind::kParagraph:
    case FactKind::kQuote:
      return text && base;
    case FactKind::kListItem:
      return text && !fact.url && !fact.language && fact.level == 0 &&
             fact.depth >= 1 && fact.depth <= 8 &&
             fact.ordered == fact.ordinal.has_value() &&
             fact.table_columns == 0 && fact.table_cells.empty();
    case FactKind::kLink:
      return text && fact.url && !fact.language && fact.level == 0 &&
             fact.depth == 0 && !fact.ordered && !fact.ordinal &&
             fact.table_columns == 0 && fact.table_cells.empty();
    case FactKind::kImage:
      return fact.url && !fact.language && fact.level == 0 && fact.depth == 0 &&
             !fact.ordered && !fact.ordinal && fact.table_columns == 0 &&
             fact.table_cells.empty();
    case FactKind::kTable:
      return fact.text.empty() && !fact.url && !fact.language &&
             fact.level == 0 && fact.depth == 0 && !fact.ordered &&
             !fact.ordinal && fact.table_columns >= 1 &&
             fact.table_columns <= 32 && !fact.table_cells.empty() &&
             fact.table_cells.size() % fact.table_columns == 0 &&
             fact.table_cells.size() / fact.table_columns <= 256;
    case FactKind::kCodeBlock:
      return text && !fact.url && fact.level == 0 && fact.depth == 0 &&
             !fact.ordered && !fact.ordinal && fact.table_columns == 0 &&
             fact.table_cells.empty();
    case FactKind::kDivider:
      return fact.text.empty() && base;
  }
  return false;
}

class Writer final {
 public:
  explicit Writer(Kind kind) : bytes_(kMagic.begin(), kMagic.end()) {
    U16(kVersion);
    U8(static_cast<std::uint8_t>(kind));
    U8(0);
  }
  void U8(std::uint8_t value) { bytes_.push_back(value); }
  void U16(std::uint16_t value) {
    U8(value >> 8);
    U8(value);
  }
  void U32(std::uint32_t value) {
    U8(value >> 24);
    U8(value >> 16);
    U8(value >> 8);
    U8(value);
  }
  bool U64Nonzero(std::uint64_t value) {
    if (value == 0) return Fail(CodecError::kInvalidValue);
    for (int shift = 56; shift >= 0; shift -= 8) U8(value >> shift);
    return true;
  }
  void Bool(bool value) { U8(value ? 1 : 0); }
  bool String(const std::string& value, std::size_t max, bool allow_empty) {
    if (value.size() > max) return Fail(CodecError::kLengthExceeded);
    if ((!allow_empty && value.empty()) || !IsValidUtf8(value))
      return Fail(CodecError::kInvalidValue);
    U32(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
    return true;
  }
  bool Id(const std::string& value) {
    return ValidId(value) ? String(value, kMaxIdBytes, false)
                          : Fail(CodecError::kInvalidValue);
  }
  bool TabId(const std::string& value) {
    return ValidTabId(value) ? String(value, kMaxIdBytes, false)
                             : Fail(CodecError::kInvalidValue);
  }
  bool OptionalString(const std::optional<std::string>& value,
                      std::size_t max) {
    return String(value.value_or(""), max, true);
  }
  bool Fail(CodecError error) {
    if (!error_) error_ = error;
    return false;
  }
  std::optional<std::vector<std::uint8_t>> Finish(CodecError* error) {
    if (!error_ && bytes_.size() > kMaxFrameBytes)
      error_ = CodecError::kFrameTooLarge;
    if (error_) {
      SetError(*error_, error);
      return std::nullopt;
    }
    return bytes_;
  }

 private:
  std::vector<std::uint8_t> bytes_;
  std::optional<CodecError> error_;
};

class Reader final {
 public:
  explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
  bool U8(std::uint8_t* out) {
    const auto* p = Take(1);
    if (!p) return false;
    *out = p[0];
    return true;
  }
  bool U16(std::uint16_t* out) {
    const auto* p = Take(2);
    if (!p) return false;
    *out = (p[0] << 8) | p[1];
    return true;
  }
  bool U32(std::uint32_t* out) {
    const auto* p = Take(4);
    if (!p) return false;
    *out = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
    return true;
  }
  bool U64Nonzero(std::uint64_t* out) {
    const auto* p = Take(8);
    if (!p) return false;
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | p[i];
    if (value == 0) return Fail(CodecError::kInvalidValue);
    *out = value;
    return true;
  }
  bool Bool(bool* out) {
    std::uint8_t v = 0;
    if (!U8(&v)) return false;
    if (v > 1) return Fail(CodecError::kInvalidValue);
    *out = v == 1;
    return true;
  }
  bool String(std::string* out, std::size_t max, bool allow_empty) {
    std::uint32_t len = 0;
    if (!U32(&len)) return false;
    if (len > max) return Fail(CodecError::kLengthExceeded);
    const auto* p = Take(len);
    if (!p) return false;
    std::string value(reinterpret_cast<const char*>(p), len);
    if (!IsValidUtf8(value)) return Fail(CodecError::kInvalidUtf8);
    if (!allow_empty && value.empty()) return Fail(CodecError::kInvalidValue);
    *out = std::move(value);
    return true;
  }
  bool Id(std::string* out) {
    return String(out, kMaxIdBytes, false) &&
           (ValidId(*out) || Reject(CodecError::kInvalidValue));
  }
  bool TabId(std::string* out) {
    return String(out, kMaxIdBytes, false) &&
           (ValidTabId(*out) || Reject(CodecError::kInvalidValue));
  }
  bool OptionalString(std::optional<std::string>* out, std::size_t max) {
    std::string value;
    if (!String(&value, max, true)) return false;
    *out = value.empty() ? std::nullopt
                         : std::optional<std::string>(std::move(value));
    return true;
  }
  bool Empty() const { return offset_ == bytes_.size(); }
  CodecError error() const { return error_.value_or(CodecError::kTruncated); }
  bool Reject(CodecError error) { return Fail(error); }

 private:
  const std::uint8_t* Take(std::size_t count) {
    if (count > bytes_.size() - offset_) {
      Fail(CodecError::kTruncated);
      return nullptr;
    }
    const auto* value = bytes_.data() + offset_;
    offset_ += count;
    return value;
  }
  bool Fail(CodecError error) {
    if (!error_) error_ = error;
    return false;
  }
  const std::vector<std::uint8_t>& bytes_;
  std::size_t offset_ = kHeaderBytes;
  std::optional<CodecError> error_;
};

bool EncodeFact(Writer* writer, const Fact& fact) {
  if (!ValidFact(fact)) return writer->Fail(CodecError::kInvalidValue);
  writer->U8(static_cast<std::uint8_t>(fact.kind));
  if (!writer->String(fact.text, kMaxFactTextBytes, true) ||
      !writer->OptionalString(fact.url, kMaxUrlBytes) ||
      !writer->OptionalString(fact.language, kMaxLanguageBytes))
    return false;
  writer->U8(fact.level);
  writer->U8(fact.depth);
  writer->Bool(fact.ordered);
  writer->U32(fact.ordinal.value_or(0));
  writer->U16(fact.table_columns);
  writer->U16(static_cast<std::uint16_t>(fact.table_cells.size()));
  for (const auto& cell : fact.table_cells)
    if (!writer->String(cell, kMaxTableCellBytes, true)) return false;
  return true;
}

bool DecodeFact(Reader* reader, Fact* fact) {
  std::uint8_t kind = 0;
  std::uint32_t ordinal = 0;
  std::uint16_t count = 0;
  if (!reader->U8(&kind)) return false;
  if (kind > 8) return reader->Reject(CodecError::kInvalidValue);
  if (!reader->String(&fact->text, kMaxFactTextBytes, true) ||
      !reader->OptionalString(&fact->url, kMaxUrlBytes) ||
      !reader->OptionalString(&fact->language, kMaxLanguageBytes) ||
      !reader->U8(&fact->level) || !reader->U8(&fact->depth) ||
      !reader->Bool(&fact->ordered) || !reader->U32(&ordinal) ||
      !reader->U16(&fact->table_columns) || !reader->U16(&count))
    return false;
  if (count > kMaxTableCells)
    return reader->Reject(CodecError::kLengthExceeded);
  fact->kind = static_cast<FactKind>(kind);
  if (ordinal != 0) fact->ordinal = ordinal;
  fact->table_cells.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    std::string cell;
    if (!reader->String(&cell, kMaxTableCellBytes, true)) return false;
    fact->table_cells.push_back(std::move(cell));
  }
  return ValidFact(*fact) || reader->Reject(CodecError::kInvalidValue);
}

template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

}  // namespace

std::optional<std::vector<std::uint8_t>> Encode(const Message& message,
                                                CodecError* error) {
  const Kind kind = std::visit(
      Overloaded{[](const Begin&) { return Kind::kBegin; },
                 [](const FactBatch&) { return Kind::kFactBatch; },
                 [](const Terminal&) { return Kind::kTerminal; },
                 [](const Cancel&) { return Kind::kCancel; },
                 [](const Navigation&) { return Kind::kNavigation; },
                 [](const CloseTab&) { return Kind::kCloseTab; },
                 [](const Shutdown&) { return Kind::kShutdown; },
                 [](const MarkdownChunk&) { return Kind::kMarkdownChunk; },
                 [](const ErrorReply&) { return Kind::kErrorReply; }},
      message);
  Writer writer(kind);
  std::visit(
      Overloaded{[&](const Begin& v) {
                   writer.Id(v.request_id);
                   writer.TabId(v.tab_id);
                   writer.U64Nonzero(v.navigation_id);
                   writer.U64Nonzero(v.generation);
                   if (static_cast<std::uint8_t>(v.mode) > 1)
                     writer.Fail(CodecError::kInvalidValue);
                   writer.U8(static_cast<std::uint8_t>(v.mode));
                   writer.String(v.url, kMaxUrlBytes, false);
                   if (v.title.find_first_of("\n\r\t") != std::string::npos)
                     writer.Fail(CodecError::kInvalidValue);
                   writer.String(v.title, kMaxTitleBytes, false);
                 },
                 [&](const FactBatch& v) {
                   writer.Id(v.request_id);
                   writer.TabId(v.tab_id);
                   writer.U64Nonzero(v.navigation_id);
                   writer.U64Nonzero(v.generation);
                   writer.U32(v.sequence);
                   if (v.facts.empty() || v.facts.size() > kMaxFacts) {
                     writer.Fail(CodecError::kInvalidValue);
                     return;
                   }
                   writer.U16(static_cast<std::uint16_t>(v.facts.size()));
                   for (const auto& f : v.facts)
                     if (!EncodeFact(&writer, f)) break;
                 },
                 [&](const Terminal& v) {
                   writer.Id(v.request_id);
                   writer.TabId(v.tab_id);
                   writer.U64Nonzero(v.navigation_id);
                   writer.U64Nonzero(v.generation);
                   if (static_cast<std::uint8_t>(v.status) > 3 ||
                       static_cast<std::uint8_t>(v.error) > 8 ||
                       ((v.status == TerminalStatus::kCompleted) !=
                        (v.error == EngineError::kNone)))
                     writer.Fail(CodecError::kInvalidValue);
                   writer.U8(static_cast<std::uint8_t>(v.status));
                   writer.U8(static_cast<std::uint8_t>(v.error));
                 },
                 [&](const Cancel& v) { writer.Id(v.request_id); },
                 [&](const Navigation& v) {
                   writer.TabId(v.tab_id);
                   writer.U64Nonzero(v.navigation_id);
                   writer.U64Nonzero(v.generation);
                 },
                 [&](const CloseTab& v) { writer.TabId(v.tab_id); },
                 [&](const Shutdown&) {},
                 [&](const MarkdownChunk& v) {
                   writer.Id(v.request_id);
                   writer.TabId(v.tab_id);
                   writer.U64Nonzero(v.navigation_id);
                   writer.U64Nonzero(v.generation);
                   writer.U32(v.sequence);
                   writer.Bool(v.completed);
                   writer.String(v.markdown, kMaxMarkdownBytes, true);
                 },
                 [&](const ErrorReply& v) {
                   writer.Id(v.request_id);
                   if (static_cast<std::uint8_t>(v.code) > 7)
                     writer.Fail(CodecError::kInvalidValue);
                   writer.U8(static_cast<std::uint8_t>(v.code));
                 }},
      message);
  return writer.Finish(error);
}

std::optional<Message> Decode(const std::vector<std::uint8_t>& bytes,
                              CodecError* error) {
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return std::nullopt;
  }
  if (bytes.size() < kHeaderBytes) {
    SetError(CodecError::kTruncated, error);
    return std::nullopt;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    SetError(CodecError::kInvalidMagic, error);
    return std::nullopt;
  }
  if (bytes[4] != 0 || bytes[5] != kVersion) {
    SetError(CodecError::kUnsupportedVersion, error);
    return std::nullopt;
  }
  if (bytes[7] != 0) {
    SetError(CodecError::kInvalidFlags, error);
    return std::nullopt;
  }
  Reader reader(bytes);
  std::optional<Message> result;
  switch (bytes[6]) {
    case 1: {
      Begin v;
      std::uint8_t mode = 0;
      if (reader.Id(&v.request_id) && reader.TabId(&v.tab_id) &&
          reader.U64Nonzero(&v.navigation_id) &&
          reader.U64Nonzero(&v.generation) && reader.U8(&mode)) {
        if (mode > 1) {
          reader.Reject(CodecError::kInvalidValue);
          break;
        }
        if (reader.String(&v.url, kMaxUrlBytes, false) &&
            reader.String(&v.title, kMaxTitleBytes, false) &&
            (v.title.find_first_of("\n\r\t") == std::string::npos ||
             reader.Reject(CodecError::kInvalidValue))) {
          v.mode = static_cast<Mode>(mode);
          result = v;
        }
      }
      break;
    }
    case 2: {
      FactBatch v;
      std::uint16_t count = 0;
      if (reader.Id(&v.request_id) && reader.TabId(&v.tab_id) &&
          reader.U64Nonzero(&v.navigation_id) &&
          reader.U64Nonzero(&v.generation) && reader.U32(&v.sequence) &&
          reader.U16(&count)) {
        if (count == 0 || count > kMaxFacts) {
          reader.Reject(CodecError::kInvalidValue);
          break;
        }
        v.facts.reserve(count);
        bool ok = true;
        for (std::uint16_t i = 0; i < count && ok; ++i) {
          Fact f;
          ok = DecodeFact(&reader, &f);
          if (ok) v.facts.push_back(std::move(f));
        }
        if (ok) result = v;
      }
      break;
    }
    case 3: {
      Terminal v;
      std::uint8_t status = 0, code = 0;
      if (reader.Id(&v.request_id) && reader.TabId(&v.tab_id) &&
          reader.U64Nonzero(&v.navigation_id) &&
          reader.U64Nonzero(&v.generation) && reader.U8(&status) &&
          reader.U8(&code)) {
        if (status > 3 || code > 8) {
          reader.Reject(CodecError::kInvalidValue);
          break;
        }
        v.status = static_cast<TerminalStatus>(status);
        v.error = static_cast<EngineError>(code);
        if ((v.status == TerminalStatus::kCompleted) ==
            (v.error == EngineError::kNone))
          result = v;
        else
          reader.Reject(CodecError::kInvalidValue);
      }
      break;
    }
    case 4: {
      Cancel v;
      if (reader.Id(&v.request_id)) result = v;
      break;
    }
    case 5: {
      Navigation v;
      if (reader.TabId(&v.tab_id) && reader.U64Nonzero(&v.navigation_id) &&
          reader.U64Nonzero(&v.generation))
        result = v;
      break;
    }
    case 6: {
      CloseTab v;
      if (reader.TabId(&v.tab_id)) result = v;
      break;
    }
    case 7:
      result = Shutdown{};
      break;
    case 8: {
      MarkdownChunk v;
      if (reader.Id(&v.request_id) && reader.TabId(&v.tab_id) &&
          reader.U64Nonzero(&v.navigation_id) &&
          reader.U64Nonzero(&v.generation) && reader.U32(&v.sequence) &&
          reader.Bool(&v.completed) &&
          reader.String(&v.markdown, kMaxMarkdownBytes, true))
        result = v;
      break;
    }
    case 9: {
      ErrorReply v;
      std::uint8_t code = 0;
      if (reader.Id(&v.request_id) && reader.U8(&code)) {
        if (code > 7) {
          reader.Reject(CodecError::kInvalidValue);
          break;
        }
        v.code = static_cast<HostError>(code);
        result = v;
      }
      break;
    }
    default:
      SetError(CodecError::kUnknownKind, error);
      return std::nullopt;
  }
  if (!result) {
    SetError(reader.error(), error);
    return std::nullopt;
  }
  if (!reader.Empty()) {
    SetError(CodecError::kTrailingBytes, error);
    return std::nullopt;
  }
  return result;
}

const char* ToString(CodecError error) {
  switch (error) {
    case CodecError::kFrameTooLarge:
      return "content-host frame exceeds size limit";
    case CodecError::kInvalidMagic:
      return "content-host magic rejected";
    case CodecError::kUnsupportedVersion:
      return "content-host version rejected";
    case CodecError::kUnknownKind:
      return "content-host message kind rejected";
    case CodecError::kInvalidFlags:
      return "content-host flags rejected";
    case CodecError::kTruncated:
      return "content-host frame truncated";
    case CodecError::kTrailingBytes:
      return "content-host frame has trailing bytes";
    case CodecError::kInvalidUtf8:
      return "content-host string is not UTF-8";
    case CodecError::kInvalidValue:
      return "content-host value rejected";
    case CodecError::kLengthExceeded:
      return "content-host field exceeds size limit";
  }
  return "unknown";
}

}  // namespace crayon::cef_shell::ipc::content_host
