#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_omnibox/omnibox_parser.h"

namespace crayon::browser_omnibox {

/// Origin of a suggestion item (used for sorting and UI decoration).
enum class SuggestionSource {
  kEmptyPlaceholder = 0,
  kHistory,
  kBookmark,
  kShortcut,
};

constexpr bool IsValid(SuggestionSource source) noexcept {
  switch (source) {
    case SuggestionSource::kEmptyPlaceholder:
    case SuggestionSource::kHistory:
    case SuggestionSource::kBookmark:
    case SuggestionSource::kShortcut:
      return true;
  }
  return false;
}

/// A single suggestion shown below the omnibox.
struct OmniboxSuggestion final {
  std::string title;
  std::string url_or_query;
  SuggestionSource source;
};

/// Maximum number of visible suggestions.
inline constexpr std::size_t kMaxSuggestions = 8;

/// Lifecycle of the omnibox interaction surface.
enum class OmniboxState {
  kIdle = 0,       // No focus, no text
  kEditing,        // User is typing
  kSuggesting,     // Showing suggestion list
  kLoading,        // Submitted, waiting for navigation
  kCommitted,      // Navigation confirmed or cancelled
};

constexpr bool IsValid(OmniboxState state) noexcept {
  switch (state) {
    case OmniboxState::kIdle:
    case OmniboxState::kEditing:
    case OmniboxState::kSuggesting:
    case OmniboxState::kLoading:
    case OmniboxState::kCommitted:
      return true;
  }
  return false;
}

/// Index into the suggestion list.  std::nullopt means "no selection".
using SuggestionIndex = std::optional<std::size_t>;

/// Platform-neutral omnibox state machine.
///
/// State transitions:
///   Idle      --Focus--> Editing
///   Editing   --Input--> Editing / Suggesting
///   Suggesting--Submit--> Loading
///   Suggesting--Cancel--> Idle
///   Loading   --Complete/Error--> Committed
///   Loading   --Cancel--> Idle
///   Committed --Focus--> Editing
///
/// All public methods are noexcept; invalid transitions are ignored.
class OmniboxStateMachine final {
 public:
  OmniboxStateMachine() = default;

  // Query
  OmniboxState state() const noexcept { return state_; }
  const std::string& current_text() const noexcept { return current_text_; }
  const std::vector<OmniboxSuggestion>& suggestions() const noexcept {
    return suggestions_;
  }
  SuggestionIndex selected_index() const noexcept { return selected_index_; }
  bool active() const noexcept { return active_; }

  // Input events
  void OnFocus() noexcept;
  void OnEdit(std::string text) noexcept;
  void OnSuggestionsUpdated(std::vector<OmniboxSuggestion> suggestions) noexcept;
  void OnSubmit() noexcept;
  void OnCancel() noexcept;
  void OnNavigationComplete() noexcept;
  void OnNavigationFailed() noexcept;

  // Lifecycle
  void Shutdown() noexcept;

 private:
  void ClearSuggestions() noexcept;
  void ClampSelection() noexcept;

  OmniboxState state_ = OmniboxState::kIdle;
  std::string current_text_;
  std::vector<OmniboxSuggestion> suggestions_;
  SuggestionIndex selected_index_;
  bool active_ = true;
};

}  // namespace crayon::browser_omnibox
