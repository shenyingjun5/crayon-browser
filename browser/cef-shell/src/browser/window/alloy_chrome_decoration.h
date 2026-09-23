#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_CHROME_DECORATION_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_CHROME_DECORATION_H_

// Geometry and state for the chrome shapes CEF Views cannot draw.
//
// CefView exposes SetBackgroundColor only: no corner radius, no background
// image, no paint hook (CEF 150's CefViewDelegate has no OnPaint). Every
// non-rectangular chrome shape therefore has to be cut by a native overlay
// (macOS: a transparent, hitTest-neutral NSView) that paints the surrounding
// band color back over the corners. That overlay is the only consumer of this
// projection, and it cannot infer anything from the CEF view tree itself, so
// the window layer publishes everything it needs here: where each tab is,
// which tab is active, whether it is loading, where its indicator slot sits,
// and where the omnibox pill is.
//
// Values are density-independent pixels in WINDOW coordinates, matching
// CefView::ConvertPointToWindow.

#include <vector>

#include "include/cef_values.h"

namespace crayon::browser::cef_shell::window {

// tokens.json -> metrics.tabStripHeightDip / navigationBarHeightDip. The chrome
// band is assembled from these two rows, and both the CEF views and the native
// decoration must agree on them.
inline constexpr int kTabStripHeightDip = 40;
inline constexpr int kNavigationBarHeightDip = 48;

// tokens.json -> metrics.cornerRadiusDip. Shared by the tab top corners and
// the inverted (concave) bottom corners that join the active tab to the
// navigation bar.
inline constexpr int kChromeCornerRadiusDip = 8;

// tokens.json -> metrics.pillRadiusDip. The omnibox is a pill: its height is
// exactly twice this radius, which is what makes the end caps true semicircles
// instead of a rectangle with rounded corners.
inline constexpr int kOmniboxPillRadiusDip = 18;
inline constexpr int kOmniboxPillHeightDip = 2 * kOmniboxPillRadiusDip;

// Vertical margin that centres the pill inside the navigation bar.
inline constexpr int kOmniboxPillMarginDip =
    (kNavigationBarHeightDip - kOmniboxPillHeightDip) / 2;

// tokens.json -> metrics.groupGapDip. Trailing gap between the omnibox pill and
// the window edge; the reference leaves the field short of the frame.
inline constexpr int kChromeTrailingInsetDip = 8;

// tokens.json -> metrics.iconCanvasDip. Leading slot reserved in every tab for
// the page indicator (favicon / loading spinner). Reserved unconditionally so
// the title does not shift when a load starts or ends.
inline constexpr int kTabIndicatorSlotWidthDip = 24;

// Height of the native decoration overlay: it has to cover both chrome rows
// because the omnibox pill sits in the second one.
inline constexpr int kChromeDecorationHeightDip =
    kTabStripHeightDip + kNavigationBarHeightDip;

struct TabDecoration final {
  // The tab row, as mounted in the tab strip panel.
  CefRect bounds;
  // The leading indicator slot inside that row.
  CefRect indicator;
  bool active = false;
  bool loading = false;
};

struct ChromeDecoration final {
  std::vector<TabDecoration> tabs;
  // The omnibox pill. Empty when the toolbar is not mounted in a window, in
  // which case no pill corners may be cut.
  CefRect omnibox;
};

} // namespace crayon::browser::cef_shell::window

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_CHROME_DECORATION_H_
