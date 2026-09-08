#pragma once

#include <optional>
#include <string>

#include "browser/window/alloy_session_restore.h"

namespace crayon::browser::cef_shell::macos {

// macOS shell-private bounded checkpoint I/O. The shared session module owns
// snapshot schema and restore policy.
class AlloySessionRestoreMac final {
public:
  static window::AlloySessionFileResult SaveCheckpoint(
      const std::string &utf8_absolute_path,
      const browser_session::SessionProfileSnapshot &snapshot,
      browser_session::WindowKind kind);

  static std::optional<browser_session::SessionProfileSnapshot>
  LoadCheckpoint(const std::string &utf8_absolute_path,
                 const std::string &profile_id,
                 window::AlloySessionFileResult *result = nullptr);
};

} // namespace crayon::browser::cef_shell::macos
