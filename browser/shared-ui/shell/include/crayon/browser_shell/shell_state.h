#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "crayon/browser_engine/event_sink.h"
#include "crayon/browser_shell/command_registry.h"

namespace crayon::browser_shell {

enum class NavigationState {
  kNone = 0,
  kStarted,
  kCommitted,
  kCompleted,
  kFailed,
};

constexpr bool IsValid(NavigationState state) noexcept {
  switch (state) {
    case NavigationState::kNone:
    case NavigationState::kStarted:
    case NavigationState::kCommitted:
    case NavigationState::kCompleted:
    case NavigationState::kFailed:
      return true;
  }
  return false;
}

struct ShellTabView final {
  std::string profile_id;
  std::string tab_id;
  std::uint64_t navigation_id = 0;
  NavigationState navigation_state = NavigationState::kNone;
  std::string url;
};

class ShellState final : public ShellCommandObserver {
 public:
  bool OnProfileCreated(std::string profile_id);
  bool OnProfileDestroyed(const std::string& profile_id);
  bool OnTabCreated(std::string profile_id, std::string tab_id);
  bool OnTabClosed(const std::string& tab_id);
  bool OnNavigation(const std::string& tab_id, std::uint64_t navigation_id,
                    NavigationState state, std::string url);

  bool SetFocus(FocusArea area, std::optional<std::string> tab_id);
  std::optional<FocusToken> CaptureFocusForRestore();
  bool RestoreFocus(const FocusToken& token);

  void OnCommandAccepted(ShellCommand command, CommandOrigin origin) override;
  void Shutdown() noexcept;

  bool active() const noexcept { return active_; }
  FocusArea focus_area() const noexcept { return focus_area_; }
  const std::optional<std::string>& focused_tab_id() const noexcept {
    return focused_tab_id_;
  }
  const ShellTabView* FindTab(const std::string& tab_id) const noexcept;
  std::size_t tab_count() const noexcept { return tabs_.size(); }

 private:
  bool IsLiveTab(const std::string& tab_id) const noexcept;
  void InvalidateRestoreTokenForTab(const std::string& tab_id) noexcept;

  std::set<std::string> profiles_;
  std::set<std::string> retired_profiles_;
  std::map<std::string, ShellTabView> tabs_;
  std::set<std::string> retired_tabs_;
  FocusArea focus_area_ = FocusArea::kNone;
  std::optional<std::string> focused_tab_id_;
  std::optional<FocusToken> restore_token_;
  std::uint64_t next_focus_generation_ = 1;
  bool active_ = true;
};

class EngineEventAdapter final : public browser_engine::EngineEventSink {
 public:
  explicit EngineEventAdapter(ShellState& state) : state_(&state) {}
  ~EngineEventAdapter() override;

  EngineEventAdapter(const EngineEventAdapter&) = delete;
  EngineEventAdapter& operator=(const EngineEventAdapter&) = delete;

  void OnProfileEvent(const browser_engine::ProfileEvent& event) override;
  void OnTabEvent(const browser_engine::TabEvent& event) override;
  void OnNavigationEvent(const browser_engine::NavigationEvent& event) override;
  void OnPermissionRequest(
      const browser_engine::PermissionRequest& request) override;
  void OnTrustedInput(const browser_engine::TrustedInputFact& fact) override;
  void OnObservation(const browser_engine::ObservationEvent& event) override;

  void Shutdown() noexcept;
  bool active() const noexcept { return state_ != nullptr; }

 private:
  ShellState* state_;
};

}  // namespace crayon::browser_shell
