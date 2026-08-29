#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_engine/ids.h"
#include "crayon/browser_engine/result.h"
#include "crayon/browser_engine/types.h"

namespace crayon::browser_engine {

inline constexpr std::size_t kMaxSnapshotStreams = 8;
inline constexpr std::size_t kMaxSnapshotChunks = 64;
inline constexpr std::size_t kMaxSnapshotFactsPerChunk = 64;
inline constexpr std::size_t kMaxSnapshotChunkBytes = 64 * 1024;
inline constexpr std::size_t kMaxSnapshotFactTextBytes = 16 * 1024;
inline constexpr std::size_t kMaxSnapshotCodeBytes = 32 * 1024;
inline constexpr std::size_t kMaxCompactSnapshotFactTextBytes = 2 * 1024;
inline constexpr std::size_t kMaxStandardSnapshotFacts = 4096;
inline constexpr std::size_t kMaxCompactSnapshotFacts = 512;
inline constexpr std::size_t kMaxStandardSnapshotBytes = 1024 * 1024;
inline constexpr std::size_t kMaxCompactSnapshotBytes = 128 * 1024;
inline constexpr std::size_t kMaxSnapshotTableCells = 256 * 32;

enum class SnapshotMode { kStandard = 0, kCompact };
enum class SnapshotFactKind {
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
enum class SnapshotTerminalStatus {
  kCompleted = 0,
  kCancelled,
  kStaleNavigation,
  kRejected,
};

struct SnapshotRequest final {
  SnapshotRequestId request_id;
  TabId tab_id;
  NavigationId navigation_id;
  SnapshotMode mode = SnapshotMode::kStandard;
};

// A normalized, visible structure fact. Fields unused by a kind must retain
// their zero/empty value; this prevents an open-ended renderer payload.
struct SnapshotFact final {
  SnapshotFactKind kind = SnapshotFactKind::kParagraph;
  std::string text;
  std::optional<BrowserUrl> url;
  std::optional<std::string> language;
  std::uint8_t level = 0;
  std::uint8_t depth = 0;
  bool ordered = false;
  std::optional<std::uint32_t> ordinal;
  std::uint16_t table_columns = 0;
  std::vector<std::string> table_cells;
};

struct SnapshotDocumentMetadata final {
  BrowserUrl url;
  std::string title;
};

struct SnapshotChunk final {
  SnapshotRequestId request_id;
  TabId tab_id;
  NavigationId navigation_id;
  std::uint32_t sequence = 0;
  std::optional<SnapshotDocumentMetadata> document;
  std::vector<SnapshotFact> facts;
};

struct SnapshotTerminal final {
  SnapshotRequestId request_id;
  TabId tab_id;
  NavigationId navigation_id;
  SnapshotTerminalStatus status = SnapshotTerminalStatus::kRejected;
  EngineErrorCode error = EngineErrorCode::kNone;
};

bool IsValid(SnapshotMode value) noexcept;
bool IsValid(SnapshotFactKind value) noexcept;
bool IsValid(SnapshotTerminalStatus value) noexcept;
bool IsValid(const SnapshotFact& fact) noexcept;
bool IsValid(const SnapshotFact& fact, SnapshotMode mode) noexcept;
std::optional<std::size_t> SnapshotFactByteSize(
    const SnapshotFact& fact) noexcept;
bool IsValid(const SnapshotChunk& chunk) noexcept;
bool IsValid(const SnapshotChunk& chunk, SnapshotMode mode) noexcept;
std::optional<std::size_t> SnapshotChunkByteSize(
    const SnapshotChunk& chunk) noexcept;
std::size_t SnapshotModeMaxFacts(SnapshotMode mode) noexcept;
std::size_t SnapshotModeMaxBytes(SnapshotMode mode) noexcept;

class SnapshotStreamSink {
 public:
  virtual ~SnapshotStreamSink() = default;
  // The sink must outlive its terminal callback or adapter Stop(). Adapters
  // never invoke these callbacks inline from StartSnapshot/CancelSnapshot.
  virtual void OnSnapshotChunk(const SnapshotChunk& chunk) = 0;
  virtual void OnSnapshotTerminal(const SnapshotTerminal& terminal) = 0;
};

}  // namespace crayon::browser_engine
