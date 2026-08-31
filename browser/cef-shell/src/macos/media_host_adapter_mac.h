#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "browser/observation_gateway/observation_gateway.h"
#include "macos/media_host_process_mac.h"

namespace crayon::browser::cef_shell::macos {

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
  MediaHostAdapter();
  explicit MediaHostAdapter(std::unique_ptr<MediaHostTransport> transport);
  bool Start(std::string executable_path);
  void Stop();
  bool healthy() const noexcept;

  bool Submit(media_host_ipc::Message message);
  bool AdvanceNavigation(std::uint32_t tab_id, std::uint64_t navigation_id,
                         std::uint64_t generation);
  bool CloseTab(std::uint32_t tab_id, std::uint64_t generation);
  void Consume(std::vector<BrowserMediaFact> facts);
  void Tick();
  std::vector<media_host_ipc::Message> Drain(std::size_t max_messages);
  std::vector<MediaPlanningEvent> DrainPlanning(std::size_t max_events);

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

  bool Admit(const media_host_ipc::Message &message, std::string *request_id,
             Context *context);
  bool Current(const Context &context) const;
  void PollReplies();
  void InvalidateTab(const std::string &tab_id);
  void FailAll();
  bool EnsureContext(const BrowserMediaFact &fact);
  std::string NextRequestId();

  std::unique_ptr<MediaHostTransport> process_;
  std::map<std::string, Context> requests_;
  std::map<std::uint64_t, Context> candidates_;
  std::map<std::string, TabState> tabs_;
  std::deque<media_host_ipc::Message> replies_;
  std::deque<MediaPlanningEvent> planning_events_;
  bool saw_healthy_ = false;
  std::uint64_t process_generation_ = 0;
  std::uint64_t next_request_id_ = 1;
};

} // namespace crayon::browser::cef_shell::macos
