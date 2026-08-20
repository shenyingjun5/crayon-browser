#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_PERMISSION_HANDLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_PERMISSION_HANDLER_H_

#include "browser/permission/permission_store.h"

#include "include/cef_client.h"
#include "include/cef_permission_handler.h"

namespace crayon::browser::cef_shell::permission {

// Adapts CEF permission callbacks to the PermissionStore.
//
// All callbacks run on the CEF UI thread.  The default for every request is
// deny; an explicit allow in the store is required to permit the operation.
//
// Supported CEF permission kinds:
// - Media (camera / microphone)  -> OnRequestMediaAccessPermission
// - Notifications                -> OnShowNotification
// - Geolocation                  -> OnRequestGeolocationPermission (if avail)
// - Clipboard                    -> OnRequestClipboardPermission (if avail)
//
// Download permission is handled separately by CefDownloadHandlerAdapter.
class CefPermissionHandlerAdapter final : public CefPermissionHandler {
 public:
  explicit CefPermissionHandlerAdapter(PermissionStore* store);

  // CefPermissionHandler overrides.
  bool OnRequestMediaAccessPermission(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString& requesting_origin,
      uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback) override;

  bool OnShowNotification(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString& origin_url,
      CefRefPtr<CefNotification> notification,
      CefRefPtr<CefNotificationCallback> callback) override;

  bool OnRequestGeolocationPermission(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString& requesting_url,
      CefRefPtr<CefGeolocationCallback> callback) override;

  bool OnRequestClipboardPermission(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString& requesting_url,
      CefRefPtr<CefClipboardCallback> callback) override;

 private:
  // Maps a CEF URL to a PermissionKind decision via the store.
  // Returns true when the store contains an allow decision.
  bool IsAllowed(const CefString& url, PermissionKind kind);

  PermissionStore* store_;

  IMPLEMENT_REFCOUNTING(CefPermissionHandlerAdapter);
  DISALLOW_COPY_AND_ASSIGN(CefPermissionHandlerAdapter);
};

}  // namespace crayon::browser::cef_shell::permission

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_PERMISSION_HANDLER_H_
