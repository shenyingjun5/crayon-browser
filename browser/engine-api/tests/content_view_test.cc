#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "crayon/browser_engine/content_view.h"

namespace {

using namespace crayon::browser_engine;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

template <typename T> T Require(std::optional<T> value) {
  if (!value.has_value())
    std::abort();
  return std::move(*value);
}

template <typename T> T Id(const char *value) {
  return Require(T::TryCreate(std::string(value)));
}

bool StrongTypesAndCapabilityMaskFailClosed() {
  CHECK(!MountEpoch::TryCreate(0).has_value());
  CHECK(MountEpoch::TryCreate(1).has_value());
  CHECK(!ContentCapabilitySet::TryCreate(ContentCapabilitySet::kKnownMask + 1U)
             .has_value());
  const auto capabilities = Require(ContentCapabilitySet::TryCreate(
      (1U << static_cast<unsigned>(ContentCapability::kNavigate)) |
      (1U << static_cast<unsigned>(ContentCapability::kSnapshot))));
  CHECK(capabilities.Supports(ContentCapability::kNavigate));
  CHECK(capabilities.Supports(ContentCapability::kSnapshot));
  CHECK(!capabilities.Supports(ContentCapability::kFind));
  CHECK(!capabilities.Supports(static_cast<ContentCapability>(99)));
  CHECK(!IsValid(static_cast<ContentPurpose>(99)));
  CHECK(!IsValid(static_cast<ContentViewResultKind>(99)));
  return true;
}

bool RequestsAndResultsHaveClosedShapes() {
  const auto epoch = Require(MountEpoch::TryCreate(7));
  const ContentViewMountRequest mount{
      Id<ProfileId>("profile"), Id<TabId>("tab"), NavigationId::FromRaw(3),
      epoch, ContentPurpose::kWeb};
  CHECK(IsValid(mount));
  auto invalid_mount = mount;
  invalid_mount.navigation_id = NavigationId::FromRaw(0);
  CHECK(!IsValid(invalid_mount));
  invalid_mount = mount;
  invalid_mount.purpose = static_cast<ContentPurpose>(99);
  CHECK(!IsValid(invalid_mount));

  const auto navigate = Require(ContentCapabilitySet::TryCreate(1U));
  CHECK(IsValid(ContentViewResult{mount, ContentViewResultKind::kCreated,
                                  navigate, EngineErrorCode::kNone}));
  CHECK(!IsValid(ContentViewResult{mount, ContentViewResultKind::kCreated,
                                   navigate,
                                   EngineErrorCode::kNavigationFailed}));
  CHECK(IsValid(ContentViewResult{mount, ContentViewResultKind::kCreateFailed,
                                  ContentCapabilitySet::None(),
                                  EngineErrorCode::kNavigationFailed}));
  CHECK(!IsValid(ContentViewResult{mount, ContentViewResultKind::kCreateFailed,
                                   navigate,
                                   EngineErrorCode::kNavigationFailed}));
  CHECK(IsValid(ContentViewResult{mount, ContentViewResultKind::kClosed,
                                  ContentCapabilitySet::None(),
                                  EngineErrorCode::kNone}));
  CHECK(!IsValid(
      ContentViewResult{mount, static_cast<ContentViewResultKind>(99),
                        ContentCapabilitySet::None(), EngineErrorCode::kNone}));
  CHECK(!IsValid(ContentViewResult{mount, ContentViewResultKind::kCrashed,
                                   ContentCapabilitySet::None(),
                                   static_cast<EngineErrorCode>(99)}));
  return true;
}

} // namespace

int main() {
  return StrongTypesAndCapabilityMaskFailClosed() &&
                 RequestsAndResultsHaveClosedShapes()
             ? 0
             : 1;
}
