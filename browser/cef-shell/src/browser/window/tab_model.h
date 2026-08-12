#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_MODEL_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_MODEL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crayon::browser::cef_shell::window {

inline constexpr std::size_t kMaximumTabsPerWindow = 32;
inline constexpr double kDefaultZoomFactor = 1.0;
inline constexpr double kMinimumWindowZoomFactor = 0.25;
inline constexpr double kMaximumWindowZoomFactor = 5.0;

using TabId = std::uint64_t;

enum class TabLifecycle { kCreating, kReady, kClosing, kCrashed };

struct TabSnapshot final {
  TabId id;
  int browser_id;
  TabLifecycle lifecycle;
  std::string url;
  bool loading;
  bool can_go_back;
  bool can_go_forward;
  double zoom_factor;
  std::uint64_t navigation_generation;
};

class TabModel final {
public:
  std::optional<TabId> CreateTab();
  bool BindBrowser(TabId tab_id, int browser_id);
  bool Activate(TabId tab_id);
  bool RequestClose(TabId tab_id);
  bool DetachBrowser(int browser_id);
  bool MarkCrashed(int browser_id);

  bool UpdateAddress(int browser_id, std::string url);
  bool UpdateLoading(int browser_id, bool loading, bool can_go_back,
                     bool can_go_forward);
  bool BeginNavigation(int browser_id);
  bool SetZoom(TabId tab_id, double factor);

  std::optional<TabId> active_tab() const noexcept { return active_tab_; }
  const TabSnapshot *Find(TabId tab_id) const noexcept;
  const TabSnapshot *FindByBrowser(int browser_id) const noexcept;
  std::size_t size() const noexcept { return tabs_.size(); }
  bool empty() const noexcept { return tabs_.empty(); }
  std::vector<TabId> ordered_tabs() const;

private:
  TabSnapshot *FindMutable(TabId tab_id) noexcept;
  TabSnapshot *FindByBrowserMutable(int browser_id) noexcept;
  void SelectReplacementFor(std::size_t removed_index);

  std::vector<TabSnapshot> tabs_;
  std::optional<TabId> active_tab_;
  TabId next_tab_id_ = 1;
};

} // namespace crayon::browser::cef_shell::window

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_MODEL_H_
