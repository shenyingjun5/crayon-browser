// CEF-08 contract tests: toolbar aggregation bounds, cast-button
// closed transitions (page input cannot enable), error shell actions,
// and locale parity including the MED-19 mirror-key ban.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "crayon/browser_chrome/chrome_toolbar.h"

namespace {

using crayon::browser_chrome::CastButtonModel;
using crayon::browser_chrome::CastButtonState;
using crayon::browser_chrome::ChromeToolbar;
using crayon::browser_chrome::kMaxAddressDisplayLen;
using crayon::browser_chrome::kMaxTabTitleLen;
using crayon::browser_chrome::PageErrorAction;
using crayon::browser_chrome::PageErrorKind;
using crayon::browser_chrome::PageErrorShell;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool ToolbarAggregation() {
  ChromeToolbar toolbar;
  toolbar.SetNavigation(true, false);
  CHECK(toolbar.can_go_back() && !toolbar.can_go_forward());
  CHECK(toolbar.SetAddressDisplay("https://example.com/page"));
  CHECK(toolbar.address_display() == "https://example.com/page");
  CHECK(toolbar.SetTabTitle("Example — 100 tabs"));
  CHECK(toolbar.tab_title() == "Example — 100 tabs");
  // Defense in depth: oversize and empty inputs are rejected.
  CHECK(!toolbar.SetAddressDisplay(std::string(kMaxAddressDisplayLen + 1, 'a')));
  CHECK(!toolbar.SetAddressDisplay(""));
  CHECK(!toolbar.SetTabTitle(std::string(kMaxTabTitleLen + 1, 't')));
  return true;
}

bool CastButtonStickyDefaults() {
  CastButtonModel button;
  CHECK(button.state() == CastButtonState::kHidden);
  // No media surface: even a browser-verified fact cannot enable it.
  button.SetBrowserVerifiedEligible(true);
  CHECK(button.state() == CastButtonState::kHidden);
  button.SetMediaPresent(true);
  CHECK(button.state() == CastButtonState::kDisabled);
  // The picker cannot open from Disabled — only browser verification
  // moves the button out (page claims never do).
  CHECK(!button.OpenReceiverPicker());
  CHECK(button.state() == CastButtonState::kDisabled);
  button.SetBrowserVerifiedEligible(true);
  CHECK(button.state() == CastButtonState::kEligible);
  CHECK(button.OpenReceiverPicker());
  CHECK(button.state() == CastButtonState::kSelecting);
  button.NotifySessionStarted();
  CHECK(button.state() == CastButtonState::kCasting);
  CHECK(std::string(button.label_key()) == "cast.stop");
  CHECK(button.RequestStop());
  CHECK(!button.RequestStop());
  CHECK(button.state() == CastButtonState::kStopping);
  button.NotifySessionStopped();
  // Stopping resets to Disabled: eligibility must be re-verified.
  CHECK(button.state() == CastButtonState::kDisabled);
  // Withdrawn verification collapses pre-session states.
  CastButtonModel second;
  second.SetMediaPresent(true);
  second.SetBrowserVerifiedEligible(true);
  second.OpenReceiverPicker();
  second.SetBrowserVerifiedEligible(false);
  CHECK(second.state() == CastButtonState::kDisabled);
  // Leaving the window hides the button entirely.
  second.SetMediaPresent(false);
  CHECK(second.state() == CastButtonState::kHidden);
  return true;
}

bool ErrorShellActions() {
  PageErrorShell shell;
  CHECK(!shell.visible());
  CHECK(shell.PrimaryAction() == PageErrorAction::kNone);
  CHECK(shell.Show(PageErrorKind::kNetwork));
  CHECK(shell.visible());
  CHECK(std::string(shell.message_key()) == "error.network");
  CHECK(shell.PrimaryAction() == PageErrorAction::kReload);
  CHECK(shell.Show(PageErrorKind::kBlockedScheme));
  CHECK(shell.PrimaryAction() == PageErrorAction::kBack);
  CHECK(shell.Show(PageErrorKind::kCrash));
  CHECK(std::string(shell.message_key()) == "error.crash");
  shell.Dismiss();
  CHECK(!shell.visible());
  return true;
}

/// Minimal JSON string-key extraction for the parity contract (the
/// locale files are flat `"key": "value"` objects).
std::set<std::string> ExtractKeys(const std::string& path, bool* ok) {
  std::set<std::string> keys;
  std::ifstream input(path);
  if (!input) {
    *ok = false;
    return keys;
  }
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t start = line.find('"');
    if (start == std::string::npos) {
      continue;
    }
    const std::size_t end = line.find('"', start + 1);
    if (end == std::string::npos) {
      continue;
    }
    keys.insert(line.substr(start + 1, end - start - 1));
  }
  *ok = true;
  return keys;
}

bool LocaleParityAndMirrorBan() {
  const char* repo_root = std::getenv("CRAYON_REPO_ROOT");
  if (repo_root == nullptr) {
    std::cerr << "CRAYON_REPO_ROOT not set\n";
    return false;
  }
  bool ok_en = false;
  bool ok_zh = false;
  const std::set<std::string> en =
      ExtractKeys(std::string(repo_root) + "/browser/shared-ui/locales/en-US.json", &ok_en);
  const std::set<std::string> zh =
      ExtractKeys(std::string(repo_root) + "/browser/shared-ui/locales/zh-CN.json", &ok_zh);
  CHECK(ok_en && ok_zh);
  CHECK(en == zh);  // parity: identical key sets
  // MED-19: mirror semantics must not come back through locales.
  for (const std::string& key : en) {
    CHECK(key.find("mirror") == std::string::npos);
  }
  // Every message key the chrome models emit exists in both locales.
  const char* required[] = {"cast.select_receiver", "cast.selecting", "cast.stop",
                            "cast.disabled",         "cast.stopping",  "error.network",
                            "error.crash",           "error.blocked_scheme"};
  for (const char* key : required) {
    CHECK(en.count(key) == 1);
    CHECK(zh.count(key) == 1);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = ToolbarAggregation() && CastButtonStickyDefaults() &&
                  ErrorShellActions() && LocaleParityAndMirrorBan();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "chrome_contract_test passed\n";
  return EXIT_SUCCESS;
}
