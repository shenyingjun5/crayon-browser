#include "macos/cast_chrome_mac.h"

#import <AppKit/AppKit.h>

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "include/internal/cef_types_mac.h"

@interface CrayonCastButtonTarget : NSObject
@property(nonatomic, copy) void (^action)(void);
- (void)invoke:(id)sender;
@end

@implementation CrayonCastButtonTarget
- (void)invoke:(id)sender {
  static_cast<void>(sender);
  if (self.action)
    self.action();
}
@end

namespace crayon::browser::cef_shell::macos {
namespace {

using browser_cast_view::ReceiverOption;
using browser_chrome::CastButtonState;

NSString* Text(const std::string& value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding];
}

struct Surface final {
  NSWindow* window = nil;
  NSTitlebarAccessoryViewController* accessory = nil;
  NSButton* button = nil;
  CrayonCastButtonTarget* target = nil;
  NSAlert* alert = nil;
  NSPopUpButton* receiver_popup = nil;
  std::vector<ReceiverOption> receivers;
  CastButtonState state = CastButtonState::kHidden;
  bool presenting = false;
  bool suppress_completion = false;
};

struct State final {
  CastChromeStrings strings;
  CastChromeCallbacks callbacks;
  std::map<int, Surface> surfaces;
  int active_browser_id = 0;
  bool closed = false;
};

void UpdatePicker(Surface* surface, const CastChromeStrings& strings) {
  if (!surface || !surface->receiver_popup)
    return;
  [surface->receiver_popup removeAllItems];
  for (const auto& receiver : surface->receivers) {
    [surface->receiver_popup addItemWithTitle:Text(receiver.display_name)];
  }
  const bool has_receivers = !surface->receivers.empty();
  if (surface->alert) {
    surface->alert.informativeText =
        has_receivers ? @"" : Text(strings.picker_empty);
    if (surface->alert.buttons.count > 0)
      surface->alert.buttons[0].enabled = has_receivers;
  }
}

void ClosePicker(Surface* surface) {
  if (!surface || !surface->presenting || !surface->alert)
    return;
  surface->suppress_completion = true;
  surface->presenting = false;
  if (surface->window.attachedSheet == surface->alert.window) {
    [surface->window endSheet:surface->alert.window
                   returnCode:NSModalResponseCancel];
  } else {
    [surface->alert.window orderOut:nil];
  }
}

void PresentPicker(const std::shared_ptr<State>& state, int browser_id) {
  auto found = state->surfaces.find(browser_id);
  if (state->closed || found == state->surfaces.end() ||
      browser_id != state->active_browser_id || found->second.presenting)
    return;
  Surface& surface = found->second;
  if (!surface.window)
    return;

  surface.alert = [[NSAlert alloc] init];
  surface.alert.messageText = Text(state->strings.picker_title);
  surface.alert.alertStyle = NSAlertStyleInformational;
  [surface.alert addButtonWithTitle:Text(state->strings.picker_select)];
  [surface.alert addButtonWithTitle:Text(state->strings.picker_refresh)];
  [surface.alert addButtonWithTitle:Text(state->strings.picker_cancel)];
  surface.receiver_popup =
      [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 320, 28)
                                 pullsDown:NO];
  surface.receiver_popup.accessibilityLabel = Text(state->strings.picker_title);
  surface.alert.accessoryView = surface.receiver_popup;
  surface.suppress_completion = false;
  surface.presenting = true;
  UpdatePicker(&surface, state->strings);

  [surface.alert
      beginSheetModalForWindow:surface.window
             completionHandler:^(NSModalResponse response) {
               auto current = state->surfaces.find(browser_id);
               if (current == state->surfaces.end())
                 return;
               Surface& completed = current->second;
               const bool suppressed = completed.suppress_completion;
               completed.presenting = false;
               completed.suppress_completion = false;
               if (state->closed || suppressed)
                 return;
               if (response == NSAlertFirstButtonReturn) {
                 const NSInteger index =
                     completed.receiver_popup.indexOfSelectedItem;
                 if (index >= 0 &&
                     static_cast<std::size_t>(index) <
                         completed.receivers.size() &&
                     state->callbacks.select) {
                   const std::string device_id =
                       completed.receivers[static_cast<std::size_t>(index)]
                           .device_id;
                   if (!state->callbacks.select(device_id))
                     PresentPicker(state, browser_id);
                 }
               } else if (response == NSAlertSecondButtonReturn) {
                 if (state->callbacks.refresh)
                   static_cast<void>(state->callbacks.refresh());
                 PresentPicker(state, browser_id);
               } else if (state->callbacks.cancel) {
                 state->callbacks.cancel();
               }
             }];
}

NSImage* CastImage() {
  NSString* path = [[NSBundle mainBundle] pathForResource:@"cast-device"
                                                   ofType:@"svg"];
  NSImage* image = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
  [image setTemplate:YES];
  image.size = NSMakeSize(20, 20);
  return image;
}

}  // namespace

struct CastChromeMac::Impl final {
  explicit Impl(CastChromeStrings strings, CastChromeCallbacks callbacks)
      : state(std::make_shared<State>(
            State{std::move(strings), std::move(callbacks)})) {}

  std::shared_ptr<State> state;
};

CastChromeMac::CastChromeMac(CastChromeStrings strings,
                             CastChromeCallbacks callbacks)
    : impl_(std::make_unique<Impl>(std::move(strings), std::move(callbacks))) {}

CastChromeMac::~CastChromeMac() {
  Close();
}

bool CastChromeMac::AttachWindow(int browser_id, void* native_view) {
  if (browser_id <= 0 || !native_view || impl_->state->closed) {
    return false;
  }
  if (impl_->state->surfaces.find(browser_id) != impl_->state->surfaces.end())
    return true;
  NSView* view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(native_view);
  NSWindow* window = view.window;
  if (!window)
    return false;

  Surface surface;
  surface.window = window;
  surface.accessory = [[NSTitlebarAccessoryViewController alloc] init];
  surface.accessory.layoutAttribute = NSLayoutAttributeRight;
  NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 40, 32)];
  surface.button = [[NSButton alloc] initWithFrame:NSMakeRect(4, 0, 32, 32)];
  surface.button.bezelStyle = NSBezelStyleTexturedRounded;
  surface.button.imagePosition = NSImageOnly;
  NSImage* image = CastImage();
  if (!image)
    return false;
  surface.button.image = image;
  surface.button.toolTip = Text(impl_->state->strings.button_select);
  surface.button.accessibilityLabel = Text(impl_->state->strings.button_select);
  surface.button.hidden = YES;
  surface.target = [[CrayonCastButtonTarget alloc] init];
  std::shared_ptr<State> state = impl_->state;
  surface.target.action = ^{
    auto found = state->surfaces.find(browser_id);
    if (state->closed || found == state->surfaces.end() ||
        browser_id != state->active_browser_id || !state->callbacks.activate)
      return;
    const bool was_casting = found->second.state == CastButtonState::kCasting;
    if (state->callbacks.activate() && !was_casting)
      PresentPicker(state, browser_id);
  };
  surface.button.target = surface.target;
  surface.button.action = @selector(invoke:);
  [container addSubview:surface.button];
  surface.accessory.view = container;
  [window addTitlebarAccessoryViewController:surface.accessory];
  impl_->state->surfaces.emplace(browser_id, std::move(surface));
  return true;
}

void CastChromeMac::DetachWindow(int browser_id) {
  auto found = impl_->state->surfaces.find(browser_id);
  if (found == impl_->state->surfaces.end())
    return;
  ClosePicker(&found->second);
  if (found->second.window && found->second.accessory) {
    const NSUInteger index =
        [found->second.window.titlebarAccessoryViewControllers
            indexOfObject:found->second.accessory];
    if (index != NSNotFound) {
      [found->second.window removeTitlebarAccessoryViewControllerAtIndex:index];
    }
  }
  impl_->state->surfaces.erase(found);
  if (impl_->state->active_browser_id == browser_id)
    impl_->state->active_browser_id = 0;
}

void CastChromeMac::SetActiveWindow(int browser_id) {
  if (impl_->state->closed ||
      impl_->state->surfaces.find(browser_id) == impl_->state->surfaces.end()) {
    return;
  }
  impl_->state->active_browser_id = browser_id;
  for (auto& [id, surface] : impl_->state->surfaces) {
    if (id != browser_id) {
      surface.button.hidden = YES;
      ClosePicker(&surface);
    }
  }
}

void CastChromeMac::Render(
    const browser_cast_view::CastUiCoordinator& coordinator) {
  if (impl_->state->closed)
    return;
  for (auto& [id, surface] : impl_->state->surfaces) {
    const bool active = id == impl_->state->active_browser_id;
    surface.state = coordinator.button().state();
    surface.receivers = coordinator.receivers();
    surface.button.hidden =
        !active || surface.state == CastButtonState::kHidden;
    surface.button.enabled =
        active && (surface.state == CastButtonState::kEligible ||
                   surface.state == CastButtonState::kCasting);
    const bool casting = surface.state == CastButtonState::kCasting;
    surface.button.toolTip =
        Text(casting ? impl_->state->strings.button_stop
                     : impl_->state->strings.button_select);
    surface.button.accessibilityLabel = surface.button.toolTip;
    if (surface.state != CastButtonState::kSelecting)
      ClosePicker(&surface);
    else if (surface.presenting)
      UpdatePicker(&surface, impl_->state->strings);
  }
}

void CastChromeMac::Close() {
  if (!impl_ || impl_->state->closed)
    return;
  impl_->state->closed = true;
  std::vector<int> browser_ids;
  browser_ids.reserve(impl_->state->surfaces.size());
  for (const auto& [browser_id, surface] : impl_->state->surfaces) {
    static_cast<void>(surface);
    browser_ids.push_back(browser_id);
  }
  for (int browser_id : browser_ids)
    DetachWindow(browser_id);
  impl_->state->callbacks = {};
}

}  // namespace crayon::browser::cef_shell::macos
