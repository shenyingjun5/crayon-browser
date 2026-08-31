// PLT-M05b3a: UI-thread-only owner that keeps the toolbar cast button,
// feature surface and receiver picker synchronized. It consumes only closed
// Browser/runtime facts and emits user actions; it performs no SDK or I/O.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_cast_view/cast_feature_view.h"
#include "crayon/browser_chrome/chrome_toolbar.h"

namespace crayon::browser_cast_view {

inline constexpr std::size_t kMaxReceiverOptions = 64;
inline constexpr std::size_t kMaxReceiverIdBytes = 128;
inline constexpr std::size_t kMaxReceiverNameBytes = 512;

struct ReceiverOption final {
  std::string device_id;
  std::string display_name;
  bool is_crayon_receiver = false;

  friend bool operator==(const ReceiverOption &left,
                         const ReceiverOption &right) {
    return left.device_id == right.device_id &&
           left.display_name == right.display_name &&
           left.is_crayon_receiver == right.is_crayon_receiver;
  }
};

enum class CastUiActionKind {
  kRefreshReceivers = 0,
  kSelectReceiver,
  kStopSession,
};

struct CastUiAction final {
  CastUiActionKind kind = CastUiActionKind::kRefreshReceivers;
  std::string device_id;
  std::uint64_t session_generation = 0;

  friend bool operator==(const CastUiAction &left, const CastUiAction &right) {
    return left.kind == right.kind && left.device_id == right.device_id &&
           left.session_generation == right.session_generation;
  }
};

/// Single UI-thread owner for the cast feature projection.
///
/// The coordinator never derives eligibility from page data. Its only
/// eligibility input is the Browser-verified boolean supplied by the shell.
/// Receiver names are untrusted presentation strings; snapshots are replaced
/// atomically only after every entry passes the closed bounds.
class CastUiCoordinator final {
public:
  void SetPageActive(bool active);
  void SetMediaPresent(bool present);
  void SetBrowserVerifiedEligible(bool eligible);

  /// Opens the picker and emits one refresh action. Returns nullopt unless
  /// both view models are in their verified eligible state.
  std::optional<CastUiAction> OpenPicker();
  void CancelPicker();

  /// Atomically replaces the current picker snapshot. Invalid/oversize/
  /// duplicate snapshots are rejected without changing the prior snapshot.
  bool ReplaceReceivers(std::vector<ReceiverOption> receivers);
  std::optional<CastUiAction>
  SelectReceiver(const std::string &device_id) const;

  /// Applies an already-authoritative runtime policy result.
  bool ApplyPolicyOutcome(PolicyOutcome outcome,
                          RejectReason reason = RejectReason::kGeneral);

  /// Session generations are monotonically fenced at the UI boundary as
  /// defense in depth. Zero and stale generations are rejected; duplicate
  /// terminal delivery is an idempotent no-op.
  bool NotifySessionStarted(std::uint64_t generation);
  std::optional<CastUiAction> RequestStop();
  bool NotifySessionEnded(std::uint64_t generation);

  const CastFeatureViewModel &feature() const { return feature_; }
  const browser_chrome::CastButtonModel &button() const { return button_; }
  const std::vector<ReceiverOption> &receivers() const { return receivers_; }
  std::optional<std::uint64_t> active_session_generation() const {
    return active_session_generation_;
  }

private:
  static bool ValidReceiver(const ReceiverOption &receiver);
  bool PickerOpen() const;
  void ClearPicker();

  CastFeatureViewModel feature_;
  browser_chrome::CastButtonModel button_;
  std::vector<ReceiverOption> receivers_;
  std::optional<std::uint64_t> active_session_generation_;
  std::uint64_t last_session_generation_ = 0;
  bool page_active_ = false;
  bool media_present_ = false;
};

} // namespace crayon::browser_cast_view
