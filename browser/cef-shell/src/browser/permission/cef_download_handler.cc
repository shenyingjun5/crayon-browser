#include "browser/permission/cef_download_handler.h"

#include "browser/permission/site_origin.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::permission {

CefDownloadHandlerAdapter::CefDownloadHandlerAdapter(PermissionStore* store)
    : store_(store) {
  CEF_REQUIRE_UI_THREAD();
}

void CefDownloadHandlerAdapter::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(suggested_name);

  if (!browser || !download_item) {
    return;
  }

  CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
  if (!main_frame) {
    return;
  }

  const std::optional<std::string> origin =
      ExtractSiteOrigin(main_frame->GetURL().ToString());
  if (!origin.has_value() ||
      store_->Query(*origin, PermissionKind::kDownload) ==
          PermissionDecision::kDeny) {
    // Cancel the download by not invoking the callback.
    return;
  }

  callback->Continue(CefString(), true);
}

void CefDownloadHandlerAdapter::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  static_cast<void>(download_item);
  static_cast<void>(callback);
  // No action needed; the download was already gated in OnBeforeDownload.
}

}  // namespace crayon::browser::cef_shell::permission
