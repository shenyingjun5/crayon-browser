// Multi-signal action discovery contract tests (ACT-04, AC-004): closed
// signal/hint vocabularies, multi-signal agreement matching, occlusion
// exclusion, ambiguity fail-closed, bounds and stable verdict names.

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "crayon/browser_engine/action_discovery.h"

namespace {

using crayon::browser_engine::ActionCandidate;
using crayon::browser_engine::ActionSignal;
using crayon::browser_engine::ActionSignalKind;
using crayon::browser_engine::DiscoveryEvidence;
using crayon::browser_engine::DiscoveryHint;
using crayon::browser_engine::DiscoveryHintKind;
using crayon::browser_engine::DiscoveryTargetId;
using crayon::browser_engine::DiscoveryVerdict;
using crayon::browser_engine::FuseDiscoveryEvidence;
using crayon::browser_engine::IsValid;
using crayon::browser_engine::kMaxDiscoveryCandidates;
using crayon::browser_engine::kMaxDiscoveryHints;
using crayon::browser_engine::kMaxDiscoverySignalBytes;
using crayon::browser_engine::ToStableName;

void Check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

DiscoveryTargetId Target(const char* raw) {
  auto target = DiscoveryTargetId::TryCreate(raw);
  Check(target.has_value(), "target id must be valid");
  return *target;
}

DiscoveryHint Hint(DiscoveryHintKind kind, std::string value) {
  DiscoveryHint hint;
  hint.kind = kind;
  hint.value = std::move(value);
  return hint;
}

ActionSignal Signal(ActionSignalKind kind, std::string value) {
  ActionSignal signal;
  signal.kind = kind;
  signal.value = std::move(value);
  return signal;
}

ActionCandidate Candidate(const char* id, std::vector<ActionSignal> signals,
                          bool occluded = false) {
  ActionCandidate candidate{Target(id)};
  candidate.occluded = occluded;
  candidate.signals = std::move(signals);
  return candidate;
}

void TestClosedVocabulariesAndStableNames() {
  Check(IsValid(ActionSignalKind::kRole) &&
            IsValid(ActionSignalKind::kStructuralOrdinal) &&
            !IsValid(static_cast<ActionSignalKind>(99)),
        "signal kinds are a closed set");
  Check(IsValid(DiscoveryHintKind::kRole) &&
            !IsValid(static_cast<DiscoveryHintKind>(99)),
        "hint kinds are a closed set");
  Check(std::string(ToStableName(DiscoveryVerdict::kUnique)) == "unique" &&
            std::string(ToStableName(DiscoveryVerdict::kAmbiguous)) ==
                "ambiguous" &&
            std::string(ToStableName(DiscoveryVerdict::kNoMatch)) == "no_match",
        "verdict names are stable");
}

void TestMultiSignalAgreement() {
  const std::vector<DiscoveryHint> hints = {
      Hint(DiscoveryHintKind::kRole, "button"),
      Hint(DiscoveryHintKind::kAccessibleName, "提交订单"),
  };
  const std::vector<ActionCandidate> candidates = {
      Candidate("t-1", {Signal(ActionSignalKind::kRole, "button"),
                        Signal(ActionSignalKind::kVisibleText, "其他")}),
      Candidate("t-2", {Signal(ActionSignalKind::kRole, "button"),
                        Signal(ActionSignalKind::kAccessibleName, "提交订单")}),
  };
  const DiscoveryEvidence evidence = FuseDiscoveryEvidence(hints, candidates);
  Check(evidence.verdict == DiscoveryVerdict::kUnique, "single full match");
  Check(evidence.target.has_value() && *evidence.target == Target("t-2"),
        "winner target is reported");
  Check(evidence.match_count == 1, "one matching candidate");
  Check(evidence.matched_hints.size() == 2, "both hints satisfied");
  // Any-signal voting must not match: a candidate missing one hint loses.
  const std::vector<ActionCandidate> partial = {
      Candidate("t-3", {Signal(ActionSignalKind::kRole, "button")}),
  };
  Check(FuseDiscoveryEvidence(hints, partial).verdict ==
            DiscoveryVerdict::kNoMatch,
        "partial signal agreement never matches");
}

void TestAmbiguityFailsClosed() {
  const std::vector<DiscoveryHint> hints = {
      Hint(DiscoveryHintKind::kVisibleText, "购买"),
  };
  const std::vector<ActionCandidate> candidates = {
      Candidate("t-1", {Signal(ActionSignalKind::kVisibleText, "购买")}),
      Candidate("t-2", {Signal(ActionSignalKind::kVisibleText, "购买")}),
  };
  const DiscoveryEvidence evidence = FuseDiscoveryEvidence(hints, candidates);
  Check(evidence.verdict == DiscoveryVerdict::kAmbiguous, "two matches");
  Check(!evidence.target.has_value(), "ambiguous match reports no target");
  Check(evidence.match_count == 2, "match count kept for internal evidence");
}

void TestOccludedAndMalformedInputFailsClosed() {
  const std::vector<DiscoveryHint> hints = {
      Hint(DiscoveryHintKind::kRole, "button"),
  };
  // Occluded candidates are excluded even when they uniquely match.
  const std::vector<ActionCandidate> occluded = {
      Candidate("t-1", {Signal(ActionSignalKind::kRole, "button")}, true),
  };
  Check(FuseDiscoveryEvidence(hints, occluded).verdict ==
            DiscoveryVerdict::kNoMatch,
        "occluded candidates never match");
  // Malformed hints: empty, over budget, duplicate kinds, unknown kind.
  Check(!IsValid(std::vector<DiscoveryHint>{}), "hints must not be empty");
  Check(!IsValid({Hint(DiscoveryHintKind::kRole, "")}),
        "hint values must not be empty");
  std::string over(kMaxDiscoverySignalBytes + 1, 'x');
  Check(!IsValid({Hint(DiscoveryHintKind::kRole, over)}),
        "hint values are bounded");
  Check(!IsValid({Hint(DiscoveryHintKind::kRole, "button"),
                  Hint(DiscoveryHintKind::kRole, "button")}),
        "duplicate hint kinds are rejected");
  Check(!IsValid({Hint(static_cast<DiscoveryHintKind>(42), "x")}),
        "unknown hint kinds are rejected");
  // Malformed candidates: over budget, duplicate targets, bad signals.
  std::vector<ActionCandidate> oversized;
  for (std::size_t index = 0; index <= kMaxDiscoveryCandidates; ++index) {
    oversized.push_back(
        Candidate(("t-x" + std::to_string(index)).c_str(), {}));
  }
  Check(!IsValid(oversized), "candidate list is bounded");
  Check(!IsValid({Candidate("t-1", {}), Candidate("t-1", {})}),
        "duplicate targets are rejected");
  std::string long_value(kMaxDiscoverySignalBytes + 1, 'x');
  Check(!IsValid({Candidate("t-1", {Signal(ActionSignalKind::kRole, long_value)})}),
        "signal values are bounded");
  // Malformed input yields a stable no-match, never a guess.
  const DiscoveryEvidence rejected =
      FuseDiscoveryEvidence(std::vector<DiscoveryHint>{}, occluded);
  Check(rejected.verdict == DiscoveryVerdict::kNoMatch &&
            !rejected.target.has_value(),
        "malformed input fails closed");
}

void TestDynamicListBoundaries() {
  // A dynamic list saturating the candidate budget is still bounded input;
  // the contract rejects the request instead of truncating silently.
  std::vector<ActionCandidate> saturated;
  for (std::size_t index = 0; index <= kMaxDiscoveryCandidates; ++index) {
    saturated.push_back(
        Candidate(("t-" + std::to_string(index)).c_str(),
                  {Signal(ActionSignalKind::kStructuralOrdinal,
                          std::to_string(index))}));
  }
  Check(!IsValid(saturated), "saturated candidate list is rejected");
  saturated.pop_back();
  Check(IsValid(saturated), "budgeted candidate list is accepted");
}

}  // namespace

int main() {
  try {
    TestClosedVocabulariesAndStableNames();
    TestMultiSignalAgreement();
    TestAmbiguityFailsClosed();
    TestOccludedAndMalformedInputFailsClosed();
    TestDynamicListBoundaries();
    std::cout << "action_discovery_test: passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "action_discovery_test: " << error.what() << '\n';
    return 1;
  }
}
