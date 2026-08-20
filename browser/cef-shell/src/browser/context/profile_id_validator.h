#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_CONTEXT_PROFILE_ID_VALIDATOR_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_CONTEXT_PROFILE_ID_VALIDATOR_H_

#include <string>

namespace crayon::browser::cef_shell::context {

/// Maximum length of a profile identifier.
inline constexpr std::size_t kMaxProfileIdLength = 64;

/// Minimum length of a profile identifier.
inline constexpr std::size_t kMinProfileIdLength = 1;

/// Returns true if the profile ID is non-empty, within length limits, and
/// contains only ASCII alphanumeric characters, hyphens, and underscores.
/// Rejects path separators, dots, spaces, control characters, and Unicode.
bool IsValidProfileId(const std::string& profile_id) noexcept;

/// Maps a valid profile ID to a deterministic directory name.
/// Uses SHA-256 and returns the first 16 bytes as lowercase hex (32 chars).
/// The profile ID itself never appears in the returned path component,
/// preventing path traversal and information leakage.
std::string MapProfileIdToDirectoryName(const std::string& profile_id);

/// Builds a full cache path from a base directory and profile ID.
/// Returns "base/profiles/<hash>/" where <hash> is the deterministic
/// directory name derived from the profile ID.
std::string BuildProfileCachePath(const std::string& base_cache_path,
                                  const std::string& profile_id);

}  // namespace crayon::browser::cef_shell::context

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_CONTEXT_PROFILE_ID_VALIDATOR_H_
