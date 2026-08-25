#include "crayon/browser_mdv/mdv_viewer.h"

#include "crayon/browser_markdown/markdown_render.h"

namespace crayon::browser_mdv {
namespace {

using crayon::browser_markdown::RenderStatus;

MdvLoadStatus ToLoadStatus(RenderStatus status) {
  switch (status) {
    case RenderStatus::kOk:
      return MdvLoadStatus::kLoaded;
    case RenderStatus::kInputTooLarge:
      return MdvLoadStatus::kTooLarge;
    case RenderStatus::kInvalidUtf8:
      return MdvLoadStatus::kInvalidUtf8;
    case RenderStatus::kOutputPolicyViolation:
      return MdvLoadStatus::kRenderPolicyViolation;
  }
  return MdvLoadStatus::kRenderPolicyViolation;
}

}  // namespace

MdvLoadStatus MdvViewerModel::LoadContent(const std::string& content, bool utf8_valid,
                                          std::uint64_t now_ms) {
  has_document_ = true;
  rendered_html_.clear();
  if (content.empty()) {
    load_status_ = MdvLoadStatus::kEmpty;
    return load_status_;
  }
  if (!utf8_valid) {
    load_status_ = MdvLoadStatus::kInvalidUtf8;
    return load_status_;
  }
  // Render synchronously through the MDV-02 engine; the viewer never
  // produces HTML itself.
  RenderStatus status = RenderStatus::kOk;
  std::string html = crayon::browser_markdown::RenderMarkdownToSafeHtml(content, &status);
  load_status_ = ToLoadStatus(status);
  if (status == RenderStatus::kOk) {
    rendered_html_ = std::move(html);
    static_cast<void>(RequestRender(now_ms));
  }
  return load_status_;
}

RenderRevision MdvViewerModel::RequestRender(std::uint64_t now_ms) {
  // Debounce: requests inside the merge window reuse the pending
  // revision so the engine renders at most once per window.
  if (render_pending_ && now_ms - last_request_ms_ <= kRenderDebounceMs) {
    last_request_ms_ = now_ms;
    return pending_revision_;
  }
  ++revision_;
  pending_revision_ = revision_;
  render_pending_ = true;
  last_request_ms_ = now_ms;
  return pending_revision_;
}

bool MdvViewerModel::DeliverRender(RenderRevision revision, std::string html) {
  if (!render_pending_ || revision != pending_revision_) {
    return false;  // stale result: dropped, never rendered (MD-004)
  }
  render_pending_ = false;
  rendered_html_ = std::move(html);
  return true;
}

void MdvViewerModel::SetViewMode(MdvViewMode mode) {
  view_mode_ = mode;
}

void MdvViewerModel::CloseDocument() {
  has_document_ = false;
  load_status_ = MdvLoadStatus::kEmpty;
  rendered_html_.clear();
  render_pending_ = false;
  // Revision keeps advancing: in-flight renders of the old document
  // can never land on the next one.
  ++revision_;
}

}  // namespace crayon::browser_mdv
