#pragma once

#include <cstddef>
#include <map>

#include "crayon/browser_engine/content_view.h"

namespace crayon::browser_shell {

inline constexpr std::size_t kMaximumContentViewRecords = 256;

enum class ContentViewLifecycleState {
  kCreating = 0,
  kMounted,
  kClosing,
  kDetached,
};

enum class ContentViewRegistryResult {
  kAccepted = 0,
  kInvalidArgument,
  kInvalidState,
  kAlreadyExists,
  kNotFound,
  kStaleEpoch,
  kCapacityExceeded,
  kUnsupported,
  kInactive,
};

struct ContentViewRecord final {
  browser_engine::ContentViewMountRequest mount;
  browser_engine::ContentCapabilitySet capabilities;
  ContentViewLifecycleState state;
  bool created;
};

class ContentViewRegistry final {
public:
  explicit ContentViewRegistry(std::size_t capacity) noexcept;

  ContentViewRegistryResult
  BeginMount(const browser_engine::ContentViewMountRequest &request);
  ContentViewRegistryResult BeginClose(const browser_engine::TabId &tab_id,
                                       browser_engine::MountEpoch epoch);
  ContentViewRegistryResult
  OnResult(const browser_engine::ContentViewResult &result);
  ContentViewRegistryResult RequireCapability(
      const browser_engine::TabId &tab_id, browser_engine::MountEpoch epoch,
      browser_engine::ContentCapability capability) const noexcept;
  ContentViewRegistryResult
  ForgetDetachedTab(const browser_engine::TabId &tab_id);

  const ContentViewRecord *
  Find(const browser_engine::TabId &tab_id) const noexcept;
  std::size_t size() const noexcept { return records_.size(); }
  bool active() const noexcept { return active_; }
  void Shutdown() noexcept;

private:
  bool Matches(const ContentViewRecord &record,
               const browser_engine::ContentViewMountRequest &mount) const;

  std::size_t capacity_;
  std::map<std::string, ContentViewRecord> records_;
  bool active_;
};

} // namespace crayon::browser_shell
