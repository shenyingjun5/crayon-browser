#import <AppKit/AppKit.h>

#include <iostream>
#include <string>

#include "browser/media_host/cast_shell_controller.h"
#include "macos/cast_chrome_mac.h"

namespace {

namespace cast = crayon::browser_cast_view;
namespace chrome = crayon::browser_chrome;
using crayon::browser::cef_shell::macos::CastChromeCallbacks;
using crayon::browser::cef_shell::macos::CastChromeMac;
using crayon::browser::cef_shell::macos::CastChromeStrings;

#define CHECK_CHROME(condition)                                           \
  do {                                                                    \
    if (!(condition)) {                                                   \
      std::cerr << "check failed at line " << __LINE__ << ": " #condition \
                << '\n';                                                  \
      return false;                                                       \
    }                                                                     \
  } while (false)

void DrainAppKit() {
  [[NSRunLoop currentRunLoop]
      runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
}

NSButton* CastButton(NSWindow* window) {
  if (window.titlebarAccessoryViewControllers.count != 1)
    return nil;
  NSView* container = window.titlebarAccessoryViewControllers[0].view;
  return container.subviews.count >= 1
             ? static_cast<NSButton*>(container.subviews[0])
             : nil;
}

NSButton* FindButton(NSView* view, NSString* title) {
  if ([view isKindOfClass:[NSButton class]]) {
    NSButton* button = static_cast<NSButton*>(view);
    if ([button.title isEqualToString:title])
      return button;
  }
  for (NSView* child in view.subviews) {
    if (NSButton* button = FindButton(child, title))
      return button;
  }
  return nil;
}

NSPopUpButton* FindPopup(NSView* view) {
  if ([view isKindOfClass:[NSPopUpButton class]])
    return static_cast<NSPopUpButton*>(view);
  for (NSView* child in view.subviews) {
    if (NSPopUpButton* popup = FindPopup(child))
      return popup;
  }
  return nil;
}

NSTextField* FindInput(NSView* view, NSString* label) {
  if ([view isKindOfClass:[NSTextField class]] &&
      [view.accessibilityLabel isEqualToString:label])
    return static_cast<NSTextField*>(view);
  for (NSView* child in view.subviews) {
    if (NSTextField* input = FindInput(child, label)) return input;
  }
  return nil;
}

bool RunChromeContract() {
  [NSApplication sharedApplication];
  NSWindow* first = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 800, 600)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  NSWindow* second = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(20, 20, 800, 600)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  first.releasedWhenClosed = NO;
  second.releasedWhenClosed = NO;
  [first makeKeyAndOrderFront:nil];

  cast::CastUiCoordinator coordinator;
  int refreshes = 0;
  int cancels = 0;
  std::string selected;
  std::string connected_code;
  int code_calls = 0;
  int pause_calls = 0;
  int seek_calls = 0;
  bool paused = false;
  std::uint64_t seek_seconds = 0;
  CastChromeMac chrome(
      CastChromeStrings{"Choose device", "Stop casting", "Cast to device",
                        "No devices", "Cast", "Refresh", "Cancel", "Cast code",
                        "Connect code", "Code failed", "Pause", "Resume",
                        "Seek", "Seconds", "Control failed", "Cast rejected",
                        "No cast route", "DRM protected", "Retry cast"},
      CastChromeCallbacks{[&coordinator] {
                            if (coordinator.active_session_generation())
                              return coordinator.RequestStop().has_value();
                            return coordinator.OpenPicker().has_value();
                          },
                          [&refreshes] {
                            ++refreshes;
                            return true;
                          },
                          [&coordinator, &cancels] {
                            ++cancels;
                            coordinator.CancelPicker();
                          },
                          [&selected](const std::string& device_id) {
                            selected = device_id;
                            return true;
                          },
                          [&](std::string code) {
                            ++code_calls;
                            connected_code = std::move(code);
                            return connected_code == "123456";
                          },
                          [&](bool value) {
                            ++pause_calls;
                            paused = value;
                            return true;
                          },
                          [&](std::uint64_t value) {
                            ++seek_calls;
                            seek_seconds = value;
                            return true;
                          }});

  CHECK_CHROME(chrome.AttachWindow(1, (__bridge void*)first.contentView));
  CHECK_CHROME(chrome.AttachWindow(1, (__bridge void*)first.contentView));
  CHECK_CHROME(first.titlebarAccessoryViewControllers.count == 1);
  chrome.SetActiveWindow(1);
  chrome.Render(coordinator);
  NSButton* first_button = CastButton(first);
  CHECK_CHROME(first_button && first_button.hidden);

  coordinator.SetPageActive(true);
  coordinator.SetMediaPresent(true);
  coordinator.SetBrowserVerifiedEligible(true);
  chrome.Render(coordinator);
  CHECK_CHROME(!first_button.hidden && first_button.enabled);
  CHECK_CHROME([first_button.toolTip isEqualToString:@"Choose device"]);
  [first_button performClick:nil];
  DrainAppKit();
  CHECK_CHROME(coordinator.feature().state() ==
               cast::CastFeatureState::kSelecting);
  CHECK_CHROME(first.attachedSheet != nil);
  NSButton* select_button =
      FindButton(first.attachedSheet.contentView, @"Cast");
  CHECK_CHROME(select_button && !select_button.enabled);
  CHECK_CHROME(FindButton(first.attachedSheet.contentView, @"Connect code") !=
               nil);
  chrome.Render(coordinator);
  NSButton* connect =
      FindButton(first.attachedSheet.contentView, @"Connect code");
  NSTextField* code = FindInput(first.attachedSheet.contentView, @"Cast code");
  CHECK_CHROME(code && connect.enabled);
  code.stringValue = @"";
  [connect performClick:nil];
  CHECK_CHROME(code_calls == 0);
  code.stringValue = @"12345678901234567890123456789012345";
  [connect performClick:nil];
  CHECK_CHROME(code_calls == 0);
  code.stringValue = @"invalid";
  [connect performClick:nil];
  CHECK_CHROME(code_calls == 1 && connect.enabled);
  code.stringValue = @"123456";
  [connect performClick:nil];
  CHECK_CHROME(code_calls == 2 && !connect.enabled &&
               code.stringValue.length == 0);
  [connect performClick:nil];
  CHECK_CHROME(code_calls == 2);
  chrome.Render(coordinator, {true});
  CHECK_CHROME(!connect.enabled && !select_button.enabled);
  chrome.Render(coordinator, {false, true});
  CHECK_CHROME(connect.enabled);

  CHECK_CHROME(coordinator.ReplaceReceivers(
      {{"phone_1", "Living room", true}, {"phone_2", "Bedroom", true}}));
  chrome.Render(coordinator);
  NSPopUpButton* popup = FindPopup(first.attachedSheet.contentView);
  CHECK_CHROME(popup && popup.numberOfItems == 2 && select_button.enabled);
  [popup selectItemAtIndex:1];
  chrome.Render(coordinator);
  CHECK_CHROME(popup.indexOfSelectedItem == 1);
  CHECK_CHROME(coordinator.ReplaceReceivers(
      {{"phone_2", "Bedroom", true}, {"phone_1", "Living room", true}}));
  chrome.Render(coordinator);
  CHECK_CHROME(popup.indexOfSelectedItem == 0);
  [select_button performClick:nil];
  DrainAppKit();
  CHECK_CHROME(selected == "phone_2");

  coordinator.CancelPicker();
  chrome.Render(coordinator);
  [first_button performClick:nil];
  DrainAppKit();
  NSButton* refresh_button =
      FindButton(first.attachedSheet.contentView, @"Refresh");
  CHECK_CHROME(refresh_button != nil);
  [refresh_button performClick:nil];
  DrainAppKit();
  CHECK_CHROME(refreshes == 1 && first.attachedSheet != nil);
  NSButton* cancel_button =
      FindButton(first.attachedSheet.contentView, @"Cancel");
  CHECK_CHROME(cancel_button != nil);
  [cancel_button performClick:nil];
  DrainAppKit();
  CHECK_CHROME(cancels == 1);

  CHECK_CHROME(coordinator.OpenPicker().has_value());
  CHECK_CHROME(coordinator.ApplyPolicyOutcome(cast::PolicyOutcome::kReject,
                                              cast::RejectReason::kNoRoute));
  chrome.Render(coordinator);
  NSView* rejected_controls = first.titlebarAccessoryViewControllers[0].view;
  NSTextField* rejected_status = FindInput(rejected_controls, @"No cast route");
  CHECK_CHROME(rejected_status && !rejected_status.hidden);
  CHECK_CHROME([first_button.accessibilityLabel isEqualToString:@"Retry cast"]);
  chrome.Render(coordinator);
  CHECK_CHROME(first.attachedSheet == nil);
  [first_button performClick:nil];
  DrainAppKit();
  chrome.Render(coordinator);
  CHECK_CHROME(first.attachedSheet != nil && rejected_status.hidden);
  coordinator.CancelPicker();
  chrome.Render(coordinator);

  for (const auto reason :
       {cast::RejectReason::kDrmProtected, cast::RejectReason::kGeneral,
        cast::RejectReason::kGateDenied}) {
    CHECK_CHROME(coordinator.OpenPicker().has_value());
    CHECK_CHROME(
        coordinator.ApplyPolicyOutcome(cast::PolicyOutcome::kReject, reason));
    chrome.Render(coordinator);
    NSString* label = reason == cast::RejectReason::kDrmProtected
                          ? @"DRM protected"
                          : @"Cast rejected";
    CHECK_CHROME([rejected_status.stringValue isEqualToString:label]);
    CHECK_CHROME(!rejected_status.hidden && first.attachedSheet == nil);
    CHECK_CHROME(coordinator.OpenPicker().has_value());
    coordinator.CancelPicker();
  }
  CHECK_CHROME(coordinator.OpenPicker().has_value());
  CHECK_CHROME(coordinator.ApplyPolicyOutcome(cast::PolicyOutcome::kReject,
                                              cast::RejectReason::kNoRoute));
  chrome.Render(coordinator);
  CHECK_CHROME(chrome.AttachWindow(2, (__bridge void*)second.contentView));
  chrome.SetActiveWindow(2);
  CHECK_CHROME(rejected_status.hidden);
  chrome.SetActiveWindow(1);
  chrome.Render(coordinator);
  CHECK_CHROME(!rejected_status.hidden);
  chrome.DetachWindow(2);
  coordinator.SetPageActive(false);
  chrome.Render(coordinator);
  CHECK_CHROME(rejected_status.hidden && first_button.hidden);
  coordinator.SetPageActive(true);
  coordinator.SetMediaPresent(true);
  coordinator.SetBrowserVerifiedEligible(true);

  CHECK_CHROME(coordinator.OpenPicker().has_value());
  CHECK_CHROME(coordinator.ApplyPolicyOutcome(cast::PolicyOutcome::kDirect));
  CHECK_CHROME(coordinator.NotifySessionStarted(9));
  chrome.Render(coordinator);
  CHECK_CHROME([first_button.toolTip isEqualToString:@"Stop casting"]);
  NSView* controls = first.titlebarAccessoryViewControllers[0].view;
  NSButton* pause = FindButton(controls, @"Pause");
  NSButton* seek = FindButton(controls, @"Seek");
  NSTextField* seconds = FindInput(controls, @"Seconds");
  CHECK_CHROME(pause && seek && seconds && !pause.hidden && pause.enabled);
  [pause performClick:nil];
  CHECK_CHROME(pause_calls == 1 && paused && !pause.enabled &&
               first_button.enabled);
  [pause performClick:nil];
  CHECK_CHROME(pause_calls == 1);
  chrome.Render(coordinator, {false, false, true});
  CHECK_CHROME(!seek.enabled && first_button.enabled);
  chrome.Render(coordinator, {false, false, false, false, true});
  CHECK_CHROME([pause.title isEqualToString:@"Resume"] && pause.enabled);
  [pause performClick:nil];
  CHECK_CHROME(pause_calls == 2 && !paused);
  chrome.Render(coordinator, {false, false, false, true});
  CHECK_CHROME([pause.toolTip isEqualToString:@"Control failed"]);
  for (NSString* invalid in
       @[ @"", @"-1", @"1.5", @"1x", @"604801", @"9999999999999999999999" ]) {
    seconds.stringValue = invalid;
    [seek performClick:nil];
    CHECK_CHROME(seek_calls == 0 && seek.enabled);
  }
  for (NSString* valid in @[ @"0", @"604800", @"42" ]) {
    seconds.stringValue = valid;
    [seek performClick:nil];
    CHECK_CHROME(!seek.enabled);
    chrome.Render(coordinator);
  }
  CHECK_CHROME(seek_calls == 3 && seek_seconds == 42);
  [pause performClick:nil];
  CHECK_CHROME(!pause.enabled);
  [first_button performClick:nil];
  CHECK_CHROME(coordinator.button().state() ==
               chrome::CastButtonState::kStopping);
  CHECK_CHROME(first.attachedSheet == nil);
  chrome.Render(coordinator);
  CHECK_CHROME(pause.hidden && seek.hidden && seconds.stringValue.length == 0);

  CHECK_CHROME(chrome.AttachWindow(2, (__bridge void*)second.contentView));
  chrome.SetActiveWindow(2);
  chrome.Render(coordinator);
  CHECK_CHROME(first_button.hidden);
  CHECK_CHROME(CastButton(second) && !CastButton(second).hidden &&
               !CastButton(second).enabled);
  chrome.DetachWindow(2);
  CHECK_CHROME(second.titlebarAccessoryViewControllers.count == 0);
  CHECK_CHROME(coordinator.NotifySessionEnded(9));
  // A stopped session deliberately loses eligibility. Model the next
  // Browser-verified playback before opening a fresh picker.
  coordinator.SetBrowserVerifiedEligible(true);
  chrome.SetActiveWindow(1);
  chrome.Render(coordinator);
  [first_button performClick:nil];
  DrainAppKit();
  CHECK_CHROME(first.attachedSheet != nil);
  connect = FindButton(first.attachedSheet.contentView, @"Connect code");
  code = FindInput(first.attachedSheet.contentView, @"Cast code");
  CHECK_CHROME(connect && code);
  code.stringValue = @"123456";
  [connect performClick:nil];
  CHECK_CHROME(code_calls == 3 && !connect.enabled);
  chrome.Close();
  DrainAppKit();
  CHECK_CHROME(first.titlebarAccessoryViewControllers.count == 0);
  CHECK_CHROME(first.attachedSheet == nil && cancels == 1);
  [pause performClick:nil];
  [connect performClick:nil];
  CHECK_CHROME(pause_calls == 3 && code_calls == 3);
  [first close];
  [second close];
  return true;
}

bool RunCodeLookupWithController() {
  namespace host = crayon::browser::cef_shell::media_host;
  namespace mh = host::media_host_ipc;
  NSWindow *window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 800, 600)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.releasedWhenClosed = NO;
  [window makeKeyAndOrderFront:nil];
  int starts = 0;
  int lookups = 0;
  host::CastCommandPort port;
  port.discovery = [](mh::DiscoveryAction) { return true; };
  port.list_devices = [](std::optional<std::uint64_t>, std::uint16_t) {
    return true;
  };
  port.resolve_cast_code = [&](std::string) {
    return std::optional<std::string>("lookup-" + std::to_string(++lookups));
  };
  port.start_cast = [&](std::uint64_t id, std::string device, bool handoff) {
    ++starts;
    return id == 71 && device == "phone" && !handoff;
  };
  host::CastShellController controller(std::move(port));
  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning(
      {{host::MediaPlanningEventKind::kCandidate, 71, "fixture.invalid",
        std::nullopt, std::nullopt, std::nullopt}});
  CastChromeCallbacks callbacks;
  callbacks.activate = [&] { return controller.ActivateCastButton(); };
  callbacks.cancel = [&] { controller.CancelReceiverPicker(); };
  callbacks.select = [&](const std::string &id) {
    return controller.SelectReceiver(id);
  };
  callbacks.connect_cast_code = [&](std::string code) {
    return controller.ConnectCastCode(std::move(code));
  };
  CastChromeMac chrome({"Choose device", "Stop casting", "Cast to device",
                        "No devices", "Start casting", "Refresh", "Cancel",
                        "Cast code", "Find device", "Code failed", "Pause",
                        "Resume", "Seek", "Seconds", "Control failed",
                        "Cast rejected", "No route", "DRM protected", "Retry"},
                       std::move(callbacks));
  CHECK_CHROME(chrome.AttachWindow(3, (__bridge void *)window.contentView));
  chrome.SetActiveWindow(3);
  for (const bool cancel : {true, false}) {
    chrome.Render(controller.coordinator());
    [CastButton(window) performClick:nil];
    DrainAppKit();
    NSWindow *sheet = window.attachedSheet;
    CHECK_CHROME(sheet != nil);
    NSTextField *code = FindInput(sheet.contentView, @"Cast code");
    CHECK_CHROME(code != nil);
    code.stringValue = @"123456";
    [FindButton(sheet.contentView, @"Find device") performClick:nil];
    CHECK_CHROME(controller.presentation().cast_code_pending && starts == 0);
    controller.ConsumeCast({mh::ResolveCastCodeReply{
        "lookup-" + std::to_string(lookups),
        mh::Device{"phone", "Fixture phone", mh::DeviceState::kReady, true},
        std::nullopt}});
    chrome.Render(controller.coordinator());
    CHECK_CHROME(window.attachedSheet == sheet && starts == 0);
    NSButton *start = FindButton(sheet.contentView, @"Start casting");
    CHECK_CHROME(start && start.enabled);
    if (cancel) {
      [FindButton(sheet.contentView, @"Cancel") performClick:nil];
      DrainAppKit();
      CHECK_CHROME(starts == 0 && !controller.start_pending());
    } else {
      [start performClick:nil];
      DrainAppKit();
      CHECK_CHROME(starts == 1 && controller.start_pending());
      CHECK_CHROME(!controller.SelectReceiver("phone") && starts == 1);
    }
  }
  chrome.Close();
  DrainAppKit();
  [window close];
  return true;
}

} // namespace

int main() {
  @autoreleasepool {
    const bool ok = RunChromeContract() && RunCodeLookupWithController();
    if (ok)
      std::cout << "cast_chrome_mac_test passed\n";
    return ok ? 0 : 1;
  }
}
