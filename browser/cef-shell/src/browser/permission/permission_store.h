#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_STORE_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_STORE_H_

#include "browser/permission/permission_decision.h"
#include "browser/permission/permission_kind.h"

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace crayon::browser::cef_shell::permission {

// Stores per-site permission decisions.  All lookups default to kDeny.
//
// Decisions may be:
// - kAllowSession: valid only until ClearSessionDecisions() is called.
// - kAllowPersistent: survives until explicitly removed or overwritten.
// - kDeny: explicit denial (survives ClearSessionDecisions).
//
// Thread safety: Query may be called from any thread.  Record,
// ClearSessionDecisions and ClearAll require external serialisation (they
// are intended to be called on the CEF UI thread alongside CEF callbacks).
class PermissionStore final {
 public:
  PermissionStore();

  // Query the stored decision for |origin| + |kind|.
  // Returns kDeny when no record exists.
  PermissionDecision Query(const std::string& origin,
                           PermissionKind kind) const;

  // Record a decision.  Overwrites any previous decision for the pair.
  void Record(const std::string& origin, PermissionKind kind,
              PermissionDecision decision);

  // Remove all session-only decisions.  Persistent decisions remain.
  void ClearSessionDecisions();

  // Remove every decision (session and persistent).
  void ClearAll();

  // Remove all decisions for |origin|.
  void ClearForOrigin(const std::string& origin);

  // Returns a snapshot of all recorded (origin, kind) pairs and their
  // decisions.  Useful for persistence and diagnostics.
  std::vector<std::tuple<std::string, PermissionKind, PermissionDecision>>
  Snapshot() const;

 private:
  struct KeyHash {
    std::size_t operator()(
        const std::pair<std::string, PermissionKind>& key) const noexcept;
  };

  mutable std::shared_mutex mutex_;
  std::unordered_map<std::pair<std::string, PermissionKind>, PermissionDecision,
                     KeyHash>
      decisions_;
};

}  // namespace crayon::browser::cef_shell::permission

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_PERMISSION_STORE_H_
