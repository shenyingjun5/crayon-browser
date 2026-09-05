#include "browser/context/profile_context_factory.h"

#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

#include <filesystem>
#include <utility>

namespace crayon::browser::cef_shell::context {

ProfileContextFactory::ProfileContextFactory(std::string base_cache_path)
    : base_cache_path_(std::move(base_cache_path)) {}

CefRefPtr<CefRequestContext> ProfileContextFactory::GetPersistentContext(
    const std::string &profile_id,
    CefRefPtr<CefRequestContextHandler> handler) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || base_cache_path_.empty() || !IsValidProfileId(profile_id)) {
    return nullptr;
  }

  const auto it = persistent_contexts_.find(profile_id);
  if (it != persistent_contexts_.end()) {
    return it->second;
  }

  const std::string cache_path =
      BuildProfileCachePath(base_cache_path_, profile_id);
  std::error_code directory_error;
  std::filesystem::create_directories(cache_path, directory_error);
  if (directory_error)
    return nullptr;

  CefRequestContextSettings settings;
  CefString(&settings.cache_path) = cache_path;

  CefRefPtr<CefRequestContext> context =
      CefRequestContext::CreateContext(settings, handler);
  if (context) {
    persistent_contexts_[profile_id] = context;
  }
  return context;
}

CefRefPtr<CefRequestContext> ProfileContextFactory::CreateTemporaryContext(
    CefRefPtr<CefRequestContextHandler> handler) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_)
    return nullptr;

  CefRequestContextSettings settings;
  // cache_path left empty => in-memory only
  return CefRequestContext::CreateContext(settings, handler);
}

void ProfileContextFactory::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  active_ = false;
  persistent_contexts_.clear();
}

} // namespace crayon::browser::cef_shell::context
