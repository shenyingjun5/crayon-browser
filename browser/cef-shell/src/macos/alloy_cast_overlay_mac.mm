// PLT-SHELL-22M: macOS Browser-owned cast overlay. Placement, expiry and
// picker gating come from the shared CastSelectionPresentation; the page can
// influence placement only, and clicks return an opaque CastMediaRef for
// controller revalidation. No device selection, connection or playback here.
#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

#include "macos/alloy_cast_overlay_mac.h"

@interface CrayonOverlayButton : NSButton
@property(nonatomic) std::size_t index;
@property(nonatomic, copy) void (^onClick)(std::size_t);
@end
@implementation CrayonOverlayButton
- (BOOL)sendAction:(SEL)action to:(id)target {
  static_cast<void>(action);
  static_cast<void>(target);
  if (self.onClick) {
    self.onClick(self.index);
  }
  return YES;
}
@end

namespace crayon::browser::cef_shell::macos {

namespace {

constexpr double kMaximumViewportDip = 32768.0;
constexpr double kMinimumScale = 0.25;
constexpr double kMaximumScale = 8.0;

}  // namespace

struct AlloyCastOverlayMac::Impl {
  std::string accessible_name;
  Clock clock;
  Activate activate;
  NSWindow* root = nil;
  NSView* container = nil;
  NSView* host = nil;
  std::vector<CrayonOverlayButton*> buttons;
  std::vector<AlloyCastOverlayObservation> observations;
  browser_cast_view::CastSelectionPresentation presentation;
  browser_cast_view::CastViewContext context;
  std::size_t visible = 0;
  bool dispatching = false;
  bool window_active = false;
  bool attached = false;

  bool OnMainThread() const { return [NSThread isMainThread] == YES; }

  bool ContainerWindowVisible() const {
    return container && container.window && container.window.visible;
  }

  void HideAll() {
    for (CrayonOverlayButton* button : buttons) {
      button.hidden = YES;
    }
    if (host) host.hidden = YES;
    visible = 0;
  }

  std::optional<browser_cast_view::CastOverlayBounds> Place(
      const AlloyCastOverlayObservation& value) const {
    if (!std::isfinite(value.viewport_width) ||
        !std::isfinite(value.viewport_height) || value.viewport_width <= 0 ||
        value.viewport_height <= 0 ||
        value.viewport_width > kMaximumViewportDip ||
        value.viewport_height > kMaximumViewportDip) {
      return std::nullopt;
    }
    return presentation.PlaceOverlay(
        value.anchor, static_cast<int>(std::ceil(value.viewport_width)),
        static_cast<int>(std::ceil(value.viewport_height)), clock());
  }

  void Render() {
    if (!OnMainThread()) return;
    HideAll();
    // Focus gate, evaluated lazily: the overlay renders only while the
    // host window is the app's key window, or while the app has no key
    // window at all (background/automation launch).
    const bool window_active =
        !root || root.keyWindow || NSApp.keyWindow == nil;
    if (!attached || dispatching || !window_active ||
        presentation.PickerVisible() || !ContainerWindowVisible() ||
        observations.empty() ||
        observations.size() > browser_cast_view::kCastSelectionPageSize) {
      std::cerr << "overlay_render_gate attached=" << attached
                << " dispatching=" << dispatching
                << " window_active=" << window_active
                << " picker=" << presentation.PickerVisible()
                << " container_window=" << ContainerWindowVisible()
                << " observations=" << observations.size() << std::endl;
      return;
    }
    const NSRect container_bounds = container.bounds;
    if (container_bounds.size.width <= 0 ||
        container_bounds.size.height <= 0) {
      return;
    }
    for (std::size_t index = 0; index < observations.size(); ++index) {
      for (std::size_t prior = 0; prior < index; ++prior) {
        if (observations[prior].anchor.media ==
            observations[index].anchor.media) {
          HideAll();
          return;
        }
      }
    }
    for (std::size_t index = 0; index < observations.size(); ++index) {
      const auto placed = Place(observations[index]);
      if (!placed) {
        HideAll();
        return;
      }
      const double scale_x =
          container_bounds.size.width / observations[index].viewport_width;
      const double scale_y =
          container_bounds.size.height / observations[index].viewport_height;
      if (!std::isfinite(scale_x) || !std::isfinite(scale_y) ||
          scale_x < kMinimumScale || scale_x > kMaximumScale ||
          scale_y < kMinimumScale || scale_y > kMaximumScale) {
        HideAll();
        return;
      }
      if (index >= buttons.size()) break;
      // CSS coordinates are top-left origin; AppKit is bottom-left.
      const NSRect frame = NSMakeRect(
          placed->x * scale_x,
          container_bounds.size.height - (placed->y + placed->height) * scale_y,
          placed->width * scale_x, placed->height * scale_y);
      buttons[index].frame = frame;
      buttons[index].hidden = NO;
      ++visible;
    }
    host.hidden = NO;
  }

  void ActivateControl(std::size_t index) {
    if (!activate || index >= observations.size() || dispatching) return;
    dispatching = true;
    const bool accepted = activate(observations[index].anchor.media);
    dispatching = false;
    if (!accepted) {
      // The controller rejected the ref for the current context; the
      // overlay must not keep offering a stale target.
      HideAll();
    }
  }
};

AlloyCastOverlayMac::AlloyCastOverlayMac(std::string accessible_name,
                                         Clock clock, Activate activate)
    : impl_(new Impl{.accessible_name = std::move(accessible_name),
                     .clock = std::move(clock),
                     .activate = std::move(activate)}) {}

AlloyCastOverlayMac::~AlloyCastOverlayMac() {
  Detach();
}

bool AlloyCastOverlayMac::Attach(void* root_window, void* container_view) {
  if (impl_->attached) return false;
  NSWindow* root = (__bridge NSWindow*)root_window;
  NSView* container = (__bridge NSView*)container_view;
  if (!root || !container || !container.window ||
      impl_->accessible_name.empty() || !impl_->clock || !impl_->activate) {
    return false;
  }
  impl_->root = root;
  impl_->container = container;
  impl_->window_active = root.keyWindow;

  NSView* host = [[NSView alloc] initWithFrame:NSZeroRect];
  host.hidden = YES;
  for (std::size_t index = 0;
       index < browser_cast_view::kCastSelectionPageSize; ++index) {
    CrayonOverlayButton* button =
        [[CrayonOverlayButton alloc] initWithFrame:NSMakeRect(0, 0, 96, 36)];
    button.buttonType = NSButtonTypeMomentaryLight;
    button.bezelStyle = NSBezelStyleRounded;
    button.index = index;
    button.tag = static_cast<NSInteger>(index);
    button.hidden = YES;
    button.title = @(impl_->accessible_name.c_str());
    button.accessibilityLabel = @(impl_->accessible_name.c_str());
    // Clicks are intercepted in sendAction:to: and routed to the controller
    // callback; no first-responder action escapes into the page.
    button.target = button;
    button.action = @selector(performClick:);
    button.onClick = ^(std::size_t clicked) {
      impl_->ActivateControl(clicked);
    };
    [host addSubview:button];
    impl_->buttons.push_back(button);
  }
  impl_->host = host;
  [container addSubview:host positioned:NSWindowAbove relativeTo:nil];
  impl_->attached = true;

  return true;
}

void AlloyCastOverlayMac::BindContext(
    browser_cast_view::CastViewContext context) {
  impl_->context = context;
  impl_->presentation.BindContext(context);
}

bool AlloyCastOverlayMac::Apply(
    browser_cast_view::CastSelectionSnapshot snapshot) {
  return impl_->presentation.Apply(std::move(snapshot));
}

void AlloyCastOverlayMac::SetObservations(
    std::vector<AlloyCastOverlayObservation> observations) {
  impl_->observations = std::move(observations);
  impl_->Render();
}

void AlloyCastOverlayMac::Tick() {
  impl_->Render();
}

void AlloyCastOverlayMac::Invalidate() {
  impl_->presentation.Clear();
  impl_->HideAll();
}

void AlloyCastOverlayMac::Detach() {
  if (!impl_->attached) return;
  impl_->presentation.Clear();
  impl_->HideAll();
  impl_->observations.clear();
  impl_->buttons.clear();
  [impl_->host removeFromSuperview];
  impl_->host = nil;
  impl_->container = nil;
  impl_->root = nil;
  impl_->attached = false;
}

std::size_t AlloyCastOverlayMac::visible_count() const noexcept {
  return impl_->visible;
}

std::optional<browser_cast_view::CastOverlayBounds>
AlloyCastOverlayMac::placed_frame(std::size_t index) const {
  if (index >= impl_->buttons.size() || index >= impl_->visible ||
      impl_->buttons[index].hidden) {
    return std::nullopt;
  }
  const NSRect frame = impl_->buttons[index].frame;
  return browser_cast_view::CastOverlayBounds{
      static_cast<int>(frame.origin.x), static_cast<int>(frame.origin.y),
      static_cast<int>(frame.size.width),
      static_cast<int>(frame.size.height)};
}

}  // namespace crayon::browser::cef_shell::macos
