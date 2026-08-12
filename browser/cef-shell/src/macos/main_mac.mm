#import <Cocoa/Cocoa.h>

#include <string>

#include "include/cef_application_mac.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_library_loader.h"
#include "macos/app.h"

namespace {

NSString* const kDisplayNameKey = @"CFBundleDisplayName";
NSString* const kBundleNameKey = @"CFBundleName";

enum class ExitCode : int {
  kSuccess = 0,
  kFrameworkLoadFailed = 10,
  kProductNameMissing = 11,
  kCefInitializeFailed = 20,
};

std::string LoadProductName() {
  NSDictionary* localized_info = [[NSBundle mainBundle] localizedInfoDictionary];
  NSString* product_name = localized_info[kDisplayNameKey];
  if (product_name.length == 0U) {
    product_name =
        [[NSBundle mainBundle] objectForInfoDictionaryKey:kBundleNameKey];
  }
  if (product_name.length == 0U) {
    return {};
  }
  return std::string(product_name.UTF8String);
}

}  // namespace

@interface CrayonApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handling_send_event_;
}
@end

@interface CrayonAppDelegate : NSObject <NSApplicationDelegate> {
 @private
  CefRefPtr<crayon::browser::cef_shell::BrowserClient> browser_client_;
}

- (instancetype)initWithBrowserClient:
    (CefRefPtr<crayon::browser::cef_shell::BrowserClient>)browserClient;
- (void)tryToTerminateApplication;
@end

@implementation CrayonApplication
- (BOOL)isHandlingSendEvent {
  return handling_send_event_;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  handling_send_event_ = handlingSendEvent;
}

- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sending_event_scope;
  [super sendEvent:event];
}

- (void)terminate:(id)sender {
  static_cast<void>(sender);
  CrayonAppDelegate* delegate =
      static_cast<CrayonAppDelegate*>(self.delegate);
  [delegate tryToTerminateApplication];
}
@end

@implementation CrayonAppDelegate
- (instancetype)initWithBrowserClient:
    (CefRefPtr<crayon::browser::cef_shell::BrowserClient>)browserClient {
  self = [super init];
  if (self) {
    browser_client_ = browserClient;
  }
  return self;
}

- (void)tryToTerminateApplication {
  if (browser_client_) {
    browser_client_->CloseAllBrowsers(false);
  }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)sender {
  static_cast<void>(sender);
  return NSTerminateNow;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)application
                    hasVisibleWindows:(BOOL)hasVisibleWindows {
  static_cast<void>(application);
  static_cast<void>(hasVisibleWindows);
  if (browser_client_) {
    browser_client_->ShowMainWindow();
  }
  return NO;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)application {
  static_cast<void>(application);
  return YES;
}
@end

int main(int argc, char* argv[]) {
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    return static_cast<int>(ExitCode::kFrameworkLoadFailed);
  }

  @autoreleasepool {
    [CrayonApplication sharedApplication];
    CefMainArgs main_args(argc, argv);

    std::string product_name = LoadProductName();
    if (product_name.empty()) {
      return static_cast<int>(ExitCode::kProductNameMissing);
    }

    CefSettings settings;
#if !defined(CEF_USE_SANDBOX)
    settings.no_sandbox = true;
#endif
    settings.log_severity = LOGSEVERITY_DISABLE;

    CefRefPtr<crayon::browser::cef_shell::BrowserApp> app(
        new crayon::browser::cef_shell::BrowserApp(std::move(product_name)));
    if (!CefInitialize(main_args, settings, app, nullptr)) {
      const int cef_exit_code = CefGetExitCode();
      return cef_exit_code == 0
                 ? static_cast<int>(ExitCode::kCefInitializeFailed)
                 : cef_exit_code;
    }

    CrayonAppDelegate* delegate = [[CrayonAppDelegate alloc]
        initWithBrowserClient:app->browser_client()];
    NSApp.delegate = delegate;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];

    CefRunMessageLoop();
    NSApp.delegate = nil;
    CefShutdown();
  }
  return static_cast<int>(ExitCode::kSuccess);
}
