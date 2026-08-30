#include "crayon/browser_page_tools/page_markdown_export.h"

#include "crayon/browser_page_tools/page_tools.h"

namespace crayon::browser_page_tools {

namespace {

/// Characters rejected in suggested filenames (filesystem-hostile set).
bool IsIllegalFilenameCharacter(char character) noexcept {
    switch (character) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return true;
        default:
            return static_cast<unsigned char>(character) < 0x20U;
    }
}

}  // namespace

PageMarkdownExportController::PageMarkdownExportController(
    ExportIoHooks export_hooks, crayon::browser_mdv_save::SaveIoHooks save_hooks)
    : hooks_(export_hooks), save_(save_hooks) {}

bool PageMarkdownExportController::OpenPreview(const std::string& title,
                                               const std::string& markdown) {
    has_session_ = false;
    overwrite_pending_ = false;
    pending_target_.clear();
    title_.clear();
    markdown_.clear();
    suggested_filename_.clear();
    residual_temp_path_.clear();
    feedback_ = ExportFeedback::kNone;
    if (markdown.empty() || markdown.size() > kMaxExportMarkdownBytes) {
        feedback_ = ExportFeedback::kRejectedPayload;
        return false;
    }
    has_session_ = true;
    title_ = title;
    markdown_ = markdown;
    suggested_filename_ = SuggestFilename(title);
    return true;
}

const std::string* PageMarkdownExportController::payload() const {
    return has_session_ ? &markdown_ : nullptr;
}

std::string PageMarkdownExportController::SuggestFilename(const std::string& title) {
    std::string name;
    name.reserve(title.size());
    bool collapsed_space = true;  // suppress leading separators
    for (const char character : title) {
        if (IsIllegalFilenameCharacter(character) || character == ' ' ||
            character == '\t' || character == '\n' || character == '\r') {
            if (!collapsed_space) {
                name.push_back('-');
                collapsed_space = true;
            }
            continue;
        }
        name.push_back(character);
        collapsed_space = false;
    }
    // Trim trailing separators and dots.
    while (!name.empty() && (name.back() == '-' || name.back() == '.')) {
        name.pop_back();
    }
    // Byte-cap without splitting a UTF-8 sequence.
    if (name.size() > kMaxFilenameLen - 3) {
        std::size_t keep = kMaxFilenameLen - 3;
        while (keep > 0 && (static_cast<unsigned char>(name[keep]) & 0xC0U) == 0x80U) {
            --keep;
        }
        name.resize(keep);
    }
    if (name.empty()) {
        name = "page";
    }
    name += ".md";
    return name;
}

ExportFeedback PageMarkdownExportController::SaveAs(const std::string& target_path) {
    if (!has_session_) {
        return ExportFeedback::kNone;
    }
    // Only the bare filename is validated here; the caller supplies the
    // user-selected directory and owns the path grant (CT-007: only the
    // user-chosen path is ever written).
    const std::size_t last_separator = target_path.find_last_of("/\\");
    const std::string filename = last_separator == std::string::npos
                                     ? target_path
                                     : target_path.substr(last_separator + 1);
    if (!IsValidOutputFilename(filename)) {
        feedback_ = ExportFeedback::kRejectedFilename;
        return feedback_;
    }
    overwrite_pending_ = false;
    pending_target_.clear();
    if (hooks_.file_exists != nullptr && hooks_.file_exists(target_path) == 0) {
        overwrite_pending_ = true;
        pending_target_ = target_path;
        feedback_ = ExportFeedback::kOverwriteRequired;
        return feedback_;
    }
    return RunSave(target_path);
}

ExportFeedback PageMarkdownExportController::ConfirmOverwrite() {
    if (!has_session_ || !overwrite_pending_) {
        return ExportFeedback::kNone;
    }
    const std::string target = pending_target_;
    overwrite_pending_ = false;
    pending_target_.clear();
    return RunSave(target);
}

void PageMarkdownExportController::Cancel() {
    has_session_ = false;
    overwrite_pending_ = false;
    pending_target_.clear();
    title_.clear();
    markdown_.clear();
    suggested_filename_.clear();
    residual_temp_path_.clear();
    feedback_ = ExportFeedback::kCancelled;
}

ExportFeedback PageMarkdownExportController::RunSave(const std::string& target_path) {
    const auto state =
        save_.Save(crayon::browser_mdv_save::SaveKind::kSaveAs, target_path, markdown_);
    residual_temp_path_ = save_.residual_temp_path();
    switch (state) {
        case crayon::browser_mdv_save::SaveState::kSucceeded:
            feedback_ = ExportFeedback::kSaved;
            break;
        case crayon::browser_mdv_save::SaveState::kFailedInvalidTarget:
            feedback_ = ExportFeedback::kRejectedFilename;
            break;
        default:
            // stat/temp-write/rename/residual failures surface as one
            // closed failed feedback; the residual temp path, when any,
            // is reported to the user.
            feedback_ = ExportFeedback::kFailed;
            break;
    }
    return feedback_;
}

}  // namespace crayon::browser_page_tools
