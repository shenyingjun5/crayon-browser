#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace crayon::browser_tabs {

/// Maximum number of recently-closed tabs that can be restored.
inline constexpr std::size_t kMaxRestorableTabs = 10;

/// Maximum number of tabs in a single window.
inline constexpr std::size_t kMaxTabCount = 32;

/// Platform-neutral tab strip state machine.
///
/// Owns tab order, active tab index, and a bounded recently-closed stack.
/// Actual browser-engine tab lifecycle is managed by the engine adapter;
/// this state machine only tracks what the shared UI needs to render
/// and respond to keyboard shortcuts.
class TabStripStateMachine final {
 public:
  struct ClosedTabInfo final {
    std::string tab_id;
    std::string url;
  };

  TabStripStateMachine() = default;

  // Commands (called from UI / keyboard shortcuts)
  bool AddTab(std::string tab_id);
  bool CloseTab(const std::string& tab_id);
  bool ActivateTab(const std::string& tab_id);
  bool SelectNext() noexcept;
  bool SelectPrevious() noexcept;
  bool MoveTab(std::size_t from_index, std::size_t to_index) noexcept;
  bool RestoreClosed();

  // Engine event observers
  void OnTabCreated(std::string tab_id);
  void OnTabClosed(const std::string& tab_id);

  // Queries
  std::size_t tab_count() const noexcept { return tabs_.size(); }
  bool empty() const noexcept { return tabs_.empty(); }
  bool active() const noexcept { return active_; }

  const std::vector<std::string>& tabs() const noexcept { return tabs_; }
  std::optional<std::size_t> active_index() const noexcept;
  std::optional<std::string> active_tab_id() const noexcept;

  bool CanRestoreClosed() const noexcept { return !closed_stack_.empty(); }
  std::size_t restorable_count() const noexcept { return closed_stack_.size(); }

  std::optional<std::size_t> FindTabIndex(const std::string& tab_id) const noexcept;

  // Lifecycle
  void Shutdown() noexcept;

 private:
  void ClampActiveIndex() noexcept;
  void PushClosed(const std::string& tab_id);

  std::vector<std::string> tabs_;
  std::optional<std::size_t> active_index_;
  std::deque<ClosedTabInfo> closed_stack_;
  bool active_ = true;
};

}  // namespace crayon::browser_tabs
