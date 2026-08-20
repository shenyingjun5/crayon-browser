#include "crayon/browser_windows/window_state_machine.h"

#include <algorithm>

namespace crayon::browser_windows {

bool WindowStateMachine::IsValidWindowId(const std::string& window_id) noexcept {
  return !window_id.empty() && window_id.size() <= kMaxWindowIdLength;
}

WindowStateMachine::WindowState* WindowStateMachine::FindMutable(
    const std::string& window_id) noexcept {
  if (!active_) {
    return nullptr;
  }
  const auto it = windows_.find(window_id);
  return it == windows_.end() ? nullptr : &it->second;
}

const WindowStateMachine::WindowState* WindowStateMachine::Find(
    const std::string& window_id) const noexcept {
  if (!active_) {
    return nullptr;
  }
  const auto it = windows_.find(window_id);
  return it == windows_.end() ? nullptr : &it->second;
}

std::size_t WindowStateMachine::PopupCountOf(
    const std::string& opener_id) const noexcept {
  std::size_t count = 0;
  for (const auto& [id, state] : windows_) {
    if (state.kind == WindowKind::kPopup && state.opener_id == opener_id) {
      ++count;
    }
  }
  return count;
}

void WindowStateMachine::TouchFocus(const std::string& window_id) {
  auto& recency = focus_recency_;
  recency.erase(std::remove(recency.begin(), recency.end(), window_id),
                recency.end());
  recency.push_back(window_id);
}

bool WindowStateMachine::InsertWindow(std::string window_id,
                                      WindowKind kind,
                                      std::string opener_id) {
  WindowState state;
  state.kind = kind;
  state.opener_id = std::move(opener_id);
  windows_.emplace(window_id, std::move(state));
  TouchFocus(window_id);
  return true;
}

bool WindowStateMachine::CreateWindow(const std::string& window_id) {
  if (!active_ || !IsValidWindowId(window_id)) {
    return false;
  }
  if (windows_.count(window_id) != 0 || windows_.size() >= kMaxWindows) {
    return false;
  }
  return InsertWindow(window_id, WindowKind::kNormal, std::string{});
}

PopupDecision WindowStateMachine::RequestPopup(
    const std::string& opener_window_id,
    const std::string& popup_window_id,
    PopupSource source) {
  if (!active_ || !IsValidWindowId(popup_window_id) ||
      windows_.count(popup_window_id) != 0) {
    return PopupDecision::kDenyInvalidRequest;
  }
  const PopupDecision decision =
      EvaluatePopupRequest(source, windows_.count(opener_window_id) != 0,
                           PopupCountOf(opener_window_id),
                           windows_.size() >= kMaxWindows);
  if (!IsAllowed(decision)) {
    return decision;
  }
  InsertWindow(popup_window_id, WindowKind::kPopup, opener_window_id);
  return decision;
}

bool WindowStateMachine::CloseWindow(const std::string& window_id) noexcept {
  if (windows_.erase(window_id) == 0) {
    return false;
  }
  RestoreFocusAfterClose(window_id);
  return true;
}

void WindowStateMachine::RestoreFocusAfterClose(
    const std::string& closed_id) noexcept {
  auto& recency = focus_recency_;
  recency.erase(std::remove(recency.begin(), recency.end(), closed_id),
                recency.end());
  // Prefer the most recently used normal window; if only popups remain, the
  // most recent popup stays the defined focus target.
  for (auto it = recency.rbegin(); it != recency.rend(); ++it) {
    const auto found = windows_.find(*it);
    if (found == windows_.end() ||
        found->second.kind != WindowKind::kNormal) {
      continue;
    }
    if (*it != recency.back()) {
      std::string id = *it;
      recency.erase(std::next(it).base());
      recency.push_back(std::move(id));
    }
    return;
  }
}

bool WindowStateMachine::FocusWindow(const std::string& window_id) {
  if (FindMutable(window_id) == nullptr) {
    return false;
  }
  TouchFocus(window_id);
  return true;
}

bool WindowStateMachine::EnterFullscreen(const std::string& window_id) {
  WindowState* state = FindMutable(window_id);
  if (state == nullptr || state->kind == WindowKind::kPopup ||
      state->mode != WindowMode::kNormal) {
    return false;
  }
  state->mode = WindowMode::kFullscreen;
  return true;
}

bool WindowStateMachine::ExitFullscreen(const std::string& window_id) {
  WindowState* state = FindMutable(window_id);
  if (state == nullptr || state->mode != WindowMode::kFullscreen) {
    return false;
  }
  state->mode = WindowMode::kNormal;
  return true;
}

bool WindowStateMachine::EnterPictureInPicture(const std::string& window_id,
                                               bool media_active) {
  WindowState* state = FindMutable(window_id);
  if (state == nullptr || state->kind == WindowKind::kPopup ||
      state->mode != WindowMode::kNormal || !media_active) {
    return false;
  }
  state->mode = WindowMode::kPictureInPicture;
  return true;
}

bool WindowStateMachine::ExitPictureInPicture(const std::string& window_id) {
  WindowState* state = FindMutable(window_id);
  if (state == nullptr || state->mode != WindowMode::kPictureInPicture) {
    return false;
  }
  state->mode = WindowMode::kNormal;
  return true;
}

bool WindowStateMachine::HasWindow(const std::string& window_id) const {
  return Find(window_id) != nullptr;
}

bool WindowStateMachine::IsPopup(const std::string& window_id) const {
  const WindowState* state = Find(window_id);
  return state != nullptr && state->kind == WindowKind::kPopup;
}

WindowMode WindowStateMachine::ModeOf(const std::string& window_id) const {
  const WindowState* state = Find(window_id);
  return state != nullptr ? state->mode : WindowMode::kNormal;
}

std::optional<std::string> WindowStateMachine::OpenerOf(
    const std::string& window_id) const {
  const WindowState* state = Find(window_id);
  if (state == nullptr || state->kind != WindowKind::kPopup) {
    return std::nullopt;
  }
  return state->opener_id;
}

std::optional<std::string> WindowStateMachine::focused_window_id() const {
  if (!active_ || focus_recency_.empty()) {
    return std::nullopt;
  }
  return focus_recency_.back();
}

void WindowStateMachine::Shutdown() noexcept {
  active_ = false;
  windows_.clear();
  focus_recency_.clear();
}

}  // namespace crayon::browser_windows
