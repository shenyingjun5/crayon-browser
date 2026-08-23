// CEF-10: browser-side trusted-input cross-check gate.
//
// The renderer's media observations are untrusted claims (CEF-09 tags
// them as such).  This gate owns the browser-process facts — real user
// input, the foreground tab and trusted playback progression — and is
// the only place a playback-eligible verdict is produced:
//   BR-003  page fakes `playing` with no trusted input        -> deny
//   BR-004  user input + currentTime progressed afterwards    -> allow
//   BR-005  playback already progressing before the input
//           (autoplay after an unrelated click)              -> deny
//   BR-007  claims from a stale navigation                    -> deny
// Every deny is a closed reason; nothing page-reported can reach the
// allow path.
//
// Thread contract: single-threaded (browser UI thread).
#pragma once

#include <cstdint>

namespace crayon::cef_shell::input_proof {

/// Minimum trusted playback progression (seconds) required after the
/// last trusted input.
inline constexpr double kMinProgressSeconds = 0.05;

/// Closed verdict reasons.
enum class ProofResult {
  kEligible = 0,
  kDeniedNoTrustedInput,        // BR-003: page claim without any input
  kDeniedInputNotOnActiveTab,
  kDeniedStaleNavigation,       // BR-007
  kDeniedAlreadyProgressing,    // BR-005: autoplay beat the input
  kDeniedNoProgressAfterInput,  // click never led to real playback
};

/// Cross-check gate.  Facts are fed from browser-trusted sources only
/// (CEF input events, active-tab tracking, browser-side video state);
/// the claim side carries only the identity tuple.
class InputProofGate final {
 public:
  explicit InputProofGate(std::uint32_t active_tab) : active_tab_(active_tab) {}

  /// Records a trusted user input (click/key) on `tab`/`navigation_id`.
  /// Snapshots whether playback was already progressing at that moment.
  void NoteUserInput(std::uint32_t tab, std::uint64_t navigation_id);

  /// Records a browser-verified playback sample for a tab; the advance
  /// versus the previous sample marks trusted progression.
  void NotePlaybackProgress(std::uint32_t tab, std::uint64_t navigation_id,
                            double current_seconds);

  /// Switches the foreground tab.
  void SetActiveTab(std::uint32_t tab);

  /// Evaluates the playback-eligible claim for `tab`/`navigation_id`.
  ProofResult Evaluate(std::uint32_t tab, std::uint64_t navigation_id) const;

 private:
  std::uint32_t active_tab_ = 0;

  bool has_input_ = false;
  std::uint32_t last_input_tab_ = 0;
  std::uint64_t last_input_navigation_ = 0;
  double input_baseline_seconds_ = 0;
  bool progressing_at_input_ = false;

  bool has_progress_ = false;
  std::uint32_t last_progress_tab_ = 0;
  std::uint64_t last_progress_navigation_ = 0;
  double previous_progress_seconds_ = 0;
  double last_progress_seconds_ = 0;
};

}  // namespace crayon::cef_shell::input_proof
