#pragma once

#include "crayon/browser_tabs/tab_strip_state_machine.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crayon::browser_tabs {

/// Maximum number of tab groups in a single window.
inline constexpr std::size_t kMaxTabGroups = 8;

/// Maximum length of a group identifier.
inline constexpr std::size_t kMaxGroupIdLength = 64;

/// Advanced tab strip features built on top of the basic state machine.
///
/// Adds pin, duplicate, mute, search, grouping and cross-window move tracking.
/// All tab lifecycle commands go through this machine; the underlying basic
/// state machine is an implementation detail.
class AdvancedTabStripStateMachine final {
 public:
  AdvancedTabStripStateMachine() = default;

  // --- Basic commands (forwarded with advanced state sync) ---
  bool AddTab(std::string tab_id);
  bool CloseTab(const std::string& tab_id);
  bool ActivateTab(const std::string& tab_id);
  bool SelectNext() noexcept;
  bool SelectPrevious() noexcept;
  bool MoveTab(std::size_t from_index, std::size_t to_index) noexcept;
  bool RestoreClosed();

  void OnTabCreated(std::string tab_id);
  void OnTabClosed(const std::string& tab_id);
  void Shutdown() noexcept;

  // --- Pin ---
  bool PinTab(const std::string& tab_id);
  bool UnpinTab(const std::string& tab_id);
  bool IsPinned(const std::string& tab_id) const;

  // --- Duplicate ---
  bool DuplicateTab(const std::string& tab_id);

  // --- Mute ---
  bool MuteTab(const std::string& tab_id);
  bool UnmuteTab(const std::string& tab_id);
  bool IsMuted(const std::string& tab_id) const;

  // --- Group ---
  bool AddTabToGroup(const std::string& tab_id,
                     const std::string& group_id);
  bool RemoveTabFromGroup(const std::string& tab_id);
  std::optional<std::string> GetTabGroup(
      const std::string& tab_id) const;

  // --- Search ---
  std::vector<std::string> SearchTabs(
      const std::string& query) const;

  // --- Cross-window move readiness ---
  bool CanMoveTabToWindow(
      const std::string& tab_id) const noexcept;

  // --- Queries (forwarded or enhanced) ---
  std::size_t tab_count() const noexcept { return base_.tab_count(); }
  bool empty() const noexcept { return base_.empty(); }
  bool active() const noexcept { return base_.active(); }
  const std::vector<std::string>& tabs() const noexcept {
    return base_.tabs();
  }
  std::optional<std::size_t> active_index() const noexcept {
    return base_.active_index();
  }
  std::optional<std::string> active_tab_id() const noexcept {
    return base_.active_tab_id();
  }
  bool CanRestoreClosed() const noexcept {
    return base_.CanRestoreClosed();
  }
  std::size_t restorable_count() const noexcept {
    return base_.restorable_count();
  }
  std::optional<std::size_t> FindTabIndex(
      const std::string& tab_id) const noexcept {
    return base_.FindTabIndex(tab_id);
  }

  /// Returns tabs with pinned ones first, preserving relative order.
  std::vector<std::string> ordered_tabs() const;
  std::vector<std::string> pinned_tabs() const;
  std::vector<std::string> tabs_in_group(
      const std::string& group_id) const;

 private:
  void CleanupTabState(const std::string& tab_id) noexcept;

  TabStripStateMachine base_;
  std::unordered_set<std::string> pinned_;
  std::unordered_set<std::string> muted_;
  std::unordered_map<std::string, std::string> groups_;  // tab_id -> group_id
};

}  // namespace crayon::browser_tabs
