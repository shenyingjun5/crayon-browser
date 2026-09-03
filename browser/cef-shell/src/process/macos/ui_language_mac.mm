#include "process/macos/ui_language_mac.h"

#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace crayon::browser::cef_shell::process {
namespace {

bool Utf8(CFStringRef value, std::string* output) {
  if (!value || !output) {
    return false;
  }
  if (CFStringGetLength(value) <= 0) {
    return false;
  }
  std::array<char,
             ::crayon::browser::localization::kMaximumLocaleTagBytes + 1U>
      buffer{};
  if (!CFStringGetCString(value, buffer.data(), buffer.size(),
                          kCFStringEncodingUTF8)) {
    return false;
  }
  output->assign(buffer.data());
  return !output->empty();
}

}  // namespace

MacPreferredUiLanguages ReadMacPreferredUiLanguages() {
  CFArrayRef languages = CFLocaleCopyPreferredLanguages();
  if (!languages) {
    return {};
  }

  MacPreferredUiLanguages result;
  const CFIndex count = CFArrayGetCount(languages);
  if (count <= 0 ||
      static_cast<std::size_t>(count) >
          ::crayon::browser::localization::kMaximumPreferredLocaleCount) {
    CFRelease(languages);
    return result;
  }

  result.language_tags.reserve(static_cast<std::size_t>(count));
  std::size_t total_bytes = 0;
  for (CFIndex index = 0; index < count; ++index) {
    const auto tag =
        static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, index));
    std::string utf8;
    if (!Utf8(tag, &utf8)) {
      CFRelease(languages);
      return {};
    }
    total_bytes += utf8.size();
    if (total_bytes >
        ::crayon::browser::localization::kMaximumPreferredLocaleBytes) {
      CFRelease(languages);
      return {};
    }
    result.language_tags.push_back(std::move(utf8));
  }
  CFRelease(languages);
  result.api_succeeded = true;
  return result;
}

}  // namespace crayon::browser::cef_shell::process
