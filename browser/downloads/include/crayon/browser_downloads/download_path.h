#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace crayon::browser_downloads {

/// Maximum length of a sanitized download file name in bytes.
inline constexpr std::size_t kMaxFileNameLength = 128;

/// Maximum dedupe suffix (" (n)") tried before failing closed.
inline constexpr unsigned kMaxDedupeIndex = 999;

/// Sanitizes an untrusted download file name.
///
/// Strips path separators (both `/` and `\`), ASCII control characters and
/// trailing dots/spaces; enforces the length bound.  Returns `std::nullopt`
/// when nothing usable remains — callers must fail closed.
std::optional<std::string> SanitizeDownloadFileName(
    const std::string& untrusted_name);

/// Predicate reporting whether `path` already exists.  Injected by the
/// caller so this module never touches the file system itself.
using PathExistsPredicate = bool (*)(const std::string& path);

/// Resolves a collision-free target path inside `directory` for
/// `file_name` (already sanitized).  On collision appends " (n)" before the
/// final extension, up to `kMaxDedupeIndex`; beyond that fails closed.
/// Returns `std::nullopt` for empty inputs or a null predicate.
std::optional<std::string> ResolveUniqueDownloadPath(
    const std::string& directory,
    const std::string& file_name,
    PathExistsPredicate path_exists);

}  // namespace crayon::browser_downloads
