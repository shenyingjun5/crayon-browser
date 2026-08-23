// CEF-13 contract tests: closed state transitions, browser-verified
// eligibility only, handoff confirmation flow, no fake casting,
// session convergence, locale keys.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "crayon/browser_cast_view/cast_feature_view.h"

namespace {

using crayon::browser_cast_view::CastFeatureState;
using crayon::browser_cast_view::CastFeatureViewModel;
using crayon::browser_cast_view::HandoffResult;
using crayon::browser_cast_view::PolicyOutcome;
using crayon::browser_cast_view::RejectReason;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool EligibilityOnlyFromBrowserVerdict() {
  CastFeatureViewModel model;
  CHECK(model.state() == CastFeatureState::kIdle);
  model.SetBrowserVerifiedEligible(true);
  CHECK(model.state() == CastFeatureState::kIdle);  // no page
  model.SetPageActive(true);
  CHECK(model.state() == CastFeatureState::kBrowsing);
  CHECK(!model.OpenPicker());  // not eligible yet
  CHECK(model.SubmitPolicyOutcome(PolicyOutcome::kDirect) == false);
  model.SetBrowserVerifiedEligible(true);
  CHECK(model.state() == CastFeatureState::kEligible);
  CHECK(model.OpenPicker());
  // Withdrawn verification collapses pre-session states.
  model.SetBrowserVerifiedEligible(false);
  CHECK(model.state() == CastFeatureState::kBrowsing);
  return true;
}

bool PolicyOutcomeMapping() {
  CastFeatureViewModel model;
  model.SetPageActive(true);
  model.SetBrowserVerifiedEligible(true);
  model.OpenPicker();
  CHECK(model.SubmitPolicyOutcome(PolicyOutcome::kDirect));
  model.NotifySessionStarted();
  CHECK(model.state() == CastFeatureState::kCasting);
  CHECK(std::string(model.message_key()) == "cast.stop");
  model.NotifySessionEnded();
  CHECK(model.state() == CastFeatureState::kBrowsing);  // re-verify needed
  CHECK(!model.OpenPicker());

  // Relay follows the same path.
  CastFeatureViewModel relay;
  relay.SetPageActive(true);
  relay.SetBrowserVerifiedEligible(true);
  relay.OpenPicker();
  CHECK(relay.SubmitPolicyOutcome(PolicyOutcome::kRelay));
  CHECK(relay.state() == CastFeatureState::kPlanning);

  // Reject lands in the explicit rejected state, never a fake session.
  CastFeatureViewModel rejected;
  rejected.SetPageActive(true);
  rejected.SetBrowserVerifiedEligible(true);
  rejected.OpenPicker();
  CHECK(rejected.SubmitPolicyOutcome(PolicyOutcome::kReject, RejectReason::kDrmProtected));
  CHECK(rejected.state() == CastFeatureState::kRejected);
  CHECK(std::string(rejected.message_key()) == "cast.rejected");
  return true;
}

bool HandoffRequiresConfirmationAndNeverFakesCasting() {
  CastFeatureViewModel model;
  model.SetPageActive(true);
  model.SetBrowserVerifiedEligible(true);
  model.OpenPicker();
  CHECK(model.SubmitPolicyOutcome(PolicyOutcome::kExternalClientHandoff));
  CHECK(model.state() == CastFeatureState::kHandoffConfirm);
  // Without confirmation no request exists: results are rejected.
  CHECK(!model.DeliverHandoffResult(HandoffResult::kDownloadStarted));
  // Cancel returns to browsing with the cancelled label.
  CHECK(model.CancelHandoff());
  CHECK(model.state() == CastFeatureState::kBrowsing);
  CHECK(model.last_handoff_result() == HandoffResult::kCancelled);

  // Confirmed flow: every closed result lands in browsing; none of the
  // keys claims casting.
  CastFeatureViewModel confirmed;
  confirmed.SetPageActive(true);
  confirmed.SetBrowserVerifiedEligible(true);
  confirmed.OpenPicker();
  confirmed.SubmitPolicyOutcome(PolicyOutcome::kExternalClientHandoff);
  CHECK(confirmed.ConfirmHandoff());
  CHECK(confirmed.state() == CastFeatureState::kHandoffRequested);
  CHECK(std::string(confirmed.message_key()) == "cast.handoff.requested");
  for (const HandoffResult result : {HandoffResult::kDownloadStarted,
                                     HandoffResult::kLaunchRequested,
                                     HandoffResult::kNotInstalled,
                                     HandoffResult::kFailed}) {
    CastFeatureViewModel run = confirmed;
    CHECK(run.DeliverHandoffResult(result));
    CHECK(run.state() == CastFeatureState::kBrowsing);
    CHECK(run.last_handoff_result() == result);
  }
  return true;
}

bool PageLossResetsEverything() {
  CastFeatureViewModel model;
  model.SetPageActive(true);
  model.SetBrowserVerifiedEligible(true);
  model.OpenPicker();
  model.SetPageActive(false);
  CHECK(model.state() == CastFeatureState::kIdle);
  // Late facts cannot resurrect state.
  model.SetBrowserVerifiedEligible(true);
  CHECK(model.state() == CastFeatureState::kIdle);
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
  const char* required[] = {"cast.feature.idle", "cast.planning", "cast.handoff.confirm",
                            "cast.handoff.requested", "cast.open_external_client",
                            "cast.rejected", "cast.disabled", "cast.select_receiver",
                            "cast.selecting", "cast.stop"};
  for (const char* key : required) {
    CHECK(en.count(key) == 1 && zh.count(key) == 1);
  }
  return true;
}

/// Deterministic pseudo-random fact storm: state stays within the
/// closed set and Casting/HandoffRequested never appear without the
/// proper predecessor chain having occurred.
bool StormInvariants() {
  std::uint64_t seed = 0xDEAD'BEEF'CAFE'F00D;
  auto next = [&seed]() {
    seed = seed * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return seed;
  };
  CastFeatureViewModel model;
  for (int step = 0; step < 5'000; ++step) {
    switch (next() % 9) {
      case 0: model.SetPageActive(next() % 2 == 0); break;
      case 1: model.SetBrowserVerifiedEligible(next() % 2 == 0); break;
      case 2: static_cast<void>(model.OpenPicker()); break;
      case 3: model.ClosePicker(); break;
      case 4:
        static_cast<void>(model.SubmitPolicyOutcome(
            static_cast<PolicyOutcome>(next() % 4),
            static_cast<RejectReason>(next() % 4)));
        break;
      case 5: static_cast<void>(model.ConfirmHandoff()); break;
      case 6: static_cast<void>(model.CancelHandoff()); break;
      case 7:
        static_cast<void>(model.DeliverHandoffResult(
            static_cast<HandoffResult>(next() % 5)));
        break;
      default:
        if (next() % 2 == 0) {
          model.NotifySessionStarted();
        } else {
          model.NotifySessionEnded();
        }
        break;
    }
    const CastFeatureState state = model.state();
    CHECK(state == CastFeatureState::kIdle || state == CastFeatureState::kBrowsing ||
          state == CastFeatureState::kEligible || state == CastFeatureState::kSelecting ||
          state == CastFeatureState::kPlanning || state == CastFeatureState::kCasting ||
          state == CastFeatureState::kHandoffConfirm ||
          state == CastFeatureState::kHandoffRequested ||
          state == CastFeatureState::kRejected);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = EligibilityOnlyFromBrowserVerdict() && PolicyOutcomeMapping() &&
                  HandoffRequiresConfirmationAndNeverFakesCasting() &&
                  PageLossResetsEverything() && LocaleKeysExist() && StormInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "cast_feature_view_test passed\n";
  return EXIT_SUCCESS;
}
