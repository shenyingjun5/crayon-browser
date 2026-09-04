#include <cstdlib>
#include <iostream>
#include <limits>

#include "browser/input_proof/player_input_proof.h"
#include "renderer/media_observer/media_observer.h"

namespace {
using namespace crayon::cef_shell::input_proof;
using namespace crayon::cef_shell::renderer;

#define CHECK(value)                                                           \
  do {                                                                         \
    if (!(value)) {                                                            \
      std::cerr << __LINE__ << ": " << #value << '\n';                         \
      return false;                                                            \
    }                                                                          \
  } while (false)

MediaObservation
Sample(std::uint32_t element, double seconds,
       MediaPlaybackState state = MediaPlaybackState::kPlaying) {
  MediaObservation value;
  value.navigation_id = 10;
  value.element_id = element;
  value.source_kind = MediaSourceKind::kHttpUrl;
  value.source_url = "https://media.example/video.mp4";
  value.current_time_seconds = seconds;
  value.playback = state;
  value.visible_fraction = 1;
  return value;
}

bool SeparatePlayersWithSameUrl() {
  PlayerInputProof proof;
  proof.SetActiveTab(1);
  proof.Observe(1, Sample(1, 0, MediaPlaybackState::kPaused));
  proof.Observe(1, Sample(2, 10));
  proof.Observe(1, Sample(2, 11));
  // A's pause must not clear the already-progressing state of B.
  proof.Observe(1, Sample(1, 0, MediaPlaybackState::kPaused));
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 0)) != ProofResult::kEligible);
  CHECK(proof.Observe(1, Sample(2, 12)) ==
        ProofResult::kDeniedAlreadyProgressing);
  CHECK(proof.Observe(1, Sample(1, 0.2)) == ProofResult::kEligible);
  CHECK(proof.retained_count() == 2);
  return true;
}

bool ProgressCannotCrossElements() {
  PlayerInputProof proof;
  proof.SetActiveTab(1);
  proof.Observe(1, Sample(1, 0, MediaPlaybackState::kPaused));
  proof.Observe(1, Sample(2, 0, MediaPlaybackState::kPaused));
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 1)) == ProofResult::kEligible);
  CHECK(proof.Observe(1, Sample(2, 0)) ==
        ProofResult::kDeniedNoProgressAfterInput);
  CHECK(proof.Observe(1, Sample(3, 5)) == ProofResult::kDeniedNoTrustedInput);
  return true;
}

bool SourceAndNavigationInvalidateInput() {
  PlayerInputProof proof;
  proof.SetActiveTab(1);
  proof.Observe(1, Sample(1, 0, MediaPlaybackState::kPaused));
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 1)) == ProofResult::kEligible);
  auto changed = Sample(1, 2);
  changed.source_url = "https://media.example/other.mp4";
  CHECK(proof.Observe(1, changed) == ProofResult::kDeniedNoTrustedInput);
  // Returning to the original URL does not recover its old proof.
  CHECK(proof.Observe(1, Sample(1, 3)) == ProofResult::kDeniedNoTrustedInput);
  proof.ForgetTab(1);
  proof.NoteUserInput(1, 10);
  auto next = Sample(1, 4);
  next.navigation_id = 11;
  CHECK(proof.Observe(1, next) == ProofResult::kDeniedNoTrustedInput);
  CHECK(proof.retained_count() == 1);
  // A new navigation needs its own observed paused source and later input.
  next.playback = MediaPlaybackState::kPaused;
  proof.Observe(1, next);
  proof.NoteUserInput(1, 11);
  next.playback = MediaPlaybackState::kPlaying;
  next.current_time_seconds += 0.2;
  CHECK(proof.Observe(1, next) == ProofResult::kEligible);
  proof.ForgetTab(1);
  proof.ForgetTab(1);
  CHECK(proof.retained_count() == 0);
  return true;
}

bool FocusRevokesInputButKeepsProgress() {
  PlayerInputProof proof;
  proof.SetActiveTab(1);
  proof.Observe(1, Sample(1, 0));
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 1)) == ProofResult::kEligible);
  proof.SetActiveTab(2);
  proof.SetActiveTab(1);
  CHECK(proof.Observe(1, Sample(1, 2)) == ProofResult::kDeniedNoTrustedInput);
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 3)) ==
        ProofResult::kDeniedAlreadyProgressing);
  // A paused sample can advance slightly; sample then freeze its baseline.
  proof.Observe(1, Sample(1, 3.1, MediaPlaybackState::kPaused));
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 3.3)) == ProofResult::kEligible);
  proof.SetActiveTab(1); // Repeated focus notification does not revoke.
  CHECK(proof.Observe(1, Sample(1, 3.4)) == ProofResult::kEligible);
  return true;
}

bool BoundedAndInvalidFacts() {
  PlayerInputProof proof;
  proof.SetActiveTab(1);
  auto invalid = Sample(0, 0);
  CHECK(proof.Observe(1, invalid) != ProofResult::kEligible);
  invalid = Sample(1, std::numeric_limits<double>::quiet_NaN());
  CHECK(proof.Observe(1, invalid) != ProofResult::kEligible);
  invalid = Sample(1, 0);
  invalid.source_url.assign(kMaxSourceUrlLen + 1, 'x');
  CHECK(proof.Observe(1, invalid) != ProofResult::kEligible);
  CHECK(proof.retained_count() == 0);
  for (std::uint32_t tab = 1; tab <= kMaxPlayerProofs / kMaxPlayerProofsPerPage;
       ++tab) {
    for (std::uint32_t element = 1; element <= kMaxPlayerProofsPerPage;
         ++element)
      proof.Observe(tab, Sample(element, 0));
    CHECK(proof.Observe(tab, Sample(kMaxPlayerProofsPerPage + 1, 0)) !=
          ProofResult::kEligible);
  }
  CHECK(proof.retained_count() == kMaxPlayerProofs);
  CHECK(proof.Observe(100, Sample(1, 0)) != ProofResult::kEligible);
  CHECK(proof.dropped_capacity_total() ==
        kMaxPlayerProofs / kMaxPlayerProofsPerPage + 1);
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 1)) == ProofResult::kEligible);
  proof.ForgetTab(1);
  CHECK(proof.retained_count() == kMaxPlayerProofs - kMaxPlayerProofsPerPage);
  return true;
}

bool BrowserInstancesAndSourceEpochs() {
  PlayerInputProof proof;
  proof.SetActiveTab(1);
  CHECK(!proof.Reference(1, 10, 1));
  proof.Observe(1, Sample(1, 0, MediaPlaybackState::kPaused), 1);
  proof.Observe(1, Sample(2, 0, MediaPlaybackState::kPaused), 1);
  const auto first = *proof.Reference(1, 10, 1);
  const auto second = *proof.Reference(1, 10, 2);
  CHECK(first.instance_id != second.instance_id);
  CHECK(first.source_revision == 1 && second.source_revision == 1);
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 1), 1) == ProofResult::kEligible);
  CHECK(proof.Reference(1, 10, 1)->source_revision == first.source_revision);
  // Reloading the same URL is a new source, not the old playback proof.
  CHECK(proof.Observe(1, Sample(1, 0, MediaPlaybackState::kPaused), 2) !=
        ProofResult::kEligible);
  const auto reloaded = *proof.Reference(1, 10, 1);
  CHECK(reloaded.instance_id == first.instance_id);
  CHECK(reloaded.source_revision == first.source_revision + 1);
  CHECK(proof.Observe(1, Sample(1, 1), 2) ==
        ProofResult::kDeniedNoTrustedInput);
  CHECK(proof.Observe(1, Sample(1, 0, MediaPlaybackState::kPaused), 1) ==
        ProofResult::kDeniedStaleSource);
  CHECK(proof.Reference(1, 10, 1)->source_revision == reloaded.source_revision);
  CHECK(!proof.Remove(1, 10, 1, 1));
  CHECK(proof.Remove(1, 10, 1, 2));
  CHECK(!proof.Reference(1, 10, 1));
  CHECK(!proof.Remove(1, 10, 1, 2));
  proof.NoteUserInput(1, 10);
  CHECK(proof.Observe(1, Sample(1, 1), 2) ==
        ProofResult::kDeniedNoTrustedInput);
  CHECK(proof.Reference(1, 10, 1)->instance_id != first.instance_id);
  CHECK(proof.Reference(1, 10, 2)->instance_id == second.instance_id);
  proof.ForgetTab(1);
  CHECK(!proof.Reference(1, 10, 2));
  return true;
}

bool OpaqueSourcesInvalidateAndRemovalFreesCapacity() {
  PlayerInputProof proof;
  proof.SetActiveTab(1);
  auto blob = Sample(1, 0, MediaPlaybackState::kPaused);
  blob.source_kind = MediaSourceKind::kBlobUrl;
  blob.source_url.clear();
  proof.Observe(1, blob, 1);
  proof.NoteUserInput(1, 10);
  blob.playback = MediaPlaybackState::kPlaying;
  blob.current_time_seconds = 1;
  CHECK(proof.Observe(1, blob, 1) == ProofResult::kEligible);
  CHECK(proof.Observe(1, blob, 2) == ProofResult::kDeniedNoTrustedInput);
  CHECK(proof.Reference(1, 10, 1)->source_revision == 2);
  CHECK(proof.Observe(1, blob, 0) != ProofResult::kEligible);
  for (std::uint32_t id = 2; id <= kMaxPlayerProofsPerPage; ++id)
    proof.Observe(1, Sample(id, 0), 1);
  proof.Observe(1, Sample(kMaxPlayerProofsPerPage + 1, 0), 1);
  CHECK(!proof.Reference(1, 10, kMaxPlayerProofsPerPage + 1));
  CHECK(proof.Remove(1, 10, 1, 2));
  proof.Observe(1, Sample(kMaxPlayerProofsPerPage + 1, 0), 1);
  CHECK(proof.Reference(1, 10, kMaxPlayerProofsPerPage + 1));
  CHECK(proof.retained_count() == kMaxPlayerProofsPerPage);
  return true;
}
} // namespace

int main() {
  if (!SeparatePlayersWithSameUrl() || !ProgressCannotCrossElements() ||
      !SourceAndNavigationInvalidateInput() ||
      !FocusRevokesInputButKeepsProgress() || !BoundedAndInvalidFacts() ||
      !BrowserInstancesAndSourceEpochs() ||
      !OpaqueSourcesInvalidateAndRemovalFreesCapacity())
    return EXIT_FAILURE;
  std::cout << "player_input_proof_test: 7 cases passed\n";
  return EXIT_SUCCESS;
}
