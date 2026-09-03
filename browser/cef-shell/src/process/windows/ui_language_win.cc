#include "process/windows/ui_language_win.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crayon::browser::cef_shell::process {
namespace {

constexpr ULONG kMaximumLanguageCount =
    static_cast<ULONG>(
        ::crayon::browser::localization::kMaximumPreferredLocaleCount);
constexpr ULONG kMaximumMultiStringCharacters =
    static_cast<ULONG>(
        ::crayon::browser::localization::kMaximumPreferredLocaleBytes + 2U);

bool ParseMultiString(const std::vector<wchar_t>& buffer,
                      ULONG expected_count,
                      std::vector<std::wstring>* language_tags) {
  if (!language_tags || expected_count == 0 ||
      expected_count > kMaximumLanguageCount || buffer.size() < 2 ||
      buffer.back() != L'\0') {
    return false;
  }

  std::size_t offset = 0;
  language_tags->clear();
  language_tags->reserve(expected_count);
  for (ULONG index = 0; index < expected_count; ++index) {
    if (offset >= buffer.size() || buffer[offset] == L'\0') {
      return false;
    }
    const std::size_t begin = offset;
    while (offset < buffer.size() && buffer[offset] != L'\0') {
      ++offset;
    }
    if (offset >= buffer.size()) {
      return false;
    }
    language_tags->emplace_back(buffer.data() + begin, offset - begin);
    ++offset;
  }

  if (offset >= buffer.size() || buffer[offset] != L'\0') {
    return false;
  }
  for (; offset < buffer.size(); ++offset) {
    if (buffer[offset] != L'\0') {
      return false;
    }
  }
  return true;
}

bool WideTagToUtf8(std::wstring_view tag, std::string* utf8) {
  if (!utf8 || tag.empty() || tag.size() >
                                  ::crayon::browser::localization::
                                      kMaximumPreferredLocaleBytes) {
    return false;
  }
  const int tag_length = static_cast<int>(tag.size());
  const int utf8_length =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, tag.data(),
                          tag_length, nullptr, 0, nullptr, nullptr);
  if (utf8_length <= 0 ||
      static_cast<std::size_t>(utf8_length) >
          ::crayon::browser::localization::kMaximumLocaleTagBytes) {
    return false;
  }
  utf8->assign(static_cast<std::size_t>(utf8_length), '\0');
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, tag.data(),
                             tag_length, utf8->data(), utf8_length, nullptr,
                             nullptr) == utf8_length;
}

}  // namespace

WindowsPreferredUiLanguages ReadWindowsPreferredUiLanguages() {
  ULONG language_count = 0;
  ULONG buffer_length = 0;
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &language_count, nullptr,
                                   &buffer_length) ||
      language_count == 0 || language_count > kMaximumLanguageCount ||
      buffer_length < 2 || buffer_length > kMaximumMultiStringCharacters) {
    return {};
  }

  std::vector<wchar_t> buffer(buffer_length, L'\0');
  ULONG actual_count = language_count;
  ULONG actual_length = buffer_length;
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &actual_count,
                                   buffer.data(), &actual_length) ||
      actual_count == 0 || actual_count > kMaximumLanguageCount ||
      actual_length < 2 || actual_length > buffer.size()) {
    return {};
  }
  buffer.resize(actual_length);

  WindowsPreferredUiLanguages result;
  if (!ParseMultiString(buffer, actual_count, &result.language_tags)) {
    return {};
  }
  result.api_succeeded = true;
  return result;
}

::crayon::browser::localization::LocaleSnapshot ResolveWindowsLocaleSnapshot(
    const WindowsPreferredUiLanguages& preferred_languages) {
  using ::crayon::browser::localization::AppLocale;
  using ::crayon::browser::localization::ResolveLocaleSnapshot;
  using ::crayon::browser::localization::SnapshotFor;
  if (!preferred_languages.api_succeeded ||
      preferred_languages.language_tags.empty() ||
      preferred_languages.language_tags.size() > kMaximumLanguageCount) {
    return SnapshotFor(AppLocale::kEnUs);
  }

  std::vector<std::string> utf8_tags;
  utf8_tags.reserve(preferred_languages.language_tags.size());
  std::size_t total_bytes = 0;
  for (const std::wstring& tag : preferred_languages.language_tags) {
    std::string utf8;
    if (!WideTagToUtf8(tag, &utf8)) {
      return SnapshotFor(AppLocale::kEnUs);
    }
    total_bytes += utf8.size();
    if (total_bytes >
        ::crayon::browser::localization::kMaximumPreferredLocaleBytes) {
      return SnapshotFor(AppLocale::kEnUs);
    }
    utf8_tags.push_back(std::move(utf8));
  }
  return ResolveLocaleSnapshot(utf8_tags);
}

}  // namespace crayon::browser::cef_shell::process
