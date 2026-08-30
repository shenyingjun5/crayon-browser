// CNT-08: page-to-Markdown export view model.
//
// Owns the local export flow for the Markdown conversion of the current
// page: bounded preview, clipboard copy, save-as with an overwrite
// confirmation, cancel without residue and closed failure feedback. The
// atomic write itself (same-directory temp file + rename, conflict and
// residual reporting) is delegated to the MDV-06 save controller; this
// module owns only the export policy around it.
//
// The converted payload comes from the verified page-data pipeline and is
// bounded upstream; this controller refuses payloads beyond its budget
// instead of truncating silently. Thread contract: single-threaded, UI
// thread only.
#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "crayon/browser_mdv/mdv_save.h"

namespace crayon::browser_page_tools {

/// Maximum accepted Markdown payload, in bytes; larger conversions are a
/// producer bug and fail closed here.
inline constexpr std::size_t kMaxExportMarkdownBytes = 1024 * 1024;

/// Closed export feedback; stable and data-free.
enum class ExportFeedback {
    kNone = 0,
    kCopied,
    kSaved,
    kCancelled,
    kRejectedPayload,
    kRejectedFilename,
    kOverwriteRequired,
    kFailed,
};

/// Injected existence probe (0 when the file exists, non-zero otherwise).
struct ExportIoHooks {
    int (*file_exists)(const std::string& path);
};

/// View model for one page-Markdown export session.
class PageMarkdownExportController final {
public:
    PageMarkdownExportController(ExportIoHooks export_hooks,
                                 crayon::browser_mdv_save::SaveIoHooks save_hooks);

    /// Opens a new export session with the converted payload; an empty or
    /// over-budget payload is rejected and leaves no session behind.
    bool OpenPreview(const std::string& title, const std::string& markdown);

    /// The payload for the clipboard/copy path; unavailable without a
    /// session.
    const std::string* payload() const;

    /// Derives the suggested save-as filename from the page title: closed
    /// illegal-character sanitisation, whitespace collapsing, a 128-byte
    /// cap and a forced `.md` suffix. Never empty.
    static std::string SuggestFilename(const std::string& title);

    /// Requests a save to `target_path`. When the file already exists the
    /// controller enters the overwrite-confirmation state and returns
    /// `ExportFeedback::kOverwriteRequired`; only an explicit
    /// `ConfirmOverwrite` proceeds. Any other validation failure or save
    /// failure maps to its closed feedback.
    ExportFeedback SaveAs(const std::string& target_path);

    /// Confirms a pending overwrite and runs the save.
    ExportFeedback ConfirmOverwrite();

    /// Cancels the session and any pending confirmation; no residue.
    void Cancel();

    bool has_session() const noexcept { return has_session_; }
    bool overwrite_pending() const noexcept { return overwrite_pending_; }
    ExportFeedback feedback() const noexcept { return feedback_; }
    const std::string& suggested_filename() const noexcept { return suggested_filename_; }
    const std::string& residual_temp_path() const noexcept { return residual_temp_path_; }

private:
    ExportFeedback RunSave(const std::string& target_path);

    ExportIoHooks hooks_;
    crayon::browser_mdv_save::MdvSaveController save_;
    bool has_session_ = false;
    bool overwrite_pending_ = false;
    std::string pending_target_;
    std::string title_;
    std::string markdown_;
    std::string suggested_filename_;
    std::string residual_temp_path_;
    ExportFeedback feedback_ = ExportFeedback::kNone;
};

}  // namespace crayon::browser_page_tools
