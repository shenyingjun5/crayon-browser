#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_EDITING_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_EDITING_H_

#include <memory>
#include <string>

#include "browser/mdv/cef_mdv_handler.h"
#include "crayon/browser_mdv/mdv_edit.h"
#include "crayon/browser_mdv/mdv_save.h"
#include "crayon/browser_mdv/mdv_viewer.h"
#include "include/cef_browser.h"
#include "include/wrapper/cef_message_router.h"

namespace crayon::browser::cef_shell::mdv {

using crayon::browser_mdv::MdvPageStrings;

/// Owns the editing and save pipeline for the viewer page (MDV-10):
/// page edit bursts flow through the MDV-05 model over the controlled
/// "mdvQuery" binding; Ctrl+S runs the MDV-06 atomic write-back with
/// real filesystem hooks; dirty navigations are blocked into the
/// in-page three-choice confirm; conflicts surface explicitly.
/// Single-threaded: CEF UI thread only.
class MdvEditController
    : public std::enable_shared_from_this<MdvEditController> {
 public:
  MdvEditController(std::shared_ptr<MdvRuntimeState> state,
                    MdvPageStrings strings);

  /// Called by the entry controller after a successful gated load.
  void OnDocumentLoaded(CefRefPtr<CefBrowser> browser,
                        const std::string& path_utf8,
                        const std::string& normalized_text, std::uint64_t size,
                        std::uint64_t mtime);

  /// Message-router query entry; rejects non-mdv-origin frames.
  bool OnPageQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int64_t query_id, const CefString& request, bool persistent,
                   CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  /// Dirty navigation intercept: cancels a user-gestured navigation
  /// while the buffer is dirty and opens the in-page confirm; the
  /// resolved decision re-issues the pending navigation.
  bool InterceptWhileDirty(CefRefPtr<CefBrowser> browser,
                           const std::string& url, bool user_gesture);

  /// Ctrl+S: runs the write-back save from the edit buffer.
  bool HandleSaveCommand(CefRefPtr<CefBrowser> browser, int command_id);

 private:
  class SaveDialogCallback;
  friend class SaveDialogCallback;

  void PerformSave(CefRefPtr<CefBrowser> browser,
                   crayon::browser_mdv_save::SaveKind kind,
                   const std::string& target_path);
  void StartSaveAsDialog(CefRefPtr<CefBrowser> browser);
  void ApplyDecision(CefRefPtr<CefBrowser> browser, const std::string& value);
  void RenderAndStore();
  void PushState(CefRefPtr<CefBrowser> browser);
  void ReleasePendingNavigation(CefRefPtr<CefBrowser> browser);

  std::shared_ptr<MdvRuntimeState> state_;
  MdvPageStrings strings_;
  crayon::browser_mdv::MdvViewerModel viewer_;
  crayon::browser_mdv_edit::MdvEditModel edit_;
  crayon::browser_mdv_save::MdvSaveController save_;
  std::string current_path_;
  std::string pending_url_;
  bool conflict_pending_ = false;
};

}  // namespace crayon::browser::cef_shell::mdv

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_EDITING_H_
