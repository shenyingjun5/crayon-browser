#include "browser/mdv/cef_mdv_handler.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "crayon/browser_markdown/markdown_render.h"
#include "crayon/browser_mdv/mdv_page.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::mdv {
namespace {

using crayon::browser_mdv::ClassifyMdvRequest;
using crayon::browser_mdv::kMdvCsp;
using crayon::browser_mdv::kMdvHost;
using crayon::browser_mdv::kMdvScheme;
using crayon::browser_mdv::MdvLoadStatus;
using crayon::browser_mdv::MdvPageSnapshot;
using crayon::browser_mdv::MdvPageStrings;
using crayon::browser_mdv::MdvRequestParts;
using crayon::browser_mdv::RenderMdvDocument;

constexpr char kHtmlMimeType[] = "text/html";
constexpr char kCssMimeType[] = "text/css";
constexpr char kJsMimeType[] = "text/javascript";
constexpr char kTextMimeType[] = "text/plain";
constexpr char kUtf8Charset[] = "utf-8";

// Deterministic fixture document exercising the enabled syntax set
// (headings, table, fenced code, task list, safe link, raw-HTML escape).
// Real file entries arrive with MDV-09; this slice is content-driven.
constexpr char kFixtureMarkdown[] =
    "# 蜡笔文档查看器\n\n"
    "这是内置 Markdown 查看器的**只读**示例文档。\n\n"
    "| 特性 | 状态 |\n|---|---|\n| 表格 | 支持 |\n| 任务列表 | 只读展示 |\n\n"
    "- [x] 渲染引擎接入\n- [ ] 文件入口（后续切片）\n\n"
    "```cpp\nint answer = 42;\n```\n\n"
    "安全链接：<https://example.com/ok> 与原始 HTML "
    "<b>按纯文本转义</b>。\n";

class MdvMemoryResourceHandler final : public CefResourceHandler {
 public:
  MdvMemoryResourceHandler(int status_code, std::string status_text,
                           std::string mime_type, std::string body)
      : status_code_(status_code),
        status_text_(std::move(status_text)),
        mime_type_(std::move(mime_type)),
        body_(std::move(body)) {}

  bool Open(CefRefPtr<CefRequest> request, bool& handle_request,
            CefRefPtr<CefCallback> callback) override {
    static_cast<void>(request);
    static_cast<void>(callback);
    handle_request = true;
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t& response_length,
                          CefString& redirect_url) override {
    CEF_REQUIRE_IO_THREAD();
    static_cast<void>(redirect_url);
    response->SetStatus(status_code_);
    response->SetStatusText(status_text_);
    response->SetMimeType(mime_type_);
    response->SetCharset(kUtf8Charset);
    CefResponse::HeaderMap headers;
    headers.emplace("Cache-Control", "no-store");
    // Contract CSP (MDV-01 §2): byte-for-byte the shared constant.
    headers.emplace("Content-Security-Policy", kMdvCsp);
    headers.emplace("Cross-Origin-Resource-Policy", "same-origin");
    headers.emplace("Referrer-Policy", "no-referrer");
    headers.emplace("X-Content-Type-Options", "nosniff");
    headers.emplace("X-Frame-Options", "DENY");
    response->SetHeaderMap(headers);
    response_length = static_cast<int64_t>(body_.size());
  }

  bool Read(void* data_out, int bytes_to_read, int& bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override {
    static_cast<void>(callback);
    bytes_read = 0;
    if (cancelled_ || data_out == nullptr || bytes_to_read <= 0 ||
        offset_ >= body_.size()) {
      return false;
    }
    const std::size_t available = body_.size() - offset_;
    const std::size_t requested = static_cast<std::size_t>(bytes_to_read);
    const std::size_t length = std::min(available, requested);
    std::memcpy(data_out, body_.data() + offset_, length);
    offset_ += length;
    bytes_read = static_cast<int>(length);
    return true;
  }

  void Cancel() override { cancelled_ = true; }

 private:
  const int status_code_;
  const std::string status_text_;
  const std::string mime_type_;
  const std::string body_;
  std::size_t offset_ = 0;
  bool cancelled_ = false;

  IMPLEMENT_REFCOUNTING(MdvMemoryResourceHandler);
  DISALLOW_COPY_AND_ASSIGN(MdvMemoryResourceHandler);
};

class MdvSchemeHandlerFactory final : public CefSchemeHandlerFactory {
 public:
  MdvSchemeHandlerFactory(std::string document, std::string stylesheet,
                          std::string script)
      : document_(std::move(document)),
        stylesheet_(std::move(stylesheet)),
        script_(std::move(script)) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    CEF_REQUIRE_IO_THREAD();
    static_cast<void>(browser);
    static_cast<void>(frame);
    if (!request || scheme_name.ToString() != kMdvScheme) {
      return nullptr;
    }
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts)) {
      return nullptr;
    }
    const bool has_credentials = !CefString(&parts.username).empty() ||
                                 !CefString(&parts.password).empty();
    const MdvRequestParts request_parts{
        request->GetMethod().ToString(),
        CefString(&parts.scheme).ToString(),
        CefString(&parts.host).ToString(),
        CefString(&parts.path).ToString(),
        has_credentials,
        !CefString(&parts.port).empty(),
        request->GetURL().ToString().find('?') != std::string::npos,
        request->GetURL().ToString().find('#') != std::string::npos,
    };
    const auto route = ClassifyMdvRequest(request_parts);

    std::string body;
    std::string mime_type = kTextMimeType;
    switch (route.kind) {
      case crayon::browser_mdv::MdvResourceKind::kDocument:
        mime_type = kHtmlMimeType;
        if (route.include_body) {
          body = document_;
        }
        break;
      case crayon::browser_mdv::MdvResourceKind::kStylesheet:
        mime_type = kCssMimeType;
        if (route.include_body) {
          body = stylesheet_;
        }
        break;
      case crayon::browser_mdv::MdvResourceKind::kScript:
        mime_type = kJsMimeType;
        if (route.include_body) {
          body = script_;
        }
        break;
      case crayon::browser_mdv::MdvResourceKind::kMethodNotAllowed:
        break;
      case crayon::browser_mdv::MdvResourceKind::kNotFound:
        break;
    }
    return new MdvMemoryResourceHandler(route.status_code,
                                        StatusText(route.status_code),
                                        mime_type, std::move(body));
  }

 private:
  static std::string StatusText(int status_code) {
    switch (status_code) {
      case 200:
        return "OK";
      case 404:
        return "Not Found";
      case 405:
        return "Method Not Allowed";
      default:
        return "Error";
    }
  }

  const std::string document_;
  const std::string stylesheet_;
  const std::string script_;

  IMPLEMENT_REFCOUNTING(MdvSchemeHandlerFactory);
  DISALLOW_COPY_AND_ASSIGN(MdvSchemeHandlerFactory);
};

/// Builds the read-only page snapshot from the fixture through the real
/// MDV-03 load/render gating path.
MdvPageSnapshot BuildFixtureSnapshot() {
  MdvPageSnapshot snapshot;
  snapshot.view_mode = crayon::browser_mdv::MdvViewMode::kPreview;
  snapshot.has_document = true;
  snapshot.source_text = kFixtureMarkdown;

  crayon::browser_mdv::MdvViewerModel model;
  model.LoadContent(kFixtureMarkdown,
                    /*utf8_valid=*/true, /*now_ms=*/0);
  const auto revision = model.RequestRender(/*now_ms=*/1000);
  crayon::browser_markdown::RenderStatus status =
      crayon::browser_markdown::RenderStatus::kOk;
  const std::string html = crayon::browser_markdown::RenderMarkdownToSafeHtml(
      kFixtureMarkdown, &status);
  if (status == crayon::browser_markdown::RenderStatus::kOk &&
      model.DeliverRender(revision, html)) {
    snapshot.load_status = model.load_status();
    snapshot.rendered_html = model.rendered_html();
  } else {
    snapshot.load_status = MdvLoadStatus::kRenderPolicyViolation;
    snapshot.rendered_html.clear();
  }
  return snapshot;
}

}  // namespace

bool RegisterMdvSchemeHandlerFactory(MdvPageStrings strings) {
  const auto snapshot = BuildFixtureSnapshot();
  return CefRegisterSchemeHandlerFactory(
      kMdvScheme, kMdvHost,
      new MdvSchemeHandlerFactory(RenderMdvDocument(snapshot, strings),
                                  crayon::browser_mdv::RenderMdvStylesheet(),
                                  crayon::browser_mdv::RenderMdvScript()));
}

}  // namespace crayon::browser::cef_shell::mdv
