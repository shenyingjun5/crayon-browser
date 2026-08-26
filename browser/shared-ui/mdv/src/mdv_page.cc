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
           << "\" data-app=\"mdv\" data-dirty=\""
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
              "<nav class=\"view-bar\" aria-label=\""
           << EscapeHtml(strings.document_title) << "\">"
           << ViewButton(strings.view_source, "source", snapshot.view_mode)
           << ViewButton(strings.view_preview, "preview", snapshot.view_mode)
           << ViewButton(strings.view_split, "split", snapshot.view_mode)
           << "</nav>";
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
           << EscapeHtml(strings.view_source)
           << "\"><textarea id=\"md-source\" spellcheck=\"false\">"
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
      << ".md-divider{width:6px;cursor:col-resize;background:canvasText;"
         "opacity:.08;flex:0 0 auto;}"
      << ".md-divider:hover,.md-divider[data-dragging]{opacity:.25;}"
      << "body[data-view=preview] .md-source-pane{display:none;}"
      << "body[data-view=source] .md-preview-pane{display:none;}"
      << ".md-source-pane textarea{width:100%;height:100%;border:none;"
         "outline:none;resize:none;background:transparent;color:inherit;"
         "font:13px/1.6 ui-monospace,monospace;}"
      << ".md-source-pane pre{white-space:pre-wrap;word-break:break-word;}"
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
      << "@media(prefers-color-scheme:dark){a{color:#9ecbff;}}";
  return css.str();
}

std::string RenderMdvScript() {
  // In-memory script: view switching, edit-burst queries over the
  // controlled mdvQuery binding, confirm-dialog decisions and the
  // beforeunload dirty guard.  No inline handlers (CSP), no network.
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
     << "var source=document.getElementById('md-source');"
     << "var preview=document.getElementById('md-preview');"
     << "var confirmBox=document.getElementById('md-confirm');"
     << "function apply(state){"
     << "if(!state){return;}"
     << "if(typeof "
        "state.preview==='string'&&preview){preview.innerHTML=state.preview;}"
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
     << "function sendQuery(payload){"
     << "if(typeof window.mdvQuery!=='function'){return;}"
     << "window.mdvQuery({request:JSON.stringify(payload),persistent:false,"
     << "onSuccess:function(response){try{apply(JSON.parse(response));}catch(e)"
        "{}},"
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
     << "});}"
     << "var divider=document.getElementById('md-divider');"
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
  return js.str();
}

}  // namespace crayon::browser_mdv
