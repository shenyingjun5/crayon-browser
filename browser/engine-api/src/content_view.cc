#include "crayon/browser_engine/content_view.h"

namespace crayon::browser_engine {
namespace {

template <typename Enum> constexpr int Raw(Enum value) noexcept {
  return static_cast<int>(value);
}

} // namespace

bool IsValid(ContentPurpose value) noexcept {
  return Raw(value) >= Raw(ContentPurpose::kWeb) &&
         Raw(value) <= Raw(ContentPurpose::kLocalDocument);
}

bool IsValid(ContentCapability value) noexcept {
  return Raw(value) >= Raw(ContentCapability::kNavigate) &&
         Raw(value) <= Raw(ContentCapability::kFind);
}

bool ContentCapabilitySet::Supports(
    ContentCapability capability) const noexcept {
  return IsValid(capability) &&
         (bits_ & (1U << static_cast<std::uint32_t>(capability))) != 0;
}

bool IsValid(ContentViewResultKind value) noexcept {
  return Raw(value) >= Raw(ContentViewResultKind::kCreated) &&
         Raw(value) <= Raw(ContentViewResultKind::kCrashed);
}

bool IsValid(const ContentViewMountRequest &request) noexcept {
  return request.navigation_id.value() != 0 && IsValid(request.purpose);
}

bool IsValid(const ContentViewResult &result) noexcept {
  if (!IsValid(result.mount) || !IsValid(result.kind)) {
    return false;
  }
  const auto error_value = Raw(result.error);
  if (error_value < Raw(EngineErrorCode::kNone) ||
      error_value > Raw(EngineErrorCode::kNavigationFailed)) {
    return false;
  }
  if (result.kind == ContentViewResultKind::kCreated) {
    return result.error == EngineErrorCode::kNone;
  }
  if (result.capabilities != ContentCapabilitySet::None()) {
    return false;
  }
  if (result.kind == ContentViewResultKind::kCreateFailed ||
      result.kind == ContentViewResultKind::kCrashed) {
    return result.error != EngineErrorCode::kNone;
  }
  return result.error == EngineErrorCode::kNone;
}

} // namespace crayon::browser_engine
