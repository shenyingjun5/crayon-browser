#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "macos/media_host_process_mac.h"

namespace crayon::browser::cef_shell::macos {

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
  void Tick();
  std::vector<media_host_ipc::Message> Drain(std::size_t max_messages);

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

  std::unique_ptr<MediaHostTransport> process_;
  std::map<std::string, Context> requests_;
  std::map<std::uint64_t, Context> candidates_;
  std::map<std::string, TabState> tabs_;
  std::deque<media_host_ipc::Message> replies_;
  bool saw_healthy_ = false;
  std::uint64_t process_generation_ = 0;
};

} // namespace crayon::browser::cef_shell::macos
