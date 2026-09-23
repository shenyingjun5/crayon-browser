#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "browser/window/alloy_chrome_palette.h"
#include "macos/alloy_titlebar_mac.h"

namespace {
using crayon::browser::cef_shell::macos::titlebar::ChromeDecoration;
using crayon::browser::cef_shell::macos::titlebar::ChromeRect;
using crayon::browser::cef_shell::macos::titlebar::TabDecoration;

constexpr CGFloat kTabCornerRadius = 8; // design tokens: cornerRadiusDip
constexpr CGFloat kTabTopInset = 4;     // design tokens: controlGapDip
// design tokens: tabStripHeightDip + navigationBarHeightDip. The overlay has to
// cover both chrome rows because the omnibox pill lives in the second one.
constexpr CGFloat kChromeHeight = 88;
constexpr CGFloat kIndicatorDiameter = 16;   // design tokens: glyphDip
constexpr CGFloat kIndicatorLineWidth = 2;   // design tokens: focusRingDip
constexpr CGFloat kIndicatorSweep = 300;     // arc visible at any phase
constexpr NSTimeInterval kIndicatorPeriod = 1.0;
constexpr NSTimeInterval kIndicatorFrame = 1.0 / 30.0;
// Height of the hairline that separates the chrome band from the page.
constexpr CGFloat kChromeSeparatorHeight = 1;

NSWindow *ResolveWindow(void *handle) {
  id object = (__bridge id)handle;
  if ([object isKindOfClass:[NSWindow class]])
    return (NSWindow *)object;
  if ([object isKindOfClass:[NSView class]])
    return [(NSView *)object window];
  return nil;
}

NSColor *ColorFromArgb(std::uint32_t color) {
  return [NSColor colorWithSRGBRed:((color >> 16) & 255) / 255.0
                             green:((color >> 8) & 255) / 255.0
                              blue:(color & 255) / 255.0
                             alpha:((color >> 24) & 255) / 255.0];
}

NSColor *StripColor() {
  return ColorFromArgb(
      crayon::browser::cef_shell::window::chrome_palette::kTabStripBackground);
}

NSColor *TabColor() {
  return ColorFromArgb(
      crayon::browser::cef_shell::window::chrome_palette::kActiveTabBackground);
}

NSColor *IndicatorColor() {
  return ColorFromArgb(
      crayon::browser::cef_shell::window::chrome_palette::kTabLoadingIndicator);
}

NSColor *SeparatorColor() {
  return ColorFromArgb(
      crayon::browser::cef_shell::window::chrome_palette::kChromeSeparator);
}

NSRect ToRect(const ChromeRect &rect) {
  return NSMakeRect(rect.x, rect.y, rect.width, rect.height);
}

bool HasArea(const ChromeRect &rect) {
  return rect.width > 0 && rect.height > 0;
}
} // namespace

// CEF Views exposes rectangular backgrounds only. This transparent native
// decoration cuts the shapes CEF cannot: the tab top corners, the active tab's
// inverted (flaring) bottom corners that join it to the navigation bar, the tab
// loading indicator, and the omnibox pill end caps. It replaces no control:
// text, focus, accessibility and pointer dispatch stay with CEF, and hitTest
// returns nil so no click can ever land on the decoration.
@interface CrayonChromeDecoration : NSView {
@public
  std::vector<TabDecoration> tabs_;
  ChromeRect omnibox_;
  CGFloat phase_;
}
@end

@implementation CrayonChromeDecoration {
  NSTimer *_indicatorTimer;
}

- (BOOL)isFlipped {
  return YES;
}

- (BOOL)isOpaque {
  return NO;
}

- (BOOL)isAccessibilityElement {
  return NO;
}

- (NSView *)hitTest:(NSPoint)point {
  return nil;
}

- (void)dealloc {
  [self stopIndicator];
}

- (void)viewDidMoveToWindow {
  // The decoration outlives neither the window nor its own superview: stopping
  // here is what keeps a window close from leaving a timer running.
  if (self.window == nil) {
    [self stopIndicator];
  } else if ([self hasLoadingTab]) {
    [self startIndicator];
  }
}

- (BOOL)hasLoadingTab {
  return std::any_of(tabs_.begin(), tabs_.end(),
                     [](const TabDecoration &tab) { return tab.loading; });
}

- (void)applyDecoration:(const ChromeDecoration &)decoration {
  tabs_ = decoration.tabs;
  omnibox_ = decoration.omnibox;
  if ([self hasLoadingTab]) {
    [self startIndicator];
  } else {
    [self stopIndicator];
  }
  self.needsDisplay = YES;
}

- (void)startIndicator {
  if (_indicatorTimer != nil || self.window == nil)
    return;
  __weak CrayonChromeDecoration *weakSelf = self;
  _indicatorTimer =
      [NSTimer timerWithTimeInterval:kIndicatorFrame
                             repeats:YES
                               block:^(NSTimer *) {
                                 [weakSelf advanceIndicator];
                               }];
  [[NSRunLoop mainRunLoop] addTimer:_indicatorTimer
                            forMode:NSRunLoopCommonModes];
}

- (void)stopIndicator {
  [_indicatorTimer invalidate];
  _indicatorTimer = nil;
}

- (void)advanceIndicator {
  phase_ = std::fmod(phase_ + kIndicatorFrame / kIndicatorPeriod, 1.0);
  for (const TabDecoration &tab : tabs_) {
    if (!tab.loading)
      continue;
    // Repaint only the slot: the rest of the band is static.
    [self setNeedsDisplayInRect:NSInsetRect(ToRect(tab.indicator), -2, -2)];
  }
}

// Paints |color| over the part of |square| that lies outside the disc centred
// on |center|, with |center| on one of the square's own corners. That is the
// region bounded by the arc and the opposite corner: the convex cut of a
// rounded rectangle corner and (mirrored) the flare an active tab pours into
// the navigation bar.
- (void)cutCorner:(NSRect)square
           center:(NSPoint)center
           radius:(CGFloat)radius {
  [NSGraphicsContext saveGraphicsState];
  NSRectClip(square);
  NSBezierPath *path = [NSBezierPath bezierPathWithRect:square];
  [path appendBezierPathWithArcWithCenter:center
                                   radius:radius
                               startAngle:0
                                 endAngle:360];
  path.windingRule = NSWindingRuleEvenOdd;
  [path fill];
  [NSGraphicsContext restoreGraphicsState];
}

- (void)drawTopCorners:(NSRect)rect {
  [StripColor() setFill];
  [NSGraphicsContext saveGraphicsState];
  NSRectClip(rect);
  NSBezierPath *mask = [NSBezierPath bezierPathWithRect:rect];
  [mask appendBezierPathWithRoundedRect:NSMakeRect(
                                            rect.origin.x,
                                            rect.origin.y + kTabTopInset,
                                            rect.size.width,
                                            rect.size.height + kTabCornerRadius)
                                xRadius:kTabCornerRadius
                                yRadius:kTabCornerRadius];
  mask.windingRule = NSWindingRuleEvenOdd;
  [mask fill];
  [NSGraphicsContext restoreGraphicsState];
}

// The active tab does not end at a straight line: its side edges curve outwards
// into the navigation bar, and the band colour fills the notch between the edge
// and the bar. The arc is tangent to the tab's side edge and to the bar, so it
// is a quarter circle centred one radius outside the tab's bottom corner.
- (void)drawBottomCorners:(NSRect)rect {
  const CGFloat bottom = NSMaxY(rect);
  const CGFloat radius = std::min(
      kTabCornerRadius, std::min(NSHeight(rect) / 2, NSWidth(rect) / 2));
  if (radius <= 0)
    return;
  // CEF paints the tab row as a rectangle, so the flare has to be added, not
  // cut: the arc's region is painted in the tab's own colour and the result
  // reaches outside the row's bounds. Measured before this correction (product
  // screenshot, rows 60-78 at the active tab's left edge): the boundary was a
  // straight vertical line, i.e. the flare was painted in the band colour
  // outside the tab, which is invisible because the band is already there.
  [TabColor() setFill];
  const NSRect left =
      NSMakeRect(NSMinX(rect) - radius, bottom - radius, radius, radius);
  [self cutCorner:left
           center:NSMakePoint(NSMinX(left), NSMinY(left))
           radius:radius];
  const NSRect right =
      NSMakeRect(NSMaxX(rect), bottom - radius, radius, radius);
  [self cutCorner:right
           center:NSMakePoint(NSMaxX(right), NSMinY(right))
           radius:radius];
}

- (void)drawIndicator:(const TabDecoration &)tab {
  const NSRect slot = ToRect(tab.indicator);
  const CGFloat diameter =
      std::min(kIndicatorDiameter, std::min(NSWidth(slot), NSHeight(slot)));
  const CGFloat radius = (diameter - kIndicatorLineWidth) / 2;
  if (radius <= 0)
    return;
  const NSPoint center = NSMakePoint(NSMidX(slot), NSMidY(slot));
  const CGFloat start = -90 + phase_ * 360;
  NSBezierPath *arc = [NSBezierPath bezierPath];
  arc.lineWidth = kIndicatorLineWidth;
  arc.lineCapStyle = NSLineCapStyleRound;
  [arc appendBezierPathWithArcWithCenter:center
                                  radius:radius
                              startAngle:start
                                endAngle:start + kIndicatorSweep
                               clockwise:NO];
  [IndicatorColor() setStroke];
  [arc stroke];
}

// The omnibox is a pill: both end caps are true semicircles, because the radius
// always follows the measured height rather than a fixed corner token. That is
// what keeps the field a pill if the navigation bar ever changes height.
- (void)drawOmniboxPill:(NSRect)rect {
  if (NSIsEmptyRect(rect))
    return;
  const CGFloat radius =
      std::min(NSHeight(rect) / 2, NSWidth(rect) / 2);
  if (radius <= 0)
    return;
  [self cutCorner:NSMakeRect(NSMinX(rect), NSMinY(rect), radius, radius)
           center:NSMakePoint(NSMinX(rect) + radius, NSMinY(rect) + radius)
           radius:radius];
  [self cutCorner:NSMakeRect(NSMaxX(rect) - radius, NSMinY(rect), radius, radius)
           center:NSMakePoint(NSMaxX(rect) - radius, NSMinY(rect) + radius)
           radius:radius];
  [self cutCorner:NSMakeRect(NSMinX(rect), NSMaxY(rect) - radius, radius, radius)
           center:NSMakePoint(NSMinX(rect) + radius, NSMaxY(rect) - radius)
           radius:radius];
  [self cutCorner:NSMakeRect(NSMaxX(rect) - radius, NSMaxY(rect) - radius,
                             radius, radius)
           center:NSMakePoint(NSMaxX(rect) - radius, NSMaxY(rect) - radius)
           radius:radius];
}

- (void)drawRect:(NSRect)dirtyRect {
  for (const TabDecoration &tab : tabs_) {
    const NSRect rect = ToRect(tab.bounds);
    if (NSIsEmptyRect(rect))
      continue;
    [self drawTopCorners:rect];
    if (tab.active)
      [self drawBottomCorners:rect];
    if (tab.loading)
      [self drawIndicator:tab];
  }
  if (HasArea(omnibox_)) {
    // The pill corners are cut with the toolbar colour, not the band colour:
    // the pill sits on the navigation bar.
    [ColorFromArgb(crayon::browser::cef_shell::window::chrome_palette::
                       kToolbarBackground) setFill];
    [self drawOmniboxPill:ToRect(omnibox_)];
  }
  [self drawBandSeparator];
}

// PLT-SHELL-24M2FIX-C5: the chrome band became a surface (strip #DEE2F0,
// toolbar #F9F9FF) with the page still pure white, so the band needs a bottom
// edge to read as separate. The reference build draws one hairline there; this
// draws the same line as the last row of the decoration's frame.
- (void)drawBandSeparator {
  const CGFloat height = NSHeight(self.bounds);
  if (height < kChromeSeparatorHeight)
    return;
  [SeparatorColor() setFill];
  NSRectFill(NSMakeRect(0, height - kChromeSeparatorHeight,
                        NSWidth(self.bounds), kChromeSeparatorHeight));
}
@end

namespace crayon::browser::cef_shell::macos::titlebar {

bool ApplyMergedTitlebar(void *native_window) {
  NSWindow *window = ResolveWindow(native_window);
  if (!window) {
    return false;
  }

  window.titlebarAppearsTransparent = YES;
  window.titleVisibility = NSWindowTitleHidden;

  window.backgroundColor = StripColor();
  return true;
}

void UpdateChromeDecoration(void *native_window,
                            const ChromeDecoration &decoration) {
  NSWindow *window = ResolveWindow(native_window);
  if (!window) {
    return;
  }
  NSView *content = window.contentView;
  if (!content)
    return;
  CrayonChromeDecoration *view = nil;
  for (NSView *child in content.subviews) {
    if ([child isKindOfClass:[CrayonChromeDecoration class]]) {
      view = (CrayonChromeDecoration *)child;
      break;
    }
  }
  if (!view) {
    view = [[CrayonChromeDecoration alloc] initWithFrame:NSZeroRect];
    view.wantsLayer = YES;
    [content addSubview:view positioned:NSWindowAbove relativeTo:nil];
  }
  [view applyDecoration:decoration];
  const NSRect bounds = content.bounds;
  const CGFloat height = std::min(kChromeHeight, NSHeight(bounds));
  view.frame = NSMakeRect(0, content.isFlipped ? 0 : NSHeight(bounds) - height,
                          NSWidth(bounds), height);
  view.needsDisplay = YES;
}

} // namespace crayon::browser::cef_shell::macos::titlebar
