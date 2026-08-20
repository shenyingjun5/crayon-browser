#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "crayon/browser_history_view/history_page_state_machine.h"

namespace {

using crayon::browser_history_view::HistoryPageStateMachine;
using crayon::browser_history_view::HistoryProjection;
using crayon::browser_history_view::kMaxVisibleEntries;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

HistoryProjection MakeEntry(std::uint64_t id) {
  return HistoryProjection{id, "page-" + std::to_string(id), 100 + id};
}

bool SetEntriesValidates() {
  HistoryPageStateMachine page;
  CHECK(page.SetEntries({MakeEntry(1), MakeEntry(2)}));
  CHECK(page.entries().size() == 2);
  CHECK(!page.SetEntries({HistoryProjection{0, "bad", 1}}));
  CHECK(page.entries().size() == 2);
  return true;
}

bool CapacityEnforced() {
  HistoryPageStateMachine page;
  std::vector<HistoryProjection> entries;
  for (std::size_t i = 1; i <= kMaxVisibleEntries; ++i) {
    entries.push_back(MakeEntry(i));
  }
  CHECK(page.SetEntries(entries));
  entries.push_back(MakeEntry(kMaxVisibleEntries + 1));
  CHECK(!page.SetEntries(entries));
  CHECK(page.entries().size() == kMaxVisibleEntries);
  return true;
}

bool QueryIsBounded() {
  HistoryPageStateMachine page;
  CHECK(page.SetQuery("crayon"));
  CHECK(page.query() == "crayon");
  CHECK(!page.SetQuery(std::string(257, 'q')));
  CHECK(page.query() == "crayon");
  return true;
}

bool ClearAndShutdown() {
  HistoryPageStateMachine page;
  page.SetEntries({MakeEntry(1)});
  page.SetQuery("q");
  page.OnHistoryCleared();
  CHECK(page.entries().empty());
  CHECK(page.query().empty());
  page.SetEntries({MakeEntry(3)});
  page.Shutdown();
  CHECK(!page.active());
  CHECK(page.entries().empty());
  CHECK(!page.SetEntries({MakeEntry(4)}));
  CHECK(!page.SetQuery("x"));
  return true;
}

}  // namespace

int main() {
  if (!SetEntriesValidates() || !CapacityEnforced() || !QueryIsBounded() ||
      !ClearAndShutdown()) {
    return 1;
  }
  return 0;
}
