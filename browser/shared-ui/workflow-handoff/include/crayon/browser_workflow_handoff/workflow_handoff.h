// WFL-03: immutable AwaitingHuman desktop presentation contract.
//
// Lifecycle ownership remains in crayon-workflow::handoff. This module only
// maps its closed view snapshot to localized, accessible UI fields.
#pragma once

#include <cstdint>
#include <string>

namespace crayon::browser_workflow_handoff {

enum class HandoffReason { kCaptcha = 0, kLoginRequired, kRiskCheck, kUnknown };

struct HandoffPresentation {
  std::string origin;
  std::string reason_locale_key;
  std::uint64_t remaining_ms = 0;
  bool show_continue = true;
  bool show_cancel = true;
  bool modal = true;
};

/// Builds an immutable presentation. `origin` is the domain-validated origin
/// from the Rust view; challenge values and evidence notes are not accepted.
HandoffPresentation PresentHandoff(HandoffReason reason,
                                   const std::string& origin,
                                   std::uint64_t remaining_ms);

}  // namespace crayon::browser_workflow_handoff
