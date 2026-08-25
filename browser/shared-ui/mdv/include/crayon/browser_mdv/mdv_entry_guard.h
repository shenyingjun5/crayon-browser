// MDV-04: controlled local `.md` entry guard (MDV-01 §3–§5).
//
// All three entries — menu file dialog, drag-drop, omnibox local path
// — share one gate: an explicit user-gesture source check (page
// content can never open the viewer), the closed path validation
// matrix (`.md` suffix case-insensitive, no control characters,
// bounded length, no `..` traversal, existence + regular-file via the
// injected stat callback), and the load bounds (5 MiB, strict UTF-8
// with one BOM strip, CRLF/CR → LF, empty file legal).
//
// Full symlink/junction resolution hardening belongs to the platform
// path_guard (PRV-04 semantics) behind the injected callback.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace crayon::browser_mdv {

/// Platform-safe path length bound used by the model (the platform
/// port may tighten it further).
inline constexpr std::size_t kMaxEntryPathLen = 4096;
/// Maximum load size, in bytes (MDV-01 §5).
inline constexpr std::size_t kMaxLoadBytes = 5 * 1024 * 1024;

/// Closed entry origins; only explicit user gestures may open files.
enum class EntrySource { kUserCommand = 0, kPage };

/// Closed entry validation failures with actionable semantics.
enum class EntryError {
  kOk = 0,
  kPageInitiated,       // page content can never open files (§3)
  kNotMarkdown,         // suffix is not `.md` (case-insensitive)
  kInvalidCharacter,    // control character (< 0x20 or DEL) in path
  kPathTooLong,         // exceeds the bounded length
  kTraversal,           // contains a `..` segment
  kNotFound,            // does not exist or unreadable (stat callback)
  kNotRegularFile,      // directory / device / pipe (stat callback)
};

/// Injected stat probe: returns the file kind for `path` — 0 when the
/// path does not exist, 1 for a regular file, 2 for anything else.
using StatProbe = int (*)(const std::string& path);

/// Reports whether `path` ends with `.md` (case-insensitive).
bool HasMarkdownSuffix(const std::string& path);

/// Validates one entry attempt end-to-end (§3 gesture gate → §4 path
/// matrix).  `stat_probe` must classify existence and file kind.
EntryError ValidateEntry(const std::string& path, EntrySource source, StatProbe stat_probe);

/// Normalizes loaded bytes per §5: strips one leading UTF-8 BOM and
/// converts CRLF/CR to LF.  Size and UTF-8 validation stay with the
/// caller (MdvViewerModel + MDV-02 engine re-validate).
std::string NormalizeLoadedContent(const std::string& bytes);

/// Closed load-gate outcome binding the entry and content bounds.
struct LoadGateResult {
  EntryError entry = EntryError::kPageInitiated;
  bool content_within_bounds = false;
  bool utf8_valid = false;
  bool ok() const { return entry == EntryError::kOk && content_within_bounds && utf8_valid; }
};

/// Runs the full load gate: entry validation, size bound, strict
/// UTF-8.  Returns the normalized content on success (empty string on
/// any failure; an empty FILE is legal and yields empty success).
LoadGateResult GateLocalLoad(const std::string& path, EntrySource source,
                             const std::string& bytes, StatProbe stat_probe,
                             std::string* normalized);

}  // namespace crayon::browser_mdv
