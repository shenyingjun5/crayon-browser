#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "browser/media_host/media_host_adapter.h"
#include "crayon/browser_cast_view/cast_ui_coordinator.h"

namespace crayon::browser::cef_shell::media_host {

namespace media_host_ipc = ::crayon::cef_shell::ipc::media_host;

// UI-thread command port. Every callback must only enqueue into the bounded
// media-host adapter; implementations must not perform SDK or pipe I/O.
struct CastCommandPort final {
  std::function<bool(media_host_ipc::DiscoveryAction)> discovery;
  std::function<bool(std::optional<std::uint64_t>, std::uint16_t)> list_devices;
  std::function<bool(std::uint64_t, std::string, bool)> start_cast;
  std::function<bool(std::uint64_t)> stop_cast;
  std::function<std::optional<std::string>(std::string)> resolve_cast_code;
  std::function<std::optional<std::string>(
      std::uint64_t, media_host_ipc::CastControlAction,
      std::optional<std::uint64_t>)>
      control_cast;
};

struct CastShellPresentation final {
  bool cast_code_pending = false;
  bool cast_code_failed = false;
  bool control_pending = false;
  bool control_failed = false;
  bool playback_paused = false;

  friend bool operator==(const CastShellPresentation& left,
                         const CastShellPresentation& right) {
    return left.cast_code_pending == right.cast_code_pending &&
           left.cast_code_failed == right.cast_code_failed &&
           left.control_pending == right.control_pending &&
           left.control_failed == right.control_failed &&
           left.playback_paused == right.playback_paused;
  }
  friend bool operator!=(const CastShellPresentation& left,
                         const CastShellPresentation& right) {
    return !(left == right);
  }
};

// Single UI-thread shell owner between Browser-verified media facts, the
// shared CastUiCoordinator and the asynchronous MHV1 command port. Platform
// chrome and picker widgets consume this closed presentation state in b3e2.
class CastShellController final {
 public:
  explicit CastShellController(CastCommandPort commands);

  void OnNavigation();
  void OnPageClosed();
  void OnBrowserVerifiedMedia();
  void OnHostUnavailable();
  void Shutdown();

  void ConsumePlanning(std::vector<MediaPlanningEvent> events);
  void ConsumeCast(std::vector<media_host_ipc::Message> messages);

  bool ActivateCastButton();
  bool RefreshReceivers();
  void CancelReceiverPicker();
  bool SelectReceiver(const std::string& device_id);
  // Legacy UI callback name: resolves the code only, never connects or starts.
  bool ConnectCastCode(std::string cast_code);
  bool SetPaused(bool paused);
  bool SeekSession(std::uint64_t position_seconds);
  bool StopSession();

  const browser_cast_view::CastUiCoordinator& coordinator() const {
    return coordinator_;
  }
  bool device_page_pending() const { return device_page_pending_; }
  bool start_pending() const { return start_pending_; }
  CastShellPresentation presentation() const;

 private:
  void ResetPage(bool page_active);
  void StopActiveSession();
  bool RequestFirstDevicePage(media_host_ipc::DiscoveryAction action);
  bool HandleDevicePage(const media_host_ipc::DevicePageReply& page);
  void HandleStartReply(const media_host_ipc::StartCastReply& reply);
  void HandleResolveCastCodeReply(
      const media_host_ipc::ResolveCastCodeReply& reply);
  void HandleControlCastReply(
      const media_host_ipc::ControlCastReply& reply);
  void HandleSessionEvents(const media_host_ipc::SessionEventsReply& reply);
  bool ControlSession(media_host_ipc::CastControlAction action,
                      std::optional<std::uint64_t> position_seconds);
  void FailSelection();

  CastCommandPort commands_;
  browser_cast_view::CastUiCoordinator coordinator_;
  std::optional<std::uint64_t> current_candidate_;
  std::optional<std::uint64_t> device_snapshot_revision_;
  std::uint16_t expected_device_offset_ = 0;
  std::vector<browser_cast_view::ReceiverOption> pending_receivers_;
  bool page_active_ = false;
  bool browser_verified_media_ = false;
  bool discovery_active_ = false;
  bool device_page_pending_ = false;
  bool start_pending_ = false;
  std::optional<std::string> cast_code_request_id_;
  bool cast_code_failed_ = false;
  std::optional<std::string> control_request_id_;
  bool control_failed_ = false;
  bool playback_paused_ = false;
  std::optional<media_host_ipc::CastControlAction> pending_control_action_;
  bool shutdown_ = false;
};

}  // namespace crayon::browser::cef_shell::media_host
