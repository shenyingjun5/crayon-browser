#import <Cocoa/Cocoa.h>

#include <iostream>
#include <vector>

#include "macos/application_menu_mac.h"

using crayon::browser::cef_shell::macos::ApplicationCommand;
using crayon::browser::cef_shell::macos::ApplicationMenuMac;

namespace {
bool SendShortcut(NSString* key, NSEventModifierFlags modifiers) {
  NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown
      location:NSZeroPoint modifierFlags:modifiers timestamp:0
      windowNumber:0 context:nil characters:key charactersIgnoringModifiers:key
      isARepeat:NO keyCode:0];
  return [NSApp.mainMenu performKeyEquivalent:event];
}
bool Check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
}  // namespace

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    NSMenu* previous = [[NSMenu alloc] initWithTitle:@"Previous"];
    [NSApp setMainMenu:previous];
    std::vector<ApplicationCommand> received;
    {
      ApplicationMenuMac menu("Test Browser", [](const char* key) {
                                return std::string(key) == "IDS_EXIT_MAC"
                                           ? std::string("Quit $1") : std::string(key);
                              },
                              [&](auto command) { received.push_back(command); });
      if (!Check(menu.installed(), "menu must install")) return 1;
      if (!Check([NSApp.mainMenu.itemArray.firstObject.submenu.itemArray.lastObject.title
                     isEqualToString:@"Quit Test Browser"], "product name substitution")) return 1;
      for (NSString* key in @[@"t", @"o", @"s", @"l", @"w"]) {
        if (!Check(SendShortcut(key, NSEventModifierFlagCommand),
                   "native command shortcut must dispatch")) return 1;
      }
      if (!Check(received == std::vector<ApplicationCommand>{
          ApplicationCommand::kNewTab, ApplicationCommand::kOpenFile,
          ApplicationCommand::kSave, ApplicationCommand::kFocusLocation,
          ApplicationCommand::kCloseTab}, "shortcut command order")) return 1;
      if (!Check(!SendShortcut(@"t", 0), "plain typing must not dispatch")) return 1;
      if (!Check(SendShortcut(@"N", NSEventModifierFlagCommand | NSEventModifierFlagShift)
          && received.back() == ApplicationCommand::kNewIncognitoWindow,
          "shift command selects incognito")) return 1;
      if (!Check(SendShortcut(@"}", NSEventModifierFlagCommand | NSEventModifierFlagShift)
          && received.back() == ApplicationCommand::kNextTab,
          "shift command selects next tab")) return 1;
      if (!Check(SendShortcut(@"{", NSEventModifierFlagCommand | NSEventModifierFlagShift)
          && received.back() == ApplicationCommand::kPreviousTab,
          "shift command selects previous tab")) return 1;
      NSMenu* installed = NSApp.mainMenu;
      ApplicationMenuMac invalid("Test Browser", [](const char*) { return ""; },
                                  [](auto) {});
      if (!Check(!invalid.installed() && NSApp.mainMenu == installed,
                 "missing labels must preserve current menu")) return 1;
    }
    if (!Check(NSApp.mainMenu == previous, "menu must restore previous menu before target dies")) return 1;
    std::cout << "PASS native menu shortcuts, modifiers, invalid labels and disposal\n";
  }
  return 0;
}
