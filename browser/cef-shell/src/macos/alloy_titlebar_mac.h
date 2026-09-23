#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TITLEBAR_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TITLEBAR_MAC_H_

#include <vector>

// Native window controls share the first row with the CEF tab strip.
namespace crayon::browser::cef_shell::macos::titlebar {

inline constexpr int kTabStripHeight = 40;
// Where the system traffic lights end and the strip's own content may start.
inline constexpr int kWindowControlsInset = 80;
// PLT-SHELL-24M2FIX-C5: left edge of the first tab. Chrome 153 measured on this
// machine puts the green light's edge at 72.5 DIP and its first tab at 126 DIP,
// a 53.5 DIP gap its tab-search button sits in. This build has no such button,
// so the same distance is kept as empty draggable strip: the window controls
// here sit about 7 DIP further right than Chrome's, hence 132 rather than 126.
inline constexpr int kTabStripLeadingInset = 132;

// Apply colors only. CEF owns the frameless geometry and native controls.
bool ApplyMergedTitlebar(void *native_window);

// Density-independent rectangle in window coordinates, matching
// CefView::ConvertPointToWindow. Kept free of CEF types so this adapter stays a
// drawing layer and nothing else.
struct ChromeRect {
  int x;
  int y;
  int width;
  int height;
};

// One tab: where it is, where its page indicator sits, and its presentation.
struct TabDecoration {
  ChromeRect bounds;
  ChromeRect indicator;
  bool active;
  bool loading;
};

// The whole chrome band: every tab plus the omnibox pill. An empty rect means
// "nothing to draw there", which is how the caller withdraws a shape.
struct ChromeDecoration {
  std::vector<TabDecoration> tabs;
  ChromeRect omnibox;
  bool omnibox_focused;
  ChromeRect omnibox_field;
};

// PLT-SHELL-24M2FIX-C4: draws the chrome shapes CefView cannot express. CEF
// Views exposes SetBackgroundColor only — no corner radius, no paint hook — so
// these are cut by a transparent, hitTest-neutral decoration above the CEF
// views: the tab top corners, the active tab's inverted bottom corners that
// join it to the navigation bar, the tab loading indicator, and the omnibox
// pill end caps. Visual only; CEF keeps every hit target, focus ring and
// accessibility node.
void UpdateChromeDecoration(void *native_window,
                            const ChromeDecoration &decoration);

} // namespace crayon::browser::cef_shell::macos::titlebar

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TITLEBAR_MAC_H_
