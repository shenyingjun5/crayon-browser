#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "crayon/browser_bookmarks/bookmark_store.h"

namespace crayon::browser_bookmarks {

/// Maximum accepted serialized bookmark file size (4 MiB).
inline constexpr std::size_t kMaxBookmarkFileBytes = 4 * 1024 * 1024;

/// Codec failure.  Stable variants carry no file content or paths.
enum class BookmarkCodecError {
  kBadHeader = 0,
  kTruncated,
  kLengthOverflow,
  kUnknownRecordType,
  kDepthJump,
  kCountMismatch,
  kTrailingGarbage,
  kIoFailure,
  /// The parsed tree violates store bounds (capacity/depth/validation).
  kContentRejected,
};

/// Serializes the store into the deterministic `CRAYON-BOOKMARKS v1`
/// length-prefixed text format (DFS order from the root).
std::string SerializeBookmarks(const BookmarkStore& store);

/// Parses a serialized document into a fresh store.  Any corruption fails
/// closed: an error is returned and no partial tree is produced.
std::optional<BookmarkStore> DeserializeBookmarks(
    const std::string& document,
    BookmarkCodecError* error = nullptr);

/// Atomically persists the store: writes `<path>.tmp` then renames it over
/// `path`.  On failure the destination file is left untouched and the
/// temporary file is removed best-effort.
bool SaveBookmarksToFile(const BookmarkStore& store,
                         const std::string& path,
                         BookmarkCodecError* error = nullptr);

/// Loads a store from disk.  Missing/unreadable/oversized/corrupt files all
/// fail closed with an error; nothing is partially imported.
std::optional<BookmarkStore> LoadBookmarksFromFile(
    const std::string& path,
    BookmarkCodecError* error = nullptr);

}  // namespace crayon::browser_bookmarks
