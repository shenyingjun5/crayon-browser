#pragma once

#include <optional>
#include <string>

#include "crayon/browser_session/session_restore.h"
#include "crayon/browser_session/session_snapshot.h"

namespace crayon::browser::cef_shell::window {

enum class AlloySessionFileResult {
  kSuccess = 0,
  kSkippedIncognito,
  kNotFound,
  kInvalidPath,
  kTooLarge,
  kCorrupt,
  kProfileMismatch,
  kIoError,
};

// Windows-only atomic persistence adapter. The session module remains the
// schema/policy owner; this adapter only performs bounded same-directory I/O.
class AlloySessionRestore final {
public:
  static AlloySessionFileResult SaveCheckpoint(
      const std::wstring &path,
      const browser_session::SessionProfileSnapshot &snapshot,
      browser_session::WindowKind kind);

  static std::optional<browser_session::SessionProfileSnapshot>
  LoadCheckpoint(const std::wstring &path, const std::string &profile_id,
                 AlloySessionFileResult *result = nullptr);
};

} // namespace crayon::browser::cef_shell::window
