#include <cstdlib>
#include <iostream>
#include <string>

#include "browser/window/alloy_history.h"

namespace {
using crayon::browser::cef_shell::window::AlloyHistory;
using crayon::browser::cef_shell::window::AlloyHistoryResult;
using crayon::browser_engine::ProfileId;

#define CHECK(condition) do { if (!(condition)) { std::cerr << __FILE__ << ':' \
  << __LINE__ << " CHECK failed: " << #condition << '\n'; return false; } } while (false)

ProfileId Profile(const char *value) {
  auto profile = ProfileId::TryCreate(value);
  if (!profile) std::abort();
  return *profile;
}

bool RegularContract() {
  std::string opened;
  bool accept_open = false;
  AlloyHistory history(Profile("profile-a"), false,
                       {[&](const std::string &url) {
                         opened = url;
                         return accept_open;
                       }});
  CHECK(history.BeginNavigation(1));
  CHECK(history.CommitNavigation(2, "https://stale.test/", "stale", 1) ==
        AlloyHistoryResult::kStaleNavigation);
  CHECK(history.CommitNavigation(1, "https://first.test/", "First", 10) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.CommitNavigation(1, "https://duplicate.test/", "duplicate", 11) ==
        AlloyHistoryResult::kStaleNavigation);
  CHECK(history.BeginNavigation(2));
  CHECK(history.CommitNavigation(2, "https://second.test/", "Second", 20) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.Search("test") && history.view().entries().size() == 2 &&
        history.view().entries()[0].visited_at == 20);
  CHECK(history.RecordClosedTab("https://closed.test/", "Closed", 30) ==
        AlloyHistoryResult::kSuccess);
  CHECK(history.RestoreRecentlyClosed() == AlloyHistoryResult::kRejected);
  CHECK(history.store().recently_closed_count() == 1);
  accept_open = true;
  CHECK(history.RestoreRecentlyClosed() == AlloyHistoryResult::kSuccess);
  CHECK(opened == "https://closed.test/" &&
        history.store().recently_closed_count() == 0);
  CHECK(history.DeleteRange(10, 10) == 1);
  CHECK(history.DeleteUrl("https://second.test/") == 1);
  CHECK(history.ClearAll() && history.view().entries().empty());

  CHECK(history.BeginNavigation(3));
  CHECK(history.CommitNavigation(3, "https://export.test/", "Export", 40) ==
        AlloyHistoryResult::kSuccess);
  const auto exported = history.Export();
  CHECK(!history.Import("corrupt"));
  CHECK(history.store().entries().size() == 1);
  CHECK(history.Import(exported));
  CHECK(history.store().entries().size() == 1);
  CHECK(history.Shutdown() && history.Shutdown());
  CHECK(!history.BeginNavigation(4));
  return true;
}

bool EphemeralAndProfileIsolation() {
  AlloyHistory regular(Profile("profile-a"), false, {});
  AlloyHistory ephemeral(Profile("profile-b"), true, {});
  CHECK(ephemeral.BeginNavigation(1));
  CHECK(ephemeral.CommitNavigation(1, "https://private.test/", "Private", 1) ==
        AlloyHistoryResult::kEphemeral);
  CHECK(ephemeral.RecordClosedTab("https://private.test/", "Private", 2) ==
        AlloyHistoryResult::kEphemeral);
  CHECK(ephemeral.Export().empty() && !ephemeral.Import("CRAYON-HISTORY v1\n"));
  CHECK(regular.store().entries().empty());
  return true;
}
} // namespace

int main() {
  return RegularContract() && EphemeralAndProfileIsolation() ? EXIT_SUCCESS
                                                              : EXIT_FAILURE;
}
