#pragma once

#include <cstdint>
#include <string>

namespace crayon::browser_privacy {

/// Uniform ceilings applied by the strict privacy mode.  The values are
/// identical for every profile and every session: strict mode never
/// derives a per-profile random identity (PV-009).
inline constexpr std::int32_t kHardwareConcurrencyCeiling = 4;
inline constexpr double kDeviceMemoryCeiling = 4.0;
inline constexpr std::int32_t kScreenQuantumPx = 100;

/// Closed set of high-entropy web APIs governed by the strict mode.
enum class HighEntropyApi {
  kUserAgent = 0,
  kClientHints,
  kScreenMetrics,
  kCanvasReadback,
  kWebGlParameters,
  kAudioFingerprint,
  kFontEnumeration,
  kHardwareConcurrency,
  kDeviceMemory,
  kTimezone,
  kBattery,
  kWebRtcLocalIp,
  kMediaDeviceLabels,
};

constexpr bool IsValid(HighEntropyApi api) noexcept {
  switch (api) {
    case HighEntropyApi::kUserAgent:
    case HighEntropyApi::kClientHints:
    case HighEntropyApi::kScreenMetrics:
    case HighEntropyApi::kCanvasReadback:
    case HighEntropyApi::kWebGlParameters:
    case HighEntropyApi::kAudioFingerprint:
    case HighEntropyApi::kFontEnumeration:
    case HighEntropyApi::kHardwareConcurrency:
    case HighEntropyApi::kDeviceMemory:
    case HighEntropyApi::kTimezone:
    case HighEntropyApi::kBattery:
    case HighEntropyApi::kWebRtcLocalIp:
    case HighEntropyApi::kMediaDeviceLabels:
      return true;
  }
  return false;
}

/// Closed restriction applied to one high-entropy API.
enum class RestrictionAction {
  /// Leave the API untouched.
  kAllow = 0,
  /// Replace with one uniform value for all users.
  kFreeze,
  /// Round to a coarse step.
  kQuantize,
  /// Limit to a uniform ceiling or curated baseline.
  kClamp,
  /// Do not expose the API at all.
  kBlock,
};

constexpr bool IsValid(RestrictionAction action) noexcept {
  switch (action) {
    case RestrictionAction::kAllow:
    case RestrictionAction::kFreeze:
    case RestrictionAction::kQuantize:
    case RestrictionAction::kClamp:
    case RestrictionAction::kBlock:
      return true;
  }
  return false;
}

/// Strict anti-tracking mode policy.
///
/// Plain data consumed by a future CEF adapter; this module performs no
/// I/O and holds no randomness, clock or profile state.
struct StrictModePolicy final {
  /// Master switch.  When false every API is allowed.
  bool enabled = false;
  /// Compatibility switch: expose a clamped baseline set of WebGL
  /// parameters instead of blocking them outright.
  bool webgl_compatibility = false;
  /// Compatibility switch: expose a curated font list instead of blocking
  /// font enumeration outright.
  bool font_compatibility = false;
};

/// Folds contradictory combinations toward consistency: a disabled policy
/// clears its compatibility switches.  Privacy-relevant values are never
/// folded downward.
StrictModePolicy Normalize(const StrictModePolicy& policy) noexcept;

/// Returns the closed restriction for `api` under `policy`.
/// Out-of-domain enum values fail closed as `kBlock`.
RestrictionAction ActionFor(HighEntropyApi api,
                            const StrictModePolicy& policy) noexcept;

/// Clamps a hardware-concurrency value to the uniform ceiling.  Inputs
/// below one are folded to one; the result never exceeds
/// `kHardwareConcurrencyCeiling`.
std::int32_t ClampHardwareConcurrency(std::int32_t value) noexcept;

/// Clamps a device-memory value (GiB) to the uniform ceiling.  Inputs at
/// or below zero fold to the ceiling (suspicious inputs normalize to the
/// uniform value); the result never exceeds `kDeviceMemoryCeiling`.
double ClampDeviceMemory(double value) noexcept;

/// Rounds a screen dimension down to a multiple of `kScreenQuantumPx`.
/// Negative inputs fold to zero.
std::int32_t QuantizeScreenDimension(std::int32_t value_px) noexcept;

/// Deterministic one-line snapshot used as the compatibility golden.
/// Contains only booleans — never site, profile or user data.
std::string Describe(const StrictModePolicy& policy);

}  // namespace crayon::browser_privacy
