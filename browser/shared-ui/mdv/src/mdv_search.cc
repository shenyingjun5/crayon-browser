#include "crayon/browser_mdv/mdv_search.h"

namespace crayon::browser_mdv {
namespace {

/// ASCII case folding only. Multi-byte UTF-8 bytes (>= 0x80) never fold:
/// a fold would let an ASCII query byte match part of a multi-byte char.
constexpr char kFoldLowerBound = 'A';
constexpr char kFoldUpperBound = 'Z';
constexpr char kFoldDelta = 'a' - 'A';

/// UTF-8 continuation byte (0x80..0xBF) — only ever legal inside a
/// multi-byte sequence, never as a match boundary.
bool IsContinuationByte(char value) {
  return static_cast<unsigned char>(value) >= 0x80 &&
         static_cast<unsigned char>(value) <= 0xBF;
}

/// A match may not begin or end inside a multi-byte character: the byte
/// before the match start and the byte at the match end must not be
/// continuations (position 0 / end-of-source are valid boundaries).
bool MatchOnCharacterBoundaries(const std::string& source, std::size_t begin,
                                std::size_t end) {
  if (begin > 0 && IsContinuationByte(source[begin])) {
    return false;
  }
  if (end < source.size() && IsContinuationByte(source[end])) {
    return false;
  }
  return true;
}

}  // namespace

char SearchFoldByte(char value) {
  if (value >= kFoldLowerBound && value <= kFoldUpperBound) {
    return static_cast<char>(value + kFoldDelta);
  }
  return value;
}

MdvSearchResult SearchDocument(const std::string& source,
                               const std::string& query) {
  MdvSearchResult result;
  if (query.empty() || query.size() > kMaxSearchQueryBytes ||
      query.size() > source.size()) {
    return result;
  }
  const std::size_t window = query.size();
  const std::size_t last_start = source.size() - window;
  std::size_t position = 0;
  while (position <= last_start) {
    std::size_t offset = 0;
    while (offset < window && SearchFoldByte(source[position + offset]) ==
                                  SearchFoldByte(query[offset])) {
      ++offset;
    }
    if (offset == window &&
        MatchOnCharacterBoundaries(source, position, position + window)) {
      if (result.matches.size() == kMaxSearchMatches) {
        result.truncated = true;
        return result;
      }
      result.matches.push_back({position, position + window});
      // Resume after this match: no overlapping matches.
      position += window;
    } else {
      ++position;
    }
  }
  return result;
}

std::size_t SearchStep(const MdvSearchResult& result, int direction) {
  if (result.matches.empty()) {
    return 0;
  }
  const std::size_t last = result.matches.size() - 1;
  if (direction == 0 || result.matches.size() == 1) {
    return result.cursor <= last ? result.cursor : 0;
  }
  if (direction > 0) {
    return result.cursor == last ? 0 : result.cursor + 1;
  }
  return result.cursor == 0 ? last : result.cursor - 1;
}

}  // namespace crayon::browser_mdv
