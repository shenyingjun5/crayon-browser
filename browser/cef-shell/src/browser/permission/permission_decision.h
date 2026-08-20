#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_DECISION_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_DECISION_H_

namespace crayon::browser::cef_shell::permission {

// Decision returned by the permission store for a (site, kind) pair.
// kDeny is the default for every pair that has no explicit record.
enum class PermissionDecision {
  kDeny = 0,
  kAllowSession,
  kAllowPersistent,
};

}  // namespace crayon::browser::cef_shell::permission

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_DECISION_H_
