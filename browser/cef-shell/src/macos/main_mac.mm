#import <Cocoa/Cocoa.h>

#include <string>
#include <array>

#include "include/cef_application_mac.h"
#include "include/cef_command_line.h"
#include "include/cef_resource_bundle.h"
#include "include/wrapper/cef_library_loader.h"
#include "macos/app.h"
#include "macos/application_menu_mac.h"
#include "process/macos/ui_language_mac.h"

namespace {

enum class ExitCode : int {
  kSuccess = 0,
  kFrameworkLoadFailed = 10,
  kProductStringsMissing = 11,
  kCefInitializeFailed = 20,
};

}  // namespace

@interface CrayonApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handling_send_event_;
}
@end

@interface CrayonAppDelegate : NSObject <NSApplicationDelegate> {
 @private
  CefRefPtr<crayon::browser::cef_shell::window::TabController> tab_controller_;
}

- (instancetype)initWithTabController:
    (CefRefPtr<crayon::browser::cef_shell::window::TabController>)
        tabController;
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
- (instancetype)initWithTabController:
    (CefRefPtr<crayon::browser::cef_shell::window::TabController>)
        tabController {
  self = [super init];
  if (self) {
    tab_controller_ = tabController;
  }
  return self;
}

- (void)tryToTerminateApplication {
  if (tab_controller_) {
    tab_controller_->CloseAllBrowsers(false);
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
  if (tab_controller_) {
    tab_controller_->ShowMainWindow();
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

    const auto locale_snapshot =
        crayon::browser::cef_shell::process::ResolveMacLocaleSnapshot(
            crayon::browser::cef_shell::process::ReadMacPreferredUiLanguages());

    CefSettings settings;
#if !defined(CEF_USE_SANDBOX)
    settings.no_sandbox = true;
#endif
    settings.log_severity = LOGSEVERITY_DISABLE;
    CefString(&settings.locale) = std::string(locale_snapshot.cef_locale);
    CefString(&settings.accept_language_list) =
        std::string(locale_snapshot.accept_language_list);

    CefRefPtr<crayon::browser::cef_shell::BrowserApp> app(
        new crayon::browser::cef_shell::BrowserApp(locale_snapshot));
    if (!app->product_strings_valid()) {
      return static_cast<int>(ExitCode::kProductStringsMissing);
    }
    if (!CefInitialize(main_args, settings, app, nullptr)) {
      const int cef_exit_code = CefGetExitCode();
      return cef_exit_code == 0
                 ? static_cast<int>(ExitCode::kCefInitializeFailed)
                 : cef_exit_code;
    }

    CrayonAppDelegate* delegate = [[CrayonAppDelegate alloc]
      initWithTabController:app->tab_controller()];
    using crayon::browser::cef_shell::macos::ApplicationCommand;
    using crayon::browser::cef_shell::macos::ApplicationMenuMac;
    const crayon::browser::localization::LocaleCatalog catalog(locale_snapshot.locale);
    auto application_menu = std::make_unique<ApplicationMenuMac>(
        std::string(catalog.Find("app.title").value_or("")),
        [](const char* key) {
          const int id = cef_id_for_pack_string_name(key);
          return id > 0 ? CefResourceBundle::GetGlobal()->GetLocalizedString(id).ToString()
                        : std::string{};
        },
        [controller = app->tab_controller()](ApplicationCommand command) {
          const auto browser = controller->ActiveBrowser();
          if (!browser) return;
          if (command == ApplicationCommand::kSave && controller->HandleSaveKey(browser)) {
            return;
          }
          static constexpr std::array kCommands = {
              "IDC_ABOUT", "IDC_OPTIONS", "IDC_NEW_TAB", "IDC_NEW_WINDOW",
              "IDC_NEW_INCOGNITO_WINDOW", "IDC_OPEN_FILE", "IDC_CLOSE_TAB",
              "IDC_SAVE_PAGE", "IDC_PRINT", "IDC_FIND", "IDC_FOCUS_LOCATION",
              "IDC_RELOAD", "IDC_ZOOM_PLUS", "IDC_ZOOM_MINUS", "IDC_ZOOM_NORMAL",
              "IDC_BACK", "IDC_FORWARD", "IDC_SELECT_NEXT_TAB", "IDC_SELECT_PREVIOUS_TAB"};
          static_assert(kCommands.size() ==
                        static_cast<std::size_t>(ApplicationCommand::kPreviousTab) + 1);
          const int id = cef_id_for_command_id_name(kCommands.at(static_cast<std::size_t>(command)));
          if (id > 0 && browser->GetHost()->CanExecuteChromeCommand(id)) {
            browser->GetHost()->ExecuteChromeCommand(id, CEF_WOD_CURRENT_TAB);
          }
        });
    NSApp.delegate = delegate;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];

    CefRunMessageLoop();
    NSApp.delegate = nil;
    application_menu.reset();
    CefShutdown();
  }
  return static_cast<int>(ExitCode::kSuccess);
}
