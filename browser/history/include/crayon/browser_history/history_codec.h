#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "crayon/browser_history/history_store.h"

namespace crayon::browser_history {

/// Maximum accepted serialized history file size (4 MiB).
inline constexpr std::size_t kMaxHistoryFileBytes = 4 * 1024 * 1024;

/// Codec failure.  Stable variants carry no file content or paths.
enum class HistoryCodecError {
  kBadHeader = 0,
  kTruncated,
  kLengthOverflow,
  kUnknownRecordType,
  kIoFailure,
  /// The parsed entries violate store validation rules.
  kContentRejected,
  /// Ephemeral stores never persist; the refusal is explicit.
  kEphemeralRefused,
};

/// Serializes the store into the deterministic `CRAYON-HISTORY v1`
/// length-prefixed text format (oldest visit first).
std::string SerializeHistory(const HistoryStore& store);

/// Parses a serialized document into a fresh store.  Any corruption fails
/// closed; nothing is partially imported.
std::optional<HistoryStore> DeserializeHistory(
    const std::string& document,
    HistoryCodecError* error = nullptr);

/// Atomically persists the store via `<path>.tmp` + rename.  Ephemeral
/// stores are refused with `kEphemeralRefused`.
bool SaveHistoryToFile(const HistoryStore& store,
                       const std::string& path,
                       HistoryCodecError* error = nullptr);

/// Loads a store from disk.  Missing/unreadable/oversized/corrupt files
/// fail closed with an error.
std::optional<HistoryStore> LoadHistoryFromFile(
    const std::string& path,
    HistoryCodecError* error = nullptr);

}  // namespace crayon::browser_history
