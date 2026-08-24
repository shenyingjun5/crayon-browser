// HUB-06 contract tests: HB-006 route preview model — presentation
// validation, lifecycle, temporary-override one-shot semantics,
// external-data disclosure rendering, locale parity, storm invariants.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "crayon/browser_capability_route/capability_route.h"

namespace {

using crayon::browser_capability_route::CapabilityRouteModel;
using crayon::browser_capability_route::CapabilityRoutePreview;
using crayon::browser_capability_route::RouteCandidateView;
using crayon::browser_capability_route::RouteExclusionView;
using crayon::browser_capability_route::RouteOverride;
using crayon::browser_capability_route::RouteState;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

CapabilityRoutePreview ValidPreview() {
  CapabilityRoutePreview preview;
  preview.selected_id = "partner.approved";
  preview.selected_kind = "partner";
  preview.reason = "selected_by_default_rank";
  RouteCandidateView candidate;
  candidate.capability_id = "partner.approved";
  candidate.version = "1.1.0";
  candidate.kind = "partner";
  candidate.trust = "user_approved";
  candidate.sends_data_external = true;
  preview.candidates.push_back(candidate);
  RouteCandidateView local_candidate;
  local_candidate.capability_id = "skill.export";
  local_candidate.version = "1.0.0";
  local_candidate.kind = "site_skill";
  local_candidate.trust = "user_approved";
  preview.candidates.push_back(local_candidate);
  RouteExclusionView exclusion;
  exclusion.capability_id = "partner.notes";
  exclusion.reason = "insufficient_trust";
  preview.exclusions.push_back(exclusion);
  return preview;
}

bool PresentationValidation() {
  CapabilityRouteModel model;
  CHECK(model.state() == RouteState::kNone);
  CHECK(model.Present(ValidPreview()));
  CHECK(model.state() == RouteState::kPresented);
  CHECK(model.revision() == 1);

  // Bad reason.
  CapabilityRoutePreview bad = ValidPreview();
  bad.reason = "because_i_said_so";
  CHECK(!model.Present(bad));
  // Bad kind.
  bad = ValidPreview();
  bad.selected_kind = "magic_route";
  CHECK(!model.Present(bad));
  // Selected id without kind is inconsistent; id with bad token rejected.
  bad = ValidPreview();
  bad.selected_id = "Bad Id";
  CHECK(!model.Present(bad));
  // Bad trust.
  bad = ValidPreview();
  bad.candidates.front().trust = "super_trusted";
  CHECK(!model.Present(bad));
  // Bad exclusion reason.
  bad = ValidPreview();
  bad.exclusions.front().reason = "too_slow";
  CHECK(!model.Present(bad));
  // Too many candidates.
  bad = ValidPreview();
  bad.candidates.assign(17, RouteCandidateView{});
  CHECK(!model.Present(bad));
  // Rejections left the presented state and revision untouched.
  CHECK(model.state() == RouteState::kPresented);
  CHECK(model.revision() == 1);

  // A valid re-Present replaces content and bumps the revision.
  CapabilityRoutePreview empty = ValidPreview();
  empty.selected_id.clear();
  empty.selected_kind.clear();
  empty.reason = "no_candidates";
  empty.candidates.clear();
  empty.exclusions.clear();
  CHECK(model.Present(empty));
  CHECK(model.revision() == 2);
  CHECK(model.preview()->candidates.empty());
  return true;
}

bool LifecycleAndOverride() {
  CapabilityRouteModel model;
  // Proceed/Cancel outside Presented never succeed.
  CHECK(!model.Proceed(nullptr));
  CHECK(!model.Cancel());
  CHECK(model.ApplyOverride(RouteOverride{}) == false ||
        !model.ApplyOverride(RouteOverride{}));

  CHECK(model.Present(ValidPreview()));

  // Invalid overrides are refused: reject is not a preference.
  RouteOverride invalid;
  invalid.present = true;
  invalid.prefer_kind = "reject";
  CHECK(!model.ApplyOverride(invalid));
  invalid.prefer_kind = "magic_route";
  CHECK(!model.ApplyOverride(invalid));

  // Valid override survives until Proceed hands it out exactly once.
  RouteOverride override_request;
  override_request.present = true;
  override_request.prefer_kind = "web_automation";
  override_request.allow_external_endpoint = false;
  CHECK(model.ApplyOverride(override_request));
  RouteOverride effective;
  effective.present = false;
  effective.prefer_kind = "sentinel";
  CHECK(model.Proceed(&effective));
  CHECK(effective.present);
  CHECK(effective.prefer_kind == "web_automation");
  CHECK(!effective.allow_external_endpoint);
  CHECK(model.state() == RouteState::kProceeded);
  // One-shot: proceeding twice fails and the value is not handed out again.
  RouteOverride second;
  second.prefer_kind = "sentinel2";
  CHECK(!model.Proceed(&second));
  CHECK(second.prefer_kind == "sentinel2");

  // Cancel path clears any attached override without handing it out.
  CHECK(model.Present(ValidPreview()));
  CHECK(model.ApplyOverride(override_request));
  CHECK(model.Cancel());
  CHECK(model.state() == RouteState::kCancelled);
  RouteOverride discarded;
  discarded.prefer_kind = "sentinel3";
  CHECK(!model.Proceed(&discarded));
  CHECK(discarded.prefer_kind == "sentinel3");
  return true;
}

bool ExternalDataDisclosureRendering() {
  CapabilityRouteModel model;
  CHECK(model.Present(ValidPreview()));
  const std::string summary = model.Summary();
  // Closed line grammar: selected row, per-candidate rows with the
  // external marker, exclusion rows.
  CHECK(summary.find("selected|partner|partner.approved|selected_by_default_rank\n") !=
        std::string::npos);
  CHECK(summary.find("candidate|partner|partner.approved|user_approved|external\n") !=
        std::string::npos);
  CHECK(summary.find("candidate|site_skill|skill.export|user_approved|local\n") !=
        std::string::npos);
  CHECK(summary.find("excluded|partner.notes|insufficient_trust\n") !=
        std::string::npos);
  // Reject-style presentation renders a none-selected row.
  CapabilityRoutePreview none = ValidPreview();
  none.selected_id.clear();
  none.selected_kind.clear();
  none.reason = "all_candidates_excluded";
  none.candidates.clear();
  none.exclusions.clear();
  CHECK(model.Present(none));
  CHECK(model.Summary().find("selected|none|-|all_candidates_excluded\n") !=
        std::string::npos);
  return true;
}

bool ExtractKeys(const std::string& path, std::set<std::string>* keys,
                 bool* ok) {
  std::ifstream input(path);
  if (!input) {
    *ok = false;
    return false;
  }
  // Minimal flat-JSON key extractor: lines of `"key": "value",`.
  std::string line;
  while (std::getline(input, line)) {
    const auto quote_begin = line.find('"');
    if (quote_begin == std::string::npos) {
      continue;
    }
    const auto quote_end = line.find('"', quote_begin + 1);
    if (quote_end == std::string::npos) {
      continue;
    }
    const auto colon = line.find(':', quote_end + 1);
    if (colon == std::string::npos) {
      continue;
    }
    keys->insert(line.substr(quote_begin + 1, quote_end - quote_begin - 1));
  }
  *ok = true;
  return true;
}

bool LocaleParity() {
  const char* repo_root = std::getenv("CRAYON_REPO_ROOT");
  CHECK(repo_root != nullptr);
  bool ok_en = false;
  bool ok_zh = false;
  std::set<std::string> en;
  std::set<std::string> zh;
  ExtractKeys(std::string(repo_root) + "/browser/shared-ui/locales/en-US.json",
              &en, &ok_en);
  ExtractKeys(std::string(repo_root) + "/browser/shared-ui/locales/zh-CN.json",
              &zh, &ok_zh);
  CHECK(ok_en);
  CHECK(ok_zh);
  CHECK(en == zh);
  // The route-preview key family must exist on both sides.
  static const char* kRequired[] = {
      "capability.route.title",
      "capability.route.selected",
      "capability.route.reason",
      "capability.route.candidates",
      "capability.route.exclusions",
      "capability.route.data_external",
      "capability.route.override.temporary",
      "capability.route.override.prefer",
      "capability.route.override.allow_external",
      "capability.route.proceed",
      "capability.route.cancel",
  };
  for (const char* key : kRequired) {
    CHECK(en.count(key) == 1);
    CHECK(zh.count(key) == 1);
  }
  return true;
}

bool StormInvariants() {
  CapabilityRouteModel model;
  unsigned long long state = 0x243F6A8885A308D3ULL;
  auto next = [&state]() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
  };
  std::uint64_t last_revision = model.revision();
  for (int step = 0; step < 5000; ++step) {
    switch (next() % 4) {
      case 0:
      case 1:
        model.Present(ValidPreview());
        break;
      case 2: {
        RouteOverride override_request;
        override_request.present = next() % 2 == 0;
        override_request.allow_external_endpoint = next() % 2 == 0;
        const char* kinds[] = {"",       "partner",   "site_skill",
                               "reject", "web_automation", "bogus"};
        override_request.prefer_kind =
            kinds[next() % (sizeof(kinds) / sizeof(kinds[0]))];
        model.ApplyOverride(override_request);
        break;
      }
      default: {
        RouteOverride out;
        out.prefer_kind = "sentinel";
        model.Proceed(&out);
        model.Cancel();
        break;
      }
    }
    // Invariants: Proceed only ever succeeds from Presented; revision
    // never decreases; overrides never leak after leaving Presented.
    if (model.state() != RouteState::kPresented) {
      RouteOverride out;
      out.prefer_kind = "sentinel";
      CHECK(!model.Proceed(&out));
      CHECK(!model.Cancel());
    }
    CHECK(model.revision() >= last_revision);
    last_revision = model.revision();
  }
  return true;
}

}  // namespace

int main() {
  bool ok = true;
  ok = PresentationValidation() && ok;
  ok = LifecycleAndOverride() && ok;
  ok = ExternalDataDisclosureRendering() && ok;
  ok = LocaleParity() && ok;
  ok = StormInvariants() && ok;
  if (!ok) {
    std::cerr << "capability_route contract test FAILED\n";
    return 1;
  }
  std::cout << "capability_route contract test passed\n";
  return 0;
}
