#import <Cocoa/Cocoa.h>

#include "macos/application_menu_mac.h"

#include <utility>

using crayon::browser::cef_shell::macos::ApplicationCommand;
using crayon::browser::cef_shell::macos::ApplicationMenuMac;

@interface CrayonMenuTarget : NSObject
- (instancetype)initWithHandler:(ApplicationMenuMac::CommandHandler)handler;
- (void)executeCommand:(NSMenuItem*)sender;
@end

@implementation CrayonMenuTarget {
  ApplicationMenuMac::CommandHandler handler_;
}
- (instancetype)initWithHandler:(ApplicationMenuMac::CommandHandler)handler {
  self = [super init];
  if (self) handler_ = std::move(handler);
  return self;
}
- (void)executeCommand:(NSMenuItem*)sender {
  if (handler_ && sender.tag >= 0 &&
      sender.tag <= static_cast<NSInteger>(ApplicationCommand::kPreviousTab)) {
    handler_(static_cast<ApplicationCommand>(sender.tag));
  }
}
@end

namespace crayon::browser::cef_shell::macos {
namespace {
NSString* NativeString(const std::string& value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding];
}
}  // namespace

struct ApplicationMenuMac::State {
  NSMenu* menu = nil;
  NSMenu* previous_menu = nil;
  CrayonMenuTarget* target = nil;
  bool installed = false;
};

ApplicationMenuMac::ApplicationMenuMac(std::string app_title,
                                       LabelResolver labels,
                                       CommandHandler command)
    : state_(std::make_unique<State>()) {
  if (app_title.empty() || !labels || !command) return;
  state_->target = [[CrayonMenuTarget alloc] initWithHandler:std::move(command)];
  state_->menu = [[NSMenu alloc] initWithTitle:NativeString(app_title)];
  bool valid = true;
  auto label = [&](const char* key) {
    std::string localized = labels(key);
    const auto product_placeholder = localized.find("$1");
    if (product_placeholder != std::string::npos) {
      localized.replace(product_placeholder, 2, app_title);
    }
    NSString* text = NativeString(localized);
    if (!text.length) valid = false;
    return text ?: @"";
  };
  auto submenu = [&](NSString* title) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                               action:nil keyEquivalent:@""];
    item.submenu = [[NSMenu alloc] initWithTitle:title];
    [state_->menu addItem:item];
    return item.submenu;
  };
  auto action = [&](NSMenu* menu, const char* key, NSString* equivalent,
                    ApplicationCommand command,
                    NSEventModifierFlags modifiers = NSEventModifierFlagCommand) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:label(key)
        action:@selector(executeCommand:) keyEquivalent:equivalent];
    item.target = state_->target;
    item.tag = static_cast<NSInteger>(command);
    item.keyEquivalentModifierMask = modifiers;
    [menu addItem:item];
  };
  auto responder = [&](NSMenu* menu, const char* key, NSString* equivalent,
                       SEL selector, id target = nil) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:label(key)
        action:selector keyEquivalent:equivalent];
    item.target = target;
    [menu addItem:item];
  };
  const auto shift_command = NSEventModifierFlagShift | NSEventModifierFlagCommand;
  NSMenu* app = submenu(NativeString(app_title));
  action(app, "IDS_ABOUT_MAC", @"", ApplicationCommand::kAbout);
  action(app, "IDS_PREFERENCES", @",", ApplicationCommand::kSettings);
  [app addItem:[NSMenuItem separatorItem]];
  responder(app, "IDS_EXIT_MAC", @"q", @selector(terminate:), NSApp);

  NSMenu* file = submenu(label("IDS_FILE_MENU_MAC"));
  action(file, "IDS_NEW_TAB_MAC", @"t", ApplicationCommand::kNewTab);
  action(file, "IDS_NEW_WINDOW_MAC", @"n", ApplicationCommand::kNewWindow);
  action(file, "IDS_NEW_INCOGNITO_WINDOW_MAC", @"N",
         ApplicationCommand::kNewIncognitoWindow, shift_command);
  [file addItem:[NSMenuItem separatorItem]];
  action(file, "IDS_OPEN_FILE_MAC", @"o", ApplicationCommand::kOpenFile);
  action(file, "IDS_CLOSE_TAB_MAC", @"w", ApplicationCommand::kCloseTab);
  action(file, "IDS_SAVE_PAGE_MAC", @"s", ApplicationCommand::kSave);
  action(file, "IDS_PRINT", @"p", ApplicationCommand::kPrint);

  NSMenu* edit = submenu(label("IDS_EDIT_MENU_MAC"));
  responder(edit, "IDS_EDIT_UNDO_MAC", @"z", @selector(undo:));
  responder(edit, "IDS_EDIT_REDO_MAC", @"Z", @selector(redo:));
  [edit addItem:[NSMenuItem separatorItem]];
  responder(edit, "IDS_CUT_MAC", @"x", @selector(cut:));
  responder(edit, "IDS_COPY_MAC", @"c", @selector(copy:));
  responder(edit, "IDS_PASTE_MAC", @"v", @selector(paste:));
  responder(edit, "IDS_EDIT_SELECT_ALL_MAC", @"a", @selector(selectAll:));
  action(edit, "IDS_EDIT_FIND_MAC", @"f", ApplicationCommand::kFind);

  NSMenu* view = submenu(label("IDS_VIEW_MENU_MAC"));
  action(view, "IDS_OPEN_LOCATION_MAC", @"l", ApplicationCommand::kFocusLocation);
  action(view, "IDS_RELOAD_MENU_MAC", @"r", ApplicationCommand::kReload);
  action(view, "IDS_TEXT_BIGGER_MAC", @"+", ApplicationCommand::kZoomIn);
  action(view, "IDS_TEXT_SMALLER_MAC", @"-", ApplicationCommand::kZoomOut);
  action(view, "IDS_TEXT_DEFAULT_MAC", @"0", ApplicationCommand::kZoomReset);
  action(view, "IDS_HISTORY_BACK_MAC", @"[", ApplicationCommand::kBack);
  action(view, "IDS_HISTORY_FORWARD_MAC", @"]", ApplicationCommand::kForward);

  NSMenu* window = submenu(label("IDS_WINDOW_MENU_MAC"));
  responder(window, "IDS_MINIMIZE_WINDOW_MAC", @"m", @selector(performMiniaturize:));
  action(window, "IDS_NEXT_TAB_MAC", @"}", ApplicationCommand::kNextTab, shift_command);
  action(window, "IDS_PREV_TAB_MAC", @"{", ApplicationCommand::kPreviousTab, shift_command);
  if (!valid) return;
  state_->previous_menu = NSApp.mainMenu;
  [NSApp setMainMenu:state_->menu];
  state_->installed = true;
}

ApplicationMenuMac::~ApplicationMenuMac() {
  if (state_->installed && NSApp.mainMenu == state_->menu) {
    [state_->menu removeAllItems];
    [NSApp setMainMenu:state_->previous_menu];
  }
}

bool ApplicationMenuMac::installed() const noexcept { return state_->installed; }

}  // namespace crayon::browser::cef_shell::macos
