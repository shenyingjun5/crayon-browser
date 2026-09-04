#include "macos/cast_chrome_mac.h"

#import <AppKit/AppKit.h>

#include <charconv>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "crayon/cef_shell_ipc/media_host_codec.h"
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
namespace wire = ::crayon::cef_shell::ipc::media_host;

constexpr CGFloat kCompactWidth = 40;
constexpr CGFloat kPlaybackWidth = 300;
constexpr CGFloat kControlHeight = 32;

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
  NSTextField* code_input = nil;
  NSButton* code_connect = nil;
  CrayonCastButtonTarget* code_target = nil;
  NSButton* pause = nil;
  NSButton* seek = nil;
  NSTextField* seconds = nil;
  NSTextField* control_status = nil;
  CrayonCastButtonTarget* pause_target = nil;
  CrayonCastButtonTarget* seek_target = nil;
  std::vector<ReceiverOption> receivers;
  CastButtonState state = CastButtonState::kHidden;
  CastChromePresentation presentation;
  bool input_failed = false;
  bool seek_failed = false;
  bool rejected = false;
  browser_cast_view::RejectReason reject_reason =
      browser_cast_view::RejectReason::kGeneral;
  std::uint64_t picker_epoch = 0;
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
  NSString* selected_id =
      surface->receiver_popup.selectedItem.representedObject;
  [surface->receiver_popup removeAllItems];
  for (const auto& receiver : surface->receivers) {
    [surface->receiver_popup addItemWithTitle:Text(receiver.display_name)];
    NSMenuItem* item = surface->receiver_popup.lastItem;
    item.representedObject = Text(receiver.device_id);
    if ([item.representedObject isEqual:selected_id])
      [surface->receiver_popup selectItem:item];
  }
  const bool has_receivers = !surface->receivers.empty();
  if (surface->alert) {
    surface->alert.informativeText =
        surface->input_failed || surface->presentation.cast_code_failed
            ? Text(strings.cast_code_failed)
            : (has_receivers ? @"" : Text(strings.picker_empty));
    if (surface->alert.buttons.count > 0)
      surface->alert.buttons[0].enabled =
          has_receivers && !surface->presentation.cast_code_pending;
    if (surface->alert.buttons.count > 1)
      surface->alert.buttons[1].enabled =
          !surface->presentation.cast_code_pending;
  }
  surface->receiver_popup.enabled = !surface->presentation.cast_code_pending;
  surface->code_input.enabled = !surface->presentation.cast_code_pending;
  surface->code_connect.enabled = !surface->presentation.cast_code_pending;
}

void ClosePicker(Surface* surface) {
  if (!surface || !surface->presenting || !surface->alert)
    return;
  surface->suppress_completion = true;
  surface->presenting = false;
  ++surface->picker_epoch;
  surface->code_input.stringValue = @"";
  surface->code_target.action = nil;
  surface->code_connect.target = nil;
  surface->code_input.target = nil;
  if (surface->window.attachedSheet == surface->alert.window) {
    [surface->window endSheet:surface->alert.window
                   returnCode:NSModalResponseCancel];
  } else {
    [surface->alert.window orderOut:nil];
  }
}

Surface* ActiveSurface(const std::shared_ptr<State>& state, int browser_id) {
  auto found = state->surfaces.find(browser_id);
  return !state->closed && browser_id == state->active_browser_id &&
                 found != state->surfaces.end()
             ? &found->second
             : nullptr;
}

// Blocks must own a shared_ptr value, never capture a reference parameter
// whose stack frame ends before an AppKit action/completion is delivered.
void AddCodeInput(std::shared_ptr<State> state, int browser_id,
                  Surface* surface) {
  NSView* content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 90)];
  surface->receiver_popup.frame = NSMakeRect(0, 60, 320, 28);
  [content addSubview:surface->receiver_popup];
  surface->code_input =
      [[NSTextField alloc] initWithFrame:NSMakeRect(0, 16, 190, 26)];
  surface->code_input.placeholderString = Text(state->strings.cast_code_label);
  surface->code_input.accessibilityLabel = Text(state->strings.cast_code_label);
  surface->code_connect =
      [NSButton buttonWithTitle:Text(state->strings.cast_code_connect)
                         target:nil
                         action:@selector(invoke:)];
  surface->code_connect.frame = NSMakeRect(198, 12, 122, 32);
  surface->code_target = [[CrayonCastButtonTarget alloc] init];
  const auto epoch = surface->picker_epoch;
  surface->code_target.action = ^{
    Surface* current = ActiveSurface(state, browser_id);
    if (!current || !current->presenting || current->picker_epoch != epoch ||
        current->state != CastButtonState::kSelecting ||
        current->presentation.cast_code_pending)
      return;
    NSString* value = current->code_input.stringValue;
    const auto length = [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    bool accepted = false;
    if (length > 0 && length <= wire::kMaxCastCodeBytes &&
        state->callbacks.connect_cast_code) {
      const std::string code(value.UTF8String, length);
      accepted = state->callbacks.connect_cast_code(code);
    }
    // A callback may close/detach the surface; never reuse its old reference.
    current = ActiveSurface(state, browser_id);
    if (!current || current->picker_epoch != epoch) return;
    current->input_failed = !accepted;
    current->presentation.cast_code_pending = accepted;
    if (accepted) current->code_input.stringValue = @"";
    UpdatePicker(current, state->strings);
  };
  surface->code_connect.target = surface->code_target;
  surface->code_input.target = surface->code_target;
  surface->code_input.action = @selector(invoke:);
  [content addSubview:surface->code_input];
  [content addSubview:surface->code_connect];
  surface->alert.accessoryView = content;
}

void PresentPicker(std::shared_ptr<State> state, int browser_id) {
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
  const auto epoch = ++surface.picker_epoch;
  surface.input_failed = false;
  AddCodeInput(state, browser_id, &surface);
  surface.suppress_completion = false;
  surface.presenting = true;
  UpdatePicker(&surface, state->strings);

  [surface.alert
      beginSheetModalForWindow:surface.window
             completionHandler:^(NSModalResponse response) {
               auto current = state->surfaces.find(browser_id);
               if (current == state->surfaces.end() ||
                   current->second.picker_epoch != epoch)
                 return;
               Surface& completed = current->second;
               const bool suppressed = completed.suppress_completion;
               completed.presenting = false;
               completed.suppress_completion = false;
               completed.code_input.stringValue = @"";
               completed.code_target.action = nil;
               completed.code_connect.target = nil;
               completed.code_input.target = nil;
               if (state->closed || suppressed ||
                   browser_id != state->active_browser_id)
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

const std::string& RejectionMessage(const Surface& surface,
                                    const CastChromeStrings& strings) {
  switch (surface.reject_reason) {
    case browser_cast_view::RejectReason::kNoRoute:
      return strings.rejected_no_route;
    case browser_cast_view::RejectReason::kDrmProtected:
      return strings.rejected_drm;
    case browser_cast_view::RejectReason::kGateDenied:
    case browser_cast_view::RejectReason::kGeneral:
      return strings.rejected;
  }
  return strings.rejected;
}

void UpdatePlayback(Surface* surface, const State& state, bool active) {
  const bool casting = active && surface->state == CastButtonState::kCasting;
  const bool rejected =
      active && surface->rejected && surface->state != CastButtonState::kHidden;
  const bool enabled = casting && !surface->presentation.control_pending;
  surface->pause.hidden = surface->seek.hidden = surface->seconds.hidden =
      !casting;
  surface->pause.enabled = surface->seek.enabled = surface->seconds.enabled =
      enabled;
  surface->pause.title = Text(surface->presentation.playback_paused
                                  ? state.strings.playback_resume
                                  : state.strings.playback_pause);
  surface->pause.accessibilityLabel = surface->pause.title;
  const bool failed =
      surface->presentation.control_failed || surface->seek_failed;
  surface->pause.toolTip = Text(failed ? state.strings.playback_failed
                                       : (surface->presentation.playback_paused
                                              ? state.strings.playback_resume
                                              : state.strings.playback_pause));
  surface->seek.toolTip = Text(failed ? state.strings.playback_failed
                                      : state.strings.playback_seek);
  surface->pause.accessibilityHelp = surface->pause.toolTip;
  surface->seek.accessibilityHelp = surface->seek.toolTip;
  surface->control_status.stringValue =
      rejected ? Text(RejectionMessage(*surface, state.strings))
               : (failed ? Text(state.strings.playback_failed) : @"");
  surface->control_status.accessibilityLabel =
      surface->control_status.stringValue;
  surface->control_status.hidden = !rejected && (!casting || !failed);
  [surface->accessory.view
      setFrameSize:NSMakeSize(
                       casting || rejected ? kPlaybackWidth : kCompactWidth,
                       rejected || (casting && failed) ? kControlHeight * 2
                                                       : kControlHeight)];
  if (!casting) {
    surface->seconds.stringValue = @"";
    surface->seek_failed = false;
  }
}

void AddPlaybackControls(std::shared_ptr<State> state, int browser_id,
                         Surface* surface, NSView* container) {
  surface->pause = [NSButton buttonWithTitle:Text(state->strings.playback_pause)
                                      target:nil
                                      action:@selector(invoke:)];
  surface->pause.frame = NSMakeRect(40, 0, 88, kControlHeight);
  surface->seconds =
      [[NSTextField alloc] initWithFrame:NSMakeRect(130, 4, 78, 24)];
  surface->seconds.placeholderString = Text(state->strings.playback_seconds);
  surface->seconds.accessibilityLabel = Text(state->strings.playback_seconds);
  surface->seek = [NSButton buttonWithTitle:Text(state->strings.playback_seek)
                                     target:nil
                                     action:@selector(invoke:)];
  surface->seek.frame = NSMakeRect(210, 0, 88, kControlHeight);
  surface->control_status = [NSTextField wrappingLabelWithString:@""];
  surface->control_status.frame =
      NSMakeRect(4, kControlHeight, kPlaybackWidth - 8, kControlHeight);
  surface->control_status.textColor = [NSColor systemRedColor];
  surface->pause_target = [[CrayonCastButtonTarget alloc] init];
  surface->seek_target = [[CrayonCastButtonTarget alloc] init];
  surface->pause_target.action = ^{
    Surface* current = ActiveSurface(state, browser_id);
    if (!current || current->state != CastButtonState::kCasting ||
        current->presentation.control_pending || !state->callbacks.set_paused)
      return;
    const bool accepted =
        state->callbacks.set_paused(!current->presentation.playback_paused);
    current = ActiveSurface(state, browser_id);
    if (!current) return;
    current->presentation.control_pending = accepted;
    current->presentation.control_failed = !accepted;
    UpdatePlayback(current, *state, true);
  };
  surface->seek_target.action = ^{
    Surface* current = ActiveSurface(state, browser_id);
    if (!current || current->state != CastButtonState::kCasting ||
        current->presentation.control_pending)
      return;
    NSString* value = current->seconds.stringValue;
    const auto length = [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    std::uint64_t seconds = 0;
    bool valid =
        length > 0 && length <= std::to_string(wire::kMaxSeekSeconds).size();
    if (valid) {
      const char* text = value.UTF8String;
      const auto parsed = std::from_chars(text, text + length, seconds);
      valid = parsed.ec == std::errc{} && parsed.ptr == text + length &&
              seconds <= wire::kMaxSeekSeconds;
    }
    const bool accepted =
        valid && state->callbacks.seek && state->callbacks.seek(seconds);
    current = ActiveSurface(state, browser_id);
    if (!current) return;
    current->seek_failed = !accepted;
    current->presentation.control_pending = accepted;
    UpdatePlayback(current, *state, true);
  };
  surface->pause.target = surface->pause_target;
  surface->seek.target = surface->seek_target;
  surface->seconds.target = surface->seek_target;
  surface->seconds.action = @selector(invoke:);
  [container addSubview:surface->pause];
  [container addSubview:surface->seconds];
  [container addSubview:surface->seek];
  [container addSubview:surface->control_status];
  UpdatePlayback(surface, *state, false);
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
    if (state->callbacks.activate()) {
      Surface* current = ActiveSurface(state, browser_id);
      if (!current) return;
      current->state = was_casting ? CastButtonState::kStopping
                                   : CastButtonState::kSelecting;
      current->rejected = false;
      UpdatePlayback(current, *state, true);
      current->button.enabled = NO;
      if (!was_casting) PresentPicker(state, browser_id);
    }
  };
  surface.button.target = surface.target;
  surface.button.action = @selector(invoke:);
  [container addSubview:surface.button];
  surface.accessory.view = container;
  AddPlaybackControls(state, browser_id, &surface, container);
  [window addTitlebarAccessoryViewController:surface.accessory];
  impl_->state->surfaces.emplace(browser_id, std::move(surface));
  return true;
}

void CastChromeMac::DetachWindow(int browser_id) {
  auto found = impl_->state->surfaces.find(browser_id);
  if (found == impl_->state->surfaces.end())
    return;
  ClosePicker(&found->second);
  found->second.target.action = nil;
  found->second.pause_target.action = nil;
  found->second.seek_target.action = nil;
  found->second.code_target.action = nil;
  found->second.button.target = nil;
  found->second.pause.target = nil;
  found->second.seek.target = nil;
  found->second.seconds.target = nil;
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
      UpdatePlayback(&surface, *impl_->state, false);
      ClosePicker(&surface);
    }
  }
}

void CastChromeMac::Render(
    const browser_cast_view::CastUiCoordinator& coordinator,
    CastChromePresentation presentation) {
  if (impl_->state->closed)
    return;
  for (auto& [id, surface] : impl_->state->surfaces) {
    const bool active = id == impl_->state->active_browser_id;
    surface.state = coordinator.button().state();
    surface.rejected = coordinator.feature().state() ==
                       browser_cast_view::CastFeatureState::kRejected;
    surface.reject_reason = coordinator.feature().reject_reason();
    surface.presentation = presentation;
    surface.receivers = coordinator.receivers();
    surface.button.hidden =
        !active || surface.state == CastButtonState::kHidden;
    surface.button.enabled =
        active && (surface.state == CastButtonState::kEligible ||
                   surface.state == CastButtonState::kCasting);
    const bool casting = surface.state == CastButtonState::kCasting;
    surface.button.toolTip = Text(
        casting ? impl_->state->strings.button_stop
                : (surface.rejected ? impl_->state->strings.retry
                                    : impl_->state->strings.button_select));
    surface.button.accessibilityLabel = surface.button.toolTip;
    UpdatePlayback(&surface, *impl_->state, active);
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
  for (int browser_id : browser_ids) DetachWindow(browser_id);
  impl_->state->callbacks = {};
}

}  // namespace crayon::browser::cef_shell::macos
