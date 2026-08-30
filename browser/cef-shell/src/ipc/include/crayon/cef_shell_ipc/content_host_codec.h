// CNT-18a: platform-neutral, deterministic Browser <-> Rust content-host v1
// DTO and codec. No CEF, transport, process or extraction types live here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace crayon::cef_shell::ipc::content_host {

inline constexpr std::size_t kMaxFrameBytes = 65'536;
inline constexpr std::size_t kMaxFacts = 64;
inline constexpr std::size_t kMaxMarkdownBytes = 60 * 1024;

enum class Mode : std::uint8_t { kStandard = 0, kCompact = 1 };
enum class FactKind : std::uint8_t {
  kHeading = 0,
  kParagraph,
  kListItem,
  kLink,
  kImage,
  kTable,
  kCodeBlock,
  kDivider,
  kQuote,
};
enum class TerminalStatus : std::uint8_t {
  kCompleted = 0,
  kCancelled,
  kStaleNavigation,
  kRejected,
};
enum class EngineError : std::uint8_t {
  kNone = 0,
  kInvalidArgument,
  kInvalidState,
  kAlreadyExists,
  kNotFound,
  kStaleNavigation,
  kUnsupported,
  kCapacityExceeded,
  kNavigationFailed,
};
enum class HostError : std::uint8_t {
  kInvalidMessage = 0,
  kInvalidState,
  kStaleNavigation,
  kCapacityExceeded,
  kExtractionFailed,
  kMarkdownFailed,
  kCancelled,
  kHostUnavailable,
};
enum class CodecError {
  kFrameTooLarge = 0,
  kInvalidMagic,
  kUnsupportedVersion,
  kUnknownKind,
  kInvalidFlags,
  kTruncated,
  kTrailingBytes,
  kInvalidUtf8,
  kInvalidValue,
  kLengthExceeded,
};

struct Fact final {
  FactKind kind = FactKind::kParagraph;
  std::string text;
  std::optional<std::string> url;
  std::optional<std::string> language;
  std::uint8_t level = 0;
  std::uint8_t depth = 0;
  bool ordered = false;
  std::optional<std::uint32_t> ordinal;
  std::uint16_t table_columns = 0;
  std::vector<std::string> table_cells;
  friend bool operator==(const Fact& a, const Fact& b) {
    return a.kind == b.kind && a.text == b.text && a.url == b.url &&
           a.language == b.language && a.level == b.level &&
           a.depth == b.depth && a.ordered == b.ordered &&
           a.ordinal == b.ordinal && a.table_columns == b.table_columns &&
           a.table_cells == b.table_cells;
  }
};

struct Begin final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  Mode mode = Mode::kStandard;
  std::string url, title;
  friend bool operator==(const Begin& a, const Begin& b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation &&
           a.mode == b.mode && a.url == b.url && a.title == b.title;
  }
};
struct FactBatch final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  std::uint32_t sequence = 0;
  std::vector<Fact> facts;
  friend bool operator==(const FactBatch& a, const FactBatch& b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation &&
           a.sequence == b.sequence && a.facts == b.facts;
  }
};
struct Terminal final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  TerminalStatus status = TerminalStatus::kRejected;
  EngineError error = EngineError::kInvalidState;
  friend bool operator==(const Terminal& a, const Terminal& b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation &&
           a.status == b.status && a.error == b.error;
  }
};
struct Cancel final {
  std::string request_id;
  friend bool operator==(const Cancel& a, const Cancel& b) {
    return a.request_id == b.request_id;
  }
};
struct Navigation final {
  std::string tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  friend bool operator==(const Navigation& a, const Navigation& b) {
    return a.tab_id == b.tab_id && a.navigation_id == b.navigation_id &&
           a.generation == b.generation;
  }
};
struct CloseTab final {
  std::string tab_id;
  friend bool operator==(const CloseTab& a, const CloseTab& b) {
    return a.tab_id == b.tab_id;
  }
};
struct Shutdown final {
  friend bool operator==(Shutdown, Shutdown) { return true; }
};
struct MarkdownChunk final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  std::uint32_t sequence = 0;
  bool completed = false;
  std::string markdown;
  friend bool operator==(const MarkdownChunk& a, const MarkdownChunk& b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation &&
           a.sequence == b.sequence && a.completed == b.completed &&
           a.markdown == b.markdown;
  }
};
struct ErrorReply final {
  std::string request_id;
  HostError code = HostError::kInvalidMessage;
  friend bool operator==(const ErrorReply& a, const ErrorReply& b) {
    return a.request_id == b.request_id && a.code == b.code;
  }
};

using Message = std::variant<Begin, FactBatch, Terminal, Cancel, Navigation,
                             CloseTab, Shutdown, MarkdownChunk, ErrorReply>;

std::optional<std::vector<std::uint8_t>> Encode(const Message& message,
                                                CodecError* error);
std::optional<Message> Decode(const std::vector<std::uint8_t>& bytes,
                              CodecError* error);
const char* ToString(CodecError error);

}  // namespace crayon::cef_shell::ipc::content_host
