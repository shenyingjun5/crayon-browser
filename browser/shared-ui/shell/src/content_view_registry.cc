#include "crayon/browser_shell/content_view_registry.h"

#include <utility>

namespace crayon::browser_shell {

ContentViewRegistry::ContentViewRegistry(std::size_t capacity) noexcept
    : capacity_(capacity),
      active_(capacity > 0 && capacity <= kMaximumContentViewRecords) {}

ContentViewRegistryResult ContentViewRegistry::BeginMount(
    const browser_engine::ContentViewMountRequest &request) {
  if (!active_) {
    return ContentViewRegistryResult::kInactive;
  }
  if (!browser_engine::IsValid(request)) {
    return ContentViewRegistryResult::kInvalidArgument;
  }
  const auto existing = records_.find(request.tab_id.value());
  if (existing != records_.end()) {
    if (existing->second.state != ContentViewLifecycleState::kDetached) {
      return ContentViewRegistryResult::kAlreadyExists;
    }
    if (!(existing->second.mount.mount_epoch < request.mount_epoch)) {
      return ContentViewRegistryResult::kStaleEpoch;
    }
    existing->second =
        ContentViewRecord{request, browser_engine::ContentCapabilitySet::None(),
                          ContentViewLifecycleState::kCreating, false};
    return ContentViewRegistryResult::kAccepted;
  }
  if (records_.size() >= capacity_) {
    return ContentViewRegistryResult::kCapacityExceeded;
  }
  records_.emplace(
      request.tab_id.value(),
      ContentViewRecord{request, browser_engine::ContentCapabilitySet::None(),
                        ContentViewLifecycleState::kCreating, false});
  return ContentViewRegistryResult::kAccepted;
}

ContentViewRegistryResult
ContentViewRegistry::BeginClose(const browser_engine::TabId &tab_id,
                                browser_engine::MountEpoch epoch) {
  if (!active_) {
    return ContentViewRegistryResult::kInactive;
  }
  auto record = records_.find(tab_id.value());
  if (record == records_.end()) {
    return ContentViewRegistryResult::kNotFound;
  }
  if (record->second.mount.mount_epoch != epoch) {
    return ContentViewRegistryResult::kStaleEpoch;
  }
  if (record->second.state == ContentViewLifecycleState::kDetached) {
    return ContentViewRegistryResult::kAccepted;
  }
  if (record->second.state == ContentViewLifecycleState::kClosing) {
    return ContentViewRegistryResult::kAccepted;
  }
  record->second.state = ContentViewLifecycleState::kClosing;
  return ContentViewRegistryResult::kAccepted;
}

ContentViewRegistryResult
ContentViewRegistry::OnResult(const browser_engine::ContentViewResult &result) {
  if (!active_) {
    return ContentViewRegistryResult::kInactive;
  }
  if (!browser_engine::IsValid(result)) {
    return ContentViewRegistryResult::kInvalidArgument;
  }
  auto record = records_.find(result.mount.tab_id.value());
  if (record == records_.end()) {
    return ContentViewRegistryResult::kNotFound;
  }
  if (record->second.mount.mount_epoch != result.mount.mount_epoch) {
    return ContentViewRegistryResult::kStaleEpoch;
  }
  if (!Matches(record->second, result.mount)) {
    return ContentViewRegistryResult::kInvalidArgument;
  }

  using browser_engine::ContentViewResultKind;
  switch (record->second.state) {
  case ContentViewLifecycleState::kCreating:
    if (result.kind == ContentViewResultKind::kCreated) {
      record->second.capabilities = result.capabilities;
      record->second.created = true;
      record->second.state = ContentViewLifecycleState::kMounted;
      return ContentViewRegistryResult::kAccepted;
    }
    if ((result.kind == ContentViewResultKind::kCreateFailed &&
         !record->second.created) ||
        result.kind == ContentViewResultKind::kClosed ||
        result.kind == ContentViewResultKind::kCrashed) {
      record->second.created = false;
      record->second.state = ContentViewLifecycleState::kDetached;
      return ContentViewRegistryResult::kAccepted;
    }
    return ContentViewRegistryResult::kInvalidState;

  case ContentViewLifecycleState::kMounted:
    if (result.kind == ContentViewResultKind::kCrashed ||
        result.kind == ContentViewResultKind::kClosed) {
      record->second.created = false;
      record->second.state = ContentViewLifecycleState::kDetached;
      return ContentViewRegistryResult::kAccepted;
    }
    return ContentViewRegistryResult::kInvalidState;

  case ContentViewLifecycleState::kClosing:
    if (result.kind == ContentViewResultKind::kCreated &&
        !record->second.created) {
      record->second.capabilities = result.capabilities;
      record->second.created = true;
      return ContentViewRegistryResult::kAccepted;
    }
    if (result.kind == ContentViewResultKind::kCloseCancelled) {
      if (!record->second.created) {
        return ContentViewRegistryResult::kInvalidState;
      }
      record->second.state = ContentViewLifecycleState::kMounted;
      return ContentViewRegistryResult::kAccepted;
    }
    if ((result.kind == ContentViewResultKind::kCreateFailed &&
         !record->second.created) ||
        result.kind == ContentViewResultKind::kClosed ||
        result.kind == ContentViewResultKind::kCrashed) {
      record->second.created = false;
      record->second.state = ContentViewLifecycleState::kDetached;
      return ContentViewRegistryResult::kAccepted;
    }
    return ContentViewRegistryResult::kInvalidState;

  case ContentViewLifecycleState::kDetached:
    if (result.kind == ContentViewResultKind::kClosed ||
        result.kind == ContentViewResultKind::kCrashed) {
      return ContentViewRegistryResult::kAccepted;
    }
    return ContentViewRegistryResult::kInvalidState;
  }
  return ContentViewRegistryResult::kInvalidState;
}

ContentViewRegistryResult ContentViewRegistry::RequireCapability(
    const browser_engine::TabId &tab_id, browser_engine::MountEpoch epoch,
    browser_engine::ContentCapability capability) const noexcept {
  if (!active_) {
    return ContentViewRegistryResult::kInactive;
  }
  if (!browser_engine::IsValid(capability)) {
    return ContentViewRegistryResult::kInvalidArgument;
  }
  const auto record = records_.find(tab_id.value());
  if (record == records_.end()) {
    return ContentViewRegistryResult::kNotFound;
  }
  if (record->second.mount.mount_epoch != epoch) {
    return ContentViewRegistryResult::kStaleEpoch;
  }
  if (record->second.state != ContentViewLifecycleState::kMounted) {
    return ContentViewRegistryResult::kInvalidState;
  }
  return record->second.capabilities.Supports(capability)
             ? ContentViewRegistryResult::kAccepted
             : ContentViewRegistryResult::kUnsupported;
}

ContentViewRegistryResult
ContentViewRegistry::ForgetDetachedTab(const browser_engine::TabId &tab_id) {
  if (!active_) {
    return ContentViewRegistryResult::kInactive;
  }
  const auto record = records_.find(tab_id.value());
  if (record == records_.end()) {
    return ContentViewRegistryResult::kAccepted;
  }
  if (record->second.state != ContentViewLifecycleState::kDetached) {
    return ContentViewRegistryResult::kInvalidState;
  }
  records_.erase(record);
  return ContentViewRegistryResult::kAccepted;
}

const ContentViewRecord *
ContentViewRegistry::Find(const browser_engine::TabId &tab_id) const noexcept {
  const auto record = records_.find(tab_id.value());
  return record == records_.end() ? nullptr : &record->second;
}

void ContentViewRegistry::Shutdown() noexcept {
  active_ = false;
  records_.clear();
}

bool ContentViewRegistry::Matches(
    const ContentViewRecord &record,
    const browser_engine::ContentViewMountRequest &mount) const {
  return record.mount.profile_id == mount.profile_id &&
         record.mount.tab_id == mount.tab_id &&
         record.mount.navigation_id == mount.navigation_id &&
         record.mount.purpose == mount.purpose;
}

} // namespace crayon::browser_shell
