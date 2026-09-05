#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "crayon/browser_shell/content_view_registry.h"

namespace {

using namespace crayon::browser_engine;
using namespace crayon::browser_shell;

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

ContentViewMountRequest Mount(const char *profile, const char *tab,
                              std::uint64_t navigation, std::uint64_t epoch) {
  return ContentViewMountRequest{
      Id<ProfileId>(profile), Id<TabId>(tab), NavigationId::FromRaw(navigation),
      Require(MountEpoch::TryCreate(epoch)), ContentPurpose::kWeb};
}

ContentViewResult
Result(const ContentViewMountRequest &mount, ContentViewResultKind kind,
       ContentCapabilitySet capabilities = ContentCapabilitySet::None(),
       EngineErrorCode error = EngineErrorCode::kNone) {
  return ContentViewResult{mount, kind, capabilities, error};
}

bool LifecycleConvergesAndFencesEpochs() {
  ContentViewRegistry registry(2);
  const auto first = Mount("profile", "tab", 1, 1);
  CHECK(registry.BeginMount(first) == ContentViewRegistryResult::kAccepted);
  CHECK(registry.BeginMount(first) ==
        ContentViewRegistryResult::kAlreadyExists);
  const auto capabilities = Require(ContentCapabilitySet::TryCreate(
      (1U << static_cast<unsigned>(ContentCapability::kNavigate)) |
      (1U << static_cast<unsigned>(ContentCapability::kZoom))));
  CHECK(registry.OnResult(
            Result(first, ContentViewResultKind::kCreated, capabilities)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.RequireCapability(first.tab_id, first.mount_epoch,
                                   ContentCapability::kNavigate) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.RequireCapability(first.tab_id, first.mount_epoch,
                                   ContentCapability::kSnapshot) ==
        ContentViewRegistryResult::kUnsupported);
  CHECK(registry.RequireCapability(first.tab_id, first.mount_epoch,
                                   static_cast<ContentCapability>(99)) ==
        ContentViewRegistryResult::kInvalidArgument);

  CHECK(registry.BeginClose(first.tab_id, first.mount_epoch) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.BeginClose(first.tab_id, first.mount_epoch) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(
            Result(first, ContentViewResultKind::kCreated, capabilities)) ==
        ContentViewRegistryResult::kInvalidState);
  CHECK(registry.OnResult(
            Result(first, ContentViewResultKind::kCloseCancelled)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.Find(first.tab_id)->state ==
        ContentViewLifecycleState::kMounted);
  CHECK(registry.BeginClose(first.tab_id, first.mount_epoch) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(Result(first, ContentViewResultKind::kClosed)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(Result(first, ContentViewResultKind::kClosed)) ==
        ContentViewRegistryResult::kAccepted);

  CHECK(registry.BeginMount(first) == ContentViewRegistryResult::kStaleEpoch);
  const auto second = Mount("profile", "tab", 2, 2);
  CHECK(registry.BeginMount(second) == ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(Result(first, ContentViewResultKind::kCrashed,
                                 ContentCapabilitySet::None(),
                                 EngineErrorCode::kInvalidState)) ==
        ContentViewRegistryResult::kStaleEpoch);
  auto mismatched = second;
  mismatched.navigation_id = NavigationId::FromRaw(99);
  CHECK(
      registry.OnResult(Result(mismatched, ContentViewResultKind::kCreateFailed,
                               ContentCapabilitySet::None(),
                               EngineErrorCode::kNavigationFailed)) ==
      ContentViewRegistryResult::kInvalidArgument);
  CHECK(registry.BeginClose(second.tab_id, second.mount_epoch) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(
            Result(second, ContentViewResultKind::kCreated, capabilities)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.Find(second.tab_id)->state ==
        ContentViewLifecycleState::kClosing);
  CHECK(registry.Find(second.tab_id)->created);
  CHECK(registry.OnResult(
            Result(second, ContentViewResultKind::kCloseCancelled)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.RequireCapability(second.tab_id, second.mount_epoch,
                                   ContentCapability::kZoom) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.BeginClose(second.tab_id, second.mount_epoch) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(Result(second, ContentViewResultKind::kCreateFailed,
                                 ContentCapabilitySet::None(),
                                 EngineErrorCode::kNavigationFailed)) ==
        ContentViewRegistryResult::kInvalidState);
  CHECK(registry.OnResult(Result(second, ContentViewResultKind::kClosed)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.ForgetDetachedTab(second.tab_id) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.size() == 0);
  return true;
}

bool CapacityPartialFailureCrashAndShutdownAreBounded() {
  ContentViewRegistry invalid_zero(0);
  CHECK(!invalid_zero.active());
  ContentViewRegistry invalid_large(kMaximumContentViewRecords + 1);
  CHECK(!invalid_large.active());

  ContentViewRegistry registry(1);
  const auto first = Mount("profile", "first", 1, 1);
  const auto second = Mount("profile", "second", 1, 1);
  CHECK(registry.BeginMount(first) == ContentViewRegistryResult::kAccepted);
  CHECK(registry.BeginMount(second) ==
        ContentViewRegistryResult::kCapacityExceeded);
  CHECK(registry.BeginClose(first.tab_id, first.mount_epoch) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(
            Result(first, ContentViewResultKind::kCloseCancelled)) ==
        ContentViewRegistryResult::kInvalidState);
  CHECK(registry.OnResult(Result(first, ContentViewResultKind::kCreateFailed,
                                 ContentCapabilitySet::None(),
                                 EngineErrorCode::kNavigationFailed)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.BeginMount(second) ==
        ContentViewRegistryResult::kCapacityExceeded);
  CHECK(registry.ForgetDetachedTab(first.tab_id) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.BeginMount(second) == ContentViewRegistryResult::kAccepted);
  CHECK(registry.OnResult(Result(second, ContentViewResultKind::kCrashed,
                                 ContentCapabilitySet::None(),
                                 EngineErrorCode::kInvalidState)) ==
        ContentViewRegistryResult::kAccepted);
  CHECK(registry.Find(second.tab_id)->state ==
        ContentViewLifecycleState::kDetached);
  registry.Shutdown();
  registry.Shutdown();
  CHECK(!registry.active());
  CHECK(registry.size() == 0);
  CHECK(registry.BeginMount(first) == ContentViewRegistryResult::kInactive);
  CHECK(registry.OnResult(Result(first, ContentViewResultKind::kClosed)) ==
        ContentViewRegistryResult::kInactive);
  return true;
}

} // namespace

int main() {
  return LifecycleConvergesAndFencesEpochs() &&
                 CapacityPartialFailureCrashAndShutdownAreBounded()
             ? 0
             : 1;
}
