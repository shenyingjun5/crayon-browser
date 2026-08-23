#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_DOWNLOAD_HANDLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_DOWNLOAD_HANDLER_H_

#include "browser/permission/permission_store.h"

#include "include/cef_download_handler.h"

namespace crayon::browser::cef_shell::permission {

// Adapts CEF download events to the PermissionStore.
//
// Downloads are denied by default.  An explicit kAllowSession or
// kAllowPersistent decision for PermissionKind::kDownload is required.
class CefDownloadHandlerAdapter final : public CefDownloadHandler {
 public:
  explicit CefDownloadHandlerAdapter(PermissionStore* store);

  // CefDownloadHandler overrides.
  bool OnBeforeDownload(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefDownloadItem> download_item,
      const CefString& suggested_name,
      CefRefPtr<CefBeforeDownloadCallback> callback) override;

  void OnDownloadUpdated(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefDownloadItem> download_item,
      CefRefPtr<CefDownloadItemCallback> callback) override;

 private:
  PermissionStore* store_;

  IMPLEMENT_REFCOUNTING(CefDownloadHandlerAdapter);
  DISALLOW_COPY_AND_ASSIGN(CefDownloadHandlerAdapter);
};

}  // namespace crayon::browser::cef_shell::permission

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_DOWNLOAD_HANDLER_H_
