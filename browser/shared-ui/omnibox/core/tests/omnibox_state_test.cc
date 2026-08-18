#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "crayon/browser_omnibox/omnibox_state.h"

namespace {

using crayon::browser_omnibox::IsValid;
using crayon::browser_omnibox::OmniboxState;
using crayon::browser_omnibox::OmniboxStateMachine;
using crayon::browser_omnibox::OmniboxSuggestion;
using crayon::browser_omnibox::SuggestionSource;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool InitialStateIsIdle() {
  OmniboxStateMachine sm;
  CHECK(sm.state() == OmniboxState::kIdle);
  CHECK(sm.current_text().empty());
  CHECK(sm.suggestions().empty());
  CHECK(!sm.selected_index().has_value());
  CHECK(sm.active());
  return true;
}

bool FocusTransitionsIdleToEditing() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  CHECK(sm.state() == OmniboxState::kEditing);
  return true;
}

bool EditStoresTextAndClearsOldSuggestions() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("hello");
  CHECK(sm.current_text() == "hello");
  CHECK(sm.state() == OmniboxState::kEditing);
  return true;
}

bool SuggestionsUpdateTransitionsToSuggesting() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("ex");
  sm.OnSuggestionsUpdated({OmniboxSuggestion{"Example", "https://example.test",
                                             SuggestionSource::kHistory}});
  CHECK(sm.state() == OmniboxState::kSuggesting);
  CHECK(sm.suggestions().size() == 1);
  return true;
}

bool EmptySuggestionsStayInEditing() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("zzz");
  sm.OnSuggestionsUpdated({});
  CHECK(sm.state() == OmniboxState::kEditing);
  return true;
}

bool SubmitFromSuggestingGoesToLoading() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("ex");
  sm.OnSuggestionsUpdated({OmniboxSuggestion{"Example", "https://example.test",
                                             SuggestionSource::kHistory}});
  sm.OnSubmit();
  CHECK(sm.state() == OmniboxState::kLoading);
  return true;
}

bool SubmitFromEditingGoesToLoading() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("search term");
  sm.OnSubmit();
  CHECK(sm.state() == OmniboxState::kLoading);
  return true;
}

bool NavigationCompleteGoesToCommitted() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("test");
  sm.OnSubmit();
  sm.OnNavigationComplete();
  CHECK(sm.state() == OmniboxState::kCommitted);
  return true;
}

bool NavigationFailedAlsoGoesToCommitted() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("test");
  sm.OnSubmit();
  sm.OnNavigationFailed();
  CHECK(sm.state() == OmniboxState::kCommitted);
  return true;
}

bool CancelFromLoadingReturnsToIdle() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("test");
  sm.OnSubmit();
  CHECK(sm.state() == OmniboxState::kLoading);
  sm.OnCancel();
  CHECK(sm.state() == OmniboxState::kIdle);
  CHECK(sm.current_text().empty());
  CHECK(sm.suggestions().empty());
  return true;
}

bool CancelFromSuggestingReturnsToIdle() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("ex");
  sm.OnSuggestionsUpdated({OmniboxSuggestion{"Ex", "https://ex.test",
                                             SuggestionSource::kHistory}});
  sm.OnCancel();
  CHECK(sm.state() == OmniboxState::kIdle);
  return true;
}

bool FocusAfterCommittedGoesToEditing() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("test");
  sm.OnSubmit();
  sm.OnNavigationComplete();
  CHECK(sm.state() == OmniboxState::kCommitted);
  sm.OnFocus();
  CHECK(sm.state() == OmniboxState::kEditing);
  return true;
}

bool SuggestionsAreCappedAtMax() {
  OmniboxStateMachine sm;
  std::vector<OmniboxSuggestion> many;
  for (int i = 0; i < 20; ++i) {
    many.push_back(OmniboxSuggestion{
        std::to_string(i), "https://" + std::to_string(i) + ".test",
        SuggestionSource::kHistory});
  }
  sm.OnFocus();
  sm.OnEdit("test");
  sm.OnSuggestionsUpdated(std::move(many));
  CHECK(sm.suggestions().size() == 8);  // kMaxSuggestions
  return true;
}

bool ShutdownIgnoresLateEvents() {
  OmniboxStateMachine sm;
  sm.OnFocus();
  sm.OnEdit("test");
  sm.Shutdown();
  CHECK(!sm.active());
  CHECK(sm.state() == OmniboxState::kIdle);
  sm.OnEdit("after");
  CHECK(sm.current_text().empty());
  return true;
}

bool InvalidTransitionsAreIgnored() {
  OmniboxStateMachine sm;
  // Submit from Idle does nothing
  sm.OnSubmit();
  CHECK(sm.state() == OmniboxState::kIdle);
  // Cancel from Idle does nothing
  sm.OnCancel();
  CHECK(sm.state() == OmniboxState::kIdle);
  // NavigationComplete from Idle does nothing
  sm.OnNavigationComplete();
  CHECK(sm.state() == OmniboxState::kIdle);
  return true;
}

bool IsValidCoversAllStates() {
  CHECK(IsValid(OmniboxState::kIdle));
  CHECK(IsValid(OmniboxState::kEditing));
  CHECK(IsValid(OmniboxState::kSuggesting));
  CHECK(IsValid(OmniboxState::kLoading));
  CHECK(IsValid(OmniboxState::kCommitted));
  return true;
}

}  // namespace

int main() {
  if (!InitialStateIsIdle() ||
      !FocusTransitionsIdleToEditing() ||
      !EditStoresTextAndClearsOldSuggestions() ||
      !SuggestionsUpdateTransitionsToSuggesting() ||
      !EmptySuggestionsStayInEditing() ||
      !SubmitFromSuggestingGoesToLoading() ||
      !SubmitFromEditingGoesToLoading() ||
      !NavigationCompleteGoesToCommitted() ||
      !NavigationFailedAlsoGoesToCommitted() ||
      !CancelFromLoadingReturnsToIdle() ||
      !CancelFromSuggestingReturnsToIdle() ||
      !FocusAfterCommittedGoesToEditing() ||
      !SuggestionsAreCappedAtMax() ||
      !ShutdownIgnoresLateEvents() ||
      !InvalidTransitionsAreIgnored() ||
      !IsValidCoversAllStates()) {
    return 1;
  }
  return 0;
}
