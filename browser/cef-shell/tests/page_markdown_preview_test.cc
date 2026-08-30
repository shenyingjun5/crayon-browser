#include "browser/page_markdown/page_markdown_preview.h"

#include <string>

#include "crayon/browser_page_tools/page_markdown_export.h"

namespace {

namespace host = crayon::cef_shell::ipc::content_host;
using crayon::browser::cef_shell::page_markdown::PageMarkdownPreviewAssembler;
using crayon::browser::cef_shell::page_markdown::PreviewAssemblyResult;

host::MarkdownChunk Chunk(std::uint32_t sequence, bool completed,
                          std::string markdown) {
  return host::MarkdownChunk{
      "request-1", "tab-7", 9, 9, sequence, completed, std::move(markdown)};
}

bool Run() {
  PageMarkdownPreviewAssembler assembler;
  if (!assembler.Begin("request-1", "tab-7", 9) ||
      assembler.Begin("request-2", "tab-7", 9)) {
    return false;
  }
  if (assembler.Consume(Chunk(0, false, "# Heading")) !=
          PreviewAssemblyResult::kPending ||
      assembler.Consume(Chunk(1, true, "\n")) !=
          PreviewAssemblyResult::kCompleted) {
    return false;
  }
  const auto completed = assembler.TakeCompleted();
  if (!completed || *completed != "# Heading\n" || assembler.active()) {
    return false;
  }

  if (!assembler.Begin("request-1", "tab-7", 9) ||
      assembler.Consume(Chunk(1, true, "bad")) !=
          PreviewAssemblyResult::kRejected ||
      assembler.active()) {
    return false;
  }
  if (!assembler.Begin("request-1", "tab-7", 9)) return false;
  host::MarkdownChunk stale = Chunk(0, true, "stale");
  stale.navigation_id = 10;
  if (assembler.Consume(stale) != PreviewAssemblyResult::kRejected) {
    return false;
  }
  if (!assembler.Begin("request-1", "tab-7", 9) ||
      assembler.Consume(
          host::ErrorReply{"request-1", host::HostError::kInvalidState}) !=
          PreviewAssemblyResult::kRejected) {
    return false;
  }
  if (!assembler.Begin("request-1", "tab-7", 9)) return false;
  std::string oversized(crayon::browser_page_tools::kMaxExportMarkdownBytes + 1,
                        'x');
  if (assembler.Consume(Chunk(0, true, std::move(oversized))) !=
      PreviewAssemblyResult::kRejected) {
    return false;
  }
  if (!assembler.Begin("request-1", "tab-7", 9) ||
      assembler.Consume(Chunk(0, true, "")) !=
          PreviewAssemblyResult::kRejected) {
    return false;
  }
  assembler.Cancel();
  return !assembler.active() && !assembler.TakeCompleted().has_value();
}

}  // namespace

int main() { return Run() ? 0 : 1; }
