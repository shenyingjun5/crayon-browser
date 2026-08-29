#include "crayon/browser_mdv/mdv_page.h"

#include <algorithm>
#include <sstream>
#include <string_view>

#include "crayon/browser_markdown_runtime/extension_registry.h"
#include "crayon/browser_markdown_runtime/katex_extension.h"
#include "mdv_icons_generated.h"

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

const char* ShortcutPlatformName(MdvShortcutPlatform platform) {
  return platform == MdvShortcutPlatform::kMacOS ? "macos" : "windows";
}

struct ShortcutCopy {
  std::string display;
  std::string aria;
};

ShortcutCopy Shortcut(const char* action, MdvShortcutPlatform platform) {
  const bool mac = platform == MdvShortcutPlatform::kMacOS;
  const std::string primary = mac ? "⌘" : "Ctrl+";
  const std::string aria_primary = mac ? "Meta+" : "Control+";
  const std::string id(action);
  if (id == "h1" || id == "h2" || id == "h3") {
    const char level = id.back();
    return {primary + (mac ? "⌥" : "Alt+") + level,
            aria_primary + "Alt+" + level};
  }
  if (id == "bold") return {primary + "B", aria_primary + "B"};
  if (id == "italic") return {primary + "I", aria_primary + "I"};
  if (id == "strike") {
    return {primary + (mac ? "⇧X" : "Shift+X"), aria_primary + "Shift+X"};
  }
  if (id == "bullet-list") {
    return {primary + (mac ? "⇧8" : "Shift+8"), aria_primary + "Shift+8"};
  }
  if (id == "ordered-list") {
    return {primary + (mac ? "⇧7" : "Shift+7"), aria_primary + "Shift+7"};
  }
  if (id == "link") return {primary + "K", aria_primary + "K"};
  return {};
}

std::string TooltipAttributes(const std::string& label,
                              const std::string& hint,
                              const ShortcutCopy& shortcut) {
  std::ostringstream attributes;
  attributes << " aria-label=\"" << EscapeHtml(label)
             << "\" data-tooltip-title=\"" << EscapeHtml(label)
             << "\" data-tooltip-hint=\"" << EscapeHtml(hint) << "\"";
  if (!shortcut.display.empty()) {
    attributes << " data-shortcut=\"" << EscapeHtml(shortcut.display)
               << "\" aria-keyshortcuts=\"" << EscapeHtml(shortcut.aria)
               << "\"";
  }
  return attributes.str();
}

std::string ViewButton(const std::string& label, const std::string& hint,
                       const char* icon, const char* view, MdvViewMode current,
                       bool first) {
  std::ostringstream button;
  button << "<button type=\"button\" class=\"view-switch icon-button\" data-view=\""
         << view << "\" tabindex=\"" << (first ? "0" : "-1") << "\""
         << TooltipAttributes(label, hint, {});
  if (current == MdvViewMode::kPreview && std::string(view) == "preview") {
    button << " data-active=\"true\" aria-pressed=\"true\"";
  } else if (current == MdvViewMode::kSource && std::string(view) == "source") {
    button << " data-active=\"true\" aria-pressed=\"true\"";
  } else if (current == MdvViewMode::kSplit && std::string(view) == "split") {
    button << " data-active=\"true\" aria-pressed=\"true\"";
  } else {
    button << " aria-pressed=\"false\"";
  }
  button << ">" << icon << "</button>";
  return button.str();
}

void ToolButton(std::ostringstream& bar, const char* action,
                const std::string& label, const char* icon,
                const std::string& hint, MdvShortcutPlatform platform,
                bool first = false, bool menu_item = false) {
  const ShortcutCopy shortcut = Shortcut(action, platform);
  bar << "<button type=\"button\" class=\"md-tool icon-button"
      << (menu_item ? " structure-item" : "") << "\" data-action=\""
      << action << "\" tabindex=\"" << (first ? "0" : "-1") << "\""
      << TooltipAttributes(label, hint, shortcut);
  if (menu_item) bar << " role=\"menuitem\"";
  bar << ">" << icon;
  if (menu_item) bar << "<span>" << EscapeHtml(label) << "</span>";
  bar << "</button>";
}

std::string RenderToolbar(const MdvPageStrings& strings) {
  std::ostringstream bar;
  const std::string md = strings.tooltip_markdown + " · ";
  bar << "<div class=\"md-toolbar icon-toolbar\" role=\"toolbar\" aria-label=\""
      << EscapeHtml(strings.toolbar_title) << "\">";
  ToolButton(bar, "h1", strings.tool_heading1, icons::kHeading1, md + "# …",
             strings.shortcut_platform, true);
  ToolButton(bar, "h2", strings.tool_heading2, icons::kHeading2, md + "## …",
             strings.shortcut_platform);
  ToolButton(bar, "h3", strings.tool_heading3, icons::kHeading3, md + "### …",
             strings.shortcut_platform);
  bar << "<span class=\"md-tool-sep\"></span>";
  ToolButton(bar, "bold", strings.tool_bold, icons::kBold, md + "**…**",
             strings.shortcut_platform);
  ToolButton(bar, "italic", strings.tool_italic, icons::kItalic, md + "*…*",
             strings.shortcut_platform);
  ToolButton(bar, "strike", strings.tool_strike, icons::kStrike, md + "~~…~~",
             strings.shortcut_platform);
  ToolButton(bar, "inline-code", strings.tool_inline_code, icons::kInlineCode,
             md + "`…`", strings.shortcut_platform);
  bar << "<span class=\"md-tool-sep\"></span>";
  ToolButton(bar, "bullet-list", strings.tool_bullet_list, icons::kBulletList,
             md + "- …", strings.shortcut_platform);
  ToolButton(bar, "ordered-list", strings.tool_ordered_list,
             icons::kOrderedList, md + "1. …", strings.shortcut_platform);
  ToolButton(bar, "task-list", strings.tool_task_list, icons::kTaskList,
             md + "- [ ] …", strings.shortcut_platform);
  ToolButton(bar, "quote", strings.tool_quote, icons::kQuote, md + "> …",
             strings.shortcut_platform);
  bar << "<span class=\"md-tool-sep\"></span>";
  ToolButton(bar, "code-block", strings.tool_code_block, icons::kCodeBlock,
             md + "```", strings.shortcut_platform);
  ToolButton(bar, "table", strings.tool_table, icons::kTable, md + "| … |",
             strings.shortcut_platform);
  ToolButton(bar, "link", strings.tool_link, icons::kLink, md + "[…](…)",
             strings.shortcut_platform);
  ToolButton(bar, "divider", strings.tool_divider, icons::kDivider, md + "---",
             strings.shortcut_platform);
  bar << "<span class=\"md-tool-sep\"></span>"
         "<div class=\"structure-wrap\">"
         "<button type=\"button\" class=\"md-tool icon-button structure-toggle\" "
         "aria-haspopup=\"menu\" aria-expanded=\"false\" tabindex=\"-1\""
      << TooltipAttributes(strings.tool_structure, strings.tooltip_structure, {})
      << ">" << icons::kStructure << "</button>"
         "<div class=\"structure-menu\" role=\"menu\" hidden>";
  ToolButton(bar, "outdent", strings.tool_outdent, icons::kOutdent,
             strings.tooltip_structure, strings.shortcut_platform, false, true);
  ToolButton(bar, "indent", strings.tool_indent, icons::kIndent,
             strings.tooltip_structure, strings.shortcut_platform, false, true);
  bar << "<span class=\"structure-sep\"></span>";
  ToolButton(bar, "align-default", strings.tool_align_default,
             icons::kAlignDefault, strings.tooltip_table_alignment,
             strings.shortcut_platform, false, true);
  ToolButton(bar, "align-left", strings.tool_align_left, icons::kAlignLeft,
             strings.tooltip_table_alignment, strings.shortcut_platform, false,
             true);
  ToolButton(bar, "align-center", strings.tool_align_center, icons::kAlignCenter,
             strings.tooltip_table_alignment, strings.shortcut_platform, false,
             true);
  ToolButton(bar, "align-right", strings.tool_align_right, icons::kAlignRight,
             strings.tooltip_table_alignment, strings.shortcut_platform, false,
             true);
  bar << "</div></div>";
  bar << "</div>";
  return bar.str();
}

std::string StatusBanner(const std::string& error_text,
                         const MdvPageStrings& strings, bool save_ok,
                         MdvLoadStatus status) {
  if (!error_text.empty()) {
    const char* cls = save_ok ? "md-status md-ok" : "md-status";
    std::ostringstream override_banner;
    override_banner << "<p class=\"" << cls << "\" role=\"alert\">"
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

std::optional<std::string> PercentDecodePath(const std::string& path) {
  auto hex_value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string decoded;
  decoded.reserve(path.size());
  for (std::size_t index = 0; index < path.size(); ++index) {
    if (path[index] != '%') {
      decoded.push_back(path[index]);
      continue;
    }
    if (index + 2 >= path.size()) {
      return std::nullopt;
    }
    const int high = hex_value(path[index + 1]);
    const int low = hex_value(path[index + 2]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    decoded.push_back(static_cast<char>((high << 4) | low));
    index += 2;
  }
  // A surviving percent or separator-escape is a double-encoding attempt.
  if (decoded.find('%') != std::string::npos ||
      decoded.find('\\') != std::string::npos ||
      decoded.find('\0') != std::string::npos) {
    return std::nullopt;
  }
  return decoded;
}

MdvRoute ClassifyMdvRequest(const MdvRequestParts& request) {
  if (request.scheme != kMdvScheme || request.host != kMdvHost ||
      request.has_credentials || request.has_port || request.has_query ||
      request.has_fragment) {
    return {};
  }
  // Encoded separators never survive: `%2f`/`%5c` would change the segment
  // structure of a path whose resource ids are plain ASCII. Everything else
  // decodes exactly once and re-validates against the closed grammars.
  {
    std::string lower;
    lower.reserve(request.path.size());
    for (const char c : request.path) {
      lower.push_back(static_cast<char>(
          c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    if (lower.find("%2f") != std::string::npos ||
        lower.find("%5c") != std::string::npos) {
      return {MdvResourceKind::kNotFound, 404, false, 0, {}, {}};
    }
  }
  const std::optional<std::string> decoded_path =
      PercentDecodePath(request.path);
  if (!decoded_path || decoded_path->find("//") != std::string::npos ||
      decoded_path->find("/./") != std::string::npos) {
    return {MdvResourceKind::kNotFound, 404, false, 0, {}, {}};
  }
  const MdvRequestParts decoded_request = [&] {
    MdvRequestParts parts = request;
    parts.path = *decoded_path;
    return parts;
  }();
  const MdvRequestParts& request_ref = decoded_request;
  const bool is_get = request.method == "GET";
  const bool is_head = request.method == "HEAD";
  if (!is_get && !is_head) {
    return {MdvResourceKind::kMethodNotAllowed, 405, false, 0, {}, {}};
  }
  if (request_ref.path == "/" || request_ref.path == kResourceAppHtml) {
    return {MdvResourceKind::kDocument, 200, is_get, 0, {}, {}};
  }
  if (request_ref.path == kResourceAppCss) {
    return {MdvResourceKind::kStylesheet, 200, is_get, 0, {}, {}};
  }
  if (request_ref.path == kResourceAppJs) {
    return {MdvResourceKind::kScript, 200, is_get, 0, {}, {}};
  }
  constexpr std::string_view kRuntimePrefix = "/runtime/highlight/";
  if (request_ref.path.compare(0, kRuntimePrefix.size(), kRuntimePrefix) == 0) {
    const std::string resource_id =
        request_ref.path.substr(kRuntimePrefix.size());
    if (crayon::browser_markdown_runtime::IsValidManifestId(resource_id)) {
      MdvRoute route{MdvResourceKind::kRuntimeAsset, 200, is_get, 0, {}, {}};
      route.runtime_resource_id = resource_id;
      route.runtime_namespace = "highlight";
      return route;
    }
  }
  constexpr std::string_view kKatexPrefix = "/runtime/katex/";
  if (request_ref.path.compare(0, kKatexPrefix.size(), kKatexPrefix) == 0) {
    const std::string resource_id =
        request_ref.path.substr(kKatexPrefix.size());
    if (crayon::browser_markdown_runtime::IsKatexRuntimeResourceId(
            resource_id)) {
      MdvRoute route{MdvResourceKind::kRuntimeAsset, 200, is_get, 0, {}, {}};
      route.runtime_resource_id = resource_id;
      route.runtime_namespace = "katex";
      return route;
    }
  }
  constexpr std::string_view kMermaidPrefix = "/runtime/mermaid/";
  if (request_ref.path.compare(0, kMermaidPrefix.size(), kMermaidPrefix) ==
      0) {
    const std::string resource_id =
        request_ref.path.substr(kMermaidPrefix.size());
    // Exact resource ids are nested upstream paths; the closed grammar plus
    // the handler's exact bundle lookup reject manifest-external paths.
    if (crayon::browser_markdown_runtime::IsValidRuntimeResourceId(
            resource_id)) {
      MdvRoute route{MdvResourceKind::kRuntimeAsset, 200, is_get, 0, {}, {}};
      route.runtime_resource_id = resource_id;
      route.runtime_namespace = "mermaid";
      return route;
    }
  }
  // Opaque validated local image: /img/<digits>, 1-6 digits only.
  constexpr const char* kImagePrefix = "/img/";
  if (request_ref.path.compare(0, 5, kImagePrefix) == 0) {
    const std::string digits = request_ref.path.substr(5);
    if (!digits.empty() && digits.size() <= 6 &&
        std::all_of(digits.begin(), digits.end(),
                    [](char c) { return c >= '0' && c <= '9'; })) {
      MdvRoute route{MdvResourceKind::kImage, 200, is_get, 0, {}, {}};
      route.image_index = static_cast<std::size_t>(std::stoul(digits));
      return route;
    }
  }
  return {MdvResourceKind::kNotFound, 404, false, 0, {}, {}};
}

std::string RenderMdvDocument(const MdvPageSnapshot& snapshot,
                              const MdvPageStrings& strings) {
  std::ostringstream document;
  document << "<!doctype html><html lang=\"" << EscapeHtml(strings.language)
           << "\" data-app=\"mdv\" data-platform=\""
           << ShortcutPlatformName(strings.shortcut_platform)
           << "\" data-dirty=\""
           << (snapshot.dirty ? "true" : "false")
           << "\"><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,"
              "initial-scale=1\"><title>"
           << EscapeHtml(snapshot.document_name.empty()
                             ? strings.document_title
                             : snapshot.document_name + " - " +
                                   strings.document_title)
           << "</title><link rel=\"stylesheet\" href=\"/app.css\">"
              "</head><body data-view=\""
           << ViewModeName(snapshot.view_mode)
           << "\">"
              "<nav class=\"view-bar icon-toolbar\" role=\"toolbar\" aria-label=\""
           << EscapeHtml(strings.document_title) << "\">"
           << ViewButton(strings.view_source, strings.tooltip_view,
                         icons::kViewSource, "source", snapshot.view_mode, true)
           << ViewButton(strings.view_preview, strings.tooltip_view,
                         icons::kViewPreview, "preview", snapshot.view_mode,
                         false)
           << ViewButton(strings.view_split, strings.tooltip_view,
                         icons::kViewSplit, "split", snapshot.view_mode, false)
           << "</nav><div id=\"md-tooltip\" role=\"tooltip\" hidden>"
              "<span class=\"tooltip-title\"></span>"
              "<span class=\"tooltip-hint\"></span></div>";
  document << StatusBanner(snapshot.error_text, strings, snapshot.save_ok,
                           snapshot.load_status);
  if (!snapshot.has_document) {
    document << "<main class=\"md-empty\"><p>"
             << EscapeHtml(strings.status_empty) << "</p></main></body></html>";
    return document.str();
  }
  // Source pane: fully escaped editable textarea.  Preview pane:
  // trusted MDV-02 whitelist HTML inserted verbatim.
  document << "<div class=\"md-panes\"><section class=\"md-source-pane\" "
              "aria-label=\""
           << EscapeHtml(strings.view_source) << "\">" << RenderToolbar(strings)
           << "<textarea id=\"md-source\" spellcheck=\"false\">"
           << EscapeHtml(snapshot.source_text)
           << "</textarea></section><div id=\"md-divider\" class=\"md-"
              "divider\" aria-hidden=\"true\"></div>"
              "<section class=\"md-preview-pane\" aria-label=\""
           << EscapeHtml(strings.view_preview)
           << "\"><article id=\"md-preview\">" << snapshot.rendered_html
           << "</article></section></div>"
              "<div id=\"md-confirm\" class=\"md-confirm\" data-show=\""
           << (snapshot.confirm_visible ? "true" : "false")
           << "\" role=\"dialog\"><p>" << EscapeHtml(strings.confirm_text)
           << "</p>"
              "<button type=\"button\" data-decision=\"save\">"
           << EscapeHtml(strings.label_save)
           << "</button>"
              "<button type=\"button\" data-decision=\"discard\">"
           << EscapeHtml(strings.label_discard)
           << "</button>"
              "<button type=\"button\" data-decision=\"cancel\">"
           << EscapeHtml(strings.label_cancel)
           << "</button></div>"
              "<script src=\"/app.js\"></script></body></html>";
  return document.str();
}

std::string RenderMdvStylesheet() {
  // Fixed in-memory stylesheet; no external references.
  std::ostringstream css;
  css << ":root{color-scheme:light dark;--mdv-border:rgba(31,35,41,.15);"
         "--mdv-hover:rgba(31,35,41,.08);--mdv-active:#e8efff;"
         "--mdv-active-fg:#245bdb;--mdv-panel:canvas;--mdv-tip:#1f2329;"
         "--mdv-tip-fg:#fff;}"
      << "*{box-sizing:border-box}"
      << "body{margin:0;font:14px/1.6 system-ui,-apple-system,sans-serif;}"
      << ".view-bar{display:flex;align-items:center;gap:4px;padding:6px 10px;"
         "border-bottom:1px solid var(--mdv-border);min-height:49px;}"
      << ".icon-button{display:inline-flex;align-items:center;justify-content:"
         "center;width:36px;height:36px;flex:0 0 36px;border:1px solid "
         "transparent;background:transparent;color:inherit;border-radius:8px;"
         "padding:7px;cursor:pointer;transition:background-color .12s ease,color "
         ".12s ease;}"
      << ".icon-button svg{display:block;width:20px;height:20px;flex:none;}"
      << ".icon-button:hover{background:var(--mdv-hover);}"
      << ".icon-button:focus-visible{outline:2px solid #3370ff;outline-offset:0;}"
      << ".icon-button:disabled{opacity:.35;cursor:not-allowed;background:none;}"
      << ".view-switch[data-active]{background:var(--mdv-active);"
         "color:var(--mdv-active-fg);}"
      << ".md-panes{display:flex;height:calc(100vh - 49px);}"
      << ".md-source-pane,.md-preview-pane{flex:1;min-width:0;padding:0 14px;}"
      << ".md-source-pane{display:flex;flex-direction:column;overflow:hidden;}"
      << ".md-preview-pane{overflow:auto;}"
      << ".md-toolbar{display:flex;align-items:center;flex-wrap:nowrap;gap:2px;"
         "padding:6px 0;border-bottom:1px solid var(--mdv-border);"
         "margin-bottom:4px;overflow-x:auto;overflow-y:hidden;scrollbar-width:"
         "thin;position:relative;z-index:2;}"
      << ".md-tool{font-size:12px;}"
      << ".md-tool-sep{width:1px;height:22px;background:var(--mdv-border);"
         "flex:0 0 1px;margin:0 2px;}"
      << ".structure-wrap{position:relative;display:flex;flex:0 0 auto;}"
      << ".structure-menu{position:fixed;z-index:20;min-width:210px;padding:6px;"
         "border:1px solid var(--mdv-border);border-radius:10px;"
         "background:var(--mdv-panel);box-shadow:0 8px 28px rgba(31,35,41,.18);}"
      << ".structure-menu[hidden]{display:none;}"
      << ".structure-item{width:100%;height:36px;justify-content:flex-start;"
         "gap:10px;padding:7px 10px;flex:none;text-align:left;}"
      << ".structure-item svg{width:20px;height:20px;}"
      << ".structure-item span{white-space:nowrap;}"
      << ".structure-sep{display:block;height:1px;background:var(--mdv-border);"
         "margin:5px 4px;}"
      << "#md-tooltip{position:fixed;z-index:30;max-width:260px;padding:8px 10px;"
         "border-radius:8px;background:var(--mdv-tip);color:var(--mdv-tip-fg);"
         "box-shadow:0 6px 20px rgba(0,0,0,.2);pointer-events:none;"
         "font-size:12px;line-height:1.45;}"
      << "#md-tooltip[hidden]{display:none;}"
      << ".tooltip-title,.tooltip-hint{display:block;}"
      << ".tooltip-title{font-weight:600;}"
      << ".tooltip-hint{opacity:.72;margin-top:2px;}"
      << ".md-divider{width:6px;cursor:col-resize;background:canvasText;"
         "opacity:.08;flex:0 0 auto;}"
      << ".md-divider:hover,.md-divider[data-dragging]{opacity:.25;}"
      << "body[data-view=preview] .md-source-pane{display:none;}"
      << "body[data-view=source] .md-preview-pane{display:none;}"
      << ".md-source-pane textarea{width:100%;flex:1;min-height:0;border:none;"
         "outline:none;resize:none;background:transparent;color:inherit;"
         "font:13px/1.6 ui-monospace,monospace;}"
      << ".md-source-pane pre{white-space:pre-wrap;word-break:break-word;}"
      << "pre code.hljs{display:block;overflow-x:auto;padding:12px 14px;"
         "border-radius:8px;background:#f5f6f7;color:#1f2329;}"
      << ".hljs-comment,.hljs-quote{color:#8f959e;font-style:italic;}"
      << ".hljs-keyword,.hljs-selector-tag,.hljs-literal,.hljs-section,"
         ".hljs-link{color:#7c3aed;}"
      << ".hljs-string,.hljs-title,.hljs-name,.hljs-type,.hljs-attribute,"
         ".hljs-symbol,.hljs-bullet,.hljs-addition{color:#067d68;}"
      << ".hljs-number,.hljs-meta,.hljs-built_in,.hljs-builtin-name,"
         ".hljs-params{color:#b85c00;}"
      << ".hljs-deletion{color:#c03639;}"
      << ".hljs-emphasis{font-style:italic}.hljs-strong{font-weight:700;}"
      << ".md-math{max-width:100%;}.md-math-inline{display:inline-block;"
         "vertical-align:middle}.md-math-block{display:block;overflow-x:auto;"
         "margin:1em 0;text-align:center}.md-math-input{display:none!important}"
         ".md-math-source{white-space:pre-wrap;"
         "font:inherit;color:inherit;background:rgba(31,35,41,.06);padding:"
         "1px 4px;border-radius:4px}.md-math-block>.md-math-source{display:"
         "block;text-align:left;padding:10px 12px}.md-math[data-mdv-math-"
         "rendered=true]>.katex-display{margin:0;}"
      << ".view-bar .md-dirty{display:none;width:8px;height:8px;"
         "border-radius:50%;background:darkorange;align-self:center;}"
      << "body[data-dirty=true] .md-dirty{display:inline-block;}"
      << ".md-status.md-ok{color:seagreen;}"
      << ".md-confirm{display:none;position:fixed;inset:auto 0 0 0;margin:auto;"
         "max-width:420px;padding:16px;border:1px solid canvasText;"
         "background:canvas;z-index:10;}"
      << ".md-confirm[data-show=true]{display:block;}"
      << ".md-confirm button{margin-right:8px;padding:4px 12px;}"
      << ".md-status{color:crimson;padding:4px 14px;margin:0;}"
      << "@media(max-width:680px){.icon-button{width:32px;height:32px;"
         "flex-basis:32px;padding:5px}.view-bar{min-height:45px}.md-panes{"
         "height:calc(100vh - 45px)}.md-source-pane,.md-preview-pane{padding:0 "
         "10px}}"
      << "@media(prefers-reduced-motion:reduce){.icon-button{transition:none}}"
      << "@media(prefers-color-scheme:dark){:root{--mdv-border:rgba(255,255,"
         "255,.16);--mdv-hover:rgba(255,255,255,.1);--mdv-active:#25385f;"
         "--mdv-active-fg:#8fb4ff;--mdv-tip:#f2f3f5;--mdv-tip-fg:#1f2329;}"
         "a{color:#9ecbff;}pre code.hljs{background:#202124;color:#e8eaed;}"
         ".hljs-comment,.hljs-quote{color:#9aa0a6;}"
         ".hljs-keyword,.hljs-selector-tag,.hljs-literal,.hljs-section,"
         ".hljs-link{color:#c9a7ff;}"
         ".hljs-string,.hljs-title,.hljs-name,.hljs-type,.hljs-attribute,"
         ".hljs-symbol,.hljs-bullet,.hljs-addition{color:#81c995;}"
         ".hljs-number,.hljs-meta,.hljs-built_in,.hljs-builtin-name,"
         ".hljs-params{color:#fdd663;}.hljs-deletion{color:#f28b82;}}";
  return css.str();
}

void AppendMdvCoreScript(std::ostringstream& js) {
  // In-memory script: view switching, edit-burst queries over the
  // controlled mdvQuery binding, confirm-dialog decisions and the
  // beforeunload dirty guard.  No inline handlers (CSP), no network.
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
     << "buttons[j].setAttribute('aria-pressed','false');"
     << "}"
     << "event.currentTarget.setAttribute('data-active','true');"
     << "event.currentTarget.setAttribute('aria-pressed','true');"
     << "});}"
     << "var source=document.getElementById('md-source');"
     << "var preview=document.getElementById('md-preview');"
     << "var confirmBox=document.getElementById('md-confirm');"
     << "function apply(state){"
     << "if(!state){return;}"
     << "if(typeof "
        "state.preview==='string'&&preview){resetHighlights();resetMath();"
        "preview.innerHTML=state.preview;observeHighlights(preview);"
        "observeMath(preview);}"
     << "if(typeof "
        "state.dirty==='boolean'){body.setAttribute('data-dirty',state.dirty?'"
        "true':'false');}"
     << "if(confirmBox&&typeof "
        "state.confirm==='boolean'){confirmBox.setAttribute('data-show',state."
        "confirm?'true':'false');}"
     << "if(typeof state.banner==='string'){var "
        "b=document.querySelector('.md-status');if(!b){b=document."
        "createElement('p');b.className='md-status';b.setAttribute('role','"
        "alert');(document.querySelector('.view-bar')||body).after(b);}b."
        "textContent=state.banner;}"
     << "}"
     << "window.mdvPush=apply;"
     << "function sendQuery(payload,onReply){"
     << "if(typeof window.mdvQuery!=='function'){return;}"
     << "window.mdvQuery({request:JSON.stringify(payload),persistent:false,"
     << "onSuccess:function(response){try{var state=JSON.parse(response);"
        "if(onReply){onReply(state);}else{apply(state);}}catch(e){}},"
     << "onFailure:function(){}});"
     << "}"
     << "var throttleUntil=0;"
     << "if(source){source.addEventListener('input',function(){"
     << "var now=Date.now();if(now<throttleUntil){return;}throttleUntil=now+80;"
     << "sendQuery({type:'edit',text:source.value});"
     << "});}"
     << "if(confirmBox){confirmBox.addEventListener('click',function(event){"
     << "var "
        "d=event.target&&event.target.getAttribute?event.target.getAttribute('"
        "data-decision'):null;"
     << "if(!d){return;}"
     << "sendQuery({type:'decision',value:d});"
     << "});}"
     << "window.addEventListener('beforeunload',function(e){"
     << "if(body.getAttribute('data-dirty')==='true'){e.preventDefault();e."
        "returnValue='';}"
     << "});"
     << "var previewPane=document.querySelector('.md-preview-pane');"
     << "var syncing=false;"
     << "if(source&&previewPane){"
     << "source.addEventListener('scroll',function(){"
     << "if(syncing){return;}"
     << "var max=source.scrollHeight-source.clientHeight;"
     << "if(max<=0){return;}"
     << "syncing=true;"
     << "previewPane.scrollTop=(source.scrollTop/"
        "max)*(previewPane.scrollHeight-previewPane.clientHeight);"
     << "syncing=false;"
     << "});}";
}

void AppendMdvToolbarScript(std::ostringstream& js) {
  js << "var toolbar=document.querySelector('.md-toolbar');"
     << "var transformRevision=0;"
     << "function requestAction(action){"
     << "if(!source||!action){return;}"
     << "var revision=++transformRevision;var original=source.value;"
     << "sendQuery({type:'transform',action:action,text:original,"
        "start:source.selectionStart,end:source.selectionEnd},function(edit){"
     << "if(revision!==transformRevision||source.value!==original||!edit||"
        "edit.applied!==true){return;}"
     << "source.setRangeText(edit.replacement,edit.start,edit.end,'end');"
     << "source.selectionStart=edit.start+edit.selectionStart;"
     << "source.selectionEnd=edit.start+edit.selectionEnd;"
     << "source.focus();source.dispatchEvent(new Event('input'));hideTooltip();"
     << "});}"
     << "var structureToggle=document.querySelector('.structure-toggle');"
     << "var structureMenu=document.querySelector('.structure-menu');"
     << "function closeStructure(){if(!structureMenu||!structureToggle){return;}"
        "structureMenu.hidden=true;structureToggle.setAttribute('aria-expanded','"
        "false');}"
     << "function lineAtCaret(){if(!source){return '';}var before=source.value."
        "lastIndexOf('\\n',Math.max(0,source.selectionStart-1))+1;var after=source."
        "value.indexOf('\\n',source.selectionStart);if(after<0){after=source.value."
        "length;}return source.value.substring(before,after);}"
     << "function structureContext(){var line=lineAtCaret();var structured=/^\\s*"
        "(?:[-*+] |[-] \\[[ xX]\\] |\\d+\\. |> )/.test(line);var lines=source?"
        "source.value.split(/\\r?\\n/):[];var row=0;if(source){row=source.value."
        "substring(0,source.selectionStart).split('\\n').length-1;}var table=false;"
        "for(var i=1;i<lines.length;i++){if(/^\\s*\\|?\\s*:?-{3,}:?\\s*(?:\\|"
        "\\s*:?-{3,}:?\\s*)+\\|?\\s*$/.test(lines[i])&&row>=i-1){var n=(lines[i]."
        "match(/\\|/g)||[]).length;for(var j=i;j<lines.length;j++){if((lines[j]."
        "match(/\\|/g)||[]).length!==n){break;}if(j===row){table=true;}}}}return "
        "{structured:structured,table:table};}"
     << "function updateStructure(){if(!structureMenu||!source){return;}var c="
        "structureContext();var items=structureMenu.querySelectorAll('[data-action]"
        "');for(var i=0;i<items.length;i++){var a=items[i].getAttribute('data-"
        "action');items[i].disabled=a.indexOf('align-')===0?!c.table:!c."
        "structured;}}"
     << "if(structureToggle&&structureMenu){structureToggle.addEventListener('"
        "click',function(){var opening=structureMenu.hidden;if(!opening){"
        "closeStructure();return;}updateStructure();var r=structureToggle."
        "getBoundingClientRect();structureMenu.hidden=false;structureMenu.style."
        "top=(r.bottom+6)+'px';structureMenu.style.left=Math.max(8,Math.min(r.left,"
        "window.innerWidth-218))+'px';structureToggle.setAttribute('aria-expanded',"
        "'true');var first=structureMenu.querySelector('button:not([disabled])');"
        "if(first){first.focus();}});}"
     << "if(toolbar&&source){toolbar.addEventListener('click',function(event){"
     << "var target=event.target&&event.target.closest?event.target.closest('[data-"
        "action]'):null;if(!target||target.disabled){return;}requestAction(target."
        "getAttribute('data-action'));closeStructure();});}"
     << "if(structureMenu){structureMenu.addEventListener('keydown',function(e){var"
        " items=Array.prototype.slice.call(structureMenu.querySelectorAll('button:"
        "not([disabled])'));var at=items.indexOf(document.activeElement);if(e.key==="
        "'Escape'){e.preventDefault();closeStructure();structureToggle.focus();return"
        ";}if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();var step=e."
        "key==='ArrowDown'?1:-1;items[(at+step+items.length)%items.length].focus();}}"
        ");}"
     << "var tooltip=document.getElementById('md-tooltip');var tooltipTimer=0;"
     << "function hideTooltip(){if(tooltipTimer){clearTimeout(tooltipTimer);"
        "tooltipTimer=0;}if(tooltip){tooltip.hidden=true;}}"
     << "function showTooltip(target,immediate){if(!tooltip||!target){return;}"
        "hideTooltip();var reveal=function(){var title=target.getAttribute('data-"
        "tooltip-title')||'';var shortcut=target.getAttribute('data-shortcut')||'';"
        "tooltip.querySelector('.tooltip-title').textContent=title+(shortcut?' · '+"
        "shortcut:'');tooltip.querySelector('.tooltip-hint').textContent=target."
        "getAttribute('data-tooltip-hint')||'';tooltip.hidden=false;var r=target."
        "getBoundingClientRect();var tr=tooltip.getBoundingClientRect();tooltip.style."
        "left=Math.max(8,Math.min(r.left+r.width/2-tr.width/2,window.innerWidth-tr."
        "width-8))+'px';tooltip.style.top=Math.min(window.innerHeight-tr.height-8,r."
        "bottom+8)+'px';};if(immediate){reveal();}else{tooltipTimer=setTimeout(reveal,"
        "450);}}"
     << "document.addEventListener('mouseover',function(e){var t=e.target.closest&&e."
        "target.closest('[data-tooltip-title]');if(t&&!(e.relatedTarget&&t.contains(e."
        "relatedTarget))){showTooltip(t,false);}});"
     << "document.addEventListener('mouseout',function(e){var t=e.target.closest&&e."
        "target.closest('[data-tooltip-title]');if(t&&!t.contains(e.relatedTarget)){"
        "hideTooltip();}});"
     << "document.addEventListener('focusin',function(e){var t=e.target.closest&&e."
        "target.closest('[data-tooltip-title]');if(t){showTooltip(t,true);}});"
     << "document.addEventListener('focusout',function(e){var t=e.target.closest&&e."
        "target.closest('[data-tooltip-title]');if(t){hideTooltip();}});"
     << "document.addEventListener('keydown',function(e){if(e.key==='Escape'){"
        "hideTooltip();closeStructure();}});window.addEventListener('scroll',"
        "hideTooltip,true);"
     << "var bars=document.querySelectorAll('.icon-toolbar');for(var bi=0;bi<bars."
        "length;bi++){bars[bi].addEventListener('keydown',function(e){if(e.key!=="
        "'ArrowRight'&&e.key!=='ArrowLeft'&&e.key!=='Home'&&e.key!=='End'){return;}"
        "var items=Array.prototype.slice.call(this.querySelectorAll('.icon-button:"
        "not(.structure-item):not([disabled])'));var at=items.indexOf(document."
        "activeElement);if(at<0){return;}e.preventDefault();var next=e.key==='Home'?0:"
        "e.key==='End'?items.length-1:(at+(e.key==='ArrowRight'?1:-1)+items.length)%"
        "items.length;for(var k=0;k<items.length;k++){items[k].tabIndex=k===next?0:-1;}"
        "items[next].focus();});}"
     << "function shortcutAction(e){if(!source||document.activeElement!==source||e."
        "isComposing||e.keyCode===229||(e.getModifierState&&e.getModifierState('"
        "AltGraph'))){return null;}var mac=document.documentElement.getAttribute('"
        "data-platform')==='macos';var primary=mac?e.metaKey:e.ctrlKey;if(!primary||"
        "(mac&&e.ctrlKey)||(!mac&&e.metaKey)){return null;}var key=e.key.toLowerCase"
        "();if(e.altKey&&!e.shiftKey&&/^[123]$/.test(key)){return 'h'+key;}if(!e."
        "altKey&&!e.shiftKey){if(key==='b'){return 'bold';}if(key==='i'){return '"
        "italic';}if(key==='k'){return 'link';}}if(!e.altKey&&e.shiftKey){if(key==='"
        "x'){return 'strike';}if(key==='8'){return 'bullet-list';}if(key==='7'){"
        "return 'ordered-list';}}return null;}"
     << "document.addEventListener('keydown',function(e){if(!source){return;}if((e."
        "key==='Tab')&&document.activeElement===source&&!e.isComposing&&e.keyCode!=="
        "229&&structureContext().structured){e.preventDefault();requestAction(e."
        "shiftKey?'outdent':'indent');return;}var action=shortcutAction(e);if(action){"
        "e.preventDefault();requestAction(action);}});"
     << "if(source){source.addEventListener('select',updateStructure);source."
        "addEventListener('keyup',updateStructure);source.addEventListener('click',"
        "updateStructure);}";
}

void AppendMdvDividerScript(std::ostringstream& js) {
  js << "var divider=document.getElementById('md-divider');"
     << "var sourcePane=document.querySelector('.md-source-pane');"
     << "var panes=document.querySelector('.md-panes');"
     << "if(divider&&sourcePane&&panes){"
     << "divider.addEventListener('mousedown',function(e){"
     << "e.preventDefault();"
     << "divider.setAttribute('data-dragging','1');"
     << "var total=panes.clientWidth-6;"
     << "function onMove(ev){"
     << "var x=ev.clientX-panes.getBoundingClientRect().left;"
     << "var ratio=Math.min(0.9,Math.max(0.1,x/total));"
     << "sourcePane.style.flex='0 0 '+(ratio*100)+'%';"
     << "}"
     << "function onUp(){"
     << "divider.removeAttribute('data-dragging');"
     << "document.removeEventListener('mousemove',onMove);"
     << "document.removeEventListener('mouseup',onUp);"
     << "}"
     << "document.addEventListener('mousemove',onMove);"
     << "document.addEventListener('mouseup',onUp);"
     << "});}"
     << "})();";
}

void AppendMdvHighlightScript(std::ostringstream& js) {
  js << "var highlightObserver=null;"
     << "function resetHighlights(){if(highlightObserver){highlightObserver."
        "disconnect();highlightObserver=null;}}"
     << "function startHighlight(code){if(!code){return Promise.resolve(false);"
        "}if(code.getAttribute('data-mdv-highlighted')==='true'){return Promise."
        "resolve(true);}if(code.getAttribute('data-mdv-highlight-loading')==='"
        "true'){return Promise.resolve(false);}var language=code.getAttribute("
        "'data-mdv-highlight');var nodeId=code.getAttribute('data-mdv-node');if"
        "(!language||!nodeId){return Promise.resolve(false);}code.setAttribute("
        "'data-mdv-highlight-loading','true');return import('/runtime/highlight/"
        "adapter').then(function(adapter){return adapter.highlightCode(code,"
        "language,nodeId);}).then(function(ok){code.removeAttribute('data-mdv-"
        "highlight-loading');if(ok&&highlightObserver){highlightObserver."
        "unobserve(code);}return ok===true;},function(){code.removeAttribute("
        "'data-mdv-highlight-loading');return false;});}"
     << "function observeHighlights(root){if(!root||typeof IntersectionObserver"
        "!=='function'){return;}if(!highlightObserver){highlightObserver=new "
        "IntersectionObserver(function(entries){for(var i=0;i<entries.length;i++)"
        "{if(entries[i].isIntersecting){startHighlight(entries[i].target);}}},{"
        "root:null,rootMargin:'120px"
        " 0px',threshold:0});}var nodes=root.querySelectorAll('code[data-mdv-"
        "highlight]');for(var i=0;i<nodes.length;i++){highlightObserver.observe("
        "nodes[i]);}}"
     << "observeHighlights(preview);";
}

void AppendMdvMathScript(std::ostringstream& js) {
  js << "var mathObserver=null;"
     << "function resetMath(){if(mathObserver){mathObserver.disconnect();"
        "mathObserver=null;}}"
     << "function startMath(node){if(!node||node.getAttribute('data-mdv-math-"
        "rendered')==='true'){return;}var kind=node.getAttribute('data-mdv-"
        "math');var nodeId=node.getAttribute('data-mdv-node');if(!kind||!nodeId)"
        "{return;}import('/runtime/katex/adapter').then(function(adapter){return "
        "adapter.renderMath(node,nodeId,kind==='block');}).catch(function(){});}"
     << "function observeMath(root){if(!root||typeof IntersectionObserver!=="
        "'function'){return;}if(!mathObserver){mathObserver=new "
        "IntersectionObserver(function(entries){for(var i=0;i<entries.length;i++)"
        "{if(entries[i].isIntersecting){mathObserver.unobserve(entries[i].target);"
        "startMath(entries[i].target);}}},{root:null,rootMargin:'120px 0px',"
        "threshold:0});}var nodes=root.querySelectorAll('[data-mdv-math]');"
        "for(var i=0;i<nodes.length;i++){mathObserver.observe(nodes[i]);}}"
     << "observeMath(preview);";
}

std::string RenderMdvScript() {
  // One IIFE is assembled from bounded, single-purpose sections so the script
  // shares state without turning the C++ renderer into a >200-line function.
  std::ostringstream js;
  AppendMdvCoreScript(js);
  AppendMdvToolbarScript(js);
  AppendMdvHighlightScript(js);
  AppendMdvMathScript(js);
  AppendMdvDividerScript(js);
  return js.str();
}

}  // namespace crayon::browser_mdv
