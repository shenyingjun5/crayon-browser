#pragma once

#include <cstddef>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>

#include "crayon/browser_engine/ids.h"

namespace crayon::browser_engine {

// Multi-signal action discovery contract (ACT-04, AC-004).
//
// Discovery fuses independent verified-fact signals — element role,
// accessible name, visible text and structural ordinal — into an internal
// locator evidence verdict. The contract never carries CSS/XPath/JS query
// syntax, DOM object references or raw HTML: candidates are identified
// by opaque target tokens and closed-signal facts only. Occluded
// candidates never match; an ambiguous or absent match is a stable denial,
// never a best-effort guess.

inline constexpr std::size_t kMaxDiscoveryCandidates = 512;
inline constexpr std::size_t kMaxSignalsPerCandidate = 8;
inline constexpr std::size_t kMaxDiscoveryHints = 4;
inline constexpr std::size_t kMaxDiscoverySignalBytes = 256;

enum class ActionSignalKind {
  kRole = 0,
  kAccessibleName,
  kVisibleText,
  kStructuralOrdinal,
};

bool IsValid(ActionSignalKind value) noexcept;

enum class DiscoveryHintKind {
  kRole = 0,
  kAccessibleName,
  kVisibleText,
  kStructuralOrdinal,
};

bool IsValid(DiscoveryHintKind value) noexcept;

// One closed discovery signal of a verified candidate fact. The value is
// bounded page content; its interpretation depends on `kind`.
struct ActionSignal final {
  ActionSignalKind kind = ActionSignalKind::kRole;
  std::string value;
};

// One verified candidate observed by the engine for the current page
// state. `occluded` reports a verified occlusion fact; occluded
// candidates are excluded from matching.
struct ActionCandidate final {
  explicit ActionCandidate(DiscoveryTargetId target_id)
      : target(std::move(target_id)) {}

  DiscoveryTargetId target;
  bool occluded = false;
  std::vector<ActionSignal> signals;
};

// One required match the caller asks discovery to confirm. A candidate
// matches only when every hint finds an equal-valued signal of the same
// kind — multi-signal agreement, not any-signal voting.
struct DiscoveryHint final {
  DiscoveryHintKind kind = DiscoveryHintKind::kRole;
  std::string value;
};

enum class DiscoveryVerdict {
  kUnique = 0,
  kAmbiguous,
  kNoMatch,
};

const char* ToStableName(DiscoveryVerdict verdict) noexcept;

// Internal locator evidence: which opaque target matched, how many
// candidates matched, and which hint kinds the winner satisfied. The
// evidence stays inside the Browser process; the external surface sees
// only the verdict and the target token.
struct DiscoveryEvidence final {
  // Present only for a unique match; ambiguous and no-match carry none.
  std::optional<DiscoveryTargetId> target;
  DiscoveryVerdict verdict = DiscoveryVerdict::kNoMatch;
  std::uint32_t match_count = 0;
  std::vector<DiscoveryHintKind> matched_hints;
};

bool IsValid(const std::vector<DiscoveryHint>& hints) noexcept;
bool IsValid(const std::vector<ActionCandidate>& candidates) noexcept;

// Deterministic fusion of verified candidate signals against the required
// hints. Returns a closed verdict; malformed input yields a stable `kNoMatch`
// with no target rather than a best-effort guess.
DiscoveryEvidence FuseDiscoveryEvidence(
    const std::vector<DiscoveryHint>& hints,
    const std::vector<ActionCandidate>& candidates) noexcept;

}  // namespace crayon::browser_engine
