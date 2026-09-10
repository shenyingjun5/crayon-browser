// MRT-10: bounded document outline model for the MDV viewer.
//
// Builds a normalized outline tree from markdown outline facts, exposes
// keyboard navigation (next/previous/first/last) over entries and the
// accessible labels for the outline panel. Anchors resolve in the rendered
// document by heading ordinal (h1..h6 order) — the rendered HTML is never
// modified.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "crayon/browser_markdown/markdown_outline_facts.h"

namespace crayon::browser_mdv {

/// Maximum entries the outline panel model keeps (mirrors the facts bound).
inline constexpr std::size_t kMaxOutlineEntries = 512;

/// One visible outline entry with its normalized tree depth.
struct MdvOutlineEntry final {
  /// Session-stable anchor id (facts-derived).
  std::string anchor;
  /// Plain heading text, bounded by the facts layer.
  std::string text;
  /// Original heading level, 1..=6.
  int level = 0;
  /// Normalized nesting depth (root = 0; increases by at most 1).
  int tree_depth = 0;
  /// Rendered-document h1..h6 ordinal used for scroll resolution.
  std::uint32_t ordinal = 0;

  friend bool operator==(const MdvOutlineEntry& left, const MdvOutlineEntry& right) {
    return left.anchor == right.anchor && left.text == right.text &&
           left.level == right.level && left.tree_depth == right.tree_depth &&
           left.ordinal == right.ordinal;
  }
  friend bool operator!=(const MdvOutlineEntry& left, const MdvOutlineEntry& right) {
    return !(left == right);
  }
};

/// Keyboard/reader navigation moves over the flat entry order.
enum class OutlineMove {
  First,
  Last,
  Next,
  Previous,
};

/// Bounded outline model. Empty documents and parse failures produce an
/// empty model; the page renders no panel in that case.
class MdvOutlineModel final {
 public:
  /// Builds the normalized outline. Nesting is regularized: each entry's
  /// depth is at most one below its predecessor regardless of level jumps.
  static MdvOutlineModel Build(
      const std::vector<crayon::browser_markdown::OutlineHeading>& headings);

  const std::vector<MdvOutlineEntry>& entries() const noexcept { return entries_; }
  std::size_t size() const noexcept { return entries_.size(); }
  bool empty() const noexcept { return entries_.empty(); }

  /// Applies one navigation move from |current_index|; an out-of-range
  /// index behaves as "none selected" (First → 0, Last → last, Next/Previous
  /// stay put). Returns the same index when no move is possible.
  std::size_t Move(OutlineMove move, std::size_t current_index) const;

  /// Accessible label for one entry: visible text plus level announcement.
  std::string AccessibleLabel(std::size_t index) const;

 private:
  std::vector<MdvOutlineEntry> entries_;
};

}  // namespace crayon::browser_mdv
