#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace crayon::browser_navigation {

/// Per-tab navigation capability and loading state.
///
/// This is a platform-neutral view model.  Actual browser-engine history
/// management lives in the CEF/ArkWeb adapter; the controller only tracks
/// what the shared UI needs to know (loading state, back/forward availability,
/// and current navigation ID for fencing).
class NavigationController final {
 public:
  struct TabState final {
    std::uint64_t navigation_id = 0;
    bool is_loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
  };

  NavigationController() = default;

  // Event observers (called from engine adapter)
  void OnNavigationStarted(const std::string& tab_id,
                           std::uint64_t navigation_id);
  void OnNavigationCommitted(const std::string& tab_id,
                             std::uint64_t navigation_id);
  void OnNavigationCompleted(const std::string& tab_id,
                             std::uint64_t navigation_id);
  void OnNavigationFailed(const std::string& tab_id,
                          std::uint64_t navigation_id);

  // History capability updates (called from engine adapter)
  void SetCanGoBack(const std::string& tab_id, bool can) noexcept;
  void SetCanGoForward(const std::string& tab_id, bool can) noexcept;

  // User commands (called from UI / command registry)
  /// Returns true if the command should be forwarded to the engine.
  bool GoBack(const std::string& tab_id) noexcept;
  bool GoForward(const std::string& tab_id) noexcept;
  bool Reload(const std::string& tab_id) noexcept;
  bool Stop(const std::string& tab_id) noexcept;

  // Query
  bool IsLoading(const std::string& tab_id) const noexcept;
  bool CanGoBack(const std::string& tab_id) const noexcept;
  bool CanGoForward(const std::string& tab_id) const noexcept;
  bool CanReload(const std::string& tab_id) const noexcept;
  bool CanStop(const std::string& tab_id) const noexcept;
  std::uint64_t CurrentNavigationId(const std::string& tab_id) const noexcept;

  const TabState* FindTab(const std::string& tab_id) const noexcept;
  std::size_t TabCount() const noexcept { return tabs_.size(); }

  // Lifecycle
  void OnTabCreated(const std::string& tab_id);
  void OnTabClosed(const std::string& tab_id) noexcept;
  void Shutdown() noexcept;

 private:
  TabState* FindTabMutable(const std::string& tab_id) noexcept;

  std::map<std::string, TabState> tabs_;
  bool active_ = true;
};

}  // namespace crayon::browser_navigation
