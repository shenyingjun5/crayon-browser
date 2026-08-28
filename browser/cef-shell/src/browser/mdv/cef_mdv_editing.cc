#include "browser/mdv/cef_mdv_editing.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "browser/mdv/cef_mdv_entries.h"
#include "crayon/browser_markdown/markdown_render.h"
#include "crayon/browser_markdown_runtime/katex_extension.h"
#include "crayon/browser_mdv/mdv_entry_guard.h"
#include "crayon/browser_mdv/mdv_images.h"
#include "crayon/browser_mdv/mdv_transform.h"
#include "include/cef_id_mappers.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::mdv {
namespace {

using crayon::browser_mdv::kMdvHost;
using crayon::browser_mdv::kMdvScheme;
using crayon::browser_mdv::MdvLoadStatus;
using crayon::browser_mdv::MdvPageSnapshot;
using crayon::browser_mdv_edit::DirtyDecision;
using crayon::browser_mdv_save::SaveIoHooks;
using crayon::browser_mdv_save::SaveKind;
using crayon::browser_mdv_save::SaveState;

constexpr char kViewerPrefix[] = "crayon://mdv/";

std::filesystem::path FilesystemPath(const std::string& path_utf8) {
#if defined(_WIN32)
  return std::filesystem::u8path(path_utf8);
#else
  return std::filesystem::path(path_utf8);
#endif
}

std::uint64_t NowMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

/// Real filesystem IO hooks for the MDV-06 controller.  mtime uses the
/// file-time tick count; only drift comparison consumes it.
int HookStat(const std::string& path, std::uint64_t* size,
             std::uint64_t* mtime) {
  std::error_code error;
  const auto target = FilesystemPath(path);
  const auto status = std::filesystem::status(target, error);
  if (error || status.type() != std::filesystem::file_type::regular) {
    return 1;
  }
  const auto file_size = std::filesystem::file_size(target, error);
  if (error) {
    return 1;
  }
  const auto write_time = std::filesystem::last_write_time(target, error);
  if (error) {
    return 1;
  }
  *size = static_cast<std::uint64_t>(file_size);
  *mtime = static_cast<std::uint64_t>(write_time.time_since_epoch().count());
  return 0;
}

int HookWriteTemp(const std::string& target_path, const std::string& bytes,
                  std::string* temp_path) {
  static std::atomic<int> sequence{0};
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch();
  const std::string temp = target_path + ".tmp-" +
                           std::to_string(sequence.fetch_add(1)) + "-" +
                           std::to_string(ticks.count());
  std::ofstream out(FilesystemPath(temp), std::ios::binary);
  if (!out.is_open()) {
    return 1;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.flush();
  if (!out.good()) {
    return 1;
  }
  *temp_path = temp;
  return 0;
}

int HookRename(const std::string& temp_path, const std::string& target_path) {
  std::error_code error;
  std::filesystem::rename(FilesystemPath(temp_path), FilesystemPath(target_path),
                          error);
  return error ? 1 : 0;
}

int HookRemove(const std::string& path) {
  std::error_code error;
  // An already-absent temp file is fine.
  std::filesystem::remove(FilesystemPath(path), error);
  return error ? 1 : 0;
}

/// Base name (no directories) of a local path; empty when no document.
std::string DocumentBaseName(const std::string& path_utf8) {
  if (path_utf8.empty()) {
    return {};
  }
  const auto pos = path_utf8.find_last_of("\\/");
  return pos == std::string::npos ? path_utf8 : path_utf8.substr(pos + 1);
}

std::string SaveFailureText(SaveState state, const MdvPageStrings& strings,
                            const std::string& residual) {
  switch (state) {
    case SaveState::kFailedConflict:
      return strings.confirm_text;
    case SaveState::kFailedInvalidTarget:
    case SaveState::kFailedStat:
    case SaveState::kFailedTempWrite:
    case SaveState::kFailedRename:
    case SaveState::kFailedResidual:
      // Failures are explicit and data-free; a residual temp path must
      // be reported, never hidden.
      return residual.empty()
                 ? strings.status_render_policy
                 : strings.status_render_policy + " (" + residual + ")";
    case SaveState::kSucceeded:
    case SaveState::kIdle:
      break;
  }
  return strings.status_saved;
}

}  // namespace

MdvEditController::MdvEditController(std::shared_ptr<MdvRuntimeState> state,
                                     MdvPageStrings strings)
    : state_(std::move(state)),
      strings_(std::move(strings)),
      edit_(&viewer_),
      save_(SaveIoHooks{&HookStat, &HookWriteTemp, &HookRename, &HookRemove}) {}

void MdvEditController::OnDocumentLoaded(CefRefPtr<CefBrowser> browser,
                                         const std::string& path_utf8,

                                         const std::string& normalized_text,
                                         std::uint64_t size,
                                         std::uint64_t mtime) {
  CEF_REQUIRE_UI_THREAD();
  current_path_ = path_utf8;
  const auto dir_pos = path_utf8.find_last_of("\\/");
  current_doc_dir_ = dir_pos == std::string::npos
                         ? std::string()
                         : path_utf8.substr(0, dir_pos);
  if (browser) {
    host_browser_id_ = browser->GetIdentifier();
  }
  pending_url_.clear();
  conflict_pending_ = false;
  if (document_generation_ != std::numeric_limits<std::uint64_t>::max()) {
    ++document_generation_;
  }
  viewer_.LoadContent(normalized_text, /*utf8_valid=*/true, NowMs());
  edit_.LoadDocument(normalized_text, /*utf8_valid=*/true, NowMs());
  if (!path_utf8.empty()) {
    save_.RecordLoadedFile(path_utf8, size, mtime);
  } else {
    save_.ClearLoadedFile();
  }
  RenderAndStore();
  PushState(browser);
}

bool MdvEditController::OnPageQuery(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query_id,
    const CefString& request, bool persistent,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(persistent);
  static_cast<void>(query_id);
  // Origin gate: the binding only exists for the built-in viewer page.
  const std::string frame_url = frame ? frame->GetURL().ToString() : "";
  if (frame_url.rfind(kViewerPrefix, 0) != 0) {
    callback->Failure(1, "forbidden origin");
    return true;  // handled: rejected
  }
  CefRefPtr<CefValue> parsed = CefParseJSON(request, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) {
    callback->Failure(2, "bad request");
    return true;
  }
  CefRefPtr<CefDictionaryValue> dict = parsed->GetDictionary();
  const std::string type = dict->GetString("type").ToString();

  if (type == "transform") {
    if (!dict->HasKey("action") ||
        dict->GetType("action") != VTYPE_STRING || !dict->HasKey("text") ||
        dict->GetType("text") != VTYPE_STRING || !dict->HasKey("start") ||
        dict->GetType("start") != VTYPE_INT || !dict->HasKey("end") ||
        dict->GetType("end") != VTYPE_INT) {
      callback->Failure(2, "bad transform request");
      return true;
    }
    const std::string action_id = dict->GetString("action").ToString();
    const std::string text = dict->GetString("text").ToString();
    const int start_utf16 = dict->GetInt("start");
    const int end_utf16 = dict->GetInt("end");
    const auto action = crayon::browser_mdv::ParseMdvToolbarAction(action_id);
    const auto start = start_utf16 < 0
                           ? std::nullopt
                           : crayon::browser_mdv::Utf16OffsetToUtf8Byte(
                                 text, static_cast<std::size_t>(start_utf16));
    const auto end = end_utf16 < 0
                         ? std::nullopt
                         : crayon::browser_mdv::Utf16OffsetToUtf8Byte(
                               text, static_cast<std::size_t>(end_utf16));
    CefRefPtr<CefValue> reply = CefValue::Create();
    reply->SetDictionary(CefDictionaryValue::Create());
    CefRefPtr<CefDictionaryValue> reply_dict = reply->GetDictionary();
    if (text.size() > crayon::browser_mdv::kMaxLoadBytes || !action || !start ||
        !end) {
      reply_dict->SetBool("applied", false);
      callback->Success(CefWriteJSON(reply, JSON_WRITER_DEFAULT));
      return true;
    }
    const auto edit = crayon::browser_mdv::TransformMarkdownText(
        text, *start, *end, *action);
    if (!edit.applied) {
      reply_dict->SetBool("applied", false);
      callback->Success(CefWriteJSON(reply, JSON_WRITER_DEFAULT));
      return true;
    }
    const auto replace_start = crayon::browser_mdv::Utf8ByteOffsetToUtf16(
        text, edit.replace_start);
    const auto replace_end = crayon::browser_mdv::Utf8ByteOffsetToUtf16(
        text, edit.replace_end);
    const auto selection_start = crayon::browser_mdv::Utf8ByteOffsetToUtf16(
        edit.replacement, edit.selection_start);
    const auto selection_end = crayon::browser_mdv::Utf8ByteOffsetToUtf16(
        edit.replacement, edit.selection_end);
    if (!replace_start || !replace_end || !selection_start || !selection_end) {
      reply_dict->SetBool("applied", false);
      callback->Success(CefWriteJSON(reply, JSON_WRITER_DEFAULT));
      return true;
    }
    reply_dict->SetBool("applied", true);
    reply_dict->SetString("replacement", edit.replacement);
    reply_dict->SetInt("start", static_cast<int>(*replace_start));
    reply_dict->SetInt("end", static_cast<int>(*replace_end));
    reply_dict->SetInt("selectionStart", static_cast<int>(*selection_start));
    reply_dict->SetInt("selectionEnd", static_cast<int>(*selection_end));
    callback->Success(CefWriteJSON(reply, JSON_WRITER_DEFAULT));
    return true;
  }

  if (type == "edit") {
    if (edit_.confirm_state() ==
        crayon::browser_mdv_edit::ConfirmState::kPending) {
      callback->Success("{}");
      return true;
    }
    const std::string text = dict->GetString("text").ToString();
    if (edit_.ApplyEdit(text, NowMs())) {
      RenderAndStore();
      // Ship the fresh preview/dirty back so the page can apply it
      // without a round trip through the shell.
      CefRefPtr<CefValue> reply = CefValue::Create();
      reply->SetDictionary(CefDictionaryValue::Create());
      CefRefPtr<CefDictionaryValue> reply_dict = reply->GetDictionary();
      const auto snapshot = state_->snapshot();
      reply_dict->SetString("preview", snapshot.rendered_html);
      reply_dict->SetBool("dirty", snapshot.dirty);
      reply_dict->SetBool("confirm", snapshot.confirm_visible);
      callback->Success(CefWriteJSON(reply, JSON_WRITER_DEFAULT));
      return true;
    }
    callback->Success("{}");
    return true;
  }
  if (type == "decision") {
    ApplyDecision(browser, dict->GetString("value").ToString());
    callback->Success("{}");
    return true;
  }
  callback->Failure(3, "unknown type");
  return true;
}

bool MdvEditController::InterceptWhileDirty(CefRefPtr<CefBrowser> browser,
                                            const std::string& url,
                                            bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  if (!user_gesture || !edit_.dirty()) {
    return false;
  }
  // Dirty confirmation only guards the tab hosting the document; other
  // tabs opening files are independent (MDV-11 design decision).
  if (browser && host_browser_id_ != -1 &&
      browser->GetIdentifier() != host_browser_id_) {
    return false;
  }
  // Viewer reloads are part of the editing flow, not transitions.
  if (url.rfind(kViewerPrefix, 0) == 0) {
    return false;
  }
  if (!edit_.BeginBlockingTransition()) {
    return false;
  }
  pending_url_ = url;
  conflict_pending_ = false;
  RenderAndStore();
  auto snapshot = state_->snapshot();
  snapshot.confirm_visible = true;
  state_->SetSnapshot(std::move(snapshot));
  PushState(browser);
  return true;
}

bool MdvEditController::HandleSaveCommand(CefRefPtr<CefBrowser> browser,
                                          int command_id) {
  CEF_REQUIRE_UI_THREAD();
  const int save_page_id = cef_id_for_command_id_name("IDC_SAVE_PAGE");
  if (save_page_id <= 0 || command_id != save_page_id) {
    return false;
  }
  if (!viewer_.has_document() || current_path_.empty()) {
    return true;  // nothing to save; swallow the Chrome page-save dialog
  }
  PerformSave(browser, SaveKind::kWriteBack, current_path_);
  return true;
}

void MdvEditController::ApplyDecision(CefRefPtr<CefBrowser> browser,
                                      const std::string& value) {
  CEF_REQUIRE_UI_THREAD();
  if (conflict_pending_) {
    // Conflict overlay: save = overwrite mine (save-as semantics skip
    // the drift check), discard = save-as to a new location, cancel =
    // keep editing with the conflict resolved as "not saved".
    conflict_pending_ = false;
    if (value == "save") {
      PerformSave(browser, SaveKind::kSaveAs, current_path_);
      return;
    }
    if (value == "discard") {
      StartSaveAsDialog(browser);
      return;
    }
    RenderAndStore();
    PushState(browser);
    return;
  }
  if (edit_.confirm_state() !=
      crayon::browser_mdv_edit::ConfirmState::kPending) {
    return;
  }
  DirtyDecision decision = DirtyDecision::kCancel;
  if (value == "save") {
    decision = DirtyDecision::kSaveAndContinue;
  } else if (value == "discard") {
    decision = DirtyDecision::kDiscard;
  }
  if (!edit_.ResolveTransition(decision)) {
    return;
  }
  if (decision == DirtyDecision::kSaveAndContinue) {
    // Save now; the pending navigation is released on success.
    PerformSave(browser, SaveKind::kWriteBack, current_path_);
    return;
  }
  if (decision == DirtyDecision::kDiscard) {
    RenderAndStore();
    ReleasePendingNavigation(browser);
    return;
  }
  // Cancel: keep content, close the dialog, stay on the page.
  RenderAndStore();
  PushState(browser);
}

bool MdvEditController::SaveWriteBack(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!viewer_.has_document() || current_path_.empty()) {
    return true;  // swallow: no save target but the key was ours
  }
  PerformSave(browser, SaveKind::kWriteBack, current_path_);
  return true;
}

void MdvEditController::PerformSave(CefRefPtr<CefBrowser> browser,
                                    SaveKind kind,
                                    const std::string& target_path) {
  CEF_REQUIRE_UI_THREAD();
  const SaveState state = save_.Save(kind, target_path, edit_.edit_buffer());
  auto snapshot = state_->snapshot();
  if (state == SaveState::kSucceeded) {
    edit_.NotifySaveSucceeded();
    snapshot.save_ok = true;
    snapshot.error_text = strings_.status_saved;
    snapshot.dirty = false;
    state_->SetSnapshot(std::move(snapshot));
    conflict_pending_ = false;
    RenderAndStore();
    ReleasePendingNavigation(browser);
    return;
  }
  snapshot.save_ok = false;
  snapshot.dirty = edit_.dirty();
  if (state == SaveState::kFailedConflict) {
    // External modification: explicit three-choice via the confirm
    // overlay (save = overwrite mine via save-as semantics, discard =
    // save-as to a new location, cancel = keep editing).
    conflict_pending_ = true;
    snapshot.confirm_visible = true;
    snapshot.error_text = strings_.confirm_text;  // conflict explanation
  } else {
    snapshot.error_text =
        SaveFailureText(state, strings_, save_.residual_temp_path());
  }
  state_->SetSnapshot(std::move(snapshot));
  PushState(browser);
}

void MdvEditController::RenderAndStore() {
  const std::uint64_t now = NowMs();
  const auto revision = viewer_.RequestRender(now);
  const auto highlighted =
      crayon::browser_markdown_runtime::RenderP0MarkdownDocument(
          edit_.edit_buffer(), document_generation_, revision);
  if (highlighted.render_status ==
      crayon::browser_markdown::RenderStatus::kOk) {
    // MDV-13: classify engine image markers into their final form
    // (cloud https direct / validated local opaque route / placeholder).
    auto probe = [](const std::string& path_utf8, std::uint64_t* size) {
      std::error_code error;
      const auto target = FilesystemPath(path_utf8);
      if (!std::filesystem::is_regular_file(target, error)) {
        return false;
      }
      const auto file_size = std::filesystem::file_size(target, error);
      if (error) {
        return false;
      }
      *size = static_cast<std::uint64_t>(file_size);
      return true;
    };
    auto snapshot_for_images = state_->snapshot();
    snapshot_for_images.local_images.clear();
    const std::string prepared = crayon::browser_mdv::PreparePreviewHtml(
        highlighted.safe_html, current_doc_dir_, probe,
        &snapshot_for_images.local_images);
    state_->SetSnapshot(std::move(snapshot_for_images));
    viewer_.DeliverRender(revision, prepared);
  }
  auto snapshot = state_->snapshot();
  snapshot.view_mode = viewer_.view_mode();
  snapshot.load_status = viewer_.load_status();
  snapshot.has_document = viewer_.has_document();
  snapshot.source_text = edit_.edit_buffer();
  snapshot.rendered_html = viewer_.rendered_html();
  snapshot.document_name = DocumentBaseName(current_path_);
  snapshot.dirty = edit_.dirty();
  snapshot.confirm_visible =
      edit_.confirm_state() ==
          crayon::browser_mdv_edit::ConfirmState::kPending ||
      conflict_pending_;
  state_->SetSnapshot(std::move(snapshot));
}

void MdvEditController::PushState(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || !browser->GetMainFrame()) {
    return;
  }
  const auto snapshot = state_->snapshot();
  // The preview body is data, not code: ship it as a JSON string.
  CefRefPtr<CefValue> root = CefValue::Create();
  root->SetDictionary(CefDictionaryValue::Create());
  CefRefPtr<CefDictionaryValue> dict = root->GetDictionary();
  dict->SetString("preview", snapshot.rendered_html);
  dict->SetBool("dirty", snapshot.dirty);
  dict->SetBool("confirm", snapshot.confirm_visible);
  if (!snapshot.error_text.empty()) {
    dict->SetString("banner", snapshot.error_text);
  }
  CefString json = CefWriteJSON(root, JSON_WRITER_DEFAULT);
  std::string script = "window.mdvPush(" + json.ToString() + ");";
  browser->GetMainFrame()->ExecuteJavaScript(script, CefString(), 0);
}

void MdvEditController::ReleasePendingNavigation(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (pending_url_.empty()) {
    PushState(browser);
    return;
  }
  const std::string url = pending_url_;
  pending_url_.clear();
  if (browser) {
    browser->GetMainFrame()->LoadURL(url);
  }
}

class MdvEditController::SaveDialogCallback final
    : public CefRunFileDialogCallback {
 public:
  SaveDialogCallback(std::shared_ptr<MdvEditController> owner,
                     CefRefPtr<CefBrowser> browser)
      : owner_(std::move(owner)), browser_(std::move(browser)) {}

  void OnFileDialogDismissed(
      const std::vector<CefString>& file_paths) override {
    CEF_REQUIRE_UI_THREAD();
    if (file_paths.empty() || !owner_ || !browser_) {
      return;
    }
    std::string path;
    const std::string first = file_paths[0].ToString();
    std::string from_url;
    if (LocalPathFromFileUrl(first, &from_url)) {
      path = from_url;
    } else {
      path = first;
    }
    if (!crayon::browser_mdv::HasMarkdownSuffix(path)) {
      auto snapshot = owner_->state_->snapshot();
      snapshot.save_ok = false;
      snapshot.error_text = owner_->strings_.status_not_markdown;
      owner_->state_->SetSnapshot(std::move(snapshot));
      owner_->PushState(browser_);
      return;
    }
    owner_->PerformSave(browser_, SaveKind::kSaveAs, path);
  }

 private:
  const std::shared_ptr<MdvEditController> owner_;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(SaveDialogCallback);
  DISALLOW_COPY_AND_ASSIGN(SaveDialogCallback);
};

void MdvEditController::StartSaveAsDialog(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) {
    return;
  }
  std::vector<CefString> filters{CefString(".md")};
  browser->GetHost()->RunFileDialog(
      FILE_DIALOG_SAVE, CefString(strings_.document_title), CefString(),
      filters, new SaveDialogCallback(shared_from_this(), browser));
}

}  // namespace crayon::browser::cef_shell::mdv
