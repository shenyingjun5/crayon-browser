// PLT-SHELL-24M2UIP-c: merged-titlebar styling. The window keeps its
// standard traffic-light buttons (they stay functional through AppKit);
// they are re-positioned into the tab strip's leading inset so the strip
// reads as the topmost chrome row, like the reference Chrome shell.

#import <AppKit/AppKit.h>

#include "macos/alloy_titlebar_mac.h"

namespace crayon::browser::cef_shell::macos::titlebar {

bool ApplyMergedTitlebar(void* native_window, double strip_height) {
  NSWindow* window = nil;
  if ([(__bridge id)native_window isKindOfClass:[NSWindow class]]) {
    window = (__bridge NSWindow*)native_window;
  } else if ([(__bridge id)native_window isKindOfClass:[NSView class]]) {
    window = [(__bridge NSView*)native_window window];
  }
  if (!window) {
    return false;
  }
  window.styleMask |= NSWindowStyleMaskFullSizeContentView;
  window.titlebarAppearsTransparent = YES;
  window.titleVisibility = NSWindowTitleHidden;

  // Center the traffic lights vertically in the strip row. The buttons'
  // superview is the titlebar container pinned to the window top; with the
  // default 28pt titlebar the buttons sit at y≈6, and the strip is 40pt, so
  // shift them down by half the difference to center in the strip.
  constexpr CGFloat kButtonSize = 14.0;
  const CGFloat y = (28.0 - kButtonSize) / 2.0 - (strip_height - 28.0) / 2.0;

  NSArray<NSButton*>* buttons = @[
    [window standardWindowButton:NSWindowCloseButton],
    [window standardWindowButton:NSWindowMiniaturizeButton],
    [window standardWindowButton:NSWindowZoomButton],
  ];
  for (NSButton* button in buttons) {
    if (!button) {
      continue;
    }
    CGRect frame = button.frame;
    frame.origin.y = y;
    button.frame = frame;
  }
  return true;
}

}  // namespace crayon::browser::cef_shell::macos::titlebar
