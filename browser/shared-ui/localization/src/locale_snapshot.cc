#include "crayon/browser_localization/locale_snapshot.h"

#include <array>

namespace crayon::browser::localization {
namespace {

constexpr LocaleSnapshot kEnUsSnapshot{AppLocale::kEnUs, "en-US", "en-US",
                                       "en-US", "en-US,en"};
constexpr LocaleSnapshot kZhCnSnapshot{AppLocale::kZhCn, "zh-CN", "zh-CN",
                                       "zh-CN", "zh-CN,zh,en-US,en"};
constexpr LocaleSnapshot kZhTwSnapshot{AppLocale::kZhTw, "zh-TW", "zh-TW",
                                       "zh-TW", "zh-TW,zh,en-US,en"};

bool IsAsciiAlpha(char value) noexcept {
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z');
}

bool IsAsciiDigit(char value) noexcept {
  return value >= '0' && value <= '9';
}

char LowerAscii(char value) noexcept {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value - 'A' + 'a');
  }
  return value;
}

bool NormalizeTag(std::string_view input,
                  std::array<char, kMaximumLocaleTagBytes>& output,
                  std::size_t* output_size) noexcept {
  if (input.empty() || input.size() > kMaximumLocaleTagBytes ||
      output_size == nullptr) {
    return false;
  }
  bool previous_separator = true;
  for (std::size_t index = 0; index < input.size(); ++index) {
    const char value = input[index];
    if (value == '-' || value == '_') {
      if (previous_separator) {
        return false;
      }
      output[index] = '-';
      previous_separator = true;
      continue;
    }
    if (!IsAsciiAlpha(value) && !IsAsciiDigit(value)) {
      return false;
    }
    output[index] = LowerAscii(value);
    previous_separator = false;
  }
  if (previous_separator) {
    return false;
  }
  *output_size = input.size();
  return true;
}

std::string_view FirstSubtag(std::string_view tag) noexcept {
  const std::size_t first_separator = tag.find('-');
  if (first_separator == std::string_view::npos) {
    return tag;
  }
  return tag.substr(0, first_separator);
}

std::string_view SecondSubtag(std::string_view tag) noexcept {
  const std::size_t first_separator = tag.find('-');
  if (first_separator == std::string_view::npos) {
    return {};
  }
  const std::size_t start = first_separator + 1;
  const std::size_t second_separator = tag.find('-', start);
  if (second_separator == std::string_view::npos) {
    return tag.substr(start);
  }
  return tag.substr(start, second_separator - start);
}

}  // namespace

LocaleSnapshot SnapshotFor(AppLocale locale) noexcept {
  switch (locale) {
    case AppLocale::kZhCn:
      return kZhCnSnapshot;
    case AppLocale::kZhTw:
      return kZhTwSnapshot;
    case AppLocale::kEnUs:
      return kEnUsSnapshot;
  }
  return kEnUsSnapshot;
}

std::optional<AppLocale> ResolveLocaleTag(std::string_view tag) noexcept {
  std::array<char, kMaximumLocaleTagBytes> normalized_storage{};
  std::size_t normalized_size = 0;
  if (!NormalizeTag(tag, normalized_storage, &normalized_size)) {
    return std::nullopt;
  }
  const std::string_view normalized(normalized_storage.data(), normalized_size);
  const std::string_view language = FirstSubtag(normalized);
  if (language == "en") {
    return AppLocale::kEnUs;
  }
  if (language != "zh") {
    return std::nullopt;
  }
  const std::string_view subtag = SecondSubtag(normalized);
  if (subtag.empty()) {
    return AppLocale::kZhCn;
  }
  if (subtag == "hans" || subtag == "cn" || subtag == "sg") {
    return AppLocale::kZhCn;
  }
  if (subtag == "hant" || subtag == "tw" || subtag == "hk" ||
      subtag == "mo") {
    return AppLocale::kZhTw;
  }
  return std::nullopt;
}

LocaleSnapshot ResolveLocaleSnapshot(
    const std::vector<std::string>& preferred_languages) noexcept {
  if (preferred_languages.size() > kMaximumPreferredLocaleCount) {
    return kEnUsSnapshot;
  }
  std::size_t total_bytes = 0;
  for (const std::string& tag : preferred_languages) {
    if (tag.size() > kMaximumPreferredLocaleBytes - total_bytes) {
      return kEnUsSnapshot;
    }
    total_bytes += tag.size();
  }
  for (const std::string& tag : preferred_languages) {
    if (const std::optional<AppLocale> locale = ResolveLocaleTag(tag)) {
      return SnapshotFor(*locale);
    }
  }
  return kEnUsSnapshot;
}

}  // namespace crayon::browser::localization
