#include "browser/input_proof/player_input_proof.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crayon::cef_shell::input_proof {
namespace {

bool ValidSample(std::uint32_t tab_id,
                 const renderer::MediaObservation &sample) {
  using renderer::MediaPlaybackState;
  using renderer::MediaSourceKind;
  if (!tab_id || !sample.navigation_id || !sample.element_id ||
      !std::isfinite(sample.current_time_seconds) ||
      sample.current_time_seconds < 0 ||
      !std::isfinite(sample.visible_fraction) || sample.visible_fraction < 0 ||
      sample.visible_fraction > 1 ||
      sample.source_url.size() > renderer::kMaxSourceUrlLen)
    return false;
  switch (sample.playback) {
  case MediaPlaybackState::kIdle:
  case MediaPlaybackState::kPlaying:
  case MediaPlaybackState::kPaused:
  case MediaPlaybackState::kEnded:
    break;
  default:
    return false;
  }
  switch (sample.source_kind) {
  case MediaSourceKind::kHttpUrl:
    return !sample.source_url
                .empty(); // URL normalization belongs to the gateway.
  case MediaSourceKind::kBlobUrl:
  case MediaSourceKind::kMediaStream:
  case MediaSourceKind::kUnknown:
    return sample.source_url.empty();
  }
  return false;
}

} // namespace

ProofResult PlayerInputProof::Observe(std::uint32_t tab_id,
                                      const renderer::MediaObservation &sample,
                                      std::uint64_t source_epoch) {
  if (!source_epoch || !ValidSample(tab_id, sample))
    return ProofResult::kDeniedNoProgressAfterInput;
  auto found =
      std::find_if(players_.begin(), players_.end(), [&](const auto &player) {
        return player.tab_id == tab_id &&
               player.navigation_id == sample.navigation_id &&
               player.element_id == sample.element_id;
      });
  if (found == players_.end()) {
    const auto page_count = std::count_if(
        players_.begin(), players_.end(), [&](const auto &player) {
          return player.tab_id == tab_id &&
                 player.navigation_id == sample.navigation_id;
        });
    if (!next_instance_id_ || players_.size() >= kMaxPlayerProofs ||
        static_cast<std::size_t>(page_count) >= kMaxPlayerProofsPerPage) {
      if (dropped_capacity_total_ < std::numeric_limits<std::uint64_t>::max())
        ++dropped_capacity_total_;
      return ProofResult::kDeniedNoProgressAfterInput;
    }
    players_.push_back({tab_id,
                        sample.navigation_id,
                        sample.element_id,
                        sample.source_kind,
                        sample.source_url,
                        InputProofGate(active_tab_),
                        source_epoch,
                        {next_instance_id_++, 1}});
    found = players_.end() - 1;
  } else if (!found->reference.source_revision ||
             source_epoch < found->source_epoch) {
    return ProofResult::kDeniedStaleSource;
  } else if (source_epoch != found->source_epoch ||
             found->source_kind != sample.source_kind ||
             found->source_url != sample.source_url) {
    found->gate.RevokeInput();
    if (found->reference.source_revision ==
        std::numeric_limits<std::uint64_t>::max()) {
      // Retain a non-reusable exhausted identity; do not wrap a revision back
      // into a previously authorized reference.
      found->reference.source_revision = 0;
      return ProofResult::kDeniedStaleSource;
    }
    ++found->reference.source_revision;
    found->source_epoch = source_epoch;
    found->source_kind = sample.source_kind;
    found->source_url = sample.source_url;
    found->gate = InputProofGate(active_tab_);
  }
  found->gate.NotePlaybackProgress(tab_id, sample.navigation_id,
                                   sample.current_time_seconds);
  if (sample.playback != renderer::MediaPlaybackState::kPlaying) {
    found->gate.NotePlaybackSuspended(tab_id, sample.navigation_id);
    return ProofResult::kDeniedNoProgressAfterInput;
  }
  if (sample.visible_fraction <= 0)
    return ProofResult::kDeniedNoProgressAfterInput;
  return found->gate.Evaluate(tab_id, sample.navigation_id);
}

std::optional<PlayerReference>
PlayerInputProof::Reference(std::uint32_t tab_id, std::uint64_t navigation_id,
                            std::uint32_t element_id) const {
  for (const auto &player : players_)
    if (player.tab_id == tab_id && player.navigation_id == navigation_id &&
        player.element_id == element_id && player.reference.source_revision)
      return player.reference;
  return std::nullopt;
}

bool PlayerInputProof::Remove(std::uint32_t tab_id, std::uint64_t navigation_id,
                              std::uint32_t element_id,
                              std::uint64_t source_epoch) {
  const auto found =
      std::find_if(players_.begin(), players_.end(), [&](const auto &player) {
        return player.tab_id == tab_id &&
               player.navigation_id == navigation_id &&
               player.element_id == element_id &&
               player.source_epoch == source_epoch;
      });
  if (found == players_.end())
    return false;
  players_.erase(found);
  return true;
}

void PlayerInputProof::NoteUserInput(std::uint32_t tab_id,
                                     std::uint64_t navigation_id) {
  if (tab_id != active_tab_)
    return;
  for (auto &player : players_) {
    if (player.tab_id == tab_id && player.navigation_id == navigation_id &&
        player.reference.source_revision)
      player.gate.NoteUserInput(tab_id, navigation_id);
  }
}

void PlayerInputProof::SetActiveTab(std::uint32_t tab_id) {
  if (tab_id == active_tab_)
    return;
  active_tab_ = tab_id;
  for (auto &player : players_) {
    player.gate.RevokeInput();
    player.gate.SetActiveTab(tab_id);
  }
}

void PlayerInputProof::ForgetTab(std::uint32_t tab_id) {
  players_.erase(std::remove_if(players_.begin(), players_.end(),
                                [&](const auto &player) {
                                  return player.tab_id == tab_id;
                                }),
                 players_.end());
}

} // namespace crayon::cef_shell::input_proof
