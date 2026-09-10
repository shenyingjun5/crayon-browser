// MRT-11: bounded in-document search over the in-memory source text.
//
// The search operates on the current document's source bytes only — never
// page HTML, never disk, never the network. Queries are never persisted and
// never logged. ASCII matching folds case; multi-byte characters require an
// exact byte match so a pattern can never match across character boundaries.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace crayon::browser_mdv {

/// Maximum query bytes accepted from the search field.
inline constexpr std::size_t kMaxSearchQueryBytes = 128;
/// Maximum number of matches reported for one query.
inline constexpr std::size_t kMaxSearchMatches = 200;

/// One match as source byte offsets (inclusive begin, exclusive end).
struct MdvSearchMatch final {
  std::size_t begin = 0;
  std::size_t end = 0;

  friend bool operator==(const MdvSearchMatch& left, const MdvSearchMatch& right) {
    return left.begin == right.begin && left.end == right.end;
  }
  friend bool operator!=(const MdvSearchMatch& left, const MdvSearchMatch& right) {
    return !(left == right);
  }
};

/// Closed search state; the UI derives "x/y" from |matches| and |cursor|.
struct MdvSearchResult final {
  std::vector<MdvSearchMatch> matches;
  /// Truncated when the document contains more matches than the budget.
  bool truncated = false;
  /// Selected match index (valid only when !matches.empty()).
  std::size_t cursor = 0;
};

/// Folds ASCII A-Z only; other bytes compare exactly. Deterministic and
/// locale-independent by design (no system collation on the hot path).
char SearchFoldByte(char value);

/// Case-insensitive byte search of |needle| inside |source|. Empty queries
/// and needles longer than the source yield no matches. Overlap is prevented
/// by resuming after each match end; matches beyond the budget are counted
/// as truncation, not returned.
MdvSearchResult SearchDocument(const std::string& source,
                               const std::string& query);

/// Advances |cursor| within a result. Wraps at both ends; empty results
/// stay at 0.
std::size_t SearchStep(const MdvSearchResult& result, int direction);

}  // namespace crayon::browser_mdv
