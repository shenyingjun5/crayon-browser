#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_cast_view/cast_ui_coordinator.h"
#include "macos/media_host_adapter_mac.h"

namespace crayon::browser::cef_shell::macos {

// UI-thread command port. Every callback must only enqueue into the bounded
// media-host adapter; implementations must not perform SDK or pipe I/O.
struct CastCommandPort final {
  std::function<bool(media_host_ipc::DiscoveryAction)> discovery;
  std::function<bool(std::optional<std::uint64_t>, std::uint16_t)> list_devices;
  std::function<bool(std::uint64_t, std::string, bool)> start_cast;
  std::function<bool(std::uint64_t)> stop_cast;
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
  bool SelectReceiver(const std::string &device_id);
  bool StopSession();

  const browser_cast_view::CastUiCoordinator &coordinator() const {
    return coordinator_;
  }
  bool device_page_pending() const { return device_page_pending_; }
  bool start_pending() const { return start_pending_; }

private:
  void ResetPage(bool page_active);
  void StopActiveSession();
  bool RequestFirstDevicePage(media_host_ipc::DiscoveryAction action);
  bool HandleDevicePage(const media_host_ipc::DevicePageReply &page);
  void HandleStartReply(const media_host_ipc::StartCastReply &reply);
  void HandleSessionEvents(const media_host_ipc::SessionEventsReply &reply);
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
  bool shutdown_ = false;
};

} // namespace crayon::browser::cef_shell::macos
