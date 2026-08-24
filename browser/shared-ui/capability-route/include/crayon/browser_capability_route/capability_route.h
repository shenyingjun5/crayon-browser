// HUB-06: capability route preview and temporary override view model
// (HB-006).
//
// Presents the policy verdict for a task — selected route, reason,
// candidates, exclusions and a per-candidate external-data flag — and a
// Proceed/Cancel flow.  The user may attach a TEMPORARY override
// (preferred kind, allow-external toggle) that lives only inside this
// model: it dies on Proceed/Cancel/destruction, is never persisted, and
// is returned to the caller so the runtime can re-run the policy with
// it (HUB-04) before anything executes.
//
// All display fields are closed tokens or wire names; free text never
// enters this model.  Cost disclosure is intentionally absent until a
// provider cost source exists (HUB-09+).
//
// Accessibility: every field maps to a locale label key; the parity
// contract test pins the key set.
//
// Thread contract: single-threaded, UI thread only.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace crayon::browser_capability_route {

/// Maximum lengths and bounds for presentation fields, in bytes.
inline constexpr std::size_t kMaxIdLen = 128;
inline constexpr std::size_t kMaxVersionLen = 32;
inline constexpr std::size_t kMaxRows = 16;

/// Closed route kinds (hub wire names).
struct RouteKinds {
  static constexpr const char* kPartner = "partner";
  static constexpr const char* kSiteSkill = "site_skill";
  static constexpr const char* kWebAutomation = "web_automation";
  static constexpr const char* kHumanHandoff = "human_handoff";
  static constexpr const char* kReject = "reject";

  /// Reports whether `value` is one of the five closed kinds.
  static bool IsValid(const std::string& value);
};

/// Closed policy reasons (HUB-04 wire names).
bool IsValidReason(const std::string& value);

/// Closed trust levels (domain wire names).
bool IsValidTrust(const std::string& value);

/// Validates a closed token field (`[a-z0-9_.:-]`, bounded).
bool IsValidToken(const std::string& value);

/// One candidate row for display.
struct RouteCandidateView {
  std::string capability_id;
  std::string version;
  std::string kind;             // RouteKinds wire name
  std::string trust;            // domain trust wire name
  bool sends_data_external = false;  // declared ExternalEndpoint scope
};

/// One excluded candidate row.
struct RouteExclusionView {
  std::string capability_id;
  std::string reason;           // HUB-04 exclusion reason wire name
};

/// The presented route preview (built by the shell from the policy
/// output; immutable once presented).
struct CapabilityRoutePreview {
  std::string selected_id;      // empty when nothing was selectable
  std::string selected_kind;    // "" or a RouteKinds wire name
  std::string reason;           // policy reason wire name
  std::vector<RouteCandidateView> candidates;
  std::vector<RouteExclusionView> exclusions;
};

/// Temporary override the user may attach to the presented task.  It is
/// an input for re-running the policy, not a route decision itself.
struct RouteOverride {
  bool present = false;
  std::string prefer_kind;               // "" or non-reject kind
  bool allow_external_endpoint = true;
};

/// Presentation lifecycle states.
enum class RouteState {
  kNone = 0,
  kPresented,
  kProceeded,
  kCancelled,
};

class CapabilityRouteModel final {
 public:
  CapabilityRouteModel() = default;

  /// Presents a preview; validates all tokens/bounds.  On failure the
  /// previous state is kept untouched.  Bumps the revision.
  bool Present(const CapabilityRoutePreview& preview);

  /// Attaches the temporary override to the presented task.  Rejects
  /// invalid overrides ("reject" is not a preference) and any call made
  /// outside the Presented state.
  bool ApplyOverride(const RouteOverride& override_request);

  /// Leaves the Presented state with the user's consent.  When an
  /// override is attached, `out_effective` receives it for the runtime
  /// to re-evaluate the policy; otherwise it is left untouched.  The
  /// override dies here either way.
  bool Proceed(RouteOverride* out_effective);

  /// Leaves the Presented state without acting.
  bool Cancel();

  [[nodiscard]] RouteState state() const { return state_; }

  /// Monotonic counter bumped by every successful Present; shells use it
  /// to detect that the underlying decision changed underneath them.
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

  [[nodiscard]] const CapabilityRoutePreview* preview() const {
    return state_ == RouteState::kNone ? nullptr : &preview_;
  }

  /// Deterministic summary lines for UI text assembly: selected row,
  /// candidate rows (`kind|id|trust|external`), exclusion rows, then the
  /// data-external disclosure marker when anything sends data out.
  [[nodiscard]] std::string Summary() const;

 private:
  RouteState state_ = RouteState::kNone;
  std::uint64_t revision_ = 0;
  CapabilityRoutePreview preview_;
  RouteOverride override_;
};

}  // namespace crayon::browser_capability_route
