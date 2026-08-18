#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "crayon/browser_tabs/advanced_tab_strip_state_machine.h"

namespace {

using crayon::browser_tabs::AdvancedTabStripStateMachine;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

// --- Basic forwarding sanity ---

bool AddTabForwarded() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.tab_count() == 1);
  CHECK(*sm.active_tab_id() == "tab-1");
  return true;
}

bool CloseTabForwarded() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(sm.empty());
  return true;
}

// --- Pin ---

bool PinTabWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.PinTab("tab-1"));
  CHECK(sm.IsPinned("tab-1"));
  return true;
}

bool UnpinTabWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.PinTab("tab-1"));
  CHECK(sm.UnpinTab("tab-1"));
  CHECK(!sm.IsPinned("tab-1"));
  return true;
}

bool PinUnknownTabFails() {
  AdvancedTabStripStateMachine sm;
  CHECK(!sm.PinTab("missing"));
  return true;
}

bool PinnedTabsOrderedFirst() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.AddTab("tab-3"));
  CHECK(sm.PinTab("tab-2"));
  const auto ordered = sm.ordered_tabs();
  CHECK(ordered.size() == 3);
  CHECK(ordered[0] == "tab-2");  // pinned first
  CHECK(ordered[1] == "tab-1");
  CHECK(ordered[2] == "tab-3");
  return true;
}

bool PinStateClearedOnClose() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.PinTab("tab-1"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(!sm.IsPinned("tab-1"));
  return true;
}

// --- Duplicate ---

bool DuplicateTabWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.DuplicateTab("tab-1"));
  CHECK(sm.tab_count() == 2);
  CHECK(sm.FindTabIndex("tab-1-copy").has_value());
  return true;
}

bool DuplicateUnknownTabFails() {
  AdvancedTabStripStateMachine sm;
  CHECK(!sm.DuplicateTab("missing"));
  return true;
}

bool DuplicateCopiesPinState() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.PinTab("tab-1"));
  CHECK(sm.DuplicateTab("tab-1"));
  CHECK(sm.IsPinned("tab-1-copy"));
  return true;
}

bool DuplicateCopiesMuteState() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.MuteTab("tab-1"));
  CHECK(sm.DuplicateTab("tab-1"));
  CHECK(sm.IsMuted("tab-1-copy"));
  return true;
}

bool DuplicateCopiesGroupState() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTabToGroup("tab-1", "group-a"));
  CHECK(sm.DuplicateTab("tab-1"));
  CHECK(sm.GetTabGroup("tab-1-copy") == "group-a");
  return true;
}

bool DuplicateAvoidsCollision() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-1-copy"));
  CHECK(sm.DuplicateTab("tab-1"));
  CHECK(sm.tab_count() == 3);
  CHECK(sm.FindTabIndex("tab-1-copy-2").has_value());
  return true;
}

// --- Mute ---

bool MuteTabWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.MuteTab("tab-1"));
  CHECK(sm.IsMuted("tab-1"));
  return true;
}

bool UnmuteTabWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.MuteTab("tab-1"));
  CHECK(sm.UnmuteTab("tab-1"));
  CHECK(!sm.IsMuted("tab-1"));
  return true;
}

bool MuteUnknownTabFails() {
  AdvancedTabStripStateMachine sm;
  CHECK(!sm.MuteTab("missing"));
  return true;
}

bool MuteStateClearedOnClose() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.MuteTab("tab-1"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(!sm.IsMuted("tab-1"));
  return true;
}

// --- Group ---

bool AddTabToGroupWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTabToGroup("tab-1", "group-a"));
  CHECK(sm.GetTabGroup("tab-1") == "group-a");
  return true;
}

bool RemoveTabFromGroupWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTabToGroup("tab-1", "group-a"));
  CHECK(sm.RemoveTabFromGroup("tab-1"));
  CHECK(!sm.GetTabGroup("tab-1").has_value());
  return true;
}

bool GroupUnknownTabFails() {
  AdvancedTabStripStateMachine sm;
  CHECK(!sm.AddTabToGroup("missing", "group-a"));
  return true;
}

bool EmptyGroupIdRejected() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(!sm.AddTabToGroup("tab-1", ""));
  return true;
}

bool GroupCapacityEnforced() {
  AdvancedTabStripStateMachine sm;
  for (std::size_t i = 0; i < 9; ++i) {
    CHECK(sm.AddTab("tab-" + std::to_string(i)));
  }
  // Create 8 distinct groups (kMaxTabGroups)
  for (std::size_t i = 0; i < 8; ++i) {
    CHECK(sm.AddTabToGroup("tab-" + std::to_string(i),
                           "group-" + std::to_string(i)));
  }
  // 9th tab cannot start a 9th group
  CHECK(!sm.AddTabToGroup("tab-8", "group-new"));
  // But can join an existing group
  CHECK(sm.AddTabToGroup("tab-8", "group-0"));
  return true;
}

bool TabsInGroupQueryWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.AddTab("tab-3"));
  CHECK(sm.AddTabToGroup("tab-1", "group-a"));
  CHECK(sm.AddTabToGroup("tab-3", "group-a"));
  const auto members = sm.tabs_in_group("group-a");
  CHECK(members.size() == 2);
  return true;
}

bool GroupStateClearedOnClose() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTabToGroup("tab-1", "group-a"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(!sm.GetTabGroup("tab-1").has_value());
  return true;
}

// --- Search ---

bool SearchTabsBySubstring() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("alpha"));
  CHECK(sm.AddTab("beta"));
  CHECK(sm.AddTab("gamma"));
  const auto results = sm.SearchTabs("et");
  CHECK(results.size() == 1);
  CHECK(results[0] == "beta");
  return true;
}

bool SearchEmptyQueryReturnsAll() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("alpha"));
  CHECK(sm.AddTab("beta"));
  const auto results = sm.SearchTabs("");
  CHECK(results.size() == 2);
  return true;
}

bool SearchNoMatchReturnsEmpty() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("alpha"));
  const auto results = sm.SearchTabs("zzz");
  CHECK(results.empty());
  return true;
}

// --- Cross-window move ---

bool CanMoveTabToWindowWorks() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.CanMoveTabToWindow("tab-1"));
  CHECK(!sm.CanMoveTabToWindow("missing"));
  return true;
}

bool CannotMoveAfterShutdown() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  sm.Shutdown();
  CHECK(!sm.CanMoveTabToWindow("tab-1"));
  return true;
}

// --- Lifecycle ---

bool ShutdownClearsAdvancedState() {
  AdvancedTabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.PinTab("tab-1"));
  CHECK(sm.MuteTab("tab-1"));
  CHECK(sm.AddTabToGroup("tab-1", "group-a"));
  sm.Shutdown();
  CHECK(!sm.IsPinned("tab-1"));
  CHECK(!sm.IsMuted("tab-1"));
  CHECK(!sm.GetTabGroup("tab-1").has_value());
  return true;
}

bool EngineTabClosedCleansAdvancedState() {
  AdvancedTabStripStateMachine sm;
  sm.OnTabCreated("engine-tab-1");
  CHECK(sm.PinTab("engine-tab-1"));
  CHECK(sm.MuteTab("engine-tab-1"));
  sm.OnTabClosed("engine-tab-1");
  CHECK(!sm.IsPinned("engine-tab-1"));
  CHECK(!sm.IsMuted("engine-tab-1"));
  return true;
}

}  // namespace

int main() {
  if (!AddTabForwarded() ||
      !CloseTabForwarded() ||
      !PinTabWorks() ||
      !UnpinTabWorks() ||
      !PinUnknownTabFails() ||
      !PinnedTabsOrderedFirst() ||
      !PinStateClearedOnClose() ||
      !DuplicateTabWorks() ||
      !DuplicateUnknownTabFails() ||
      !DuplicateCopiesPinState() ||
      !DuplicateCopiesMuteState() ||
      !DuplicateCopiesGroupState() ||
      !DuplicateAvoidsCollision() ||
      !MuteTabWorks() ||
      !UnmuteTabWorks() ||
      !MuteUnknownTabFails() ||
      !MuteStateClearedOnClose() ||
      !AddTabToGroupWorks() ||
      !RemoveTabFromGroupWorks() ||
      !GroupUnknownTabFails() ||
      !EmptyGroupIdRejected() ||
      !GroupCapacityEnforced() ||
      !TabsInGroupQueryWorks() ||
      !GroupStateClearedOnClose() ||
      !SearchTabsBySubstring() ||
      !SearchEmptyQueryReturnsAll() ||
      !SearchNoMatchReturnsEmpty() ||
      !CanMoveTabToWindowWorks() ||
      !CannotMoveAfterShutdown() ||
      !ShutdownClearsAdvancedState() ||
      !EngineTabClosedCleansAdvancedState()) {
    return 1;
  }
  return 0;
}
