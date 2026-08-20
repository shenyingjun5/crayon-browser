#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_CONTEXT_PROFILE_CONTEXT_FACTORY_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_CONTEXT_PROFILE_CONTEXT_FACTORY_H_

#include "browser/context/profile_id_validator.h"
#include "include/cef_request_context.h"

#include <string>
#include <unordered_map>

namespace crayon::browser::cef_shell::context {

/// Factory for creating isolated CefRequestContext instances.
///
/// Provides two kinds of contexts:
/// - Temporary (in-memory): no disk caching, suitable for incognito/guest.
/// - Persistent: disk-backed, stored under a hashed subdirectory so the
///   profile ID never appears literally in the file system path.
///
/// All methods must be called on the CEF UI thread (CEF_REQUIRE_UI_THREAD).
class ProfileContextFactory final {
 public:
  explicit ProfileContextFactory(std::string base_cache_path);

  /// Returns a persistent context for the given profile ID.
  /// Creates a new context on first call; subsequent calls return the same
  /// instance. Returns nullptr if the profile ID is invalid.
  CefRefPtr<CefRequestContext> GetPersistentContext(
      const std::string& profile_id);

  /// Returns a shared temporary (in-memory) context with no disk cache.
  /// Created lazily on first call.
  CefRefPtr<CefRequestContext> GetTemporaryContext();

  /// Releases all held references. Safe to call multiple times.
  void Shutdown();

 private:
  const std::string base_cache_path_;
  std::unordered_map<std::string, CefRefPtr<CefRequestContext>>
      persistent_contexts_;
  CefRefPtr<CefRequestContext> temporary_context_;
};

}  // namespace crayon::browser::cef_shell::context

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_CONTEXT_PROFILE_CONTEXT_FACTORY_H_
