// MDV-08 contract tests: crayon://mdv route matrix, deterministic page
// assembly, escape/whitelist boundaries, zero-network surface.

#include "crayon/browser_mdv/mdv_page.h"

#include <iostream>
#include <string>

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

  for (const auto* body : {&document, &css, &js}) {
    for (const char* marker :
         {"http://", "https://", "fetch(", "XMLHttpRequest", "import(",
          "onclick=", "onload=", "javascript:"}) {
      CHECK(body->find(marker) == std::string::npos);
    }
  }
  CHECK(document.find("src=\"/app.js\"") != std::string::npos);
  CHECK(document.find("href=\"/app.css\"") != std::string::npos);
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
}

}  // namespace

int main() {
  TestRouteMatrix();
  TestDeterministicOutput();
  TestSourceIsEscapedAndPreviewVerbatim();
  TestNoNetworkOrInlineHandlers();
  TestEntryErrorBannerTakesPriority();
  TestEmptyAndErrorSurfaces();
  TestInitialViewStateAndCspConstantUnchanged();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "ALL TESTS PASSED\n";
  return 0;
}
