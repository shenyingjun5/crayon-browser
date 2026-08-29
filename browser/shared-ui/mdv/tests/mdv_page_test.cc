// MDV-08 contract tests: crayon://mdv route matrix, deterministic page
// assembly, escape/whitelist boundaries, zero-network surface.

#include "crayon/browser_mdv/mdv_page.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using crayon::browser_mdv::ClassifyMdvRequest;
using crayon::browser_mdv::kMdvCsp;
using crayon::browser_mdv::MdvLoadStatus;
using crayon::browser_mdv::MdvPageSnapshot;
using crayon::browser_mdv::MdvPageStrings;
using crayon::browser_mdv::MdvRequestParts;
using crayon::browser_mdv::MdvResourceKind;
using crayon::browser_mdv::MdvRoute;
using crayon::browser_mdv::MdvViewMode;

int g_failures = 0;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      ++g_failures;                                         \
    }                                                       \
  } while (false)

std::size_t Count(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  std::size_t at = 0;
  while ((at = text.find(needle, at)) != std::string_view::npos) {
    ++count;
    at += needle.size();
  }
  return count;
}

MdvRequestParts BaseRequest(std::string path = "/app.html") {
  return MdvRequestParts{"GET", "crayon", "mdv", std::move(path),
                         false, false,    false, false};
}

crayon::browser_mdv::MdvPageStrings SampleStrings() {
  return crayon::browser_mdv::MdvPageStrings{
      "zh-CN",
      "蜡笔文档",
      "源码",
      "预览",
      "分栏",
      "尚未打开文档",
      "文件超过大小上限",
      "文件不是有效的 UTF-8 编码",
      "内容触发渲染安全策略，已拒绝显示",
      "不是可打开的 Markdown 文档",
      "已保存",
      "当前文档有未保存的修改，是否继续？",
      "保存并继续",
      "放弃更改",
      "取消",
      "在文档查看器中打开",
      "编辑工具",
      "加粗",
      "斜体",
      "删除线",
      "行内代码",
      "无序列表",
      "有序列表",
      "任务列表",
      "引用",
      "代码块",
      "表格",
      "链接",
      "分割线",
      "一级标题",
      "二级标题",
      "三级标题",
      "缩进和对齐",
      "增加缩进",
      "减少缩进",
      "默认对齐",
      "左对齐",
      "居中对齐",
      "右对齐",
      "切换文档视图",
      "Markdown 格式",
      "列表或引用层级",
      "表格列对齐",
      "全屏查看图表",
      "查看源码",
      "关闭",
      "图表无法渲染，已保留源码",
      crayon::browser_mdv::MdvShortcutPlatform::kWindows,
  };
}

void TestRouteMatrix() {
  auto route = ClassifyMdvRequest(BaseRequest("/"));
  CHECK(route.kind == MdvResourceKind::kDocument && route.status_code == 200 &&
        route.include_body);
  route = ClassifyMdvRequest(BaseRequest("/app.html"));
  CHECK(route.kind == MdvResourceKind::kDocument && route.include_body);
  route = ClassifyMdvRequest(BaseRequest("/app.css"));
  CHECK(route.kind == MdvResourceKind::kStylesheet && route.include_body);
  route = ClassifyMdvRequest(BaseRequest("/app.js"));
  CHECK(route.kind == MdvResourceKind::kScript && route.include_body);
  route = ClassifyMdvRequest(BaseRequest("/runtime/highlight/adapter"));
  CHECK(route.kind == MdvResourceKind::kRuntimeAsset &&
        route.runtime_namespace == "highlight" &&
        route.runtime_resource_id == "adapter");
  route = ClassifyMdvRequest(BaseRequest("/runtime/katex/stylesheet"));
  CHECK(route.kind == MdvResourceKind::kRuntimeAsset &&
        route.runtime_namespace == "katex" &&
        route.runtime_resource_id == "stylesheet");
  route = ClassifyMdvRequest(
      BaseRequest("/runtime/katex/fonts/KaTeX_Main-Regular.woff2"));
  CHECK(route.kind == MdvResourceKind::kRuntimeAsset &&
        route.runtime_namespace == "katex");
  CHECK(ClassifyMdvRequest(BaseRequest("/runtime/katex/../LICENSE")).kind ==
        MdvResourceKind::kNotFound);
  CHECK(ClassifyMdvRequest(BaseRequest("/runtime/katex/unknown")).kind ==
        MdvResourceKind::kNotFound);

  // MDV-16/17: Mermaid runtime route keeps upstream-relative resource ids;
  // the Crayon-owned page adapter rides the same closed namespace.
  route = ClassifyMdvRequest(
      BaseRequest("/runtime/mermaid/mermaid.esm.min.mjs"));
  CHECK(route.kind == MdvResourceKind::kRuntimeAsset &&
        route.runtime_namespace == "mermaid" &&
        route.runtime_resource_id == "mermaid.esm.min.mjs");
  route = ClassifyMdvRequest(BaseRequest("/runtime/mermaid/adapter"));
  CHECK(route.kind == MdvResourceKind::kRuntimeAsset &&
        route.runtime_namespace == "mermaid" &&
        route.runtime_resource_id == "adapter");
  route = ClassifyMdvRequest(
      BaseRequest("/runtime/mermaid/chunks/mermaid.esm.min/chunk-IJBDOHL6.mjs"));
  CHECK(route.kind == MdvResourceKind::kRuntimeAsset &&
        route.runtime_namespace == "mermaid" &&
        route.runtime_resource_id == "chunks/mermaid.esm.min/chunk-IJBDOHL6.mjs");
  // Traversal, encoded separators and escapes never classify; grammar-valid
  // but manifest-unknown ids classify and are 404'd by the handler's exact
  // bundle lookup (same split as the highlight/katex namespaces).
  for (const char* hostile : {
           "/runtime/mermaid/chunks/../core.mjs",
           "/runtime/mermaid/chunks%2fmermaid.esm.min",
           "/runtime/mermaid/chunks/mermaid.esm.min/%2fcore",
           "/runtime/mermaid/mermaid.esm.min.mjs/../../x",
           "/runtime/mermaid/%2e%2e/x",
           "/runtime/mermaid/a%22b",
           "/runtime/mermaid/a%2fb",
           "/runtime/mermaid/%zz",
           "/runtime/mermaid/%2",
       }) {
    CHECK(ClassifyMdvRequest(BaseRequest(hostile)).kind ==
          MdvResourceKind::kNotFound);
  }
  // Unknown and case-aliased ids classify but are 404'd by the handler's
  // exact, case-sensitive bundle lookup.
  for (const char* manifest_absent : {
           "/runtime/mermaid/chunks/mermaid.esm.min/unknown.mjs",
           "/runtime/mermaid/CHUNKS/mermaid.esm.min/chunk-IJBDOHL6.mjs",
       }) {
    auto route = ClassifyMdvRequest(BaseRequest(manifest_absent));
    CHECK(route.kind == MdvResourceKind::kRuntimeAsset &&
          route.runtime_namespace == "mermaid");
  }

  // HEAD is accepted with the body suppressed.
  auto head = BaseRequest();
  head.method = "HEAD";
  route = ClassifyMdvRequest(head);
  CHECK(route.kind == MdvResourceKind::kDocument && !route.include_body);

  // Non-GET methods are a stable rejection.
  for (const char* method : {"POST", "PUT", "DELETE"}) {
    auto request = BaseRequest();
    request.method = method;
    route = ClassifyMdvRequest(request);
    CHECK(route.kind == MdvResourceKind::kMethodNotAllowed &&
          route.status_code == 405);
  }

  // Wrong scheme/host and decorated URLs fail closed.
  auto wrong_scheme = BaseRequest();
  wrong_scheme.scheme = "https";
  CHECK(ClassifyMdvRequest(wrong_scheme).kind == MdvResourceKind::kNotFound);
  auto wrong_host = BaseRequest();
  wrong_host.host = "newtab";
  CHECK(ClassifyMdvRequest(wrong_host).kind == MdvResourceKind::kNotFound);
  void (*decorations[])(MdvRequestParts&) = {
      [](MdvRequestParts& r) { r.has_credentials = true; },
      [](MdvRequestParts& r) { r.has_port = true; },
      [](MdvRequestParts& r) { r.has_query = true; },
      [](MdvRequestParts& r) { r.has_fragment = true; },
  };
  for (auto* decorate : decorations) {
    auto request = BaseRequest();
    decorate(request);
    CHECK(ClassifyMdvRequest(request).kind == MdvResourceKind::kNotFound);
  }
}

void TestDeterministicOutput() {
  MdvPageSnapshot snapshot;
  snapshot.view_mode = MdvViewMode::kSplit;
  snapshot.load_status = MdvLoadStatus::kLoaded;
  snapshot.has_document = true;
  snapshot.source_text = "# 标题\n\n正文 **strong** <script>alert(1)</script>";
  snapshot.rendered_html =
      "<h1>标题</h1><p>正文 <strong>strong</strong> &lt;script&gt;</p>";

  const auto strings = SampleStrings();
  const std::string first =
      crayon::browser_mdv::RenderMdvDocument(snapshot, strings);
  const std::string second =
      crayon::browser_mdv::RenderMdvDocument(snapshot, strings);
  CHECK(first == second && !first.empty());
  CHECK(crayon::browser_mdv::RenderMdvStylesheet() ==
        crayon::browser_mdv::RenderMdvStylesheet());
  CHECK(crayon::browser_mdv::RenderMdvScript() ==
        crayon::browser_mdv::RenderMdvScript());
}

void TestSourceIsEscapedAndPreviewVerbatim() {
  MdvPageSnapshot snapshot;
  snapshot.view_mode = MdvViewMode::kSplit;
  snapshot.load_status = MdvLoadStatus::kLoaded;
  snapshot.has_document = true;
  snapshot.source_text = "<img src=x onerror=alert(1)> & \"quote\"";
  snapshot.rendered_html = "<p>safe-whitelist-html</p>";
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, SampleStrings());
  // Raw source must not appear anywhere in the output.
  CHECK(document.find("<img src=x") == std::string::npos);
  CHECK(document.find("onerror") == std::string::npos ||
        document.find("onerror") > document.find("&lt;img"));
  CHECK(document.find("&#34;quote&#34;") != std::string::npos);
  // Trusted preview HTML lands verbatim.
  CHECK(document.find("<p>safe-whitelist-html</p>") != std::string::npos);
}

void TestNoNetworkOrInlineHandlers() {
  // Documented surface carries the framework references; the empty/error
  // surface intentionally stops before them.
  MdvPageSnapshot snapshot;
  snapshot.view_mode = MdvViewMode::kPreview;
  snapshot.load_status = MdvLoadStatus::kLoaded;
  snapshot.has_document = true;
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, SampleStrings());
  const std::string css = crayon::browser_mdv::RenderMdvStylesheet();
  const std::string js = crayon::browser_mdv::RenderMdvScript();

  // The link-skeleton placeholder is data, not a network reference.
  std::string js_sanitized = js;
  const std::string placeholder = "[链接文字](https://)";
  const auto at = js_sanitized.find(placeholder);
  if (at != std::string::npos) {
    js_sanitized.replace(at, placeholder.size(), "");
  }
  const std::string& js_fixed = js_sanitized;
  for (const std::string* body : {&document, &css, &js_fixed}) {
    for (const char* marker :
         {"http://", "https://", "fetch(", "XMLHttpRequest",
          "onclick=", "onload=", "javascript:"}) {
      CHECK(body->find(marker) == std::string::npos);
    }
  }
  CHECK(document.find("src=\"/app.js\"") != std::string::npos);
  CHECK(document.find("href=\"/app.css\"") != std::string::npos);
  CHECK(Count(js_fixed, "import(") == 3);
  CHECK(js_fixed.find("import('/runtime/highlight/adapter')") !=
        std::string::npos);
  CHECK(js_fixed.find("import('/runtime/katex/adapter')") != std::string::npos);
  CHECK(js_fixed.find("import('/runtime/mermaid/adapter')") !=
        std::string::npos);
  CHECK(js_fixed.find("observeMath(preview)") != std::string::npos);
  CHECK(js_fixed.find("observeMermaid(preview)") != std::string::npos);
}

void TestEntryErrorBannerTakesPriority() {
  MdvPageSnapshot snapshot;
  snapshot.load_status = MdvLoadStatus::kLoaded;
  snapshot.error_text = "不是可打开的 Markdown 文档";
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, SampleStrings());
  CHECK(document.find("不是可打开的 Markdown 文档") != std::string::npos);
  // The override is escaped like any other text surface.
  MdvPageSnapshot hostile;
  hostile.error_text = "<img src=x>";
  const std::string escaped =
      crayon::browser_mdv::RenderMdvDocument(hostile, SampleStrings());
  CHECK(escaped.find("<img src=x>") == std::string::npos);
  CHECK(escaped.find("&lt;img src=x&gt;") != std::string::npos);
}

void TestEditableSourceDividerAndConfirmOverlay() {
  MdvPageSnapshot snapshot;
  snapshot.has_document = true;
  snapshot.source_text = "# 源码";
  snapshot.confirm_visible = true;
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, SampleStrings());
  // Editable source pane: textarea, not a read-only <pre>.
  CHECK(document.find("<textarea id=\"md-source\"") != std::string::npos);
  CHECK(document.find("<pre><code>") == std::string::npos);
  // Resizable divider present.
  CHECK(document.find("id=\"md-divider\"") != std::string::npos);
  // Confirm overlay with three closed choices and localized labels.
  CHECK(document.find("id=\"md-confirm\"") != std::string::npos);
  CHECK(document.find("data-show=\"true\"") != std::string::npos);
  CHECK(document.find(SampleStrings().label_save) != std::string::npos);
  CHECK(document.find(SampleStrings().label_discard) != std::string::npos);
  CHECK(document.find(SampleStrings().label_cancel) != std::string::npos);

  MdvPageSnapshot hidden;
  hidden.has_document = true;
  hidden.confirm_visible = false;
  const std::string collapsed =
      crayon::browser_mdv::RenderMdvDocument(hidden, SampleStrings());
  CHECK(collapsed.find("data-show=\"false\"") != std::string::npos);
}

void TestDocumentNameInTitleAndScrollLinkage() {
  MdvPageSnapshot snapshot;
  snapshot.has_document = true;
  snapshot.document_name = "verify.md";
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, SampleStrings());
  CHECK(document.find("verify.md - 蜡笔文档") != std::string::npos);

  MdvPageSnapshot unnamed;
  unnamed.has_document = true;
  const std::string fallback =
      crayon::browser_mdv::RenderMdvDocument(unnamed, SampleStrings());
  CHECK(fallback.find("<title>蜡笔文档</title>") != std::string::npos);

  // Split scroll linkage: one-way proportional sync present in the page
  // script (md4c produces no source maps; V1 is ratio-based).
  const std::string script = crayon::browser_mdv::RenderMdvScript();
  CHECK(script.find("previewPane.scrollTop") != std::string::npos);
  CHECK(script.find("scrollHeight") != std::string::npos);
  CHECK(script.find("md-divider") != std::string::npos);
}

void TestToolbarClosedActionSet() {
  MdvPageSnapshot snapshot;
  snapshot.has_document = true;
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, SampleStrings());
  CHECK(document.find("class=\"md-toolbar icon-toolbar\"") !=
        std::string::npos);
  // Closed action set: exactly the 15 documented actions.
  const char* actions[] = {"h1", "h2",       "h3",         "bold",
                           "italic",  "strike",    "inline-code",
                           "bullet-list", "ordered-list", "task-list",
                           "quote",       "code-block", "table",
                           "link",         "divider"};
  for (const char* action : actions) {
    CHECK(document.find(std::string("data-action=\"") + action + "\"")
          != std::string::npos);
  }
  CHECK(document.find("data-action=\"align\"") == std::string::npos);

  const std::string script = crayon::browser_mdv::RenderMdvScript();
  CHECK(script.find("setRangeText") != std::string::npos);
  CHECK(script.find("type:'transform'") != std::string::npos);
  CHECK(script.find("linePrefix") == std::string::npos);
  CHECK(document.find("data-tooltip-title=") != std::string::npos);
  CHECK(document.find("aria-keyshortcuts=\"Control+B\"") !=
        std::string::npos);
  CHECK(document.find("class=\"structure-menu\"") != std::string::npos);
  CHECK(document.find("<svg") != std::string::npos);
  CHECK(Count(document, "data-action=\"") == 21);
  CHECK(Count(document, "xmlns=\"http://www.w3.org/2000/svg\"") == 0);
  CHECK(script.find("setTimeout(reveal,450)") != std::string::npos);
  CHECK(script.find("isComposing") != std::string::npos);
  CHECK(script.find("keyCode===229") != std::string::npos);
  CHECK(script.find("AltGraph") != std::string::npos);

  auto mac_strings = SampleStrings();
  mac_strings.shortcut_platform =
      crayon::browser_mdv::MdvShortcutPlatform::kMacOS;
  const std::string mac_document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, mac_strings);
  CHECK(mac_document.find("data-platform=\"macos\"") != std::string::npos);
  CHECK(mac_document.find("aria-keyshortcuts=\"Meta+B\"") !=
        std::string::npos);
  CHECK(mac_document.find("data-shortcut=\"⌘B\"") != std::string::npos);

  const std::string css = crayon::browser_mdv::RenderMdvStylesheet();
  CHECK(css.find("width:36px;height:36px") != std::string::npos);
  CHECK(css.find("width:20px;height:20px") != std::string::npos);
  CHECK(css.find("prefers-reduced-motion:reduce") != std::string::npos);
}

void TestEmptyAndErrorSurfaces() {
  MdvPageSnapshot empty;
  empty.load_status = MdvLoadStatus::kEmpty;
  empty.has_document = false;
  const auto strings = SampleStrings();
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(empty, strings);
  CHECK(document.find(strings.status_empty) != std::string::npos);
  CHECK(document.find("md-empty") != std::string::npos);

  MdvPageSnapshot policy;
  policy.load_status = MdvLoadStatus::kRenderPolicyViolation;
  CHECK(crayon::browser_mdv::RenderMdvDocument(policy, strings)
            .find(strings.status_render_policy) != std::string::npos);
}

void TestInitialViewStateAndCspConstantUnchanged() {
  MdvPageSnapshot source_view;
  source_view.view_mode = MdvViewMode::kSource;
  source_view.has_document = true;
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(source_view, SampleStrings());
  CHECK(document.find("data-view=\"source\"") != std::string::npos);

  // The CSP constant stays byte-for-byte locked (MDV-01 §2).
  CHECK(std::string(kMdvCsp).find("script-src 'self'") != std::string::npos);
  CHECK(std::string(kMdvCsp).find("default-src 'none'") != std::string::npos);
  CHECK(std::string(kMdvCsp).find("style-src-attr 'unsafe-inline'") !=
        std::string::npos);
  CHECK(std::string(kMdvCsp).find("font-src 'self'") != std::string::npos);
  CHECK(std::string(kMdvCsp).find("style-src-elem 'unsafe-inline'") ==
        std::string::npos);
}

void TestHighlightRoutesAndLazyBootstrap() {
  MdvRequestParts request{"GET", "crayon", "mdv",
                          "/runtime/highlight/adapter"};
  auto route = crayon::browser_mdv::ClassifyMdvRequest(request);
  CHECK(route.kind == MdvResourceKind::kRuntimeAsset);
  CHECK(route.runtime_resource_id == "adapter");
  CHECK(route.include_body);
  for (const std::string path : {"/runtime/highlight/../core",
                                 "/runtime/highlight/core.js",
                                 "/runtime/highlight/%2fcore",
                                 "/runtime/highlight/Core",
                                 "/runtime/highlight/"}) {
    request.path = path;
    CHECK(crayon::browser_mdv::ClassifyMdvRequest(request).kind ==
          MdvResourceKind::kNotFound);
  }
  const std::string script = crayon::browser_mdv::RenderMdvScript();
  CHECK(script.find("IntersectionObserver") != std::string::npos);
  CHECK(script.find("import('/runtime/highlight/adapter')") !=
        std::string::npos);
  CHECK(script.find("data-mdv-highlight-loading") != std::string::npos);
  CHECK(script.find("if(ok&&highlightObserver){highlightObserver.unobserve(") !=
        std::string::npos);
  CHECK(script.find("highlightObserver.unobserve(entries[i].target);"
                    "startHighlight(entries[i].target)") == std::string::npos);
  CHECK(script.find("highlightAuto") == std::string::npos);
  CHECK(script.find("highlightAll") == std::string::npos);
  const std::string css = crayon::browser_mdv::RenderMdvStylesheet();
  CHECK(css.find("pre code.hljs") != std::string::npos);
  CHECK(css.find("prefers-color-scheme:dark") != std::string::npos);
}

void TestMermaidRuntimeBootstrap() {
  // The page discovers decorated blocks and renders them lazily through the
  // mermaid adapter (/runtime/mermaid/adapter), which owns the strict
  // runtime singleton, the color scheme and the SVG policy gate.
  const std::string script = crayon::browser_mdv::RenderMdvScript();
  CHECK(script.find("import('/runtime/mermaid/adapter')") !=
        std::string::npos);
  CHECK(script.find(
            "adapter.renderMermaid(node,nodeId,mermaidDark?'dark':'light')") !=
        std::string::npos);
  CHECK(script.find("code[data-mdv-mermaid]") != std::string::npos);
  // One render in flight: blocks queue behind the shared promise.
  CHECK(script.find("mermaidQueue=mermaidQueue.then(") != std::string::npos);
  // Full-page scan and direct runtime access never enter the page script.
  CHECK(script.find("mermaid.run(") == std::string::npos);
  CHECK(script.find("securityLevel") == std::string::npos);
  CHECK(script.find("parseMermaidSvgCandidate") == std::string::npos);
  // Adapter failures are swallowed per block; no unhandled rejections.
  CHECK(script.find("if(!ok){markMermaidError(node);}},function(){});") !=
        std::string::npos);
  // MDV-18: viewport lazy render with a bounded first-screen fallback.
  CHECK(script.find("mermaidObserver=new IntersectionObserver(") !=
        std::string::npos);
  CHECK(script.find("mermaidObserver.unobserve(entries[i].target);"
                    "queueMermaid(entries[i].target)") != std::string::npos);
  CHECK(script.find("Math.min(nodes.length,8)") != std::string::npos);
  CHECK(script.find("function resetMermaid()") != std::string::npos);
  // Light/dark theme mapping and redraw.
  CHECK(script.find("prefers-color-scheme: dark") != std::string::npos);
  CHECK(script.find("function rethemeMermaid()") != std::string::npos);
  CHECK(script.find("removeAttribute('data-mdv-mermaid-rendered')") !=
        std::string::npos);
  // Fullscreen view: page-local overlay, Escape close, focus restore,
  // source toggle; no new Browser binding.
  CHECK(script.find("getElementById('md-mermaid-view')") !=
        std::string::npos);
  CHECK(script.find("function closeMermaidView()") != std::string::npos);
  CHECK(script.find("mermaidViewOpener.focus()") != std::string::npos);
  CHECK(script.find("event.key==='Escape'&&!mermaidView.hidden") !=
        std::string::npos);
  CHECK(script.find("svg.cloneNode(true)") != std::string::npos);
  CHECK(script.find(".mdv-mermaid-view-source") != std::string::npos);
  CHECK(script.find("setAttribute('aria-pressed','true')") !=
        std::string::npos);
  // Local error card carries the localized text, never a stack or path.
  CHECK(script.find("data-mermaid-error-text") != std::string::npos);
  CHECK(script.find("mdv-mermaid-error-card") != std::string::npos);

  // The fullscreen overlay ships with the document surface, localized and
  // hidden by default; the error text rides an escaped data attribute.
  MdvPageSnapshot snapshot;
  snapshot.has_document = true;
  const std::string document =
      crayon::browser_mdv::RenderMdvDocument(snapshot, SampleStrings());
  CHECK(document.find("id=\"md-mermaid-view\"") != std::string::npos);
  CHECK(document.find("role=\"dialog\" aria-modal=\"true\"") !=
        std::string::npos);
  CHECK(document.find("aria-label=\"全屏查看图表\"") != std::string::npos);
  CHECK(document.find("data-mermaid-view=\"close\"") != std::string::npos);
  CHECK(document.find(">关闭</button>") != std::string::npos);
  CHECK(document.find("aria-pressed=\"false\"") != std::string::npos);
  CHECK(document.find("data-mermaid-error-text=\"图表无法渲染，已保留源码\"") !=
        std::string::npos);

  const std::string css = crayon::browser_mdv::RenderMdvStylesheet();
  CHECK(css.find("data-mdv-mermaid-rendered=true]") != std::string::npos);
  CHECK(css.find("data-mdv-mermaid-error=true]") != std::string::npos);
  CHECK(css.find("svg{max-width:100%") != std::string::npos);
  CHECK(css.find("cursor:zoom-in") != std::string::npos);
  CHECK(css.find(".md-mermaid-view{position:fixed") != std::string::npos);
  CHECK(css.find(".mdv-mermaid-error-card") != std::string::npos);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--dump-script") {
    std::cout << crayon::browser_mdv::RenderMdvScript();
    return 0;
  }
  TestRouteMatrix();
  TestDeterministicOutput();
  TestSourceIsEscapedAndPreviewVerbatim();
  TestNoNetworkOrInlineHandlers();
  TestEntryErrorBannerTakesPriority();
  TestEditableSourceDividerAndConfirmOverlay();
  TestDocumentNameInTitleAndScrollLinkage();
  TestToolbarClosedActionSet();
  TestEmptyAndErrorSurfaces();
  TestInitialViewStateAndCspConstantUnchanged();
  TestHighlightRoutesAndLazyBootstrap();
  TestMermaidRuntimeBootstrap();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "ALL TESTS PASSED\n";
  return 0;
}
