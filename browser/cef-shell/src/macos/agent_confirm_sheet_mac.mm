// AGT-05C: connect-level capability confirmation panel. A standalone
// non-activating NSWindow with target/action buttons — deliberately NOT
// an NSAlert sheet: the product runs CefRunMessageLoop (not NSApp run),
// and sheet/modal completion machinery does not survive that pump (an
// attached sheet even blocked application termination). One panel at a
// time; further connect events while it is up are dropped (default deny
// — a dropped client simply stays ungranted).

#import <AppKit/AppKit.h>

#include "macos/agent_confirm_sheet_mac.h"

#include <functional>
#include <memory>

@class AgentConfirmPanelController;

namespace crayon::browser::cef_shell::agent_confirm {
namespace {

std::atomic<bool> g_presented{false};
AgentConfirmPanelController* g_panel = nil;

}  // namespace
}  // namespace crayon::browser::cef_shell::agent_confirm

// Panel controller: owns the window, forwards button actions to the
// allow callback, and cleans up on any close path.
@interface AgentConfirmPanelController : NSObject <NSWindowDelegate, NSTextFieldDelegate> {
 @private
  NSWindow* window_;
  std::function<void()> allowed_;
}
- (instancetype)initWithTitle:(const std::string&)title
                       detail:(const std::string&)detail
                   allowLabel:(const std::string&)allow_label
                    denyLabel:(const std::string&)deny_label
                      allowed:(std::function<void()>)allowed;
- (void)presentInProduct;
- (void)dismiss;
@end

@implementation AgentConfirmPanelController

- (instancetype)initWithTitle:(const std::string&)title
                       detail:(const std::string&)detail
                   allowLabel:(const std::string&)allow_label
                    denyLabel:(const std::string&)deny_label
                      allowed:(std::function<void()>)allowed {
  self = [super init];
  if (!self) {
    return self;
  }
  allowed_ = std::move(allowed);

  constexpr CGFloat kPanelWidth = 360;
  constexpr CGFloat kPanelHeight = 148;
  NSRect frame = NSMakeRect(0, 0, kPanelWidth, kPanelHeight);
  window_ =
      [[NSWindow alloc] initWithContentRect:frame
                                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  [window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
  [window_ setDelegate:self];
  [window_ setReleasedWhenClosed:NO];
  [window_ setLevel:NSStatusWindowLevel];

  NSView* content = [window_ contentView];

  NSTextField* detail_field =
      [NSTextField wrappingLabelWithString:[NSString stringWithUTF8String:detail.c_str()]];
  detail_field.frame = NSMakeRect(16, 56, kPanelWidth - 32, 56);
  detail_field.editable = NO;
  [content addSubview:detail_field];

  NSButton* deny = [NSButton buttonWithTitle:[NSString stringWithUTF8String:deny_label.c_str()]
                                      target:self
                                      action:@selector(denyClicked:)];
  deny.frame = NSMakeRect(kPanelWidth - 110, 12, 94, 28);
  deny.bezelStyle = NSBezelStyleRounded;
  [content addSubview:deny];

  NSButton* allow = [NSButton buttonWithTitle:[NSString stringWithUTF8String:allow_label.c_str()]
                                       target:self
                                       action:@selector(allowClicked:)];
  allow.frame = NSMakeRect(kPanelWidth - 214, 12, 94, 28);
  allow.bezelStyle = NSBezelStyleRounded;
  allow.keyEquivalent = @"\r";
  [content addSubview:allow];
  return self;
}

- (void)presentInProduct {
  // Place above the main window without activating the app: the user
  // keeps their focus while the confirmation stays visible.
  NSWindow* parent = [NSApp mainWindow] ?: [NSApp keyWindow];
  NSRect frame = [window_ frame];
  if (parent) {
    NSRect parent_frame = parent.frame;
    frame.origin.x = parent_frame.origin.x + (parent_frame.size.width - frame.size.width) / 2;
    frame.origin.y = parent_frame.origin.y + parent_frame.size.height - frame.size.height - 40;
  }
  [window_ setFrameOrigin:frame.origin];
  [window_ orderFrontRegardless];
}

- (void)dismiss {
  allowed_ = nullptr;
  [window_ close];
}

- (void)allowClicked:(id)sender {
  static_cast<void>(sender);
  std::function<void()> callback = allowed_;
  allowed_ = nullptr;
  [window_ close];
  // The click dispatches on the main thread, which is the CEF UI thread
  // for this product; the grant is minted after the panel is gone.
  if (callback) {
    callback();
  }
}

- (void)denyClicked:(id)sender {
  static_cast<void>(sender);
  [window_ close];
}

- (void)windowWillClose:(NSNotification*)notification {
  static_cast<void>(notification);
  allowed_ = nullptr;
  crayon::browser::cef_shell::agent_confirm::g_presented.store(false);
  crayon::browser::cef_shell::agent_confirm::g_panel = nil;
}

@end

namespace crayon::browser::cef_shell::agent_confirm {

bool PresentConnectConfirmPanel(const std::string& title, const std::string& detail,
                                const std::string& allow_label,
                                const std::string& deny_label, const SheetCallbacks& callbacks) {
  if (g_presented.load()) {
    return false;
  }
  bool expected = false;
  if (!g_presented.compare_exchange_strong(expected, true)) {
    return false;
  }
  g_panel = [[AgentConfirmPanelController alloc] initWithTitle:title
                                                        detail:detail
                                                    allowLabel:allow_label
                                                     denyLabel:deny_label
                                                       allowed:callbacks.allowed];
  [g_panel presentInProduct];
  return true;
}

void DismissConnectConfirmPanel() { [g_panel dismiss]; }

}  // namespace crayon::browser::cef_shell::agent_confirm
