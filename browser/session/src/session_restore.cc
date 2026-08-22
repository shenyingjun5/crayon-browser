#include "crayon/browser_session/session_restore.h"

#include <algorithm>

namespace crayon::browser_session {
namespace {

bool IsClosedCharset(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.';
}

}  // namespace

bool SessionRestoreCoordinator::IsValidId(const std::string& id) {
  return !id.empty() && id.size() <= kMaxIdLen &&
         std::all_of(id.begin(), id.end(), IsClosedCharset);
}

SessionRestoreCoordinator::ProfileRecord* SessionRestoreCoordinator::Find(
    const std::string& profile_id) {
  const auto it = records_.find(profile_id);
  return it == records_.end() ? nullptr : &it->second;
}

const SessionRestoreCoordinator::ProfileRecord* SessionRestoreCoordinator::Find(
    const std::string& profile_id) const {
  const auto it = records_.find(profile_id);
  return it == records_.end() ? nullptr : &it->second;
}

bool SessionRestoreCoordinator::RecordWindow(const std::string& profile_id,
                                             const std::string& window_id,
                                             std::size_t tab_count,
                                             WindowKind kind) {
  if (kind == WindowKind::kIncognito) {
    return false;  // incognito never enters the restore set
  }
  if (!IsValidId(profile_id) || !IsValidId(window_id) || tab_count == 0 ||
      tab_count > kMaxTabsPerWindow) {
    return false;
  }
  ProfileRecord* record = Find(profile_id);
  if (record == nullptr) {
    if (records_.size() >= kMaxProfiles) {
      return false;
    }
    record = &records_[profile_id];
  }
  if (record->checkpointed.size() + record->tail.size() >= kMaxWindowsPerProfile) {
    return false;
  }
  record->tail.push_back(RecordedWindow{window_id, tab_count});
  return true;
}

void SessionRestoreCoordinator::Checkpoint(const std::string& profile_id) {
  ProfileRecord* record = Find(profile_id);
  if (record == nullptr) {
    return;
  }
  record->checkpointed.insert(record->checkpointed.end(), record->tail.begin(), record->tail.end());
  record->tail.clear();
  record->crashed_last_exit = false;
}

void SessionRestoreCoordinator::MarkCrashedLastExit(const std::string& profile_id) {
  ProfileRecord* record = Find(profile_id);
  if (record != nullptr) {
    record->crashed_last_exit = true;
  }
}

RestoreDecision SessionRestoreCoordinator::PlanRestore(const std::string& profile_id,
                                                       StartupPolicy policy) const {
  const ProfileRecord* record = Find(profile_id);
  if (policy == StartupPolicy::kNewTab) {
    return RestoreDecision::kNewTabOnly;
  }
  if (record == nullptr || record->checkpointed.empty()) {
    return RestoreDecision::kNewTabOnly;
  }
  return record->crashed_last_exit ? RestoreDecision::kRestoreAfterCrash
                                   : RestoreDecision::kRestoreRecorded;
}

std::vector<RecordedWindow> SessionRestoreCoordinator::RestorableWindows(
    const std::string& profile_id, bool after_crash, std::size_t* dropped) const {
  if (dropped != nullptr) {
    *dropped = 0;
  }
  const ProfileRecord* record = Find(profile_id);
  if (record == nullptr) {
    return {};
  }
  if (!after_crash) {
    std::vector<RecordedWindow> all = record->checkpointed;
    all.insert(all.end(), record->tail.begin(), record->tail.end());
    return all;
  }
  // Crash recovery: only checkpointed windows survive; the unconfirmed
  // tail is dropped and reported.
  if (dropped != nullptr) {
    *dropped = record->tail.size();
  }
  return record->checkpointed;
}

std::uint64_t SessionRestoreCoordinator::AdvanceEpoch(const std::string& profile_id) {
  ProfileRecord* record = Find(profile_id);
  if (record == nullptr) {
    record = &records_[profile_id];
  }
  record->epoch = record->epoch + 1;
  return record->epoch;
}

bool SessionRestoreCoordinator::IsCurrentEpoch(const std::string& profile_id,
                                               std::uint64_t epoch) const {
  const ProfileRecord* record = Find(profile_id);
  return record != nullptr && record->epoch == epoch;
}

std::size_t SessionRestoreCoordinator::ClearProfile(const std::string& profile_id) {
  const auto it = records_.find(profile_id);
  if (it == records_.end()) {
    return 0;
  }
  const std::size_t count = it->second.checkpointed.size() + it->second.tail.size();
  records_.erase(it);
  return count;
}

std::size_t SessionRestoreCoordinator::recorded_window_count(
    const std::string& profile_id) const {
  const ProfileRecord* record = Find(profile_id);
  return record == nullptr ? 0 : record->checkpointed.size() + record->tail.size();
}

}  // namespace crayon::browser_session
