#import <AppKit/AppKit.h>

#include <iostream>
#include <string>

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
  return container.subviews.count == 1
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
  CastChromeMac chrome(
      CastChromeStrings{"Choose device", "Stop casting", "Cast to device",
                        "No devices", "Cast", "Refresh", "Cancel"},
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

  CHECK_CHROME(coordinator.ReplaceReceivers(
      {{"phone_1", "Living room", true}, {"phone_2", "Bedroom", true}}));
  chrome.Render(coordinator);
  NSPopUpButton* popup = FindPopup(first.attachedSheet.contentView);
  CHECK_CHROME(popup && popup.numberOfItems == 2 && select_button.enabled);
  [popup selectItemAtIndex:1];
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
  CHECK_CHROME(coordinator.ApplyPolicyOutcome(cast::PolicyOutcome::kDirect));
  CHECK_CHROME(coordinator.NotifySessionStarted(9));
  chrome.Render(coordinator);
  CHECK_CHROME([first_button.toolTip isEqualToString:@"Stop casting"]);
  [first_button performClick:nil];
  CHECK_CHROME(coordinator.button().state() ==
               chrome::CastButtonState::kStopping);
  CHECK_CHROME(first.attachedSheet == nil);

  CHECK_CHROME(chrome.AttachWindow(2, (__bridge void*)second.contentView));
  chrome.SetActiveWindow(2);
  chrome.Render(coordinator);
  CHECK_CHROME(first_button.hidden);
  CHECK_CHROME(CastButton(second) && !CastButton(second).hidden &&
               !CastButton(second).enabled);
  chrome.DetachWindow(2);
  CHECK_CHROME(second.titlebarAccessoryViewControllers.count == 0);
  chrome.Close();
  CHECK_CHROME(first.titlebarAccessoryViewControllers.count == 0);
  [first close];
  [second close];
  return true;
}

}  // namespace

int main() {
  @autoreleasepool {
    const bool ok = RunChromeContract();
    if (ok)
      std::cout << "cast_chrome_mac_test passed\n";
    return ok ? 0 : 1;
  }
}
