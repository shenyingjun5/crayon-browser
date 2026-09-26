#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <functional>
#include <string>
#include <array>

#include "include/cef_application_mac.h"
#include "include/cef_command_line.h"
#include "include/cef_resource_bundle.h"
#include "include/wrapper/cef_library_loader.h"
#include "macos/agent_confirm_sheet_mac.h"
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
  std::function<void()> quit_handler_;
}

- (instancetype)initWithTabController:
    (CefRefPtr<crayon::browser::cef_shell::window::TabController>)
        tabController
                       quitHandler:(std::function<void()>)quitHandler;
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
        tabController
                       quitHandler:(std::function<void()>)quitHandler {
  self = [super init];
  if (self) {
    tab_controller_ = tabController;
    quit_handler_ = std::move(quitHandler);
  }
  return self;
}

- (void)tryToTerminateApplication {
  // Quit funnels through BrowserApp so the background service chain stops
  // before the message loop exits; force=false keeps beforeunload dialogs
  // on the user-facing menu path.
  if (quit_handler_) {
    quit_handler_();
  }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)sender {
  static_cast<void>(sender);
  // Defensive: end an open agent-confirmation panel on the deny path.
  // The product's terminate: override bypasses this delegate callback in
  // the normal quit flow (StopBackgroundServices is the main dismissal
  // path); this only fires if AppKit consults shouldTerminate directly.
  crayon::browser::cef_shell::agent_confirm::DismissConnectConfirmPanel();
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
    // Test-isolation hook: an env-specified root_cache_path lets a test
    // instance run next to the user's session without contending for the
    // ProcessSingleton lock on the default profile. Not a product feature.
    if (const char* isolated_root =
            std::getenv("CRAYON_CEF_ROOT_CACHE_PATH")) {
      CefString(&settings.root_cache_path) = std::string(isolated_root);
    }
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
      initWithTabController:app->tab_controller()
                quitHandler:[app] { app->RequestProductQuit(false); }];
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
        [app](ApplicationCommand command) {
          // PLT-SHELL-24M2: Alloy-window commands (tab strip, omnibox,
          // navigation buttons) route through the app assembly first.
          if (app->ExecuteAppCommand(command)) {
            return;
          }
          const auto controller = app->tab_controller();
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
