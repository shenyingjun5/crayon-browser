#include "crayon/browser_engine/action_discovery.h"

#include "crayon/browser_engine/types.h"

#include <set>

namespace crayon::browser_engine {

namespace {

bool SameSignalValue(const ActionSignal& signal, const DiscoveryHint& hint) noexcept {
  return signal.kind == static_cast<ActionSignalKind>(hint.kind) &&
         signal.value == hint.value;
}

bool MatchesAllHints(const ActionCandidate& candidate,
                     const std::vector<DiscoveryHint>& hints) noexcept {
  if (candidate.occluded) {
    return false;
  }
  for (const DiscoveryHint& hint : hints) {
    bool satisfied = false;
    for (const ActionSignal& signal : candidate.signals) {
      if (SameSignalValue(signal, hint)) {
        satisfied = true;
        break;
      }
    }
    if (!satisfied) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool IsValid(ActionSignalKind value) noexcept {
  return value >= ActionSignalKind::kRole &&
         value <= ActionSignalKind::kStructuralOrdinal;
}

bool IsValid(DiscoveryHintKind value) noexcept {
  return value >= DiscoveryHintKind::kRole &&
         value <= DiscoveryHintKind::kStructuralOrdinal;
}

const char* ToStableName(DiscoveryVerdict verdict) noexcept {
  switch (verdict) {
    case DiscoveryVerdict::kUnique:
      return "unique";
    case DiscoveryVerdict::kAmbiguous:
      return "ambiguous";
    case DiscoveryVerdict::kNoMatch:
      return "no_match";
  }
  return "no_match";
}

bool IsValid(const std::vector<DiscoveryHint>& hints) noexcept {
  if (hints.empty() || hints.size() > kMaxDiscoveryHints) {
    return false;
  }
  std::set<std::size_t> seen_kinds;
  for (const DiscoveryHint& hint : hints) {
    if (!IsValid(hint.kind) ||
        !IsValidBoundedText(hint.value, kMaxDiscoverySignalBytes, false)) {
      return false;
    }
    // Duplicate kinds cannot add information and make the required-signal
    // contract ambiguous.
    if (!seen_kinds.insert(static_cast<std::size_t>(hint.kind)).second) {
      return false;
    }
  }
  return true;
}

bool IsValid(const std::vector<ActionCandidate>& candidates) noexcept {
  if (candidates.size() > kMaxDiscoveryCandidates) {
    return false;
  }
  std::set<std::string> seen_targets;
  for (const ActionCandidate& candidate : candidates) {
    if (candidate.target.value().empty() ||
        !seen_targets.insert(candidate.target.value()).second) {
      return false;
    }
    if (candidate.signals.size() > kMaxSignalsPerCandidate) {
      return false;
    }
    for (const ActionSignal& signal : candidate.signals) {
      if (!IsValid(signal.kind) ||
          !IsValidBoundedText(signal.value, kMaxDiscoverySignalBytes, false)) {
        return false;
      }
    }
  }
  return true;
}

DiscoveryEvidence FuseDiscoveryEvidence(
    const std::vector<DiscoveryHint>& hints,
    const std::vector<ActionCandidate>& candidates) noexcept {
  if (!IsValid(hints) || !IsValid(candidates)) {
    return DiscoveryEvidence{};
  }
  DiscoveryEvidence evidence;
  for (const ActionCandidate& candidate : candidates) {
    if (!MatchesAllHints(candidate, hints)) {
      continue;
    }
    ++evidence.match_count;
    if (evidence.match_count == 1) {
      evidence.target = candidate.target;
      evidence.matched_hints.reserve(hints.size());
      for (const DiscoveryHint& hint : hints) {
        evidence.matched_hints.push_back(hint.kind);
      }
    } else {
      // Ambiguity fails closed: no target is reported for a multi-match.
      evidence.target.reset();
      evidence.matched_hints.clear();
    }
  }
  evidence.verdict = evidence.match_count == 0  ? DiscoveryVerdict::kNoMatch
                     : evidence.match_count == 1 ? DiscoveryVerdict::kUnique
                                                 : DiscoveryVerdict::kAmbiguous;
  return evidence;
}

}  // namespace crayon::browser_engine
