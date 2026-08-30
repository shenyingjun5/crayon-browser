#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "crayon/browser_workflow_handoff/workflow_handoff.h"

namespace {

using crayon::browser_workflow_handoff::HandoffReason;
using crayon::browser_workflow_handoff::PresentHandoff;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

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
    if (start == std::string::npos) continue;
    const std::size_t end = line.find('"', start + 1);
    if (end != std::string::npos) keys.insert(line.substr(start + 1, end - start - 1));
  }
  *ok = true;
  return keys;
}

bool ClosedPresentation() {
  const auto view = PresentHandoff(HandoffReason::kCaptcha,
                                   "https://example.com", 59'999);
  CHECK(view.origin == "https://example.com");
  CHECK(view.reason_locale_key == "workflow.handoff.reason.captcha");
  CHECK(view.remaining_ms == 59'999);
  CHECK(view.show_continue && view.show_cancel && view.modal);
  return true;
}

bool ReasonMapping() {
  CHECK(PresentHandoff(HandoffReason::kLoginRequired, "https://a.test", 1)
            .reason_locale_key == "workflow.handoff.reason.login_required");
  CHECK(PresentHandoff(HandoffReason::kRiskCheck, "https://a.test", 1)
            .reason_locale_key == "workflow.handoff.reason.risk_check");
  CHECK(PresentHandoff(HandoffReason::kUnknown, "https://a.test", 1)
            .reason_locale_key == "workflow.handoff.reason.unknown");
  return true;
}

bool AccessibleLocaleParity() {
  const char* root = std::getenv("CRAYON_REPO_ROOT");
  CHECK(root != nullptr);
  bool ok_en = false;
  bool ok_zh = false;
  const auto en = ExtractKeys(std::string(root) + "/browser/shared-ui/locales/en-US.json", &ok_en);
  const auto zh = ExtractKeys(std::string(root) + "/browser/shared-ui/locales/zh-CN.json", &ok_zh);
  CHECK(ok_en && ok_zh && en == zh);
  const char* required[] = {
      "workflow.handoff.title", "workflow.handoff.description",
      "workflow.handoff.origin", "workflow.handoff.remaining",
      "workflow.handoff.continue", "workflow.handoff.cancel",
      "workflow.handoff.reason.captcha",
      "workflow.handoff.reason.login_required",
      "workflow.handoff.reason.risk_check",
      "workflow.handoff.reason.unknown"};
  for (const char* key : required) CHECK(en.count(key) == 1);
  return true;
}

}  // namespace

int main() {
  if (!ClosedPresentation() || !ReasonMapping() || !AccessibleLocaleParity()) {
    return EXIT_FAILURE;
  }
  std::cout << "workflow_handoff_test passed\n";
  return EXIT_SUCCESS;
}
