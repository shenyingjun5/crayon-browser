#include "browser/context/profile_context_factory.h"

#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

#include <utility>

namespace crayon::browser::cef_shell::context {

ProfileContextFactory::ProfileContextFactory(std::string base_cache_path)
    : base_cache_path_(std::move(base_cache_path)) {}

CefRefPtr<CefRequestContext> ProfileContextFactory::GetPersistentContext(
    const std::string& profile_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!IsValidProfileId(profile_id)) {
    return nullptr;
  }

  const auto it = persistent_contexts_.find(profile_id);
  if (it != persistent_contexts_.end()) {
    return it->second;
  }

  const std::string cache_path =
      BuildProfileCachePath(base_cache_path_, profile_id);

  CefRequestContextSettings settings;
  CefString(&settings.cache_path) = cache_path;

  CefRefPtr<CefRequestContext> context =
      CefRequestContext::CreateContext(settings, nullptr);
  if (context) {
    persistent_contexts_[profile_id] = context;
  }
  return context;
}

CefRefPtr<CefRequestContext> ProfileContextFactory::GetTemporaryContext() {
  CEF_REQUIRE_UI_THREAD();
  if (temporary_context_) {
    return temporary_context_;
  }

  CefRequestContextSettings settings;
  // cache_path left empty => in-memory only
  temporary_context_ = CefRequestContext::CreateContext(settings, nullptr);
  return temporary_context_;
}

void ProfileContextFactory::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  persistent_contexts_.clear();
  temporary_context_ = nullptr;
}

}  // namespace crayon::browser::cef_shell::context
