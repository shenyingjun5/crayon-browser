// UX-015 contract tests: context menu minimization, dangerous
// scheme/path rejection, clipboard bounds and controlled local-file
// entry.
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_context_menu/context_menu.h"

namespace {

using crayon::browser_context_menu::ActionSource;
using crayon::browser_context_menu::ClipboardGuard;
using crayon::browser_context_menu::ContextMenuController;
using crayon::browser_context_menu::ContextCommand;
using crayon::browser_context_menu::ContextKind;
using crayon::browser_context_menu::ContextUrlAction;
using crayon::browser_context_menu::IsAvailableIn;
using crayon::browser_context_menu::IsOpenableScheme;
using crayon::browser_context_menu::LocalFileEntryGuard;
using crayon::browser_context_menu::ValidateContextUrl;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool AvailabilityMatrixMinimizes() {
  // Links offer link commands only.
  CHECK(IsAvailableIn(ContextCommand::kOpenLink, ContextKind::kLink));
  CHECK(IsAvailableIn(ContextCommand::kCopyLinkText, ContextKind::kLink));
  CHECK(!IsAvailableIn(ContextCommand::kCopyImage, ContextKind::kLink));
  CHECK(!IsAvailableIn(ContextCommand::kCopySelection, ContextKind::kLink));
  // Images offer image commands only.
  CHECK(IsAvailableIn(ContextCommand::kDownloadImage, ContextKind::kImage));
  CHECK(!IsAvailableIn(ContextCommand::kOpenLink, ContextKind::kImage));
  CHECK(!IsAvailableIn(ContextCommand::kPrintPage, ContextKind::kImage));
  // Selection offers copy/search/paste, never link/image commands.
  CHECK(IsAvailableIn(ContextCommand::kSearchSelection, ContextKind::kSelection));
  CHECK(IsAvailableIn(ContextCommand::kPaste, ContextKind::kSelection));
  CHECK(!IsAvailableIn(ContextCommand::kSaveLinkAs, ContextKind::kSelection));
  // Plain page offers save/print/paste only.
  CHECK(IsAvailableIn(ContextCommand::kSavePageAs, ContextKind::kPage));
  CHECK(IsAvailableIn(ContextCommand::kPrintPage, ContextKind::kPage));
  CHECK(!IsAvailableIn(ContextCommand::kCopyLinkUrl, ContextKind::kPage));
  return true;
}

bool MenuLifecycleAndDispatch() {
  ContextMenuController menu;
  CHECK(!menu.open());
  CHECK(menu.VisibleCommands().empty());
  CHECK(!menu.Execute(ContextCommand::kPrintPage));  // closed menu
  CHECK(menu.Open(ContextKind::kLink));
  CHECK(menu.open());
  // Minimized set: exactly the five link commands.
  CHECK(menu.VisibleCommands().size() == 5);
  CHECK(menu.Execute(ContextCommand::kOpenLinkInNewTab));
  CHECK(menu.last_command() == ContextCommand::kOpenLinkInNewTab);
  // Hidden commands cannot be reached by a page.
  CHECK(!menu.Execute(ContextCommand::kDownloadImage));
  CHECK(!menu.Execute(ContextCommand::kPrintPage));
  menu.Close();
  CHECK(!menu.open());
  CHECK(!menu.Execute(ContextCommand::kOpenLink));
  // Unknown enum values are rejected on open.
  CHECK(!menu.Open(static_cast<ContextKind>(42)));
  return true;
}

bool SchemeGuardMatrix() {
  std::string scheme;
  CHECK(ValidateContextUrl("https://example.com/a?b=1", &scheme) == ContextUrlAction::kAllowed);
  CHECK(scheme == "https");
  CHECK(ValidateContextUrl("http://example.com", &scheme) == ContextUrlAction::kAllowed);
  CHECK(IsOpenableScheme("http") && IsOpenableScheme("https"));
  CHECK(ValidateContextUrl("javascript:alert(1)", &scheme) == ContextUrlAction::kSchemeRejected);
  CHECK(ValidateContextUrl("data:text/html,hi", &scheme) == ContextUrlAction::kSchemeRejected);
  CHECK(ValidateContextUrl("file:///etc/passwd", &scheme) == ContextUrlAction::kSchemeRejected);
  CHECK(ValidateContextUrl("vbscript:x", &scheme) == ContextUrlAction::kSchemeRejected);
  CHECK(ValidateContextUrl("blob:https://x", &scheme) == ContextUrlAction::kSchemeRejected);
  CHECK(ValidateContextUrl("", &scheme) == ContextUrlAction::kMalformed);
  CHECK(ValidateContextUrl("no-scheme-here", &scheme) == ContextUrlAction::kMalformed);
  CHECK(ValidateContextUrl(std::string(2049, 'a'), &scheme) == ContextUrlAction::kMalformed);
  return true;
}

bool ClipboardGuardBoundsAndSource() {
  ClipboardGuard clipboard;
  CHECK(clipboard.CopyText("hello", ActionSource::kPage) == false);  // page cannot write
  CHECK(!clipboard.has_pending_write());
  CHECK(clipboard.CopyText("hello", ActionSource::kUserCommand));
  CHECK(clipboard.has_pending_write());
  CHECK(clipboard.pending_text() == "hello");
  clipboard.AcknowledgeWrite();
  CHECK(!clipboard.has_pending_write());
  CHECK(clipboard.pending_text().empty());
  CHECK(!clipboard.CopyText(std::string(1'048'577, 'x'), ActionSource::kUserCommand));
  CHECK(clipboard.CopyText(std::string(1'048'576, 'x'), ActionSource::kUserCommand));
  return true;
}

bool LocalFileEntryTwoStepFlow() {
  LocalFileEntryGuard entry;
  CHECK(LocalFileEntryGuard::IsValidEntryName("report 2026.pdf"));
  CHECK(LocalFileEntryGuard::IsValidEntryName("a.b_c-d"));
  CHECK(!LocalFileEntryGuard::IsValidEntryName(""));
  CHECK(!LocalFileEntryGuard::IsValidEntryName(".hidden"));
  CHECK(!LocalFileEntryGuard::IsValidEntryName(".."));
  CHECK(!LocalFileEntryGuard::IsValidEntryName("sub/file.txt"));
  CHECK(!LocalFileEntryGuard::IsValidEntryName("windows\\file.txt"));
  CHECK(!LocalFileEntryGuard::IsValidEntryName("bad*name"));
  CHECK(!LocalFileEntryGuard::IsValidEntryName(std::string(300, 'a')));
  // Page-initiated opens are impossible.
  CHECK(!entry.RequestOpen("report.pdf", ActionSource::kPage));
  CHECK(!entry.pending());
  CHECK(entry.RequestOpen("report.pdf", ActionSource::kUserCommand));
  CHECK(entry.pending());
  CHECK(!entry.RequestOpen("other.pdf", ActionSource::kUserCommand));  // one at a time
  entry.CancelOpen();
  CHECK(!entry.pending());
  CHECK(entry.pending_name().empty());
  CHECK(!entry.ConfirmOpen());  // nothing pending
  CHECK(entry.RequestOpen("report.pdf", ActionSource::kUserCommand));
  CHECK(entry.ConfirmOpen());
  CHECK(!entry.pending());
  return true;
}

}  // namespace

int main() {
  const bool ok = AvailabilityMatrixMinimizes() && MenuLifecycleAndDispatch() &&
                  SchemeGuardMatrix() && ClipboardGuardBoundsAndSource() &&
                  LocalFileEntryTwoStepFlow();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "context_menu_contract_test passed\n";
  return EXIT_SUCCESS;
}
