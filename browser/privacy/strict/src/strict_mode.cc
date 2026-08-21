#include "crayon/browser_privacy/strict_mode.h"

#include <algorithm>

namespace crayon::browser_privacy {

StrictModePolicy Normalize(const StrictModePolicy& policy) noexcept {
  if (policy.enabled) {
    return policy;
  }
  // A disabled policy allows everything; compatibility switches are
  // meaningless and fold to false so the golden line stays canonical.
  return StrictModePolicy{};
}

RestrictionAction ActionFor(HighEntropyApi api,
                            const StrictModePolicy& policy) noexcept {
  if (!IsValid(api)) {
    return RestrictionAction::kBlock;  // Fail closed on out-of-domain input.
  }
  if (!policy.enabled) {
    return RestrictionAction::kAllow;
  }
  switch (api) {
    case HighEntropyApi::kUserAgent:
    case HighEntropyApi::kTimezone:
      return RestrictionAction::kFreeze;
    case HighEntropyApi::kScreenMetrics:
      return RestrictionAction::kQuantize;
    case HighEntropyApi::kHardwareConcurrency:
    case HighEntropyApi::kDeviceMemory:
      return RestrictionAction::kClamp;
    case HighEntropyApi::kWebGlParameters:
      // Compatibility only ever relaxes block -> clamp, never allow.
      return policy.webgl_compatibility ? RestrictionAction::kClamp
                                        : RestrictionAction::kBlock;
    case HighEntropyApi::kFontEnumeration:
      return policy.font_compatibility ? RestrictionAction::kClamp
                                       : RestrictionAction::kBlock;
    case HighEntropyApi::kClientHints:
    case HighEntropyApi::kCanvasReadback:
    case HighEntropyApi::kAudioFingerprint:
    case HighEntropyApi::kBattery:
    case HighEntropyApi::kWebRtcLocalIp:
    case HighEntropyApi::kMediaDeviceLabels:
      return RestrictionAction::kBlock;
  }
  return RestrictionAction::kBlock;
}

std::int32_t ClampHardwareConcurrency(std::int32_t value) noexcept {
  return std::clamp(value, 1, kHardwareConcurrencyCeiling);
}

double ClampDeviceMemory(double value) noexcept {
  if (value <= 0.0) {
    return kDeviceMemoryCeiling;
  }
  return value < kDeviceMemoryCeiling ? value : kDeviceMemoryCeiling;
}

std::int32_t QuantizeScreenDimension(std::int32_t value_px) noexcept {
  if (value_px <= 0) {
    return 0;
  }
  return value_px / kScreenQuantumPx * kScreenQuantumPx;
}

std::string Describe(const StrictModePolicy& policy) {
  const StrictModePolicy normalized = Normalize(policy);
  std::string line = "strict=";
  line += normalized.enabled ? '1' : '0';
  line += " webgl_compat=";
  line += normalized.webgl_compatibility ? '1' : '0';
  line += " font_compat=";
  line += normalized.font_compatibility ? '1' : '0';
  return line;
}

}  // namespace crayon::browser_privacy
