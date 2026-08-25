#include "crayon/browser_mdv/mdv_entry_guard.h"

#include <algorithm>

#include "crayon/browser_markdown/markdown_render.h"

namespace crayon::browser_mdv {
namespace {

bool HasControlChar(const std::string& path) {
  return std::any_of(path.begin(), path.end(), [](char c) {
    const auto raw = static_cast<unsigned char>(c);
    return raw < 0x20 || raw == 0x7F;
  });
}

bool EqualsIgnoreCaseAscii(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    const auto ca = static_cast<unsigned char>(a[i]);
    const auto cb = static_cast<unsigned char>(b[i]);
    if (std::tolower(ca) != std::tolower(cb)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool HasMarkdownSuffix(const std::string& path) {
  static const std::string suffix = ".md";
  if (path.size() <= suffix.size()) {
    return false;
  }
  if (!EqualsIgnoreCaseAscii(path.substr(path.size() - suffix.size()), suffix)) {
    return false;
  }
  // Require a non-empty stem: a file named ".md" (or a directory
  // boundary directly before the suffix) has no name part.
  const char stem_last = path[path.size() - suffix.size() - 1];
  return stem_last != '/' && stem_last != '.';
}

EntryError ValidateEntry(const std::string& path, EntrySource source, StatProbe stat_probe) {
  // §3: the gesture gate dominates — page-initiated opens are
  // impossible regardless of anything else.
  if (source == EntrySource::kPage) {
    return EntryError::kPageInitiated;
  }
  if (!HasMarkdownSuffix(path)) {
    return EntryError::kNotMarkdown;
  }
  if (path.size() > kMaxEntryPathLen) {
    return EntryError::kPathTooLong;
  }
  if (HasControlChar(path)) {
    return EntryError::kInvalidCharacter;
  }
  // Traversal: any `..` path segment is rejected outright; full
  // symlink resolution stays with the platform path_guard behind the
  // stat probe.
  std::size_t pos = 0;
  while (pos <= path.size()) {
    const std::size_t slash = path.find('/', pos);
    const std::string segment =
        path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
    if (segment == "..") {
      return EntryError::kTraversal;
    }
    if (slash == std::string::npos) {
      break;
    }
    pos = slash + 1;
  }
  if (stat_probe == nullptr) {
    return EntryError::kNotFound;
  }
  const int kind = stat_probe(path);
  if (kind == 0) {
    return EntryError::kNotFound;
  }
  if (kind != 1) {
    return EntryError::kNotRegularFile;
  }
  return EntryError::kOk;
}

std::string NormalizeLoadedContent(const std::string& bytes) {
  std::string data = bytes;
  if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
      static_cast<unsigned char>(data[1]) == 0xBB &&
      static_cast<unsigned char>(data[2]) == 0xBF) {
    data.erase(0, 3);
  }
  std::string out;
  out.reserve(data.size());
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] == '\r') {
      if (i + 1 < data.size() && data[i + 1] == '\n') {
        ++i;
      }
      out.push_back('\n');
    } else {
      out.push_back(data[i]);
    }
  }
  return out;
}

LoadGateResult GateLocalLoad(const std::string& path, EntrySource source,
                             const std::string& bytes, StatProbe stat_probe,
                             std::string* normalized) {
  LoadGateResult result;
  if (normalized != nullptr) {
    normalized->clear();
  }
  result.entry = ValidateEntry(path, source, stat_probe);
  if (result.entry != EntryError::kOk) {
    return result;
  }
  result.content_within_bounds = bytes.size() <= kMaxLoadBytes;
  if (!result.content_within_bounds) {
    return result;
  }
  result.utf8_valid = crayon::browser_markdown::IsValidUtf8(bytes);
  if (!result.utf8_valid) {
    return result;
  }
  if (normalized != nullptr) {
    *normalized = NormalizeLoadedContent(bytes);
  }
  return result;
}

}  // namespace crayon::browser_mdv
