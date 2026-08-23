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
  // Passive adapter: no CEF state is touched here, and construction runs
  // before CefInitialize on the main thread; thread checks live in the
  // callback methods.
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
    callback->Continue((allow_video ? kMediaAccessVideo : 0u) |
                       (allow_audio ? kMediaAccessAudio : 0u));
  } else {
    callback->Cancel();
  }
  return true;
}

bool CefPermissionHandlerAdapter::OnShowPermissionPrompt(
    CefRefPtr<CefBrowser> browser,
    uint64_t prompt_id,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefPermissionPromptCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  static_cast<void>(prompt_id);

  // CEF permission type bits (cef_permission_request_types_t).
  constexpr uint32_t kPermissionClipboard = 1 << 4;
  constexpr uint32_t kPermissionGeolocation = 1 << 8;
  constexpr uint32_t kPermissionNotifications = 1 << 15;

  const std::optional<std::string> origin =
      ExtractSiteOrigin(requesting_origin.ToString());
  if (!origin.has_value()) {
    callback->Continue(CEF_PERMISSION_RESULT_DENY);
    return true;
  }

  // Default deny: every requested bit must map onto a store kind with an
  // explicit allow; unmapped types always deny.
  bool allowed = false;
  const auto allow_kind = [&](PermissionKind kind) {
    if (store_->Query(*origin, kind) != PermissionDecision::kDeny) {
      allowed = true;
    }
  };
  if ((requested_permissions & kPermissionNotifications) != 0) {
    allow_kind(PermissionKind::kNotifications);
  }
  if ((requested_permissions & kPermissionGeolocation) != 0) {
    allow_kind(PermissionKind::kGeolocation);
  }
  if ((requested_permissions & kPermissionClipboard) != 0) {
    allow_kind(PermissionKind::kClipboardRead);
    allow_kind(PermissionKind::kClipboardWrite);
  }
  // Any requested bit without a mapping keeps `allowed` untouched
  // (deny); grant only when at least one mapped kind is allowed AND no
  // unmapped bit is requested.
  const uint32_t kMappedMask =
      kPermissionNotifications | kPermissionGeolocation | kPermissionClipboard;
  if ((requested_permissions & ~kMappedMask) != 0) {
    allowed = false;
  }
  callback->Continue(allowed ? CEF_PERMISSION_RESULT_ACCEPT
                             : CEF_PERMISSION_RESULT_DENY);
  return true;
}

void CefPermissionHandlerAdapter::OnDismissPermissionPrompt(
    CefRefPtr<CefBrowser> browser,
    uint64_t prompt_id,
    cef_permission_request_result_t result) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  static_cast<void>(prompt_id);
  static_cast<void>(result);
  // No queued prompt state to clean up: decisions are immediate.
}

}  // namespace crayon::browser::cef_shell::permission
