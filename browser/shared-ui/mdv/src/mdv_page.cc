#include "crayon/browser_mdv/mdv_page.h"

#include <sstream>

namespace crayon::browser_mdv {
namespace {

std::string EscapeHtml(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&#34;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

const char* ViewModeName(MdvViewMode mode) {
  switch (mode) {
    case MdvViewMode::kSource:
      return "source";
    case MdvViewMode::kSplit:
      return "split";
    case MdvViewMode::kPreview:
      break;
  }
  return "preview";
}

std::string ViewButton(const std::string& label, const char* view,
                       MdvViewMode current) {
  std::ostringstream button;
  button << "<button type=\"button\" class=\"view-switch\" data-view=\"" << view
         << "\"";
  if (current == MdvViewMode::kPreview && std::string(view) == "preview") {
    button << " data-active=\"true\"";
  } else if (current == MdvViewMode::kSource && std::string(view) == "source") {
    button << " data-active=\"true\"";
  } else if (current == MdvViewMode::kSplit && std::string(view) == "split") {
    button << " data-active=\"true\"";
  }
  button << ">" << EscapeHtml(label) << "</button>";
  return button.str();
}

std::string StatusBanner(const std::string& error_text, MdvLoadStatus status,
                         const MdvPageStrings& strings) {
  if (!error_text.empty()) {
    std::ostringstream override_banner;
    override_banner << "<p class=\"md-status\" role=\"alert\">"
                    << EscapeHtml(error_text) << "</p>";
    return override_banner.str();
  }
  const char* key = nullptr;
  switch (status) {
    case MdvLoadStatus::kLoaded:
    case MdvLoadStatus::kEmpty:
      return {};
    case MdvLoadStatus::kTooLarge:
      key = strings.status_too_large.c_str();
      break;
    case MdvLoadStatus::kInvalidUtf8:
      key = strings.status_invalid_utf8.c_str();
      break;
    case MdvLoadStatus::kRenderPolicyViolation:
      key = strings.status_render_policy.c_str();
      break;
  }
  std::ostringstream banner;
  banner << "<p class=\"md-status\" role=\"alert\">"
         << EscapeHtml(key ? key : "") << "</p>";
  return banner.str();
}

}  // namespace

MdvRoute ClassifyMdvRequest(const MdvRequestParts& request) {
  if (request.scheme != kMdvScheme || request.host != kMdvHost ||
      request.has_credentials || request.has_port || request.has_query ||
      request.has_fragment) {
    return {};
  }
  const bool is_get = request.method == "GET";
  const bool is_head = request.method == "HEAD";
  if (!is_get && !is_head) {
    return {MdvResourceKind::kMethodNotAllowed, 405, false};
  }
  if (request.path == "/" || request.path == kResourceAppHtml) {
    return {MdvResourceKind::kDocument, 200, is_get};
  }
  if (request.path == kResourceAppCss) {
    return {MdvResourceKind::kStylesheet, 200, is_get};
  }
  if (request.path == kResourceAppJs) {
    return {MdvResourceKind::kScript, 200, is_get};
  }
  return {MdvResourceKind::kNotFound, 404, false};
}

std::string RenderMdvDocument(const MdvPageSnapshot& snapshot,
                              const MdvPageStrings& strings) {
  std::ostringstream document;
  document << "<!doctype html><html lang=\"" << EscapeHtml(strings.language)
           << "\" data-app=\"mdv\"><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,"
              "initial-scale=1\"><title>"
           << EscapeHtml(strings.document_title)
           << "</title><link rel=\"stylesheet\" href=\"/app.css\">"
              "</head><body data-view=\""
           << ViewModeName(snapshot.view_mode)
           << "\">"
              "<nav class=\"view-bar\" aria-label=\""
           << EscapeHtml(strings.document_title) << "\">"
           << ViewButton(strings.view_source, "source", snapshot.view_mode)
           << ViewButton(strings.view_preview, "preview", snapshot.view_mode)
           << ViewButton(strings.view_split, "split", snapshot.view_mode)
           << "</nav>";
  document << StatusBanner(snapshot.error_text, snapshot.load_status, strings);
  if (!snapshot.has_document) {
    document << "<main class=\"md-empty\"><p>"
             << EscapeHtml(strings.status_empty) << "</p></main></body></html>";
    return document.str();
  }
  // Source pane: fully escaped raw markdown.  Preview pane: trusted
  // MDV-02 whitelist HTML inserted verbatim.
  document << "<div class=\"md-panes\"><section class=\"md-source-pane\" "
              "aria-label=\""
           << EscapeHtml(strings.view_source) << "\"><pre><code>"
           << EscapeHtml(snapshot.source_text)
           << "</code></pre></section><section class=\"md-preview-pane\" "
              "aria-label=\""
           << EscapeHtml(strings.view_preview) << "\"><article>"
           << snapshot.rendered_html
           << "</article></section></div>"
              "<script src=\"/app.js\"></script></body></html>";
  return document.str();
}

std::string RenderMdvStylesheet() {
  // Fixed in-memory stylesheet; no external references.
  std::ostringstream css;
  css << ":root{color-scheme:light dark}"
      << "*{box-sizing:border-box}"
      << "body{margin:0;font:14px/1.6 system-ui,sans-serif;}"
      << ".view-bar{display:flex;gap:4px;padding:6px 10px;"
         "border-bottom:1px solid canvas;}"

      << ".view-switch{border:1px solid transparent;background:none;"
         "padding:2px 10px;border-radius:6px;cursor:pointer;}"
      << ".view-switch[data-active]{border-color:currentColor;font-weight:600;}"
      << ".md-panes{display:flex;height:calc(100vh - 42px);}"
      << ".md-source-pane,.md-preview-pane{flex:1;overflow:auto;padding:"
         "0 14px;}"
      << "body[data-view=preview] .md-source-pane{display:none;}"
      << "body[data-view=source] .md-preview-pane{display:none;}"
      << ".md-source-pane pre{white-space:pre-wrap;word-break:break-word;}"
      << ".md-status{color:crimson;padding:4px 14px;margin:0;}"
      << "@media(prefers-color-scheme:dark){a{color:#9ecbff;}}";
  return css.str();
}

std::string RenderMdvScript() {
  // In-memory script: switches the body data-view attribute on click.
  // No inline handlers (CSP), no network access, no storage.
  std::ostringstream js;
  js << "'use strict';"
     << "(function(){"
     << "var body=document.body;"
     << "var buttons=document.querySelectorAll('.view-switch');"
     << "for(var i=0;i<buttons.length;i++){"
     << "buttons[i].addEventListener('click',function(event){"
     << "var next=event.currentTarget.getAttribute('data-view');"
     << "if(!next){return;}"
     << "body.setAttribute('data-view',next);"
     << "for(var j=0;j<buttons.length;j++){"
     << "buttons[j].removeAttribute('data-active');"
     << "}"
     << "event.currentTarget.setAttribute('data-active','true');"
     << "});}"
     << "})();";
  return js.str();
}

}  // namespace crayon::browser_mdv
