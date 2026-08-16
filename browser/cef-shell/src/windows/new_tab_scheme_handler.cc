#include "windows/new_tab_scheme_handler.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/cef_scheme.h"

namespace crayon::browser::cef_shell {
namespace {

constexpr char kCrayonScheme[] = "crayon";
constexpr char kNewTabHost[] = "newtab";

class NewTabResourceHandler final : public CefResourceHandler {
 public:
  explicit NewTabResourceHandler(
      crayon::browser::new_tab::NewTabResource resource)
      : resource_(std::move(resource)) {}

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
    static_cast<void>(redirect_url);
    response->SetStatus(200);
    response->SetMimeType(resource_.mime_type);
    response->SetCharset(resource_.charset);
    response->SetHeaderByName("Cache-Control", resource_.cache_control, true);
    response->SetHeaderByName("Content-Security-Policy",
                              resource_.content_security_policy, true);
    response->SetHeaderByName("X-Content-Type-Options", "nosniff", true);
    response_length = static_cast<int64_t>(resource_.body.size());
  }

  bool Read(void* data_out, int bytes_to_read, int& bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override {
    static_cast<void>(callback);
    bytes_read = 0;
    if (!data_out || bytes_to_read <= 0 || offset_ >= resource_.body.size()) {
      return false;
    }
    const std::size_t remaining = resource_.body.size() - offset_;
    const std::size_t count =
        std::min(remaining, static_cast<std::size_t>(bytes_to_read));
    std::memcpy(data_out, resource_.body.data() + offset_, count);
    offset_ += count;
    bytes_read = static_cast<int>(count);
    return true;
  }

  void Cancel() override { offset_ = resource_.body.size(); }

 private:
  const crayon::browser::new_tab::NewTabResource resource_;
  std::size_t offset_ = 0;

  IMPLEMENT_REFCOUNTING(NewTabResourceHandler);
  DISALLOW_COPY_AND_ASSIGN(NewTabResourceHandler);
};

class NewTabSchemeHandlerFactory final : public CefSchemeHandlerFactory {
 public:
  explicit NewTabSchemeHandlerFactory(
      crayon::browser::new_tab::NewTabStrings strings)
      : strings_(std::move(strings)),
        model_(crayon::browser::new_tab::BuildNewTabModel(
            crayon::browser::new_tab::ProfileMode::kStandard, {})) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    static_cast<void>(browser);
    static_cast<void>(frame);
    if (scheme_name != kCrayonScheme || !request) {
      return nullptr;
    }
    const auto request_kind = crayon::browser::new_tab::ValidateNewTabRequest(
        request->GetMethod().ToString(), request->GetURL().ToString());
    auto resource = crayon::browser::new_tab::BuildNewTabResource(
        request_kind, model_, strings_);
    if (!resource.has_value()) {
      return nullptr;
    }
    return new NewTabResourceHandler(std::move(*resource));
  }

 private:
  const crayon::browser::new_tab::NewTabStrings strings_;
  const crayon::browser::new_tab::NewTabModel model_;

  IMPLEMENT_REFCOUNTING(NewTabSchemeHandlerFactory);
  DISALLOW_COPY_AND_ASSIGN(NewTabSchemeHandlerFactory);
};

}  // namespace

void RegisterCrayonScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  if (!registrar) {
    return;
  }
  registrar->AddCustomScheme(kCrayonScheme, CEF_SCHEME_OPTION_STANDARD |
                                                CEF_SCHEME_OPTION_LOCAL |
                                                CEF_SCHEME_OPTION_SECURE);
}

bool RegisterNewTabSchemeHandler(
    crayon::browser::new_tab::NewTabStrings strings) {
  return CefRegisterSchemeHandlerFactory(
      kCrayonScheme, kNewTabHost,
      new NewTabSchemeHandlerFactory(std::move(strings)));
}

}  // namespace crayon::browser::cef_shell
