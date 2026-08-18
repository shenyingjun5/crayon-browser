#include "crayon/browser_omnibox/omnibox_state.h"

namespace crayon::browser_omnibox {

void OmniboxStateMachine::OnFocus() noexcept {
  if (!active_) return;
  switch (state_) {
    case OmniboxState::kIdle:
    case OmniboxState::kCommitted:
      state_ = OmniboxState::kEditing;
      break;
    default:
      break;
  }
}

void OmniboxStateMachine::OnEdit(std::string text) noexcept {
  if (!active_) return;
  current_text_ = std::move(text);
  selected_index_ = std::nullopt;
  switch (state_) {
    case OmniboxState::kEditing:
    case OmniboxState::kSuggesting:
      state_ = OmniboxState::kEditing;
      ClearSuggestions();
      break;
    default:
      break;
  }
}

void OmniboxStateMachine::OnSuggestionsUpdated(
    std::vector<OmniboxSuggestion> suggestions) noexcept {
  if (!active_) return;
  if (suggestions.size() > kMaxSuggestions) {
    suggestions.resize(kMaxSuggestions);
  }
  suggestions_ = std::move(suggestions);
  ClampSelection();
  switch (state_) {
    case OmniboxState::kEditing:
      if (!suggestions_.empty()) {
        state_ = OmniboxState::kSuggesting;
      }
      break;
    default:
      break;
  }
}

void OmniboxStateMachine::OnSubmit() noexcept {
  if (!active_) return;
  switch (state_) {
    case OmniboxState::kEditing:
    case OmniboxState::kSuggesting:
      state_ = OmniboxState::kLoading;
      break;
    default:
      break;
  }
}

void OmniboxStateMachine::OnCancel() noexcept {
  if (!active_) return;
  switch (state_) {
    case OmniboxState::kEditing:
    case OmniboxState::kSuggesting:
    case OmniboxState::kLoading:
      state_ = OmniboxState::kIdle;
      current_text_.clear();
      ClearSuggestions();
      selected_index_ = std::nullopt;
      break;
    default:
      break;
  }
}

void OmniboxStateMachine::OnNavigationComplete() noexcept {
  if (!active_) return;
  if (state_ == OmniboxState::kLoading) {
    state_ = OmniboxState::kCommitted;
  }
}

void OmniboxStateMachine::OnNavigationFailed() noexcept {
  if (!active_) return;
  if (state_ == OmniboxState::kLoading) {
    state_ = OmniboxState::kCommitted;
  }
}

void OmniboxStateMachine::Shutdown() noexcept {
  active_ = false;
  state_ = OmniboxState::kIdle;
  current_text_.clear();
  ClearSuggestions();
  selected_index_ = std::nullopt;
}

void OmniboxStateMachine::ClearSuggestions() noexcept {
  suggestions_.clear();
  selected_index_ = std::nullopt;
}

void OmniboxStateMachine::ClampSelection() noexcept {
  if (suggestions_.empty()) {
    selected_index_ = std::nullopt;
    return;
  }
  if (!selected_index_.has_value()) {
    return;
  }
  if (*selected_index_ >= suggestions_.size()) {
    selected_index_ = suggestions_.size() - 1;
  }
}

}  // namespace crayon::browser_omnibox
