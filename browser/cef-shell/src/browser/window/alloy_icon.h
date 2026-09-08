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
// actions keep normal keyboard focusability. Delegates should call
// ReleaseAlloyIconFocus when an activation begins so a pointer press does not
// leave the native selected outline visible after the command completes.
bool ApplyAlloyIcon(CefRefPtr<CefLabelButton> button, AlloyIcon icon,
                    const CefString &label);

// Clears focus retained by a completed icon activation, then restores the
// control's ability to receive a later keyboard focus. Text buttons are ignored.
void ReleaseAlloyIconFocus(CefRefPtr<CefButton> button);

} // namespace crayon::browser::cef_shell::window
