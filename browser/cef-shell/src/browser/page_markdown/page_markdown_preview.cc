#include "browser/page_markdown/page_markdown_preview.h"

#include <utility>
#include <variant>

#include "crayon/browser_page_tools/page_markdown_export.h"

namespace crayon::browser::cef_shell::page_markdown {

void PageMarkdownExportSession::Activate(int browser_id) {
  browser_id_ = browser_id > 0 ? browser_id : -1;
}

void PageMarkdownExportSession::Invalidate() { browser_id_ = -1; }

bool PageMarkdownExportSession::CanExport(int browser_id) const noexcept {
  return browser_id_ > 0 && browser_id == browser_id_;
}

CopyMarkdownResult PageMarkdownExportSession::Copy(
    int browser_id, const std::string& markdown,
    const std::function<bool(const std::string&)>& clipboard_write) const {
  if (!CanExport(browser_id)) return CopyMarkdownResult::kInvalidSession;
  if (markdown.empty() ||
      markdown.size() > browser_page_tools::kMaxExportMarkdownBytes) {
    return CopyMarkdownResult::kInvalidPayload;
  }
  if (!clipboard_write || !clipboard_write(markdown)) {
    return CopyMarkdownResult::kWriteFailed;
  }
  return CopyMarkdownResult::kCopied;
}

bool PageMarkdownPreviewAssembler::Begin(std::string request_id,
                                         std::string tab_id,
                                         std::uint64_t navigation_id) {
  if (active_ || request_id.empty() || tab_id.empty() || navigation_id == 0) {
    return false;
  }
  request_id_ = std::move(request_id);
  tab_id_ = std::move(tab_id);
  navigation_id_ = navigation_id;
  next_sequence_ = 0;
  markdown_.clear();
  active_ = true;
  completed_ = false;
  return true;
}

PreviewAssemblyResult PageMarkdownPreviewAssembler::Consume(
    const ::crayon::cef_shell::ipc::content_host::Message& message) {
  if (!active_) return PreviewAssemblyResult::kRejected;
  const auto* chunk =
      std::get_if<::crayon::cef_shell::ipc::content_host::MarkdownChunk>(
          &message);
  if (!chunk || chunk->request_id != request_id_ || chunk->tab_id != tab_id_ ||
      chunk->navigation_id != navigation_id_ ||
      chunk->generation != navigation_id_ ||
      chunk->sequence != next_sequence_ ||
      chunk->markdown.size() >
          browser_page_tools::kMaxExportMarkdownBytes - markdown_.size()) {
    Reject();
    return PreviewAssemblyResult::kRejected;
  }
  markdown_ += chunk->markdown;
  ++next_sequence_;
  if (!chunk->completed) return PreviewAssemblyResult::kPending;
  if (markdown_.empty()) {
    Reject();
    return PreviewAssemblyResult::kRejected;
  }
  active_ = false;
  completed_ = true;
  return PreviewAssemblyResult::kCompleted;
}

std::optional<std::string> PageMarkdownPreviewAssembler::TakeCompleted() {
  if (!completed_) return std::nullopt;
  completed_ = false;
  std::string result = std::move(markdown_);
  Cancel();
  return result;
}

void PageMarkdownPreviewAssembler::Cancel() {
  request_id_.clear();
  tab_id_.clear();
  navigation_id_ = 0;
  next_sequence_ = 0;
  markdown_.clear();
  active_ = false;
  completed_ = false;
}

void PageMarkdownPreviewAssembler::Reject() { Cancel(); }

}  // namespace crayon::browser::cef_shell::page_markdown
