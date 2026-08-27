// MDV-03: read-only markdown viewer view model (crayon://mdv).
//
// Owns the view surface state only: source/preview/split view modes,
// render-request debouncing (≤100 ms merge window, injected clock)
// with revision fencing so a late render result can never land on a
// newer document (MD-004 read-only), and content-load state bound to
// the MDV-02 renderer status codes.  No editing (MDV-05), no
// persistence (MDV-01 §10: recent files and scroll positions live in
// memory only), no URLs — the file path never enters this model.
#pragma once

#include <cstdint>
#include <string>

namespace crayon::browser_mdv {

/// Debounce merge window for render requests, in milliseconds
/// (MDV-01 §8: ≤100 ms).
inline constexpr std::uint64_t kRenderDebounceMs = 100;

/// The full-page CSP issued by the scheme handler (MDV-01 §2).
/// Golden-locked; byte-for-byte part of the zero-network contract.
constexpr char kMdvCsp[] =
    "default-src 'none'; "
    "script-src 'self'; "
    "style-src 'self'; "
    "img-src 'self' https:; "
    "connect-src 'none'; "
    "font-src 'none'; "
    "media-src 'none'; "
    "object-src 'none'; "
    "frame-src 'none'; "
    "base-uri 'none'; "
    "form-action 'none'; "
    "frame-ancestors 'none'";

/// Fixed in-memory resource paths served by the scheme handler
/// (framework only; rendered content arrives via controlled binding).
constexpr char kResourceAppHtml[] = "/app.html";
constexpr char kResourceAppCss[] = "/app.css";
constexpr char kResourceAppJs[] = "/app.js";

/// Closed view modes.
enum class MdvViewMode { kSource = 0, kPreview, kSplit };

/// Closed content-load outcomes (mirrors the MDV-02 renderer status
/// plus the load-time bounds).
enum class MdvLoadStatus {
  kLoaded = 0,
  kTooLarge,
  kInvalidUtf8,
  kRenderPolicyViolation,
  kEmpty,
};

/// One render request generation.  Monotonic per document; a render
/// result may only land when its revision matches the current one.
using RenderRevision = std::uint64_t;

/// Read-only viewer view model.
class MdvViewerModel final {
 public:
  MdvViewerModel() = default;

  /// Loads document content.  The path never reaches this model; the
  /// caller passes only the bytes.  Empty content is a legal load
  /// (kEmpty shows the empty-state surface).
  MdvLoadStatus LoadContent(const std::string& content, bool utf8_valid,
                            std::uint64_t now_ms);

  /// Requests a re-render (document or view change).  Requests inside
  /// the debounce window merge into the pending one.
  RenderRevision RequestRender(std::uint64_t now_ms);

  /// Delivers a finished render result; returns false when the result
  /// is stale (older revision) — the stale HTML is dropped, never
  /// rendered (MD-004).
  bool DeliverRender(RenderRevision revision, std::string html);

  /// View mode switching; immediate, keeps content and revision.
  void SetViewMode(MdvViewMode mode);

  /// Clears the document (tab closed / new file); revisions keep
  /// advancing so in-flight renders of the old document stay stale.
  void CloseDocument();

  MdvViewMode view_mode() const { return view_mode_; }
  MdvLoadStatus load_status() const { return load_status_; }
  RenderRevision current_revision() const { return revision_; }
  const std::string& rendered_html() const { return rendered_html_; }
  bool has_document() const { return has_document_; }

 private:
  MdvViewMode view_mode_ = MdvViewMode::kPreview;
  MdvLoadStatus load_status_ = MdvLoadStatus::kEmpty;
  bool has_document_ = false;
  RenderRevision revision_ = 0;
  RenderRevision pending_revision_ = 0;
  bool render_pending_ = false;
  std::uint64_t last_request_ms_ = 0;
  std::string rendered_html_;
};

}  // namespace crayon::browser_mdv
