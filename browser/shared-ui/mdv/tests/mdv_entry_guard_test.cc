// MDV-04 contract tests (MD-001/MD-003 model parts): gesture gate,
// path validation matrix, load bounds, normalization.
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_mdv/mdv_entry_guard.h"
#include "crayon/browser_mdv/mdv_viewer.h"

namespace {

using crayon::browser_mdv::EntryError;
using crayon::browser_mdv::EntrySource;
using crayon::browser_mdv::GateLocalLoad;
using crayon::browser_mdv::HasMarkdownSuffix;
using crayon::browser_mdv::kMaxEntryPathLen;
using crayon::browser_mdv::kMaxLoadBytes;
using crayon::browser_mdv::MdvLoadStatus;
using crayon::browser_mdv::MdvViewerModel;
using crayon::browser_mdv::NormalizeLoadedContent;
using crayon::browser_mdv::ValidateEntry;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

int RegularFile(const std::string&) { return 1; }
int Directory(const std::string&) { return 2; }
int Missing(const std::string&) { return 0; }

bool SuffixMatrix() {
  CHECK(HasMarkdownSuffix("/notes/README.md"));
  CHECK(HasMarkdownSuffix("/notes/README.MD"));
  CHECK(HasMarkdownSuffix("/notes/readme.Md"));
  CHECK(!HasMarkdownSuffix("/notes/file.markdown"));
  CHECK(!HasMarkdownSuffix("/notes/file.mdown"));
  CHECK(!HasMarkdownSuffix("/notes/file.txt"));
  CHECK(!HasMarkdownSuffix("/notes/.md"));  // suffix only, no stem
  CHECK(!HasMarkdownSuffix("md"));
  return true;
}

bool GestureGateDominates() {
  // Page-initiated opens are rejected before anything else, even with
  // a perfect path and an existing file.
  CHECK(ValidateEntry("/notes/ok.md", EntrySource::kPage, RegularFile) ==
        EntryError::kPageInitiated);
  CHECK(ValidateEntry("/notes/ok.md", EntrySource::kUserCommand, RegularFile) ==
        EntryError::kOk);
  return true;
}

bool PathMatrix() {
  const auto user = EntrySource::kUserCommand;
  CHECK(ValidateEntry("/notes/file.markdown", user, RegularFile) == EntryError::kNotMarkdown);
  const std::string with_control = "/notes/a\x01"
                                  "b.md";
  CHECK(ValidateEntry(with_control, user, RegularFile) == EntryError::kInvalidCharacter);
  const std::string with_del = "/notes/a\x7F.md";
  CHECK(ValidateEntry(with_del, user, RegularFile) == EntryError::kInvalidCharacter);
  CHECK(ValidateEntry(std::string(kMaxEntryPathLen + 1, 'a') + ".md", user, RegularFile) ==
        EntryError::kPathTooLong);
  // Traversal segments in any position.
  CHECK(ValidateEntry("/notes/../secret.md", user, RegularFile) == EntryError::kTraversal);
  // Backslash is an ordinary name character at this layer; platform
  // separators are normalized before the model on Windows.
  CHECK(ValidateEntry("/notes/ok_name\\x.md", user, RegularFile) == EntryError::kOk);
  CHECK(ValidateEntry("notes/../../x.md", user, RegularFile) == EntryError::kTraversal);
  // Existence and file kind via the injected probe.
  CHECK(ValidateEntry("/notes/gone.md", user, Missing) == EntryError::kNotFound);
  CHECK(ValidateEntry("/notes/dir.md", user, Directory) == EntryError::kNotRegularFile);
  // Null probe fails closed.
  CHECK(ValidateEntry("/notes/ok.md", user, nullptr) == EntryError::kNotFound);
  return true;
}

bool LoadBoundsMatrix() {
  const auto user = EntrySource::kUserCommand;
  std::string normalized;
  // Normal load with BOM and CRLF/CR normalization.
  const std::string content = "\xEF\xBB\xBF# T\r\nbody\rline";
  auto result = GateLocalLoad("/n/a.md", user, content, RegularFile, &normalized);
  CHECK(result.ok());
  CHECK(normalized == "# T\nbody\nline");
  // Empty file is legal and yields empty success.
  result = GateLocalLoad("/n/empty.md", user, "", RegularFile, &normalized);
  CHECK(result.ok() && normalized.empty());
  // Oversize content rejected after entry validation.
  result = GateLocalLoad("/n/big.md", user, std::string(kMaxLoadBytes + 1, 'a'), RegularFile,
                         &normalized);
  CHECK(!result.ok() && result.entry == EntryError::kOk && !result.content_within_bounds);
  CHECK(normalized.empty());
  // Binary masquerading as .md fails strict UTF-8.
  const std::string binary = std::string("\xFF\xFE# not utf8");
  result = GateLocalLoad("/n/bin.md", user, binary, RegularFile, &normalized);
  CHECK(!result.ok() && !result.utf8_valid);
  // Page source never reaches the content bounds.
  result = GateLocalLoad("/n/a.md", EntrySource::kPage, "# x", RegularFile, &normalized);
  CHECK(!result.ok() && result.entry == EntryError::kPageInitiated);
  return true;
}

bool EndToEndIntoViewer() {
  // Gate → normalize → viewer load: the exact MDV-04 → MDV-03 flow.
  MdvViewerModel viewer;
  const auto user = EntrySource::kUserCommand;
  std::string normalized;
  const auto gate = GateLocalLoad("/notes/real.md", user, "\xEF\xBB\xBF# Real\r\n\r\ntext",
                                  RegularFile, &normalized);
  CHECK(gate.ok());
  CHECK(viewer.LoadContent(normalized, gate.utf8_valid, 0) == MdvLoadStatus::kLoaded);
  CHECK(viewer.rendered_html().find("<h1>Real</h1>") != std::string::npos);
  return true;
}

}  // namespace

int main() {
  const bool ok = SuffixMatrix() && GestureGateDominates() && PathMatrix() &&
                  LoadBoundsMatrix() && EndToEndIntoViewer();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "mdv_entry_guard_test passed\n";
  return EXIT_SUCCESS;
}
