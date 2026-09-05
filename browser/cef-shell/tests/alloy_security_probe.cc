#include "alloy_security_probe.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include "browser/permission/permission_store.h"
#include "browser/permission/site_origin.h"
#include "browser/window/alloy_site_controls.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_task.h"
#include "include/test/cef_test_server.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::permission::ExtractSiteOrigin;
using crayon::browser::cef_shell::permission::PermissionStore;
using crayon::browser::cef_shell::window::AlloyPermissionDecision;
using crayon::browser::cef_shell::window::AlloySiteControlResult;
using crayon::browser::cef_shell::window::AlloySiteControls;
using crayon::browser_site_controls::CertDecision;
using crayon::browser_site_controls::CertErrorKind;
using crayon::browser_site_controls::PermissionKind;
using crayon::browser_site_controls::ProtocolDecision;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 600;
constexpr std::uint32_t kNotificationsPermission = 1u << 15;
constexpr char kExternalUrl[] = "mailto:alloy-security@example.test";

std::string Origin(const std::string &url) {
  const auto origin = ExtractSiteOrigin(url);
  return origin ? *origin : std::string();
}

class AlloySecurityProbe final : public CefApp,
                                 public CefBrowserProcessHandler,
                                 public CefClient,
                                 public CefRequestHandler,
                                 public CefResourceRequestHandler,
                                 public CefPermissionHandler,
                                 public CefLoadHandler,
                                 public CefDisplayHandler,
                                 public CefBrowserViewDelegate,
                                 public CefWindowDelegate,
                                 public CefTestServerHandler {
public:
  AlloySecurityProbe(std::string fixture_url,
                     std::shared_ptr<AlloySecurityProbeResult> result)
      : fixture_url_(std::move(fixture_url)),
        fixture_origin_(Origin(fixture_url_)), result_(std::move(result)),
        controls_(&permission_store_) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {
    return this;
  }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

  void
  OnBeforeCommandLineProcessing(const CefString &,
                                CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-default-apps");
    command->AppendSwitch("disable-sync");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    if (fixture_origin_.empty()) {
      Finish(false, "fixture-origin");
      return;
    }
    server_ =
        CefTestServer::CreateAndStart(0, true, CEF_TEST_CERT_EXPIRED, this);
    if (!server_) {
      Finish(false, "https-server");
      return;
    }
    tls_origin_ = server_->GetOrigin().ToString();
    if (!controls_.OnNavigation(1, tls_origin_)) {
      Finish(false, "tls-origin");
      return;
    }
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(this, tls_origin_ + "/security",
                                              settings, nullptr, nullptr, this);
    if (!view_) {
      Finish(false, "create-view");
      return;
    }
    CefWindow::CreateTopLevelWindow(this);
  }

  bool
  OnTestServerRequest(CefRefPtr<CefTestServer>, CefRefPtr<CefRequest>,
                      CefRefPtr<CefTestServerConnection> connection) override {
    constexpr char kBody[] =
        "<!doctype html><meta charset=utf-8><title>cert-loaded</title>";
    connection->SendHttp200Response("text/html", kBody, sizeof(kBody) - 1);
    return true;
  }

  bool OnCertificateError(CefRefPtr<CefBrowser>, cef_errorcode_t,
                          const CefString &request_url, CefRefPtr<CefSSLInfo>,
                          CefRefPtr<CefCallback> callback) override {
    if (request_url.ToString().rfind(tls_origin_, 0) != 0 ||
        certificate_attempts_ >= 2) {
      return false;
    }
    const std::uint64_t generation = certificate_attempts_ == 0 ? 1 : 2;
    ++certificate_attempts_;
    const auto request_id = controls_.BeginCertificateError(
        generation, CertErrorKind::kExpired, [callback](bool allowed) {
          if (allowed)
            callback->Continue();
          else
            callback->Cancel();
        });
    if (!request_id)
      return false;
    if (certificate_attempts_ == 1) {
      result_->certificate_deny_passed =
          controls_.ResolveCertificate(*request_id, CertDecision::kGoBack) ==
          AlloySiteControlResult::kSuccess;
    } else {
      result_->certificate_once_passed =
          controls_.ResolveCertificate(*request_id,
                                       CertDecision::kProceedOnce) ==
          AlloySiteControlResult::kSuccess;
    }
    return true;
  }

  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode, const CefString &,
                   const CefString &failed_url) override {
    if (!frame->IsMain() || certificate_attempts_ != 1 ||
        failed_url.ToString().rfind(tls_origin_, 0) != 0) {
      return;
    }
    if (!controls_.OnNavigation(2, tls_origin_)) {
      Finish(false, "certificate-retry-generation");
      return;
    }
    browser->GetMainFrame()->LoadURL(tls_origin_ + "/security");
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (!frame->IsMain() || http_status_code != 200 ||
        frame->GetURL().ToString().rfind(tls_origin_, 0) != 0 ||
        !result_->certificate_once_passed || loaded_fixture_) {
      return;
    }
    loaded_fixture_ = true;
    if (server_) {
      server_->Stop();
      server_ = nullptr;
    }
    if (!controls_.OnNavigation(3, fixture_origin_)) {
      Finish(false, "fixture-generation");
      return;
    }
    browser->GetMainFrame()->LoadURL(fixture_url_);
  }

  bool OnShowPermissionPrompt(
      CefRefPtr<CefBrowser>, std::uint64_t, const CefString &requesting_origin,
      std::uint32_t requested_permissions,
      CefRefPtr<CefPermissionPromptCallback> callback) override {
    const std::string origin = Origin(requesting_origin.ToString());
    if (origin != fixture_origin_ ||
        (requested_permissions & kNotificationsPermission) == 0 ||
        (requested_permissions & ~kNotificationsPermission) != 0) {
      callback->Continue(CEF_PERMISSION_RESULT_DENY);
      return true;
    }
    const auto request_id = controls_.BeginPermission(
        3, origin, PermissionKind::kNotifications, 10, 20,
        [callback](bool allowed) {
          callback->Continue(allowed ? CEF_PERMISSION_RESULT_ACCEPT
                                     : CEF_PERMISSION_RESULT_DENY);
        });
    if (!request_id || *request_id == 0) {
      return true;
    }
    result_->permission_prompt_passed =
        controls_.ResolvePermission(*request_id,
                                    AlloyPermissionDecision::kAllowSession,
                                    11) == AlloySiteControlResult::kSuccess;
    return true;
  }

  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool,
      bool, const CefString &, bool &disable_default_handling) override {
    disable_default_handling = false;
    return this;
  }

  void OnProtocolExecution(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                           CefRefPtr<CefRequest> request,
                           bool &allow_os_execution) override {
    allow_os_execution = false;
    protocol_blocked_on_io_.store(true, std::memory_order_release);
    const std::string url = request->GetURL().ToString();
    CefPostTask(TID_UI,
                base::BindOnce(&AlloySecurityProbe::ConfirmExternalProtocol,
                               CefRefPtr<AlloySecurityProbe>(this), url));
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString &title) override {
    if (title.ToString() == "permission:granted" && !protocol_requested_) {
      protocol_requested_ = true;
      browser->GetMainFrame()->LoadURL(kExternalUrl);
    }
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView>,
                        CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    result_->browser_closed = true;
    if (window_)
      window_->Close();
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window->SetToBoxLayout(settings);
    window->AddChildView(view_);
    layout->SetFlexForView(view_, 1);
    window->SetSize(CefSize(640, 420));
    window->Layout();
    window->Show();
    ScheduleCheck();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return finished_; }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    controls_.Shutdown();
    view_ = nullptr;
    window_ = nullptr;
    CefQuitMessageLoop();
  }

private:
  void ConfirmExternalProtocol(const std::string &url) {
    result_->external_protocol_blocked =
        protocol_blocked_on_io_.load(std::memory_order_acquire);
    const auto request_id = controls_.BeginExternalProtocol(
        3, fixture_origin_, "mailto", url,
        [this](bool allowed) { result_->external_protocol_denied = !allowed; });
    if (!request_id || *request_id == 0 ||
        controls_.ResolveExternalProtocol(*request_id,
                                          ProtocolDecision::kDeny) !=
            AlloySiteControlResult::kSuccess) {
      Finish(false, "external-protocol-confirmation");
      return;
    }
    ScheduleCheck();
  }

  void ScheduleCheck() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&AlloySecurityProbe::Check,
                                      CefRefPtr<AlloySecurityProbe>(this)),
                       kPollMilliseconds);
  }

  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (result_->certificate_deny_passed && result_->certificate_once_passed &&
        result_->permission_prompt_passed &&
        result_->external_protocol_blocked &&
        result_->external_protocol_denied) {
      Finish(true, "complete");
      return;
    }
    ScheduleCheck();
  }

  void Finish(bool passed, const char *detail) {
    if (finished_)
      return;
    finished_ = true;
    std::cout << "alloy_security_windows passed=" << passed
              << " detail=" << detail << std::endl;
    if (server_) {
      server_->Stop();
      server_ = nullptr;
    }
    if (browser_)
      browser_->GetHost()->CloseBrowser(true);
    else if (window_)
      window_->Close();
    else
      CefQuitMessageLoop();
  }

  const std::string fixture_url_;
  const std::string fixture_origin_;
  std::shared_ptr<AlloySecurityProbeResult> result_;
  PermissionStore permission_store_;
  AlloySiteControls controls_;
  CefRefPtr<CefTestServer> server_;
  std::string tls_origin_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  int certificate_attempts_ = 0;
  int checks_ = 0;
  bool loaded_fixture_ = false;
  bool protocol_requested_ = false;
  bool finished_ = false;
  std::atomic<bool> protocol_blocked_on_io_{false};

  IMPLEMENT_REFCOUNTING(AlloySecurityProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateAlloySecurityProbe(std::string fixture_url,
                         std::shared_ptr<AlloySecurityProbeResult> result) {
  return new AlloySecurityProbe(std::move(fixture_url), std::move(result));
}
