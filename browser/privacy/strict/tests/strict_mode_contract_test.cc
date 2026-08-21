#include <cstdlib>
#include <iostream>

#include "crayon/browser_privacy/strict_mode.h"

namespace {

using crayon::browser_privacy::ActionFor;
using crayon::browser_privacy::ClampDeviceMemory;
using crayon::browser_privacy::ClampHardwareConcurrency;
using crayon::browser_privacy::Describe;
using crayon::browser_privacy::HighEntropyApi;
using crayon::browser_privacy::IsValid;
using crayon::browser_privacy::kDeviceMemoryCeiling;
using crayon::browser_privacy::kHardwareConcurrencyCeiling;
using crayon::browser_privacy::Normalize;
using crayon::browser_privacy::QuantizeScreenDimension;
using crayon::browser_privacy::RestrictionAction;
using crayon::browser_privacy::StrictModePolicy;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

StrictModePolicy Strict() {
  StrictModePolicy policy;
  policy.enabled = true;
  return policy;
}

bool DisabledPolicyAllowsEverything() {
  const StrictModePolicy policy;  // enabled == false
  for (int raw = 0; raw <= static_cast<int>(HighEntropyApi::kMediaDeviceLabels);
       ++raw) {
    const auto api = static_cast<HighEntropyApi>(raw);
    CHECK(ActionFor(api, policy) == RestrictionAction::kAllow);
  }
  // Normalize folds compatibility switches off when disabled.
  StrictModePolicy candidate;
  candidate.webgl_compatibility = true;
  candidate.font_compatibility = true;
  const StrictModePolicy normalized = Normalize(candidate);
  CHECK(!normalized.enabled);
  CHECK(!normalized.webgl_compatibility);
  CHECK(!normalized.font_compatibility);
  return true;
}

bool StrictTableIsClosed() {
  const StrictModePolicy policy = Strict();
  CHECK(ActionFor(HighEntropyApi::kUserAgent, policy) ==
        RestrictionAction::kFreeze);
  CHECK(ActionFor(HighEntropyApi::kTimezone, policy) ==
        RestrictionAction::kFreeze);
  CHECK(ActionFor(HighEntropyApi::kScreenMetrics, policy) ==
        RestrictionAction::kQuantize);
  CHECK(ActionFor(HighEntropyApi::kHardwareConcurrency, policy) ==
        RestrictionAction::kClamp);
  CHECK(ActionFor(HighEntropyApi::kDeviceMemory, policy) ==
        RestrictionAction::kClamp);
  CHECK(ActionFor(HighEntropyApi::kClientHints, policy) ==
        RestrictionAction::kBlock);
  CHECK(ActionFor(HighEntropyApi::kCanvasReadback, policy) ==
        RestrictionAction::kBlock);
  CHECK(ActionFor(HighEntropyApi::kAudioFingerprint, policy) ==
        RestrictionAction::kBlock);
  CHECK(ActionFor(HighEntropyApi::kBattery, policy) ==
        RestrictionAction::kBlock);
  CHECK(ActionFor(HighEntropyApi::kWebRtcLocalIp, policy) ==
        RestrictionAction::kBlock);
  CHECK(ActionFor(HighEntropyApi::kMediaDeviceLabels, policy) ==
        RestrictionAction::kBlock);
  return true;
}

bool CompatibilityOnlyRelaxesToClamp() {
  StrictModePolicy policy = Strict();
  // Without compatibility, WebGL parameters and fonts are blocked.
  CHECK(ActionFor(HighEntropyApi::kWebGlParameters, policy) ==
        RestrictionAction::kBlock);
  CHECK(ActionFor(HighEntropyApi::kFontEnumeration, policy) ==
        RestrictionAction::kBlock);
  policy.webgl_compatibility = true;
  policy.font_compatibility = true;
  // Compatibility relaxes block -> clamp, never -> allow.
  CHECK(ActionFor(HighEntropyApi::kWebGlParameters, policy) ==
        RestrictionAction::kClamp);
  CHECK(ActionFor(HighEntropyApi::kFontEnumeration, policy) ==
        RestrictionAction::kClamp);
  // Other blocked APIs are unaffected by the switches.
  CHECK(ActionFor(HighEntropyApi::kBattery, policy) ==
        RestrictionAction::kBlock);
  return true;
}

bool InvalidEnumsFailClosed() {
  const StrictModePolicy policy = Strict();
  CHECK(ActionFor(static_cast<HighEntropyApi>(99), policy) ==
        RestrictionAction::kBlock);
  CHECK(!IsValid(static_cast<HighEntropyApi>(13)));
  CHECK(!IsValid(static_cast<RestrictionAction>(5)));
  return true;
}

bool ClampsAreUniformAndBounded() {
  // Two different high inputs collapse to the same uniform output: the
  // clamped value cannot distinguish real hardware.
  CHECK(ClampHardwareConcurrency(16) == kHardwareConcurrencyCeiling);
  CHECK(ClampHardwareConcurrency(64) == kHardwareConcurrencyCeiling);
  CHECK(ClampHardwareConcurrency(2) == 2);
  CHECK(ClampHardwareConcurrency(1) == 1);
  CHECK(ClampHardwareConcurrency(0) == 1);
  CHECK(ClampHardwareConcurrency(-8) == 1);

  CHECK(ClampDeviceMemory(16.0) == kDeviceMemoryCeiling);
  CHECK(ClampDeviceMemory(8.0) == kDeviceMemoryCeiling);
  CHECK(ClampDeviceMemory(2.0) == 2.0);
  // Suspicious inputs normalize to the uniform ceiling.
  CHECK(ClampDeviceMemory(0.0) == kDeviceMemoryCeiling);
  CHECK(ClampDeviceMemory(-1.0) == kDeviceMemoryCeiling);
  return true;
}

bool QuantizeStepsDown() {
  CHECK(QuantizeScreenDimension(1920) == 1900);
  CHECK(QuantizeScreenDimension(1999) == 1900);
  CHECK(QuantizeScreenDimension(100) == 100);
  CHECK(QuantizeScreenDimension(99) == 0);
  CHECK(QuantizeScreenDimension(0) == 0);
  CHECK(QuantizeScreenDimension(-1080) == 0);
  return true;
}

bool DescribeGoldenIsStable() {
  CHECK(Describe(StrictModePolicy{}) == "strict=0 webgl_compat=0 font_compat=0");
  StrictModePolicy policy = Strict();
  CHECK(Describe(policy) == "strict=1 webgl_compat=0 font_compat=0");
  policy.webgl_compatibility = true;
  CHECK(Describe(policy) == "strict=1 webgl_compat=1 font_compat=0");
  // Disabled policy with stray switches normalizes to the canonical line.
  StrictModePolicy stray;
  stray.webgl_compatibility = true;
  CHECK(Describe(stray) == "strict=0 webgl_compat=0 font_compat=0");
  return true;
}

bool EvaluationCarriesNoRandomIdentity() {
  // PV-009: repeated evaluation is bit-identical; nothing varies by
  // profile, session or time.
  const StrictModePolicy policy = Strict();
  for (int round = 0; round < 3; ++round) {
    CHECK(ActionFor(HighEntropyApi::kUserAgent, policy) ==
          RestrictionAction::kFreeze);
    CHECK(ClampHardwareConcurrency(32) == kHardwareConcurrencyCeiling);
    CHECK(QuantizeScreenDimension(2560) == 2500);
    CHECK(Describe(policy) == "strict=1 webgl_compat=0 font_compat=0");
  }
  return true;
}

}  // namespace

int main() {
  struct TestCase {
    const char* name;
    bool (*run)();
  };
  const TestCase kTests[] = {
      {"DisabledPolicyAllowsEverything", &DisabledPolicyAllowsEverything},
      {"StrictTableIsClosed", &StrictTableIsClosed},
      {"CompatibilityOnlyRelaxesToClamp", &CompatibilityOnlyRelaxesToClamp},
      {"InvalidEnumsFailClosed", &InvalidEnumsFailClosed},
      {"ClampsAreUniformAndBounded", &ClampsAreUniformAndBounded},
      {"QuantizeStepsDown", &QuantizeStepsDown},
      {"DescribeGoldenIsStable", &DescribeGoldenIsStable},
      {"EvaluationCarriesNoRandomIdentity", &EvaluationCarriesNoRandomIdentity},
  };
  for (const TestCase& test : kTests) {
    if (!test.run()) {
      std::cerr << "FAILED: " << test.name << '\n';
      return EXIT_FAILURE;
    }
  }
  std::cout << "privacy_strict_contract: all tests passed\n";
  return EXIT_SUCCESS;
}
