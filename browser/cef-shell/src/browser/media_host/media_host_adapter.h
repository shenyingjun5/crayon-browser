#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "browser/observation_gateway/observation_gateway.h"
#include "browser/media_host/media_host_transport.h"

namespace crayon::browser::cef_shell::media_host {

namespace media_host_ipc = ::crayon::cef_shell::ipc::media_host;

// Browser-verified fact. Full URLs remain private to this adapter/host path and
// are never present in the opaque result DTO exposed to Cast UI/session code.
struct BrowserMediaFact final {
  ::crayon::cef_shell::gateway::GatewayEvent observation;
  std::string page_url;
  std::uint64_t observed_at_ms = 0;
};

enum class MediaPlanningEventKind { kCandidate = 0, kDecision, kError };

struct MediaPlanningEvent final {
  MediaPlanningEventKind kind = MediaPlanningEventKind::kError;
  std::optional<std::uint64_t> candidate_id;
  std::string redacted_origin;
  std::optional<media_host_ipc::Protocol> protocol;
  std::optional<media_host_ipc::Decision> decision;
  std::optional<media_host_ipc::HostError> error;
};

// UI-thread-only protocol adapter. It owns generation fencing and opaque
// candidate correlation; ObservationGateway conversion belongs to M05b2c.
class MediaHostAdapter final {
public:
  explicit MediaHostAdapter(std::unique_ptr<MediaHostTransport> transport);
  bool Start(std::string executable_path);
  void Stop();
  bool healthy() const noexcept;
  std::uint64_t cast_state_epoch() const noexcept { return cast_state_epoch_; }

  bool Submit(media_host_ipc::Message message);
  bool AdvanceNavigation(std::uint32_t tab_id, std::uint64_t navigation_id,
                         std::uint64_t generation);
  bool CloseTab(std::uint32_t tab_id, std::uint64_t generation);
  void Consume(std::vector<BrowserMediaFact> facts);
  void Tick();
  std::vector<media_host_ipc::Message> Drain(std::size_t max_messages);
  std::vector<MediaPlanningEvent> DrainPlanning(std::size_t max_events);

  // Asynchronous Cast control surface. These methods only validate and
  // enqueue MHV1 commands; child I/O and SDK work never run on this thread.
  bool RequestDiscovery(media_host_ipc::DiscoveryAction action);
  bool RequestDevicePage(std::optional<std::uint64_t> snapshot_revision,
                         std::uint16_t offset);
  bool RequestStartCast(std::uint64_t candidate_id, std::string device_id,
                        bool handoff_available);
  bool RequestStopCast(std::uint64_t session_generation);
  std::optional<std::string> RequestResolveCastCode(std::string cast_code);
  std::optional<std::string> RequestControlCast(
      std::uint64_t session_generation,
      media_host_ipc::CastControlAction action,
      std::optional<std::uint64_t> position_seconds);
  std::vector<media_host_ipc::Message> DrainCast(std::size_t max_messages);

private:
  struct Context final {
    std::string tab_id;
    std::uint64_t navigation_id = 0;
    std::uint64_t generation = 0;
  };
  struct TabState final {
    std::uint64_t navigation_id = 0;
    std::uint64_t generation = 0;
    bool closed = false;
  };
  enum class CastRequestKind {
    kDiscovery = 0,
    kListDevices,
    kStartCast,
    kStopCast,
    kResolveCastCode,
    kControlCast,
    kPollSessionEvents,
  };

  bool Admit(const media_host_ipc::Message &message, std::string *request_id,
             Context *context);
  bool Current(const Context &context) const;
  void PollReplies();
  void MaybePollSessionEvents();
  bool HandleStaleCastReply(const media_host_ipc::Message &message);
  bool HandleCastReply(media_host_ipc::Message message, CastRequestKind kind);
  bool PushCastReply(media_host_ipc::Message message);
  void
  FailCastState(std::optional<std::uint64_t> cleanup_generation = std::nullopt);
  void InvalidateTab(const std::string &tab_id);
  void FailAll();
  bool EnsureContext(const BrowserMediaFact &fact);
  std::string NextRequestId();

  std::unique_ptr<MediaHostTransport> process_;
  std::map<std::string, Context> requests_;
  std::map<std::string, CastRequestKind> cast_requests_;
  std::map<std::uint64_t, Context> candidates_;
  std::map<std::string, TabState> tabs_;
  std::deque<media_host_ipc::Message> replies_;
  std::deque<MediaPlanningEvent> planning_events_;
  std::deque<media_host_ipc::Message> cast_replies_;
  std::optional<std::uint64_t> active_session_generation_;
  std::optional<std::string> poll_request_id_;
  std::uint64_t last_session_generation_ = 0;
  std::uint64_t last_state_revision_ = 0;
  std::uint64_t last_host_dropped_ = 0;
  std::chrono::steady_clock::time_point next_session_poll_{};
  bool saw_healthy_ = false;
  std::uint64_t process_generation_ = 0;
  std::uint64_t next_request_id_ = 1;
  std::uint64_t cast_state_epoch_ = 0;
};

}  // namespace crayon::browser::cef_shell::media_host
