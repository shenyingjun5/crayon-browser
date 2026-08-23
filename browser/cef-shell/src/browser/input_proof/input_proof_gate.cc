#include "browser/input_proof/input_proof_gate.h"

namespace crayon::cef_shell::input_proof {

void InputProofGate::NoteUserInput(std::uint32_t tab, std::uint64_t navigation_id) {
  has_input_ = true;
  last_input_tab_ = tab;
  last_input_navigation_ = navigation_id;
  // Snapshot the trusted playback state at the moment of the input so
  // eligibility can distinguish user-initiated playback (idle -> input
  // -> progress, BR-004) from autoplay that merely coexists with an
  // unrelated click (BR-005).
  if (has_progress_ && last_progress_tab_ == tab &&
      last_progress_navigation_ == navigation_id) {
    input_baseline_seconds_ = last_progress_seconds_;
    const double delta = last_progress_seconds_ - previous_progress_seconds_;
    progressing_at_input_ = delta > 0;
  } else {
    input_baseline_seconds_ = 0;
    progressing_at_input_ = false;
  }
}

void InputProofGate::NotePlaybackProgress(std::uint32_t tab, std::uint64_t navigation_id,
                                          double current_seconds) {
  if (has_progress_ && last_progress_tab_ == tab &&
      last_progress_navigation_ == navigation_id) {
    previous_progress_seconds_ = last_progress_seconds_;
  } else {
    // New tab/navigation: no comparable previous sample.
    previous_progress_seconds_ = current_seconds;
  }
  has_progress_ = true;
  last_progress_tab_ = tab;
  last_progress_navigation_ = navigation_id;
  last_progress_seconds_ = current_seconds;
}

void InputProofGate::SetActiveTab(std::uint32_t tab) {
  active_tab_ = tab;
}

ProofResult InputProofGate::Evaluate(std::uint32_t tab,
                                     std::uint64_t navigation_id) const {
  if (!has_input_ || navigation_id != last_input_navigation_) {
    // Either no input at all (BR-003) or the input belongs to another
    // navigation (BR-007); both deny without trusting the claim.
    return has_input_ ? ProofResult::kDeniedStaleNavigation
                      : ProofResult::kDeniedNoTrustedInput;
  }
  if (tab != last_input_tab_ || tab != active_tab_) {
    return ProofResult::kDeniedInputNotOnActiveTab;
  }
  if (progressing_at_input_) {
    return ProofResult::kDeniedAlreadyProgressing;  // BR-005
  }
  if (last_progress_tab_ != tab || last_progress_navigation_ != navigation_id) {
    return ProofResult::kDeniedNoProgressAfterInput;
  }
  if (last_progress_seconds_ - input_baseline_seconds_ < kMinProgressSeconds) {
    return ProofResult::kDeniedNoProgressAfterInput;
  }
  return ProofResult::kEligible;
}

}  // namespace crayon::cef_shell::input_proof
