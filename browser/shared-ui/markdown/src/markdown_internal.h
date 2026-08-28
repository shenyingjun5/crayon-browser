#pragma once

#include <string>

extern "C" {
#include "md4c.h"
}

namespace crayon::browser_markdown::internal {

// Both HTML rendering and extension-fact extraction must parse the same
// security-restricted Markdown dialect. Keep the flags owned in one place.
inline constexpr unsigned kParserFlags =
    MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
    MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS;

std::string NormalizeInput(const std::string& input);

}  // namespace crayon::browser_markdown::internal
