#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_KIND_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_KIND_H_

#include <cstddef>

namespace crayon::browser::cef_shell::permission {

// Permission kinds that the browser controls per-site.
// Kept in a single file so the count and ordering are stable.
enum class PermissionKind : std::size_t {
  kCamera = 0,
  kMicrophone,
  kNotifications,
  kGeolocation,
  kClipboardRead,
  kClipboardWrite,
  kDownload,
  kCount  // not a real kind; used for array sizing
};

inline constexpr std::size_t kPermissionKindCount =
    static_cast<std::size_t>(PermissionKind::kCount);

}  // namespace crayon::browser::cef_shell::permission

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_KIND_H_
