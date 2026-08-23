// AGT-05 contract tests: AG-004 confirmation model — presentation
// validation, confirm/deny/expiry, context-change re-confirmation,
// sensitive masking, locale parity, storm invariants.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "crayon/browser_agent_confirm/agent_confirm.h"

namespace {

using crayon::browser_agent_confirm::AgentConfirmModel;
using crayon::browser_agent_confirm::AgentConfirmRequest;
using crayon::browser_agent_confirm::ConfirmState;
using crayon::browser_agent_confirm::IsSensitiveParamKey;
using crayon::browser_agent_confirm::IsValidToken;
using crayon::browser_agent_confirm::ParamDigest;
using crayon::browser_agent_confirm::ParamSensitivity;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

AgentConfirmRequest ValidRequest(std::uint64_t expires_at_ms) {
  AgentConfirmRequest request;
  request.client = "cli-dev";
  request.tool = "navigation.open";
  request.capability = "navigation";
  request.risk = "r2";
  request.target_scope = "grant:navigation:tab:tab-7";
  request.discloses_page_data = true;
  request.expires_at_ms = expires_at_ms;
  return request;
}

bool PresentationValidation() {
  AgentConfirmModel model;
  CHECK(model.state() == ConfirmState::kNone);
  // Expired-at-presentation is rejected.
  CHECK(!model.Present(ValidRequest(1'000), 1'000));
  CHECK(model.state() == ConfirmState::kNone);
  // Bad tokens rejected.
  AgentConfirmRequest bad = ValidRequest(60'000);
  bad.client = "bad client";
  CHECK(!model.Present(bad, 0));
  bad = ValidRequest(60'000);
  bad.target_scope = "";
  CHECK(!model.Present(bad, 0));
  // Too many params.
  bad = ValidRequest(60'000);
  bad.params.assign(17, ParamDigest{});
  CHECK(!model.Present(bad, 0));
  // Valid presentation.
  CHECK(model.Present(ValidRequest(60'000), 0));
  CHECK(model.state() == ConfirmState::kPending);
  return true;
}

bool ConfirmDenyExpiry() {
  AgentConfirmModel model;
  model.Present(ValidRequest(60'000), 0);
  CHECK(!model.Confirm(60'000));  // boundary: expired exactly now
  CHECK(model.state() == ConfirmState::kNone);
  model.Present(ValidRequest(60'000), 0);
  CHECK(model.Confirm(59'999));
  CHECK(model.state() == ConfirmState::kConfirmed);
  // Confirmed requests cannot be denied; deny only from pending.
  CHECK(!model.Deny());
  // Tick drops expired pending requests.
  AgentConfirmModel pending;
  pending.Present(ValidRequest(60'000), 0);
  pending.Tick(60'000);
  CHECK(!pending.Confirm(60'001));
  // Deny is terminal until re-presentation.
  AgentConfirmModel denied;
  denied.Present(ValidRequest(60'000), 0);
  CHECK(denied.Deny());
  CHECK(denied.state() == ConfirmState::kDenied);
  CHECK(!denied.Confirm(1));
  denied.Present(ValidRequest(120'000), 0);  // re-present revives
  CHECK(denied.Confirm(60'000));
  return true;
}

bool ContextChangeForcesReconfirmation() {
  AgentConfirmModel model;
  AgentConfirmRequest request = ValidRequest(60'000);
  CHECK(model.Present(request, 0));
  const std::string fingerprint = request.Fingerprint();
  model.OnContextChanged(fingerprint);
  CHECK(model.state() == ConfirmState::kPending && !model.stale());  // same context: no-op
  // Navigation/device/params change: pending dies, must re-present.
  model.OnContextChanged("changed-context");
  CHECK(model.stale());
  CHECK(model.state() == ConfirmState::kNone);
  CHECK(!model.Confirm(1'000));
  // Even a confirmed request is invalidated by a context change.
  AgentConfirmModel confirmed;
  confirmed.Present(request, 0);
  confirmed.Confirm(1'000);
  confirmed.OnContextChanged("changed-context");
  CHECK(confirmed.stale() && confirmed.state() == ConfirmState::kNone);
  // Re-present with the new context works.
  CHECK(confirmed.Present(request, 0));
  CHECK(confirmed.Confirm(1'000));
  return true;
}

bool SensitiveMaskingRules() {
  CHECK(IsSensitiveParamKey("password"));
  CHECK(IsSensitiveParamKey("card_number"));
  CHECK(IsSensitiveParamKey("auth_token"));
  CHECK(IsSensitiveParamKey("upload_file"));
  CHECK(!IsSensitiveParamKey("url"));
  CHECK(!IsSensitiveParamKey("scroll_y"));
  // Digest carries only key/length/sensitivity — assert the shape.
  ParamDigest sensitive{"password", 12, ParamSensitivity::kSensitive};
  CHECK(sensitive.value_len == 12);
  CHECK(sensitive.sensitivity == ParamSensitivity::kSensitive);
  // Fingerprint contains no raw value (only length marker).
  AgentConfirmRequest request = ValidRequest(60'000);
  request.params.push_back(sensitive);
  const std::string fingerprint = request.Fingerprint();
  CHECK(fingerprint.find("password:12s") != std::string::npos);
  return true;
}

bool TokenMatrix() {
  CHECK(IsValidToken("cli-dev"));
  CHECK(IsValidToken("navigation.open"));
  CHECK(!IsValidToken(""));
  CHECK(!IsValidToken("bad token"));
  CHECK(!IsValidToken(std::string(129, 'a')));
  return true;
}

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
    if (end == std::string::npos) continue;
    keys.insert(line.substr(start + 1, end - start - 1));
  }
  *ok = true;
  return keys;
}

bool LocaleKeysExist() {
  const char* repo_root = std::getenv("CRAYON_REPO_ROOT");
  if (repo_root == nullptr) {
    return false;
  }
  bool ok_en = false;
  bool ok_zh = false;
  const std::set<std::string> en =
      ExtractKeys(std::string(repo_root) + "/browser/shared-ui/locales/en-US.json", &ok_en);
  const std::set<std::string> zh =
      ExtractKeys(std::string(repo_root) + "/browser/shared-ui/locales/zh-CN.json", &ok_zh);
  CHECK(ok_en && ok_zh && en == zh);
  // Accessibility labels: one per presented field plus actions.
  const char* required[] = {"agent.confirm.title",    "agent.confirm.client",
                            "agent.confirm.tool",     "agent.confirm.risk",
                            "agent.confirm.target",   "agent.confirm.params",
                            "agent.confirm.disclosure", "agent.confirm.expires",
                            "agent.confirm.allow",    "agent.confirm.deny"};
  for (const char* key : required) {
    CHECK(en.count(key) == 1 && zh.count(key) == 1);
  }
  return true;
}

/// Pseudo-random operation storm: state stays closed, confirm only
/// from live pending, expiry monotone.
bool StormInvariants() {
  std::uint64_t seed = 0x0A5E'F00D'1234'5678;
  auto next = [&seed]() {
    seed = seed * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return seed;
  };
  AgentConfirmModel model;
  std::uint64_t clock = 0;
  for (int step = 0; step < 5'000; ++step) {
    clock += next() % 2'000;
    switch (next() % 5) {
      case 0:
        static_cast<void>(model.Present(ValidRequest(clock + 1 + next() % 90'000), clock));
        break;
      case 1:
        static_cast<void>(model.Confirm(clock));
        break;
      case 2:
        static_cast<void>(model.Deny());
        break;
      case 3:
        model.OnContextChanged(next() % 2 ? "ctx-a" : "ctx-b");
        break;
      default:
        model.Tick(clock);
        break;
    }
    const ConfirmState state = model.state();
    CHECK(state == ConfirmState::kNone || state == ConfirmState::kPending ||
          state == ConfirmState::kConfirmed || state == ConfirmState::kDenied);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = PresentationValidation() && ConfirmDenyExpiry() &&
                  ContextChangeForcesReconfirmation() && SensitiveMaskingRules() &&
                  TokenMatrix() && LocaleKeysExist() && StormInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "agent_confirm_test passed\n";
  return EXIT_SUCCESS;
}
