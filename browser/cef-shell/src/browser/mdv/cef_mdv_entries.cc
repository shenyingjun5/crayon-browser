#include "browser/mdv/cef_mdv_entries.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>

#include "crayon/browser_markdown/markdown_render.h"
#include "crayon/browser_mdv/mdv_viewer.h"
#include "include/cef_id_mappers.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::mdv {
namespace {

using crayon::browser_mdv::GateLocalLoad;
using crayon::browser_mdv::HasMarkdownSuffix;
using crayon::browser_mdv::kMaxLoadBytes;
using crayon::browser_mdv::kMdvHost;
using crayon::browser_mdv::kMdvScheme;
using crayon::browser_mdv::kResourceAppHtml;
using crayon::browser_mdv::LoadGateResult;
using crayon::browser_mdv::NormalizeLoadedContent;
using crayon::browser_mdv::StatProbe;

/// std::filesystem-backed stat probe for the injected guard callback.
int StatProbeFilesystem(const std::string& path_utf8) {
  std::error_code error;
  const auto status =
      std::filesystem::status(std::filesystem::u8path(path_utf8), error);
  if (error || !std::filesystem::status_known(status) ||
      status.type() == std::filesystem::file_type::not_found) {
    return 0;
  }
  if (status.type() == std::filesystem::file_type::regular) {
    return 1;
  }
  return 2;
}

/// Reads at most `kMaxLoadBytes + 1` bytes so an oversized file is
/// detectable without buffering it whole.
std::string ReadBounded(const std::string& path_utf8) {
  std::ifstream file(std::filesystem::u8path(path_utf8), std::ios::binary);
  if (!file.is_open()) {
    return {};
  }
  std::string bytes;
  std::istreambuf_iterator<char> first(file);
  std::istreambuf_iterator<char> last;
  bytes.assign(first, last);
  if (bytes.size() > kMaxLoadBytes + 1) {
    bytes.resize(kMaxLoadBytes + 1);
  }
  return bytes;
}

std::string EntryFailureText(crayon::browser_mdv::EntryError error,
                             const LoadGateResult& gate,
                             const MdvPageStrings& strings) {
  using crayon::browser_mdv::EntryError;
  switch (error) {
    case EntryError::kOk:
      if (!gate.utf8_valid) {
        return strings.status_invalid_utf8;
      }
      if (!gate.content_within_bounds) {
        return strings.status_too_large;
      }
      return strings.status_render_policy;
    case EntryError::kNotMarkdown:
    case EntryError::kNotFound:
    case EntryError::kNotRegularFile:
      return strings.status_not_markdown;
    case EntryError::kInvalidCharacter:
    case EntryError::kPathTooLong:
    case EntryError::kTraversal:
      return strings.status_not_markdown;
    case EntryError::kPageInitiated:
      break;
  }
  return strings.status_not_markdown;
}

}  // namespace

MdvEntryController::MdvEntryController(std::shared_ptr<MdvRuntimeState> state,
                                       MdvPageStrings strings)
    : state_(std::move(state)), strings_(std::move(strings)) {}

class MdvEntryController::MdvFileDialogCallback final
    : public CefRunFileDialogCallback {
 public:
  MdvFileDialogCallback(std::shared_ptr<MdvEntryController> owner,
                        CefRefPtr<CefBrowser> browser)
      : owner_(std::move(owner)), browser_(std::move(browser)) {}

  void OnFileDialogDismissed(
      const std::vector<CefString>& file_paths) override {
    CEF_REQUIRE_UI_THREAD();
    // Empty selection = user cancelled; nothing loads, nothing navigates.
    if (file_paths.empty() || !owner_ || !browser_) {
      return;
    }
    std::string path;
    const std::string first = file_paths[0].ToString();
    if (!LocalPathFromFileUrl(first, &path)) {
      path = first;
    }
    owner_->LoadAndShow(browser_.get(), path, EntrySource::kUserCommand);
  }

 private:
  const std::shared_ptr<MdvEntryController> owner_;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(MdvFileDialogCallback);
  DISALLOW_COPY_AND_ASSIGN(MdvFileDialogCallback);
};

bool MdvEntryController::HandleChromeCommand(CefRefPtr<CefBrowser> browser,
                                             int command_id) {
  CEF_REQUIRE_UI_THREAD();
  // IDC_OPEN_FILE: the native Ctrl+O / menu "Open file" command.
  const int open_file_id = cef_id_for_command_id_name("IDC_OPEN_FILE");
  if (open_file_id <= 0 || command_id != open_file_id || !browser) {
    return false;
  }
  CefString title(strings_.document_title);
  // Filter list: one ".md" pattern (MDV-01 §3 E1).
  std::vector<CefString> filters{CefString(".md")};
  // The callback arrives async on the UI thread; the controller is kept
  // alive by the shell assembly's shared_ptr.
  browser->GetHost()->RunFileDialog(
      FILE_DIALOG_OPEN, title, CefString(), filters,
      new MdvFileDialogCallback(shared_from_this(), browser));
  return true;
}

bool MdvEntryController::InterceptNavigation(CefRefPtr<CefBrowser> browser,
                                             const CefString& url,
                                             bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  if (!user_gesture) {
    return false;  // page-initiated navigations are never entries
  }
  std::string path_utf8;
  if (!LocalPathFromFileUrl(url.ToString(), &path_utf8)) {
    return false;  // not a file:// URL: normal browsing
  }
  if (!HasMarkdownSuffix(path_utf8)) {
    return false;  // non-markdown local targets keep default behavior
  }
  LoadAndShow(browser, path_utf8, EntrySource::kUserCommand);
  return true;
}

void MdvEntryController::LoadAndShow(CefRefPtr<CefBrowser> browser,
                                     const std::string& path_utf8,
                                     EntrySource source) {
  CEF_REQUIRE_UI_THREAD();
  crayon::browser_mdv::MdvPageSnapshot snapshot;
  snapshot.view_mode = crayon::browser_mdv::MdvViewMode::kPreview;

  const std::string bytes = ReadBounded(path_utf8);
  std::string normalized;
  const auto gate =
      GateLocalLoad(path_utf8, source, bytes, StatProbeFilesystem, &normalized);
  if (gate.ok()) {
    crayon::browser_markdown::RenderStatus status =
        crayon::browser_markdown::RenderStatus::kOk;
    const std::string html =
        crayon::browser_markdown::RenderMarkdownToSafeHtml(normalized, &status);
    if (status == crayon::browser_markdown::RenderStatus::kOk) {
      snapshot.load_status = normalized.empty()
                                 ? crayon::browser_mdv::MdvLoadStatus::kEmpty
                                 : crayon::browser_mdv::MdvLoadStatus::kLoaded;
      snapshot.has_document = true;
      snapshot.source_text = normalized;
      snapshot.rendered_html = html;
      state_->SetSnapshot(std::move(snapshot));
    } else {
      snapshot.load_status =
          crayon::browser_mdv::MdvLoadStatus::kRenderPolicyViolation;
      snapshot.error_text = strings_.status_render_policy;
      state_->SetSnapshot(std::move(snapshot));
    }
  } else {
    snapshot.error_text = EntryFailureText(gate.entry, gate, strings_);
    state_->SetSnapshot(std::move(snapshot));
  }
  if (browser) {
    const std::string viewer_url = std::string(kMdvScheme) + "://" +
                                   std::string(kMdvHost) + kResourceAppHtml;
    browser->GetMainFrame()->LoadURL(viewer_url);
  }
}

bool LocalPathFromFileUrl(const std::string& url, std::string* path_utf8) {
  CefURLParts parts;
  CefString url_string(url);
  if (!CefParseURL(url_string, parts)) {
    return false;
  }
  const std::string scheme = CefString(&parts.scheme).ToString();
  if (scheme != "file") {
    return false;
  }
  std::string path = CefString(&parts.path).ToString();
  // Percent-decode the path component.
  CefString decoded =
      CefURIDecode(path, true,
                   static_cast<cef_uri_unescape_rule_t>(
                       UU_SPACES | UU_PATH_SEPARATORS |
                       UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS));
  path = decoded.ToString();
  // file:///D:/x.md -> D:/x.md (Windows); file:///home/x.md -> /home/x.md.
#if defined(_WIN32)
  // file:///D:/x.md -> D:/x.md; file://server/share -> //server/share.
  if (path.size() >= 3 && path[0] == '/' && path[2] == ':' &&
      ((path[1] >= 'A' && path[1] <= 'Z') ||
       (path[1] >= 'a' && path[1] <= 'z'))) {
    path = path.substr(1);
  } else if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
    path = path.substr(1);
  }
#endif
  *path_utf8 = path;
  return true;
}

}  // namespace crayon::browser::cef_shell::mdv
