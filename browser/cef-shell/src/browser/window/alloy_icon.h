#pragma once

#include "include/views/cef_label_button.h"

namespace crayon::browser::cef_shell::window {

enum class AlloyIcon {
  kTabNew,
  kTabClose,
  kTabMoveWindow,
  kBack,
  kForward,
  kReload,
  kStop,
  kSiteInfo,
  kBookmarkOutline,
  kBookmarkFilled,
  kCast,
  kMenu,
  kDownload,
  kHistory,
};

// Applies theme-colored, embedded 1x/2x representations sourced from the
// browser-design-v1 SVG manifest. The localized label remains on the control
// as its tooltip and accessible name, never as icon-only visual text. Icon
// actions keep normal keyboard focusability. The activation helper resets
// pressed styling while preserving the user's current keyboard focus.
bool ApplyAlloyIcon(CefRefPtr<CefLabelButton> button, AlloyIcon icon,
                    const CefString &label);

// Legacy name retained for callers: resets pressed state, preserving focus.
// Text buttons and disabled controls are ignored.
void ReleaseAlloyIconFocus(CefRefPtr<CefButton> button);

} // namespace crayon::browser::cef_shell::window
