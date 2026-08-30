#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "crayon/cef_shell_ipc/content_host_codec.h"

namespace crayon::browser::cef_shell::page_markdown {

enum class PreviewAssemblyResult {
  kPending = 0,
  kCompleted,
  kRejected,
};

// Bounded single-request assembler for Browser-filtered content-host replies.
// It owns no CEF or platform state and releases all partial Markdown on error.
class PageMarkdownPreviewAssembler final {
 public:
  bool Begin(std::string request_id, std::string tab_id,
             std::uint64_t navigation_id);
  PreviewAssemblyResult Consume(
      const ::crayon::cef_shell::ipc::content_host::Message& message);
  std::optional<std::string> TakeCompleted();
  void Cancel();

  bool active() const noexcept { return active_; }

 private:
  void Reject();

  std::string request_id_;
  std::string tab_id_;
  std::uint64_t navigation_id_ = 0;
  std::uint32_t next_sequence_ = 0;
  std::string markdown_;
  bool active_ = false;
  bool completed_ = false;
};

}  // namespace crayon::browser::cef_shell::page_markdown
