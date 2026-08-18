#include "crayon/browser_tabs/advanced_tab_strip_state_machine.h"

#include <algorithm>

namespace crayon::browser_tabs {

bool AdvancedTabStripStateMachine::AddTab(std::string tab_id) {
  return base_.AddTab(std::move(tab_id));
}

bool AdvancedTabStripStateMachine::CloseTab(const std::string& tab_id) {
  const bool ok = base_.CloseTab(tab_id);
  if (ok) {
    CleanupTabState(tab_id);
  }
  return ok;
}

bool AdvancedTabStripStateMachine::ActivateTab(const std::string& tab_id) {
  return base_.ActivateTab(tab_id);
}

bool AdvancedTabStripStateMachine::SelectNext() noexcept {
  return base_.SelectNext();
}

bool AdvancedTabStripStateMachine::SelectPrevious() noexcept {
  return base_.SelectPrevious();
}

bool AdvancedTabStripStateMachine::MoveTab(std::size_t from_index,
                                            std::size_t to_index) noexcept {
  return base_.MoveTab(from_index, to_index);
}

bool AdvancedTabStripStateMachine::RestoreClosed() {
  return base_.RestoreClosed();
}

void AdvancedTabStripStateMachine::OnTabCreated(std::string tab_id) {
  base_.OnTabCreated(std::move(tab_id));
}

void AdvancedTabStripStateMachine::OnTabClosed(const std::string& tab_id) {
  base_.OnTabClosed(tab_id);
  CleanupTabState(tab_id);
}

void AdvancedTabStripStateMachine::Shutdown() noexcept {
  base_.Shutdown();
  pinned_.clear();
  muted_.clear();
  groups_.clear();
}

// --- Pin ---

bool AdvancedTabStripStateMachine::PinTab(const std::string& tab_id) {
  if (!base_.active() || !base_.FindTabIndex(tab_id).has_value()) {
    return false;
  }
  pinned_.insert(tab_id);
  return true;
}

bool AdvancedTabStripStateMachine::UnpinTab(const std::string& tab_id) {
  if (!base_.active() || !base_.FindTabIndex(tab_id).has_value()) {
    return false;
  }
  pinned_.erase(tab_id);
  return true;
}

bool AdvancedTabStripStateMachine::IsPinned(
    const std::string& tab_id) const {
  return pinned_.count(tab_id) != 0;
}

// --- Duplicate ---

bool AdvancedTabStripStateMachine::DuplicateTab(
    const std::string& tab_id) {
  if (!base_.active() || !base_.FindTabIndex(tab_id).has_value()) {
    return false;
  }

  std::string copy_id = tab_id + "-copy";
  if (!base_.AddTab(copy_id)) {
    // Try numbered suffixes to avoid collision
    for (int i = 2; i <= 100; ++i) {
      copy_id = tab_id + "-copy-" + std::to_string(i);
      if (base_.AddTab(copy_id)) {
        if (IsPinned(tab_id)) {
          PinTab(copy_id);
        }
        if (IsMuted(tab_id)) {
          MuteTab(copy_id);
        }
        if (auto group = GetTabGroup(tab_id); group.has_value()) {
          AddTabToGroup(copy_id, *group);
        }
        return true;
      }
    }
    return false;
  }

  if (IsPinned(tab_id)) {
    PinTab(copy_id);
  }
  if (IsMuted(tab_id)) {
    MuteTab(copy_id);
  }
  if (auto group = GetTabGroup(tab_id); group.has_value()) {
    AddTabToGroup(copy_id, *group);
  }
  return true;
}

// --- Mute ---

bool AdvancedTabStripStateMachine::MuteTab(const std::string& tab_id) {
  if (!base_.active() || !base_.FindTabIndex(tab_id).has_value()) {
    return false;
  }
  muted_.insert(tab_id);
  return true;
}

bool AdvancedTabStripStateMachine::UnmuteTab(const std::string& tab_id) {
  if (!base_.active() || !base_.FindTabIndex(tab_id).has_value()) {
    return false;
  }
  muted_.erase(tab_id);
  return true;
}

bool AdvancedTabStripStateMachine::IsMuted(
    const std::string& tab_id) const {
  return muted_.count(tab_id) != 0;
}

// --- Group ---

bool AdvancedTabStripStateMachine::AddTabToGroup(
    const std::string& tab_id, const std::string& group_id) {
  if (!base_.active() || group_id.empty() ||
      group_id.size() > kMaxGroupIdLength ||
      !base_.FindTabIndex(tab_id).has_value()) {
    return false;
  }

  // Count distinct groups to enforce capacity
  std::unordered_set<std::string> distinct_groups;
  for (const auto& kv : groups_) {
    distinct_groups.insert(kv.second);
  }
  if (!groups_.count(tab_id) && distinct_groups.size() >= kMaxTabGroups &&
      !distinct_groups.count(group_id)) {
    return false;
  }

  groups_[tab_id] = group_id;
  return true;
}

bool AdvancedTabStripStateMachine::RemoveTabFromGroup(
    const std::string& tab_id) {
  if (!base_.active()) {
    return false;
  }
  return groups_.erase(tab_id) != 0;
}

std::optional<std::string> AdvancedTabStripStateMachine::GetTabGroup(
    const std::string& tab_id) const {
  const auto it = groups_.find(tab_id);
  if (it == groups_.end()) {
    return std::nullopt;
  }
  return it->second;
}

// --- Search ---

std::vector<std::string> AdvancedTabStripStateMachine::SearchTabs(
    const std::string& query) const {
  std::vector<std::string> results;
  if (query.empty()) {
    results = base_.tabs();
    return results;
  }
  for (const auto& tab_id : base_.tabs()) {
    if (tab_id.find(query) != std::string::npos) {
      results.push_back(tab_id);
    }
  }
  return results;
}

// --- Cross-window move readiness ---

bool AdvancedTabStripStateMachine::CanMoveTabToWindow(
    const std::string& tab_id) const noexcept {
  return base_.active() && base_.FindTabIndex(tab_id).has_value();
}

// --- Queries ---

std::vector<std::string> AdvancedTabStripStateMachine::ordered_tabs() const {
  std::vector<std::string> result;
  result.reserve(base_.tab_count());

  for (const auto& tab_id : base_.tabs()) {
    if (pinned_.count(tab_id)) {
      result.push_back(tab_id);
    }
  }
  for (const auto& tab_id : base_.tabs()) {
    if (!pinned_.count(tab_id)) {
      result.push_back(tab_id);
    }
  }
  return result;
}

std::vector<std::string> AdvancedTabStripStateMachine::pinned_tabs() const {
  std::vector<std::string> result;
  for (const auto& tab_id : base_.tabs()) {
    if (pinned_.count(tab_id)) {
      result.push_back(tab_id);
    }
  }
  return result;
}

std::vector<std::string> AdvancedTabStripStateMachine::tabs_in_group(
    const std::string& group_id) const {
  std::vector<std::string> result;
  for (const auto& tab_id : base_.tabs()) {
    if (groups_.count(tab_id) && groups_.at(tab_id) == group_id) {
      result.push_back(tab_id);
    }
  }
  return result;
}

// --- Private ---

void AdvancedTabStripStateMachine::CleanupTabState(
    const std::string& tab_id) noexcept {
  pinned_.erase(tab_id);
  muted_.erase(tab_id);
  groups_.erase(tab_id);
}

}  // namespace crayon::browser_tabs
