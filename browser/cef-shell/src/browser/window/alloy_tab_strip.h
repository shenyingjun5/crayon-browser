#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_TAB_STRIP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_TAB_STRIP_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "browser/window/alloy_chrome_decoration.h"
#include "browser/window/tab_model.h"
#include "include/views/cef_panel.h"

namespace crayon::browser::cef_shell::window {

// Windows/CEF Views projection of TabModel. The strip owns only bounded view
// bindings; TabModel remains the sole owner of tab identity, order and active
// state.
class AlloyTabStrip final {
public:
  struct Strings final {
    std::string new_tab;
    std::string close_tab;
    std::string tab_fallback;
  };

  struct Callbacks final {
    std::function<void()> new_tab;
    std::function<void(TabId)> activate_tab;
    std::function<void(TabId)> close_tab;
    std::function<std::string(TabId)> title = {};
  };

  static constexpr int kNewTabCommandId = 0x7a00;
  static constexpr int kActivateCommandBase = 0x7a20;
  static constexpr int kCloseCommandBase = 0x7a60;

  /// |leading_inset| reserves room for merged-titlebar window controls
  /// (macOS traffic lights); 0 keeps the full-width strip (Windows).
  AlloyTabStrip(Strings strings, Callbacks callbacks, int leading_inset = 0);
  ~AlloyTabStrip();

  AlloyTabStrip(const AlloyTabStrip &) = delete;
  AlloyTabStrip &operator=(const AlloyTabStrip &) = delete;

  CefRefPtr<CefPanel> panel() const;
  bool Sync(const TabModel &model);
  bool Sync(const TabModel &model, const std::vector<TabId> &ordered_tabs);
  bool RefreshTitles();
  bool Shutdown();

  /// Geometry and state for the native chrome decoration. Returns an empty
  /// list when a tab's window coordinates cannot be resolved: a stale
  /// decoration would keep drawing corners for a tab that moved or vanished.
  std::vector<TabDecoration> decoration() const;

  bool active() const noexcept;
  std::size_t rendered_tab_count() const noexcept;
  std::optional<TabId> rendered_tab_at(std::size_t index) const noexcept;
  std::optional<TabId> active_rendered_tab() const noexcept;
  bool new_tab_enabled() const noexcept;

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace crayon::browser::cef_shell::window

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_TAB_STRIP_H_
