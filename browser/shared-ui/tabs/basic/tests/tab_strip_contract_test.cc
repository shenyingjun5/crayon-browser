#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_tabs/tab_strip_state_machine.h"

namespace {

using crayon::browser_tabs::TabStripStateMachine;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool InitialStateIsEmpty() {
  TabStripStateMachine sm;
  CHECK(sm.empty());
  CHECK(sm.tab_count() == 0);
  CHECK(!sm.active_index().has_value());
  CHECK(!sm.active_tab_id().has_value());
  CHECK(!sm.CanRestoreClosed());
  CHECK(sm.active());
  return true;
}

bool AddTabSetsActive() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.tab_count() == 1);
  CHECK(sm.active_tab_id().has_value());
  CHECK(*sm.active_tab_id() == "tab-1");
  CHECK(*sm.active_index() == 0);
  return true;
}

bool DuplicateTabIdRejected() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(!sm.AddTab("tab-1"));
  CHECK(sm.tab_count() == 1);
  return true;
}

bool EmptyTabIdRejected() {
  TabStripStateMachine sm;
  CHECK(!sm.AddTab(""));
  CHECK(sm.empty());
  return true;
}

bool CloseTabRemovesAndStoresRestorable() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(sm.tab_count() == 1);
  CHECK(sm.CanRestoreClosed());
  CHECK(sm.restorable_count() == 1);
  return true;
}

bool CloseLastTabClearsActive() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(sm.empty());
  CHECK(!sm.active_index().has_value());
  CHECK(!sm.active_tab_id().has_value());
  return true;
}

bool CloseActiveSwitchesToNeighbor() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.AddTab("tab-3"));
  CHECK(sm.ActivateTab("tab-2"));
  CHECK(sm.CloseTab("tab-2"));
  CHECK(sm.tab_count() == 2);
  // Active should fall back to index 1 (was tab-3)
  CHECK(*sm.active_index() == 1);
  CHECK(*sm.active_tab_id() == "tab-3");
  return true;
}

bool DuplicateCloseIsIdempotent() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(!sm.CloseTab("tab-1"));  // already closed
  CHECK(sm.empty());
  CHECK(sm.restorable_count() == 1);
  return true;
}

bool ActivateTabWorks() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.ActivateTab("tab-1"));
  CHECK(*sm.active_tab_id() == "tab-1");
  CHECK(*sm.active_index() == 0);
  return true;
}

bool ActivateUnknownTabFails() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(!sm.ActivateTab("missing"));
  CHECK(*sm.active_tab_id() == "tab-1");
  return true;
}

bool SelectNextCycles() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.ActivateTab("tab-1"));          // start from tab-1 (index 0)
  CHECK(sm.SelectNext());
  CHECK(*sm.active_tab_id() == "tab-2");  // advance to tab-2
  CHECK(sm.SelectNext());
  CHECK(*sm.active_tab_id() == "tab-1");  // cycles back
  return true;
}

bool SelectPreviousCycles() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.ActivateTab("tab-1"));          // start from tab-1 (index 0)
  CHECK(sm.SelectPrevious());
  CHECK(*sm.active_tab_id() == "tab-2");  // cycles back from 0
  return true;
}

bool SelectNextOnEmptyFails() {
  TabStripStateMachine sm;
  CHECK(!sm.SelectNext());
  CHECK(!sm.SelectPrevious());
  return true;
}

bool MoveTabChangesOrder() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.AddTab("tab-3"));
  CHECK(sm.MoveTab(2, 0));  // tab-3 to front
  CHECK(sm.tabs()[0] == "tab-3");
  CHECK(sm.tabs()[1] == "tab-1");
  CHECK(sm.tabs()[2] == "tab-2");
  return true;
}

bool MoveTabPreservesActive() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  CHECK(sm.AddTab("tab-3"));
  CHECK(sm.ActivateTab("tab-2"));  // active = index 1
  CHECK(sm.MoveTab(2, 0));         // move tab-3 to front
  // tab-2 is now at index 2
  CHECK(*sm.active_index() == 2);
  CHECK(*sm.active_tab_id() == "tab-2");
  return true;
}

bool RestoreClosedBringsTabBack() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.CloseTab("tab-1"));
  CHECK(sm.RestoreClosed());
  CHECK(sm.tab_count() == 1);
  CHECK(*sm.active_tab_id() == "tab-1");
  CHECK(!sm.CanRestoreClosed());
  return true;
}

bool RestoreClosedOnEmptyStackFails() {
  TabStripStateMachine sm;
  CHECK(!sm.RestoreClosed());
  return true;
}

bool RestoreClosedRespectsCapacity() {
  TabStripStateMachine sm;
  for (int i = 0; i < 12; ++i) {
    CHECK(sm.AddTab("tab-" + std::to_string(i)));
  }
  CHECK(sm.tab_count() == 12);
  // Close 12 tabs
  for (int i = 0; i < 12; ++i) {
    CHECK(sm.CloseTab("tab-" + std::to_string(i)));
  }
  // Only 10 restorable (kMaxRestorableTabs)
  CHECK(sm.restorable_count() == 10);
  // Restore all 10
  for (int i = 0; i < 10; ++i) {
    CHECK(sm.RestoreClosed());
  }
  CHECK(!sm.RestoreClosed());
  return true;
}

bool MaxTabCountEnforced() {
  TabStripStateMachine sm;
  for (std::size_t i = 0; i < 32; ++i) {
    CHECK(sm.AddTab("tab-" + std::to_string(i)));
  }
  CHECK(sm.tab_count() == 32);
  CHECK(!sm.AddTab("tab-overflow"));
  return true;
}

bool EngineTabCreatedSyncsState() {
  TabStripStateMachine sm;
  sm.OnTabCreated("engine-tab-1");
  CHECK(sm.tab_count() == 1);
  CHECK(*sm.active_tab_id() == "engine-tab-1");
  return true;
}

bool EngineTabClosedSyncsState() {
  TabStripStateMachine sm;
  sm.OnTabCreated("engine-tab-1");
  sm.OnTabCreated("engine-tab-2");
  sm.OnTabClosed("engine-tab-1");
  CHECK(sm.tab_count() == 1);
  CHECK(sm.CanRestoreClosed());
  return true;
}

bool ShutdownClearsAll() {
  TabStripStateMachine sm;
  CHECK(sm.AddTab("tab-1"));
  CHECK(sm.AddTab("tab-2"));
  sm.Shutdown();
  CHECK(sm.empty());
  CHECK(!sm.active());
  CHECK(!sm.CanRestoreClosed());
  CHECK(!sm.AddTab("tab-3"));  // rejected after shutdown
  return true;
}

}  // namespace

int main() {
  if (!InitialStateIsEmpty() ||
      !AddTabSetsActive() ||
      !DuplicateTabIdRejected() ||
      !EmptyTabIdRejected() ||
      !CloseTabRemovesAndStoresRestorable() ||
      !CloseLastTabClearsActive() ||
      !CloseActiveSwitchesToNeighbor() ||
      !DuplicateCloseIsIdempotent() ||
      !ActivateTabWorks() ||
      !ActivateUnknownTabFails() ||
      !SelectNextCycles() ||
      !SelectPreviousCycles() ||
      !SelectNextOnEmptyFails() ||
      !MoveTabChangesOrder() ||
      !MoveTabPreservesActive() ||
      !RestoreClosedBringsTabBack() ||
      !RestoreClosedOnEmptyStackFails() ||
      !RestoreClosedRespectsCapacity() ||
      !MaxTabCountEnforced() ||
      !EngineTabCreatedSyncsState() ||
      !EngineTabClosedSyncsState() ||
      !ShutdownClearsAll()) {
    return 1;
  }
  return 0;
}
