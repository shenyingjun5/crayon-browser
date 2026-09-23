// PLT-SHELL-24M2FIX-C9: settings panel. A standalone non-activating NSWindow
// with one editable field and target/action buttons -- deliberately NOT an
// NSAlert sheet, for the reason recorded in agent_confirm_sheet_mac.mm (the
// product pumps the message loop with CefRunMessageLoop, which sheet/modal
// completion does not survive). One panel at a time.

#import <AppKit/AppKit.h>

#include "macos/settings_panel_mac.h"

#include <atomic>
#include <functional>
#include <memory>

@class SettingsPanelController;

namespace crayon::browser::cef_shell::settings_panel {
namespace {

std::atomic<bool> g_presented{false};
SettingsPanelController* g_panel = nil;

}  // namespace
}  // namespace crayon::browser::cef_shell::settings_panel

@interface SettingsPanelController : NSObject <NSWindowDelegate> {
 @private
  NSWindow* window_;
  NSTextField* field_;
  std::function<void(std::string)> save_;
}
- (instancetype)initWithTitle:(const std::string&)title
                   fieldLabel:(const std::string&)field_label
                   fieldValue:(const std::string&)field_value
                    saveLabel:(const std::string&)save_label
                  cancelLabel:(const std::string&)cancel_label
                         save:(std::function<void(std::string)>)save;
- (void)presentInProductWithParent:(NSWindow*)parent;
- (void)dismiss;
@end

@implementation SettingsPanelController

- (instancetype)initWithTitle:(const std::string&)title
                   fieldLabel:(const std::string&)field_label
                   fieldValue:(const std::string&)field_value
                    saveLabel:(const std::string&)save_label
                  cancelLabel:(const std::string&)cancel_label
                         save:(std::function<void(std::string)>)save {
  self = [super init];
  if (!self) {
    return self;
  }
  save_ = std::move(save);

  constexpr CGFloat kPanelWidth = 460;
  constexpr CGFloat kPanelHeight = 140;
  window_ = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, kPanelWidth, kPanelHeight)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
  [window_ setDelegate:self];
  [window_ setReleasedWhenClosed:NO];
  [window_ setLevel:NSStatusWindowLevel];

  NSView* content = [window_ contentView];

  NSTextField* label =
      [NSTextField labelWithString:[NSString stringWithUTF8String:field_label.c_str()]];
  label.frame = NSMakeRect(16, kPanelHeight - 44, kPanelWidth - 32, 20);
  [content addSubview:label];

  // Editable single-line field, pre-filled with the current value so the user
  // edits rather than retypes.
  field_ = [[NSTextField alloc] initWithFrame:NSMakeRect(16, kPanelHeight - 76, kPanelWidth - 32, 24)];
  field_.stringValue = [NSString stringWithUTF8String:field_value.c_str()];
  field_.editable = YES;
  field_.selectable = YES;
  field_.bezeled = YES;
  field_.bezelStyle = NSTextFieldSquareBezel;
  [content addSubview:field_];
  [window_ makeFirstResponder:field_];

  NSButton* cancel_button = [NSButton buttonWithTitle:[NSString stringWithUTF8String:cancel_label.c_str()]
                                        target:self
                                        action:@selector(cancelClicked:)];
  cancel_button.frame = NSMakeRect(kPanelWidth - 110, 12, 94, 28);
  cancel_button.bezelStyle = NSBezelStyleRounded;
  [content addSubview:cancel_button];

  NSButton* save_button = [NSButton buttonWithTitle:[NSString stringWithUTF8String:save_label.c_str()]
                                      target:self
                                      action:@selector(saveClicked:)];
  save_button.frame = NSMakeRect(kPanelWidth - 214, 12, 94, 28);
  save_button.bezelStyle = NSBezelStyleRounded;
  save_button.keyEquivalent = @"\r";
  [content addSubview:save_button];
  return self;
}

- (void)presentInProductWithParent:(NSWindow*)parent {
  NSRect frame = [window_ frame];
  NSWindow* anchor = parent ?: ([NSApp mainWindow] ?: [NSApp keyWindow]);
  if (anchor) {
    NSRect parent_frame = anchor.frame;
    frame.origin.x = parent_frame.origin.x + (parent_frame.size.width - frame.size.width) / 2;
    frame.origin.y = parent_frame.origin.y + parent_frame.size.height - frame.size.height - 60;
  }
  [window_ setFrameOrigin:frame.origin];
  // orderFrontRegardless: the panel must be usable without stealing the app's
  // activation state, like the confirmation panel.
  [window_ orderFrontRegardless];
  [window_ makeKeyWindow];
}

- (void)dismiss {
  save_ = nullptr;
  [window_ close];
}

- (void)saveClicked:(id)sender {
  static_cast<void>(sender);
  std::function<void(std::string)> callback = save_;
  const std::string value = field_ ? std::string(field_.stringValue.UTF8String ?: "") : std::string{};
  save_ = nullptr;
  [window_ close];
  // The click dispatches on the main thread, which is the CEF UI thread here.
  if (callback) {
    callback(value);
  }
}

- (void)cancelClicked:(id)sender {
  static_cast<void>(sender);
  save_ = nullptr;
  [window_ close];
}

- (void)windowWillClose:(NSNotification*)notification {
  static_cast<void>(notification);
  save_ = nullptr;
  field_ = nil;
  crayon::browser::cef_shell::settings_panel::g_presented.store(false);
  crayon::browser::cef_shell::settings_panel::g_panel = nil;
}

@end

namespace crayon::browser::cef_shell::settings_panel {

bool PresentSettingsPanel(const std::string& title, const std::string& field_label,
                          const std::string& field_value,
                          const std::string& save_label,
                          const std::string& cancel_label,
                          const PanelCallbacks& callbacks) {
  if (g_presented.load()) {
    return false;
  }
  bool expected = false;
  if (!g_presented.compare_exchange_strong(expected, true)) {
    return false;
  }
  g_panel = [[SettingsPanelController alloc] initWithTitle:title
                                                fieldLabel:field_label
                                                fieldValue:field_value
                                                 saveLabel:save_label
                                               cancelLabel:cancel_label
                                                      save:callbacks.save];
  NSWindow* parent = nil;
  for (NSWindow* candidate in [NSApp windows]) {
    if (candidate != g_panel) {
      parent = candidate;
      break;
    }
  }
  [g_panel presentInProductWithParent:parent];
  return true;
}

void DismissSettingsPanel() { [g_panel dismiss]; }

}  // namespace crayon::browser::cef_shell::settings_panel
