#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_ENTRIES_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_ENTRIES_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "browser/mdv/cef_mdv_handler.h"
#include "crayon/browser_mdv/mdv_entry_guard.h"
#include "include/cef_browser.h"

namespace crayon::browser::cef_shell::mdv {

using crayon::browser_mdv::EntrySource;
using crayon::browser_mdv::MdvPageStrings;

/// Routes the three user-gesture entries through the MDV-04 load gate
/// into the live viewer snapshot:
/// - E1: the native "Open file" command runs a controlled dialog with a
///   `.md` filter (RunFileDialog).
/// - E2/E3: `file://` navigations from drops or omnibox submissions are
///   intercepted before browse (user gesture required).
/// Failures land on the viewer page as an escaped status banner; there
/// is never a half-loaded state.
class MdvEntryController
    : public std::enable_shared_from_this<MdvEntryController> {
 public:
  MdvEntryController(std::shared_ptr<MdvRuntimeState> state,
                     MdvPageStrings strings);

  /// Returns true when `command_id` is the native open-file command and
  /// the dialog was started (swallows the pass-through default).
  bool HandleChromeCommand(CefRefPtr<CefBrowser> browser, int command_id);

 private:
  class MdvFileDialogCallback;

 public:
  /// Navigation interceptor for `file://` `.md` targets; returns true
  /// (cancel) only for user-gestured local markdown navigations, which
  /// are loaded into the viewer instead.
  bool InterceptNavigation(CefRefPtr<CefBrowser> browser, const CefString& url,
                           bool user_gesture);

  /// Loads `path_utf8` through the gate and shows the viewer (success
  /// or failure banner).  Must be called on the CEF UI thread.
  void LoadAndShow(CefRefPtr<CefBrowser> browser, const std::string& path_utf8,
                   EntrySource source);

  /// MDV-10: invoked after a successful gated load so the editing
  /// controller can arm its models (path, normalized bytes, size,
  /// mtime).  The entry controller still owns navigation.
  using DocumentLoadedCallback =
      std::function<void(CefRefPtr<CefBrowser>, const std::string&,
                         const std::string&, std::uint64_t, std::uint64_t)>;
  void SetDocumentLoadedCallback(DocumentLoadedCallback callback);

 private:
  const std::shared_ptr<MdvRuntimeState> state_;
  const MdvPageStrings strings_;
  DocumentLoadedCallback document_loaded_callback_;
};

/// Converts a `file://` URL to a local path (percent-decoded, Windows
/// drive-slash normalized).  Returns false for non-file URLs.
bool LocalPathFromFileUrl(const std::string& url, std::string* path_utf8);

}  // namespace crayon::browser::cef_shell::mdv

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_ENTRIES_H_
