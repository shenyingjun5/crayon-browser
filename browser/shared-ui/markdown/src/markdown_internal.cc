#include "markdown_internal.h"

namespace crayon::browser_markdown::internal {

std::string NormalizeInput(const std::string& input) {
  std::string data = input;
  if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
      static_cast<unsigned char>(data[1]) == 0xBB &&
      static_cast<unsigned char>(data[2]) == 0xBF) {
    data.erase(0, 3);
  }

  std::string normalized;
  normalized.reserve(data.size());
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] != '\r') {
      normalized.push_back(data[i]);
      continue;
    }
    if (i + 1 < data.size() && data[i + 1] == '\n') {
      ++i;
    }
    normalized.push_back('\n');
  }
  return normalized;
}

}  // namespace crayon::browser_markdown::internal
