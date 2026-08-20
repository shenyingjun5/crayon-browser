#include "browser/permission/cef_permission_handler.h"

#include "browser/permission/site_origin.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::permission {

namespace {

// CEF media-access permission flags (from cef_media_access_query_types.h).
constexpr uint32_t kMediaAccessVideo = 1 << 1;
constexpr uint32_t kMediaAccessAudio = 1 << 2;

}  // namespace

CefPermissionHandlerAdapter::CefPermissionHandlerAdapter(
    PermissionStore* store)
    : store_(store) {
  CEF_REQUIRE_UI_THREAD();
}

bool CefPermissionHandlerAdapter::OnRequestMediaAccessPermission(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  static_cast<void>(frame);

  const std::optional<std::string> origin =
      ExtractSiteOrigin(requesting_origin.ToString());
  if (!origin.has_value()) {
    callback->Cancel();
    return true;
  }

  bool allow_video = false;
  bool allow_audio = false;

  if ((requested_permissions & kMediaAccessVideo) != 0) {
    allow_video = store_->Query(*origin, PermissionKind::kCamera) !=
                  PermissionDecision::kDeny;
  }
  if ((requested_permissions & kMediaAccessAudio) != 0) {
    allow_audio = store_->Query(*origin, PermissionKind::kMicrophone) !=
                  PermissionDecision::kDeny;
  }

  if (allow_video || allow_audio) {
    callback->Continue(allow_video ? kMediaAccessVideo : 0 |
                       allow_audio ? kMediaAccessAudio : 0);
  } else {
    callback->Cancel();
  }
  return true;
}

bool CefPermissionHandlerAdapter::OnShowNotification(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& origin_url,
    CefRefPtr<CefNotification> notification,
    CefRefPtr<CefNotificationCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  static_cast<void>(frame);
  static_cast<void>(notification);

  const std::optional<std::string> origin =
      ExtractSiteOrigin(origin_url.ToString());
  if (!origin.has_value() ||
      store_->Query(*origin, PermissionKind::kNotifications) ==
          PermissionDecision::kDeny) {
    callback->Cancel();
    return true;
  }
  callback->Continue();
  return true;
}

bool CefPermissionHandlerAdapter::OnRequestGeolocationPermission(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& requesting_url,
    CefRefPtr<CefGeolocationCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  static_cast<void>(frame);

  const std::optional<std::string> origin =
      ExtractSiteOrigin(requesting_url.ToString());
  if (!origin.has_value() ||
      store_->Query(*origin, PermissionKind::kGeolocation) ==
          PermissionDecision::kDeny) {
    callback->Cancel();
    return true;
  }
  callback->Continue();
  return true;
}

bool CefPermissionHandlerAdapter::OnRequestClipboardPermission(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& requesting_url,
    CefRefPtr<CefClipboardCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  static_cast<void>(frame);

  const std::optional<std::string> origin =
      ExtractSiteOrigin(requesting_url.ToString());
  if (!origin.has_value()) {
    callback->Cancel();
    return true;
  }

  // Distinguish read vs write by checking both; deny if either is denied.
  const PermissionDecision read_decision =
      store_->Query(*origin, PermissionKind::kClipboardRead);
  const PermissionDecision write_decision =
      store_->Query(*origin, PermissionKind::kClipboardWrite);
  if (read_decision == PermissionDecision::kDeny &&
      write_decision == PermissionDecision::kDeny) {
    callback->Cancel();
    return true;
  }
  callback->Continue();
  return true;
}

bool CefPermissionHandlerAdapter::IsAllowed(const CefString& url,
                                            PermissionKind kind) {
  const std::optional<std::string> origin = ExtractSiteOrigin(url.ToString());
  if (!origin.has_value()) {
    return false;
  }
  return store_->Query(*origin, kind) != PermissionDecision::kDeny;
}

}  // namespace crayon::browser::cef_shell::permission
