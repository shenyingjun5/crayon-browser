#include "browser/new_tab/cef_new_tab_handler.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"

namespace crayon::browser::cef_shell::new_tab {
namespace {

constexpr char kHtmlMimeType[] = "text/html";
constexpr char kCssMimeType[] = "text/css";
constexpr char kTextMimeType[] = "text/plain";
constexpr char kUtf8Charset[] = "utf-8";
constexpr char kContentSecurityPolicy[] =
    "default-src 'none'; style-src 'self'; base-uri 'none'; form-action "
    "'none'; frame-ancestors 'none'; object-src 'none'; img-src 'none'; "
    "connect-src 'none'; font-src 'none'; script-src 'none'";

class MemoryResourceHandler final : public CefResourceHandler {
 public:
  MemoryResourceHandler(int status_code, std::string status_text,
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
    headers.emplace("Content-Security-Policy", kContentSecurityPolicy);
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

  IMPLEMENT_REFCOUNTING(MemoryResourceHandler);
  DISALLOW_COPY_AND_ASSIGN(MemoryResourceHandler);
};

class NewTabSchemeHandlerFactory final : public CefSchemeHandlerFactory {
 public:
  NewTabSchemeHandlerFactory(browser_new_tab::NewTabPageModel page_model,
                             browser_new_tab::NewTabPageStrings strings)
      : document_(browser_new_tab::RenderNewTabDocument(page_model, strings)),
        stylesheet_(browser_new_tab::RenderNewTabStylesheet()) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    CEF_REQUIRE_IO_THREAD();
    static_cast<void>(browser);
    static_cast<void>(frame);
    if (!request || scheme_name.ToString() != browser_new_tab::kNewTabScheme) {
      return nullptr;
    }

    const std::string request_url = request->GetURL().ToString();
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts)) {
      return nullptr;
    }
    const bool has_credentials = !CefString(&parts.username).empty() ||
                                 !CefString(&parts.password).empty();
    const browser_new_tab::NewTabRequestParts request_parts{
        request->GetMethod().ToString(),
        CefString(&parts.scheme).ToString(),
        CefString(&parts.host).ToString(),
        CefString(&parts.path).ToString(),
        has_credentials,
        !CefString(&parts.port).empty(),
        request_url.find('?') != std::string::npos,
        request_url.find('#') != std::string::npos,
    };
    const browser_new_tab::NewTabRoute route =
        browser_new_tab::ClassifyNewTabRequest(request_parts);
    if (route.kind == browser_new_tab::NewTabResourceKind::kRejected) {
      return nullptr;
    }

    std::string body;
    std::string mime_type = kTextMimeType;
    if (route.kind == browser_new_tab::NewTabResourceKind::kDocument) {
      mime_type = kHtmlMimeType;
      if (route.include_body) {
        body = document_;
      }
    } else if (route.kind == browser_new_tab::NewTabResourceKind::kStylesheet) {
      mime_type = kCssMimeType;
      if (route.include_body) {
        body = stylesheet_;
      }
    }
    return new MemoryResourceHandler(route.status_code,
                                     StatusText(route.status_code), mime_type,
                                     std::move(body));
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

  IMPLEMENT_REFCOUNTING(NewTabSchemeHandlerFactory);
  DISALLOW_COPY_AND_ASSIGN(NewTabSchemeHandlerFactory);
};

/// Renderer-side message router shared by all crayon:// pages (MDV-10
/// uses the "mdvQuery" binding; the browser side gates it to the mdv
/// origin).  Lost once in a patch overwrite and restored with a
/// contract assertion (mdv_handler_contract.cmake).
class NewTabProcessApp final : public CefApp, public CefRenderProcessHandler {
 public:
  NewTabProcessApp() {
    CefMessageRouterConfig config;
    config.js_query_function = "mdvQuery";
    router_ = CefMessageRouterRendererSide::Create(config);
  }

  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override {
    RegisterCrayonCustomSchemes(registrar);
  }

  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    router_->OnContextCreated(browser, frame, context);
  }

  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override {
    router_->OnContextReleased(browser, frame, context);
  }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override {
    return router_->OnProcessMessageReceived(browser, frame, source_process,
                                             message);
  }

 private:
  CefRefPtr<CefMessageRouterRendererSide> router_;

  IMPLEMENT_REFCOUNTING(NewTabProcessApp);
  DISALLOW_COPY_AND_ASSIGN(NewTabProcessApp);
};

}  // namespace

void RegisterCrayonCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  if (!registrar) {
    return;
  }
  constexpr int kSchemeOptions = CEF_SCHEME_OPTION_STANDARD |
                                 CEF_SCHEME_OPTION_SECURE |
                                 CEF_SCHEME_OPTION_DISPLAY_ISOLATED |
                                 CEF_SCHEME_OPTION_CORS_ENABLED;
  registrar->AddCustomScheme(browser_new_tab::kNewTabScheme, kSchemeOptions);
}

CefRefPtr<CefApp> CreateNewTabProcessApp() { return new NewTabProcessApp(); }

bool RegisterNewTabSchemeHandlerFactory(
    browser_new_tab::NewTabPageModel page_model,
    browser_new_tab::NewTabPageStrings strings) {
  return CefRegisterSchemeHandlerFactory(
      browser_new_tab::kNewTabScheme, browser_new_tab::kNewTabHost,
      new NewTabSchemeHandlerFactory(std::move(page_model),
                                     std::move(strings)));
}

}  // namespace crayon::browser::cef_shell::new_tab
