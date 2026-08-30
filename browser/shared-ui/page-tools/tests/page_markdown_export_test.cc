// CNT-08 contract tests: page-Markdown export view model — bounded
// preview, copy payload, save-as with overwrite confirmation, cancel
// without residue, closed failure feedback and sanitized filename
// suggestions.

#include <iostream>
#include <stdexcept>
#include <string>

#include "crayon/browser_page_tools/page_markdown_export.h"

namespace {

using crayon::browser_page_tools::ExportFeedback;
using crayon::browser_page_tools::ExportIoHooks;
using crayon::browser_page_tools::PageMarkdownExportController;

int FailStat(const std::string&, std::uint64_t*, std::uint64_t*) { return -1; }

int FailWrite(const std::string&, const std::string&, std::string*) { return -1; }
int SuccessWrite(const std::string&, const std::string&, std::string* temp) {
    *temp = "/tmp/crayon-export.tmp";
    return 0;
}
int FailRename(const std::string&, const std::string&) { return -1; }
int SuccessRename(const std::string&, const std::string&) { return 0; }
int SuccessRemove(const std::string&) { return 0; }

crayon::browser_mdv_save::SaveIoHooks SavingHooks() {
    return crayon::browser_mdv_save::SaveIoHooks{
        FailStat, SuccessWrite, SuccessRename, SuccessRemove};
}

ExportIoHooks AbsentHooks() {
    return ExportIoHooks{[](const std::string&) { return -1; }};
}
ExportIoHooks ExistingHooks() {
    return ExportIoHooks{[](const std::string&) { return 0; }};
}

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestPreviewBoundsAndSuggestion() {
    PageMarkdownExportController controller(AbsentHooks(), SavingHooks());
    // Empty payload rejected without a session.
    Check(!controller.OpenPreview("T", ""), "empty payload rejected");
    Check(!controller.has_session(), "no session after rejection");
    Check(controller.feedback() == ExportFeedback::kRejectedPayload,
          "payload rejection feedback");
    // Over-budget payload rejected.
    std::string huge(crayon::browser_page_tools::kMaxExportMarkdownBytes + 1, 'x');
    Check(!controller.OpenPreview("T", huge), "over-budget payload rejected");
    // Valid session.
    Check(controller.OpenPreview("My Page", "# hello"), "valid preview opens");
    Check(controller.has_session(), "session open");
    Check(controller.payload() != nullptr && *controller.payload() == "# hello",
          "copy payload exposed");
    Check(controller.suggested_filename() == "My-Page.md",
          "title sanitized into suggestion");
    // Copy path leaves the session intact.
    Check(controller.feedback() == ExportFeedback::kNone, "no spurious feedback");
}

void TestFilenameSanitization() {
    Check(PageMarkdownExportController::SuggestFilename("a/b:c*d?e") == "a-b-c-d-e.md",
          "illegal characters collapse to separators");
    Check(PageMarkdownExportController::SuggestFilename("  spaced   out  ") == "spaced-out.md",
          "whitespace collapses");
    Check(PageMarkdownExportController::SuggestFilename("...") == "page.md",
          "empty result falls back to page.md");
    Check(PageMarkdownExportController::SuggestFilename("Ünïcödé 标题") == "Ünïcödé-标题.md",
          "unicode survives");
    const auto long_title = std::string(300, 'a') + ".md";
    const auto suggested = PageMarkdownExportController::SuggestFilename(long_title);
    Check(suggested.size() <= 128 /* kMaxFilenameLen */,
          "suggestion is byte-capped");
    Check(suggested.size() >= 3 && suggested.substr(suggested.size() - 3) == ".md",
          "suffix preserved");
}

void TestSaveAsOverwriteConfirmationFlow() {
    PageMarkdownExportController controller(ExistingHooks(), SavingHooks());
    Check(controller.OpenPreview("T", "# hello"), "session open");
    // Existing target requires explicit confirmation.
    Check(controller.SaveAs("export.md") == ExportFeedback::kOverwriteRequired,
          "existing file demands confirmation");
    Check(controller.overwrite_pending(), "confirmation pending");
    Check(controller.ConfirmOverwrite() == ExportFeedback::kSaved, "confirmed save");
    Check(!controller.overwrite_pending(), "confirmation consumed");
}

void TestSaveAsNewFileAndFailures() {
    PageMarkdownExportController controller(AbsentHooks(), SavingHooks());
    Check(controller.OpenPreview("T", "# hello"), "session open");
    Check(controller.SaveAs("export.md") == ExportFeedback::kSaved, "new file saves");
    // Directory + filename is a legitimate caller-supplied target; only
    // the filename part must be valid.
    Check(controller.SaveAs("sub/dir.md") == ExportFeedback::kSaved,
          "directory plus filename accepted");
    Check(controller.SaveAs("") == ExportFeedback::kRejectedFilename,
          "empty filename rejected");
    Check(controller.SaveAs("bad<name.md") == ExportFeedback::kRejectedFilename,
          "illegal characters in filename rejected");
    // Save failure maps to closed failed feedback.
    PageMarkdownExportController failing(
        AbsentHooks(), crayon::browser_mdv_save::SaveIoHooks{FailStat, FailWrite, FailRename,
                                                             SuccessRemove});
    failing.OpenPreview("T", "# hello");
    Check(failing.SaveAs("export.md") == ExportFeedback::kFailed, "io failure feedback");
}

void TestCancelWithoutResidue() {
    PageMarkdownExportController controller(ExistingHooks(), SavingHooks());
    Check(controller.OpenPreview("T", "# hello"), "session open");
    Check(controller.SaveAs("export.md") == ExportFeedback::kOverwriteRequired,
          "overwrite pending");
    controller.Cancel();
    Check(!controller.has_session(), "session closed");
    Check(!controller.overwrite_pending(), "no pending confirmation");
    Check(controller.payload() == nullptr, "no payload residue");
    Check(controller.feedback() == ExportFeedback::kCancelled, "cancelled feedback");
    // Save without a session is a no-op.
    Check(controller.SaveAs("export.md") == ExportFeedback::kNone, "no session, no save");
}

}  // namespace

int main() {
    try {
        TestPreviewBoundsAndSuggestion();
        TestFilenameSanitization();
        TestSaveAsOverwriteConfirmationFlow();
        TestSaveAsNewFileAndFailures();
        TestCancelWithoutResidue();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "page_markdown_export_contract: " << error.what() << '\n';
        return 1;
    }
}
