#ifndef CRAYON_BROWSER_SHARED_UI_MARKDOWN_INCLUDE_CRAYON_BROWSER_MARKDOWN_MARKDOWN_OUTLINE_FACTS_H_
#define CRAYON_BROWSER_SHARED_UI_MARKDOWN_INCLUDE_CRAYON_BROWSER_MARKDOWN_MARKDOWN_OUTLINE_FACTS_H_

// MRT-10: bounded heading outline collected from md4c parse facts. Text is
// plain-source heading text (never page HTML); anchors are deterministic
// session-stable ids derived from generation/revision/ordinal.

#include <cstdint>
#include <string>
#include <vector>

namespace crayon::browser_markdown {

/// Maximum headings collected for one document.
inline constexpr std::size_t kMaxOutlineHeadings = 512;
/// Maximum UTF-8 bytes kept per heading text; longer text is truncated to
/// this bound minus the 3-byte `...` marker.
inline constexpr std::size_t kMaxOutlineHeadingTextBytes = 256;

/// One outline heading: parse fact only.
struct OutlineHeading final {
  /// ATX/setext heading level, 1..=6.
  int level = 0;
  /// Plain source text (inline markup stripped by the text callback),
  /// bounded by kMaxOutlineHeadingTextBytes.
  std::string text;
  /// Session-stable anchor: `h-<generation:016x>-<revision:016x>-<ordinal:04x>`.
  std::string anchor;
  /// Ordinal of this heading among all headings of the rendered document;
  /// the renderer page resolves it via the same h1..h6 document order.
  std::uint32_t ordinal = 0;

  friend bool operator==(const OutlineHeading& left, const OutlineHeading& right) {
    return left.level == right.level && left.text == right.text &&
           left.anchor == right.anchor && left.ordinal == right.ordinal;
  }
  friend bool operator!=(const OutlineHeading& left, const OutlineHeading& right) {
    return !(left == right);
  }
};

enum class OutlineFactsStatus {
  kComplete = 0,
  kParserFailure,
};

struct OutlineFactsResult final {
  OutlineFactsStatus status = OutlineFactsStatus::kComplete;
  std::vector<OutlineHeading> headings;
};

/// Walks the normalized markdown once and collects the bounded heading
/// outline. Deterministic: same input/generation/revision yields identical
/// facts.
OutlineFactsResult CollectOutlineFacts(const std::string& input,
                                       std::uint64_t document_generation,
                                       std::uint64_t source_revision);

}  // namespace crayon::browser_markdown

#endif  // CRAYON_BROWSER_SHARED_UI_MARKDOWN_INCLUDE_CRAYON_BROWSER_MARKDOWN_MARKDOWN_OUTLINE_FACTS_H_
