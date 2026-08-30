#import <Cocoa/Cocoa.h>

#include <unistd.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <variant>

#include "browser/window/tab_controller.h"
#include "include/cef_app.h"
#include "include/cef_application_mac.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"

#ifndef CRAYON_SNAPSHOT_TEST_HELPER_PATH
#error "CRAYON_SNAPSHOT_TEST_HELPER_PATH must be defined"
#endif

namespace {

// Test-only Browser process; Renderer execution uses the product Helper bundle.
class SnapshotFixtureApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit SnapshotFixtureApp(std::string fixture_url) : fixture_url_(std::move(fixture_url)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

  CefRefPtr<CefClient> GetDefaultClient() override {
    return controller_ ? controller_->client() : nullptr;
  }

  void OnBeforeCommandLineProcessing(const CefString &process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    static_cast<void>(process_type);
    command_line->AppendSwitch("use-mock-keychain");
    command_line->AppendSwitchWithValue("enable-logging", "stderr");
  }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    std::cerr << "snapshot_fixture context_initialized\n";
    controller_ = new crayon::browser::cef_shell::window::TabController(
        fixture_url_, [this](CefRefPtr<CefBrowser> browser) {
          std::cerr << "snapshot_fixture browser_created\n";
          if (browser && browser->GetMainFrame()) {
            browser->GetMainFrame()->LoadURL(fixture_url_);
          }
        });
    controller_->SetPageLoadCompletedCallback([this](CefRefPtr<CefBrowser> browser) {
      std::cerr << "snapshot_fixture load_completed\n";
      if (request_started_) return;
      request_started_ = true;
      request_id_ = controller_->StartPageSnapshot(browser);
      std::cerr << "snapshot_fixture request_started=" << request_id_.has_value() << '\n';
      if (!request_id_) Finish(false);
    });
    controller_->SetPageSnapshotEventsReadyCallback([this]() {
      const auto events = controller_->DrainPageSnapshots(16);
      std::cerr << "snapshot_fixture events=" << events.size() << '\n';
      for (const auto &event : events) {
        if (const auto *chunk = std::get_if<crayon::browser_engine::SnapshotChunk>(&event)) {
          ++chunk_count_;
          for (const auto &fact : chunk->facts) {
            if (fact.text == "Visible fixture heading") saw_heading_ = true;
            if (fact.text.find("hidden fixture secret") != std::string::npos) {
              saw_hidden_ = true;
            }
            if (fact.kind == crayon::browser_engine::SnapshotFactKind::kTable &&
                fact.table_columns == 2 && fact.table_cells.size() == 4) {
              saw_table_ = true;
            }
            if (fact.kind == crayon::browser_engine::SnapshotFactKind::kListItem &&
                fact.text == "ordered fixture item" && fact.ordered && fact.depth == 1 &&
                fact.ordinal == 3) {
              saw_ordered_list_ = true;
            }
          }
        } else if (const auto *terminal =
                       std::get_if<crayon::browser_engine::SnapshotTerminal>(&event)) {
          Finish(terminal->status == crayon::browser_engine::SnapshotTerminalStatus::kCompleted &&
                 chunk_count_ >= 2 && saw_heading_ && saw_table_ && saw_ordered_list_ &&
                 !saw_hidden_);
        }
      }
    });
    if (!controller_->CreateMainWindow()) Finish(false);
  }

  bool passed() const { return passed_; }

 private:
  void Finish(bool passed) {
    if (finished_) return;
    finished_ = true;
    passed_ = passed;
    std::cout << "snapshot_fixture terminal=" << (passed ? "completed" : "failed")
              << " chunks=" << chunk_count_ << " heading=" << saw_heading_
              << " table=" << saw_table_ << " ordered_list=" << saw_ordered_list_
              << " hidden=" << saw_hidden_ << '\n';
    if (controller_) controller_->CloseAllBrowsers(true);
  }

  const std::string fixture_url_;
  CefRefPtr<crayon::browser::cef_shell::window::TabController> controller_;
  std::optional<crayon::browser_engine::SnapshotRequestId> request_id_;
  std::size_t chunk_count_ = 0;
  bool request_started_ = false;
  bool saw_heading_ = false;
  bool saw_table_ = false;
  bool saw_ordered_list_ = false;
  bool saw_hidden_ = false;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(SnapshotFixtureApp);
  DISALLOW_COPY_AND_ASSIGN(SnapshotFixtureApp);
};

}  // namespace

@interface SnapshotTestApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handling_send_event_;
}
@end

@implementation SnapshotTestApplication
- (BOOL)isHandlingSendEvent {
  return handling_send_event_;
}
- (void)setHandlingSendEvent:(BOOL)value {
  handling_send_event_ = value;
}
- (void)sendEvent:(NSEvent *)event {
  CefScopedSendingEvent sending_event_scope;
  [super sendEvent:event];
}
@end

int main(int argc, char *argv[]) {
  if (argc != 2) return 2;
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) return 3;
  @autoreleasepool {
    [SnapshotTestApplication sharedApplication];
    CefMainArgs main_args(argc, argv);
    CefSettings settings;
    settings.no_sandbox = true;
    settings.log_severity = LOGSEVERITY_WARNING;
    const std::filesystem::path cache_path =
        std::filesystem::temp_directory_path() /
        ("crayon-page-snapshot-integration-" + std::to_string(getpid()));
    CefString(&settings.root_cache_path).FromString(cache_path.string());
    CefString(&settings.browser_subprocess_path).FromString(CRAYON_SNAPSHOT_TEST_HELPER_PATH);
    CefRefPtr<SnapshotFixtureApp> app(new SnapshotFixtureApp(argv[1]));
    if (!CefInitialize(main_args, settings, app, nullptr)) return 4;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];
    CefRunMessageLoop();
    const bool passed = app->passed();
    CefShutdown();
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_path, cleanup_error);
    return passed ? 0 : 1;
  }
}
