#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "browser/input_proof/input_proof_gate.h"
#include "renderer/media_observer/media_observer.h"

namespace crayon::cef_shell::input_proof {

inline constexpr std::size_t kMaxPlayerProofsPerPage =
    renderer::kMaxMediaElements;
inline constexpr std::size_t kMaxPlayerProofs = 256;

struct PlayerReference {
  std::uint64_t instance_id = 0;
  std::uint64_t source_revision = 0;
};

// Browser UI-thread owner of independent progression/input baselines. The
// caller fences navigation and verifies the actual main-frame sender before
// Observe. Page-reported identities never constitute input authorization.
// Source URLs are only compared in memory; never expose or log this registry.
class PlayerInputProof final {
public:
  ProofResult Observe(std::uint32_t tab_id,
                      const renderer::MediaObservation &observation,
                      std::uint64_t source_epoch = 1);
  // Identity alone is not eligibility. The caller must also require Observe
  // to return kEligible for the same sample/context before forwarding proof.
  std::optional<PlayerReference> Reference(std::uint32_t tab_id,
                                           std::uint64_t navigation_id,
                                           std::uint32_t element_id) const;
  bool Remove(std::uint32_t tab_id, std::uint64_t navigation_id,
              std::uint32_t element_id, std::uint64_t source_epoch);
  void NoteUserInput(std::uint32_t tab_id, std::uint64_t navigation_id);
  void SetActiveTab(std::uint32_t tab_id);
  void ForgetTab(std::uint32_t tab_id);

  std::size_t retained_count() const { return players_.size(); }
  std::uint64_t dropped_capacity_total() const {
    return dropped_capacity_total_;
  }

private:
  struct Player {
    std::uint32_t tab_id;
    std::uint64_t navigation_id;
    std::uint32_t element_id;
    renderer::MediaSourceKind source_kind;
    std::string source_url;
    InputProofGate gate;
    std::uint64_t source_epoch;
    PlayerReference reference;
  };

  std::uint32_t active_tab_ = 0;
  std::uint64_t dropped_capacity_total_ = 0;
  std::uint64_t next_instance_id_ = 1;
  std::vector<Player> players_;
};

} // namespace crayon::cef_shell::input_proof
