#include "crayon/browser_workflow_handoff/workflow_handoff.h"

namespace crayon::browser_workflow_handoff {
namespace {

const char* ReasonLocaleKey(HandoffReason reason) {
  switch (reason) {
    case HandoffReason::kCaptcha:
      return "workflow.handoff.reason.captcha";
    case HandoffReason::kLoginRequired:
      return "workflow.handoff.reason.login_required";
    case HandoffReason::kRiskCheck:
      return "workflow.handoff.reason.risk_check";
    case HandoffReason::kUnknown:
      return "workflow.handoff.reason.unknown";
  }
  return "workflow.handoff.reason.unknown";
}

}  // namespace

HandoffPresentation PresentHandoff(HandoffReason reason,
                                   const std::string& origin,
                                   std::uint64_t remaining_ms) {
  return HandoffPresentation{origin, ReasonLocaleKey(reason), remaining_ms,
                             true, true, true};
}

}  // namespace crayon::browser_workflow_handoff
