// MDV-08: crayon://mdv page assembly (route classifier + deterministic
// document/stylesheet/script renderers).
//
// The scheme handler serves exactly three fixed in-memory framework
// resources (/app.html, /app.css, /app.js) under host "mdv"; rendered
// content is generated in the Browser process from the MDV-03 viewer
// snapshot and injected server-side.  Paths never enter the URL or the
// DOM (MDV-01 §2).  All output is deterministic; the CSP is the
// golden-locked kMdvCsp from mdv_viewer.h.
#pragma once

#include <string>

#include "crayon/browser_mdv/mdv_viewer.h"

namespace crayon::browser_mdv {

inline constexpr char kMdvScheme[] = "crayon";
inline constexpr char kMdvHost[] = "mdv";

/// Localized surface strings for the viewer page.  Callers load these
/// from platform string resources; no user-visible text is hardcoded
/// here.
struct MdvPageStrings {
  std::string language;
  std::string document_title;
  std::string view_source;
  std::string view_preview;
  std::string view_split;
  std::string status_empty;
  std::string status_too_large;
  std::string status_invalid_utf8;
  std::string status_render_policy;
  std::string status_not_markdown;
  std::string status_saved;
  std::string confirm_text;
  std::string label_save;
  std::string label_discard;
  std::string label_cancel;
  std::string label_open_in_viewer;
};

/// One request's parsed coordinates (mirrors the new-tab classifier
/// shape so both built-in pages fail closed identically).
struct MdvRequestParts {
  std::string method;
  std::string scheme;
  std::string host;
  std::string path;
  bool has_credentials = false;
  bool has_port = false;
  bool has_query = false;
  bool has_fragment = false;
};

enum class MdvResourceKind {
  kDocument,
  kStylesheet,
  kScript,
  kMethodNotAllowed,
  kNotFound,
};

struct MdvRoute {
  MdvResourceKind kind = MdvResourceKind::kNotFound;
  int status_code = 404;
  /// GET requests carry a body; HEAD suppresses it.
  bool include_body = false;
};

/// Classifies one request against the fixed route table.  Anything off
/// the exact triple (scheme, host, path) or carrying credentials, port,
/// query or fragment is rejected.
MdvRoute ClassifyMdvRequest(const MdvRequestParts& request);

/// Server-side snapshot of what the page shows this round.
struct MdvPageSnapshot {
  MdvViewMode view_mode = MdvViewMode::kPreview;
  MdvLoadStatus load_status = MdvLoadStatus::kEmpty;
  bool has_document = false;
  /// Raw markdown source; escaped by the renderer before insertion.
  std::string source_text;
  /// Trusted MDV-02 whitelist HTML; inserted verbatim into the preview
  /// pane (never re-escaped).
  std::string rendered_html;
  /// Entry-failure text from the load gate (already localized); the
  /// renderer escapes it.  Non-empty takes banner priority over
  /// `load_status`.
  std::string error_text;
  /// Display name of the open document (base name only, never a path).
  std::string document_name;
  /// Editing state pushed by the MDV-10 controller.
  bool dirty = false;
  bool save_ok = false;
  bool confirm_visible = false;
};

/// Renders the full app.html body for `snapshot`.
std::string RenderMdvDocument(const MdvPageSnapshot& snapshot,
                              const MdvPageStrings& strings);

/// Renders the fixed /app.css body.
std::string RenderMdvStylesheet();

/// Renders the fixed /app.js body (view-mode switching only).
std::string RenderMdvScript();

}  // namespace crayon::browser_mdv
