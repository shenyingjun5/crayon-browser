#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_DOWNLOAD_HANDLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_DOWNLOAD_HANDLER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "browser/permission/download_observer.h"
#include "browser/permission/permission_store.h"

#include "include/cef_download_handler.h"

namespace crayon::browser::cef_shell::permission {

// Adapts CEF download events to the PermissionStore.
//
// Downloads are denied by default.  An explicit kAllowSession or
// kAllowPersistent decision for PermissionKind::kDownload is required.
class CefDownloadHandlerAdapter final : public CefDownloadHandler {
public:
  explicit CefDownloadHandlerAdapter(PermissionStore *store,
                                     CefDownloadObserver *observer = nullptr);

  bool ConfirmPending(std::uint64_t download_id,
                      const std::string &target_path);
  bool DiscardPending(std::uint64_t download_id);
  bool Pause(std::uint64_t download_id);
  bool Resume(std::uint64_t download_id);
  bool Cancel(std::uint64_t download_id);
  void Shutdown();

  // CefDownloadHandler overrides.
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString &suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;

  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

private:
  PermissionStore *store_;
  CefDownloadObserver *observer_;
  std::map<std::uint64_t, CefRefPtr<CefBeforeDownloadCallback>> pending_;
  std::map<std::uint64_t, CefRefPtr<CefDownloadItemCallback>> controls_;
  std::map<std::uint64_t, std::uint64_t> generations_;

  IMPLEMENT_REFCOUNTING(CefDownloadHandlerAdapter);
  DISALLOW_COPY_AND_ASSIGN(CefDownloadHandlerAdapter);
};

} // namespace crayon::browser::cef_shell::permission

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_CEF_DOWNLOAD_HANDLER_H_
