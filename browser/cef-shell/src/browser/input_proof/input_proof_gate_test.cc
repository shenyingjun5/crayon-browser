// CEF-10 contract tests: BR-003 (forged playing), BR-004 (trusted
// input + progression), BR-005 (autoplay after unrelated click),
// BR-007 (stale navigation), active-tab and claim-side denial.
#include <cstdlib>
#include <iostream>

#include "browser/input_proof/input_proof_gate.h"

namespace {

using crayon::cef_shell::input_proof::InputProofGate;
using crayon::cef_shell::input_proof::ProofResult;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool ForgedPlayingDenied() {  // BR-003
  InputProofGate gate(/*active_tab=*/1);
  gate.NotePlaybackProgress(1, 10, 3.0);
  gate.NotePlaybackProgress(1, 10, 4.0);  // progressing without input
  CHECK(gate.Evaluate(1, 10) == ProofResult::kDeniedNoTrustedInput);
  return true;
}

bool TrustedInputAndProgressAllowed() {  // BR-004
  InputProofGate gate(/*active_tab=*/1);
  gate.NotePlaybackProgress(1, 10, 0.0);  // idle sample
  gate.NoteUserInput(1, 10);
  gate.NotePlaybackProgress(1, 10, 1.2);  // progressed after the input
  CHECK(gate.Evaluate(1, 10) == ProofResult::kEligible);
  return true;
}

bool AutoplayAfterUnrelatedClickDenied() {  // BR-005
  InputProofGate gate(/*active_tab=*/1);
  // Autoplay is already progressing before the click.
  gate.NotePlaybackProgress(1, 10, 1.0);
  gate.NotePlaybackProgress(1, 10, 2.0);
  gate.NoteUserInput(1, 10);  // click on a non-play area
  gate.NotePlaybackProgress(1, 10, 3.0);
  CHECK(gate.Evaluate(1, 10) == ProofResult::kDeniedAlreadyProgressing);
  // A paused-then-resumed player DOES qualify: progression stopped
  // before the input.
  InputProofGate resume(/*active_tab=*/1);
  resume.NotePlaybackProgress(1, 10, 1.0);
  resume.NotePlaybackProgress(1, 10, 2.0);
  resume.NotePlaybackProgress(1, 10, 2.0);  // paused: no advance
  resume.NoteUserInput(1, 10);
  resume.NotePlaybackProgress(1, 10, 2.4);
  CHECK(resume.Evaluate(1, 10) == ProofResult::kEligible);
  return true;
}

bool ExplicitPauseMarkerBeatsSparseSampling() {
  // CEF-10 P2 fix: an explicitly reported pause must clear the
  // progressing snapshot even with a single dense sample, so a
  // subsequent user input qualifies instead of being mistaken for
  // autoplay coexistence.
  InputProofGate gate(/*active_tab=*/1);
  gate.NotePlaybackProgress(1, 10, 1.0);
  gate.NotePlaybackProgress(1, 10, 2.0);  // dense progressing stream
  gate.NotePlaybackSuspended(1, 10);      // explicit pause marker
  gate.NoteUserInput(1, 10);              // user resumes
  gate.NotePlaybackProgress(1, 10, 2.4);
  CHECK(gate.Evaluate(1, 10) == ProofResult::kEligible);
  // Marker scoped to the same tab/navigation only.
  InputProofGate scoped(/*active_tab=*/1);
  scoped.NotePlaybackProgress(1, 10, 1.0);
  scoped.NotePlaybackProgress(1, 10, 2.0);
  scoped.NotePlaybackSuspended(2, 10);  // other tab: no effect
  scoped.NoteUserInput(1, 10);
  scoped.NotePlaybackProgress(1, 10, 2.4);
  CHECK(scoped.Evaluate(1, 10) == ProofResult::kDeniedAlreadyProgressing);
  return true;
}

bool StaleNavigationDenied() {  // BR-007
  InputProofGate gate(/*active_tab=*/1);
  gate.NoteUserInput(1, 10);
  gate.NotePlaybackProgress(1, 10, 1.0);
  CHECK(gate.Evaluate(1, 11) == ProofResult::kDeniedStaleNavigation);
  return true;
}

bool ActiveTabAndBackgroundInput() {
  InputProofGate gate(/*active_tab=*/1);
  gate.NotePlaybackProgress(1, 10, 0.0);
  // Input landed on a background tab: never qualifies.
  gate.NoteUserInput(2, 10);
  gate.NotePlaybackProgress(2, 10, 1.0);
  CHECK(gate.Evaluate(2, 10) == ProofResult::kDeniedInputNotOnActiveTab);
  // Foreground switched: the same input now qualifies only on the new
  // active tab.
  InputProofGate switched(/*active_tab=*/3);
  switched.NotePlaybackProgress(3, 10, 0.0);
  switched.NoteUserInput(3, 10);
  switched.NotePlaybackProgress(3, 10, 0.5);
  CHECK(switched.Evaluate(3, 10) == ProofResult::kEligible);
  CHECK(switched.Evaluate(1, 10) == ProofResult::kDeniedInputNotOnActiveTab);
  return true;
}

bool ClickWithoutPlaybackDenied() {
  InputProofGate gate(/*active_tab=*/1);
  gate.NoteUserInput(1, 10);
  // No progression sample for the tab at all.
  CHECK(gate.Evaluate(1, 10) == ProofResult::kDeniedNoProgressAfterInput);
  // Progress below the minimum delta.
  gate.NotePlaybackProgress(1, 10, 0.0);
  gate.NotePlaybackProgress(1, 10, 0.01);
  CHECK(gate.Evaluate(1, 10) == ProofResult::kDeniedNoProgressAfterInput);
  return true;
}

/// Pseudo-random fact storm: the verdict stays within the closed set
/// and the allow path requires input + non-progressing snapshot +
/// post-input progression.
bool StormInvariants() {
  std::uint64_t state = 0xC0FF'EE12'3456'7890;
  auto next = [&state]() {
    state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return state;
  };
  InputProofGate gate(static_cast<std::uint32_t>(next() % 3));
  for (int step = 0; step < 5'000; ++step) {
    const std::uint64_t choice = next() % 4;
    const std::uint32_t tab = static_cast<std::uint32_t>(next() % 4);
    const std::uint64_t nav = next() % 3;
    const double seconds = static_cast<double>(next() % 100) / 7.0;
    switch (choice) {
      case 0:
        gate.NoteUserInput(tab, nav);
        break;
      case 1:
        gate.NotePlaybackProgress(tab, nav, seconds);
        break;
      case 2:
        gate.SetActiveTab(tab);
        break;
      default:
        break;
    }
    const ProofResult result = gate.Evaluate(tab, nav);
    CHECK(result == ProofResult::kEligible || result == ProofResult::kDeniedNoTrustedInput ||
          result == ProofResult::kDeniedInputNotOnActiveTab ||
          result == ProofResult::kDeniedStaleNavigation ||
          result == ProofResult::kDeniedAlreadyProgressing ||
          result == ProofResult::kDeniedNoProgressAfterInput);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = ForgedPlayingDenied() && TrustedInputAndProgressAllowed() &&
                  AutoplayAfterUnrelatedClickDenied() &&
                  ExplicitPauseMarkerBeatsSparseSampling() && StaleNavigationDenied() &&
                  ActiveTabAndBackgroundInput() && ClickWithoutPlaybackDenied() &&
                  StormInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "input_proof_gate_test passed\n";
  return EXIT_SUCCESS;
}
