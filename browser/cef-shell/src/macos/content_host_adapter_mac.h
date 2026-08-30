#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "browser/page_snapshot_gateway/page_snapshot_gateway.h"
#include "browser/page_snapshot_gateway/page_snapshot_observer.h"
#include "macos/content_host_process_mac.h"

namespace crayon::browser::cef_shell::macos {

using ContentSnapshotEvent = ::crayon::cef_shell::gateway::SnapshotGatewayEvent;

// Browser-UI-thread adapter from verified CEF snapshot events to the bounded
// content-host transport. Rust remains the only extract/Markdown owner.
class ContentHostAdapter final : public gateway::PageSnapshotObserver {
 public:
  ContentHostAdapter();
  explicit ContentHostAdapter(std::unique_ptr<ContentHostTransport> transport);
  bool Start(std::string executable_path);
  void Stop();
  bool healthy() const noexcept;

  void Consume(std::vector<ContentSnapshotEvent> events);
  void Tick();
  std::vector<content_host_ipc::Message> Drain(std::size_t max_messages);

  void OnSnapshotStarted(
      const browser_engine::SnapshotRequest& request) override;
  void OnSnapshotCancelled(
      const browser_engine::SnapshotRequestId& request_id) override;
  void OnSnapshotNavigation(
      const browser_engine::TabId& tab_id,
      browser_engine::NavigationId navigation_id) override;
  void OnSnapshotClosed(const browser_engine::TabId& tab_id) override;
  void OnSnapshotShutdown() override;

 private:
  struct RequestState final {
    std::string tab_id;
    std::uint64_t navigation_id = 0;
    content_host_ipc::Mode mode = content_host_ipc::Mode::kStandard;
    std::uint32_t next_batch_sequence = 0;
    bool began = false;
  };

  void ConsumeChunk(const browser_engine::SnapshotChunk& chunk);
  void ConsumeTerminal(const browser_engine::SnapshotTerminal& terminal);
  void PollReplies();
  bool Send(content_host_ipc::Message message);
  void FailAll();

  std::unique_ptr<ContentHostTransport> process_;
  std::map<std::string, RequestState> requests_;
  std::deque<content_host_ipc::Message> replies_;
};

}  // namespace crayon::browser::cef_shell::macos
