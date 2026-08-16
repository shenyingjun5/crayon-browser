#include "crayon/browser_new_tab/new_tab_page.h"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace crayon::browser_new_tab {
namespace {

constexpr std::size_t kMaximumShortcutIdBytes = 48;
constexpr std::size_t kMaximumShortcutTitleBytes = 128;
constexpr std::size_t kMaximumShortcutUrlBytes = 2048;

bool IsAsciiDigit(unsigned char character) {
  return character >= '0' && character <= '9';
}

bool IsAsciiAlpha(unsigned char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z');
}

bool IsAsciiAlphaNumeric(unsigned char character) {
  return IsAsciiAlpha(character) || IsAsciiDigit(character);
}

bool IsAsciiHexDigit(unsigned char character) {
  return IsAsciiDigit(character) || (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

unsigned char ToAsciiLower(unsigned char character) {
  return character >= 'A' && character <= 'Z'
             ? static_cast<unsigned char>(character + ('a' - 'A'))
             : character;
}

bool IsValidUtf8WithoutControls(std::string_view value) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    const auto lead = static_cast<unsigned char>(value[offset]);
    std::uint32_t code_point = 0;
    std::size_t length = 0;
    if (lead <= 0x7F) {
      code_point = lead;
      length = 1;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
      code_point = lead & 0x1F;
      length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      code_point = lead & 0x0F;
      length = 3;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      code_point = lead & 0x07;
      length = 4;
    } else {
      return false;
    }
    if (offset + length > value.size()) {
      return false;
    }
    for (std::size_t index = 1; index < length; ++index) {
      const auto continuation =
          static_cast<unsigned char>(value[offset + index]);
      if ((continuation & 0xC0) != 0x80) {
        return false;
      }
      code_point = (code_point << 6) | (continuation & 0x3F);
    }
    if ((length == 3 && code_point < 0x800) ||
        (length == 4 && code_point < 0x10000) || code_point > 0x10FFFF ||
        (code_point >= 0xD800 && code_point <= 0xDFFF) || code_point < 0x20 ||
        code_point == 0x7F) {
      return false;
    }
    offset += length;
  }
  return true;
}

bool HasNonWhitespace(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char character) {
    return character > 0x7F ||
           (character != ' ' && character != '\t' && character != '\n' &&
            character != '\r' && character != '\v' && character != '\f');
  });
}

bool IsValidShortcutId(std::string_view id) {
  if (id.empty() || id.size() > kMaximumShortcutIdBytes) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](unsigned char character) {
    return IsAsciiAlphaNumeric(character) || character == '-' ||
           character == '_' || character == '.';
  });
}

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](unsigned char lhs, unsigned char rhs) {
                      return ToAsciiLower(lhs) == ToAsciiLower(rhs);
                    });
}

bool IsValidPort(std::string_view port) {
  if (port.empty() || port.size() > 5 ||
      !std::all_of(port.begin(), port.end(), [](unsigned char character) {
        return IsAsciiDigit(character);
      })) {
    return false;
  }
  unsigned int number = 0;
  for (const char character : port) {
    number = number * 10 + static_cast<unsigned int>(character - '0');
  }
  return number > 0 && number <= 65535;
}

bool IsValidDnsOrIpv4Host(std::string_view host) {
  if (host.empty() || host.size() > 253) {
    return false;
  }
  std::size_t label_start = 0;
  while (label_start < host.size()) {
    const std::size_t label_end = host.find('.', label_start);
    const std::size_t length =
        (label_end == std::string_view::npos ? host.size() : label_end) -
        label_start;
    if (length == 0 || length > 63 ||
        !IsAsciiAlphaNumeric(static_cast<unsigned char>(host[label_start])) ||
        !IsAsciiAlphaNumeric(
            static_cast<unsigned char>(host[label_start + length - 1]))) {
      return false;
    }
    for (std::size_t index = label_start; index < label_start + length;
         ++index) {
      const auto character = static_cast<unsigned char>(host[index]);
      if (!IsAsciiAlphaNumeric(character) && character != '-') {
        return false;
      }
    }
    if (label_end == std::string_view::npos) {
      return true;
    }
    label_start = label_end + 1;
  }
  return false;
}

bool IsValidHost(std::string_view authority) {
  if (authority.empty() || authority.find('@') != std::string_view::npos) {
    return false;
  }
  std::string_view host = authority;
  if (authority.front() == '[') {
    const std::size_t close = authority.find(']');
    if (close == std::string_view::npos || close == 1) {
      return false;
    }
    host = authority.substr(1, close - 1);
    if (!std::all_of(host.begin(), host.end(), [](unsigned char character) {
          return IsAsciiHexDigit(character) || character == ':' ||
                 character == '.';
        })) {
      return false;
    }
    if (close + 1 == authority.size()) {
      return true;
    }
    return authority[close + 1] == ':' &&
           IsValidPort(authority.substr(close + 2));
  }
  const std::size_t colon = authority.rfind(':');
  if (colon != std::string_view::npos) {
    if (!IsValidPort(authority.substr(colon + 1))) {
      return false;
    }
    host = authority.substr(0, colon);
  }
  return IsValidDnsOrIpv4Host(host);
}

bool IsValidHttpUrl(std::string_view url) {
  if (url.empty() || url.size() > kMaximumShortcutUrlBytes) {
    return false;
  }
  if (!std::all_of(url.begin(), url.end(), [](unsigned char character) {
        return character >= 0x21 && character <= 0x7E && character != '\\' &&
               character != '<' && character != '>' && character != '"' &&
               character != '\'';
      })) {
    return false;
  }
  const std::size_t separator = url.find("://");
  if (separator == std::string_view::npos ||
      (!EqualsAsciiInsensitive(url.substr(0, separator), "http") &&
       !EqualsAsciiInsensitive(url.substr(0, separator), "https"))) {
    return false;
  }
  const std::size_t authority_start = separator + 3;
  const std::size_t authority_end = url.find_first_of("/?#", authority_start);
  const std::string_view authority =
      url.substr(authority_start, authority_end == std::string_view::npos
                                      ? std::string_view::npos
                                      : authority_end - authority_start);
  return IsValidHost(authority);
}

bool IsValidShortcut(const ShortcutEntry& shortcut) {
  return IsValidShortcutId(shortcut.id) && !shortcut.title.empty() &&
         shortcut.title.size() <= kMaximumShortcutTitleBytes &&
         IsValidUtf8WithoutControls(shortcut.title) &&
         HasNonWhitespace(shortcut.title) && IsValidHttpUrl(shortcut.url);
}

std::string EscapeHtml(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '&':
        escaped.append("&amp;");
        break;
      case '<':
        escaped.append("&lt;");
        break;
      case '>':
        escaped.append("&gt;");
        break;
      case '"':
        escaped.append("&quot;");
        break;
      case '\'':
        escaped.append("&#39;");
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  return escaped;
}

std::string_view FirstUtf8CodePoint(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const auto lead = static_cast<unsigned char>(value.front());
  std::size_t length = 1;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
  }
  return value.substr(0, std::min(length, value.size()));
}

}  // namespace

NewTabPageModel BuildNewTabPageModel(NewTabProfileMode profile_mode,
                                     const ShortcutConfig& config) {
  NewTabPageModel model;
  model.profile_mode = profile_mode;
  if (config.schema_version != kShortcutConfigSchemaVersion) {
    model.config_status = NewTabConfigStatus::kUnsupportedVersion;
    return model;
  }
  if (config.entries.size() > kMaximumShortcutCount) {
    model.config_status = NewTabConfigStatus::kTooManyEntries;
    return model;
  }
  std::unordered_set<std::string> ids;
  for (const ShortcutEntry& shortcut : config.entries) {
    if (!IsValidShortcut(shortcut)) {
      model.config_status = NewTabConfigStatus::kInvalidEntry;
      return model;
    }
    if (!ids.insert(shortcut.id).second) {
      model.config_status = NewTabConfigStatus::kDuplicateId;
      return model;
    }
  }
  if (profile_mode == NewTabProfileMode::kRegular) {
    model.shortcuts = config.entries;
  }
  return model;
}

NewTabRoute ClassifyNewTabRequest(const NewTabRequestParts& request) {
  if (request.scheme != kNewTabScheme || request.host != kNewTabHost ||
      request.has_credentials || request.has_port || request.has_query ||
      request.has_fragment) {
    return {};
  }
  const bool is_get = request.method == "GET";
  const bool is_head = request.method == "HEAD";
  if (!is_get && !is_head) {
    return {NewTabResourceKind::kMethodNotAllowed, 405, false};
  }
  if (request.path == "/" || request.path == "/index.html") {
    return {NewTabResourceKind::kDocument, 200, is_get};
  }
  if (request.path == "/styles.css") {
    return {NewTabResourceKind::kStylesheet, 200, is_get};
  }
  return {NewTabResourceKind::kNotFound, 404, false};
}

std::string RenderNewTabDocument(const NewTabPageModel& model,
                                 const NewTabPageStrings& strings) {
  const bool incognito = model.profile_mode == NewTabProfileMode::kIncognito;
  const std::string& heading =
      incognito ? strings.incognito_heading : strings.regular_heading;
  const std::string& description =
      incognito ? strings.incognito_description : strings.regular_description;

  std::ostringstream document;
  document << "<!doctype html><html lang=\"" << EscapeHtml(strings.language)
           << "\" data-profile-mode=\"" << (incognito ? "incognito" : "regular")
           << "\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
              "content=\"width=device-width,initial-scale=1\"><title>"
           << EscapeHtml(strings.document_title)
           << "</title><link rel=\"stylesheet\" href=\"" << kNewTabStylesheetUrl
           << "\"></head><body><main><header class=\"hero\"><span "
              "class=\"brand-mark\" aria-hidden=\"true\"></span><p "
              "class=\"eyebrow\">"
           << EscapeHtml(strings.document_title) << "</p><h1>"
           << EscapeHtml(heading) << "</h1><p class=\"description\">"
           << EscapeHtml(description) << "</p><p class=\"omnibox-hint\">"
           << EscapeHtml(strings.omnibox_hint) << "</p></header>";

  if (!incognito) {
    document << "<section aria-labelledby=\"shortcuts-heading\"><h2 "
                "id=\"shortcuts-heading\">"
             << EscapeHtml(strings.shortcuts_heading) << "</h2>";
    if (model.config_status != NewTabConfigStatus::kAccepted) {
      document << "<p class=\"empty-state\">"
               << EscapeHtml(strings.config_error) << "</p>";
    } else if (model.shortcuts.empty()) {
      document << "<p class=\"empty-state\">"
               << EscapeHtml(strings.empty_shortcuts) << "</p>";
    } else {
      document << "<ul class=\"shortcut-grid\">";
      for (const ShortcutEntry& shortcut : model.shortcuts) {
        document << "<li><a class=\"shortcut\" href=\""
                 << EscapeHtml(shortcut.url)
                 << "\" rel=\"noreferrer\"><span class=\"shortcut-mark\" "
                    "aria-hidden=\"true\">"
                 << EscapeHtml(FirstUtf8CodePoint(shortcut.title))
                 << "</span><span>" << EscapeHtml(shortcut.title)
                 << "</span></a></li>";
      }
      document << "</ul>";
    }
    document << "</section>";
  }
  document << "</main></body></html>";
  return document.str();
}

std::string RenderNewTabStylesheet() {
  return R"css(:root {
  color-scheme: light dark;
  --page: #f8f7fb;
  --surface: #ffffff;
  --text: #202124;
  --muted: #5f6368;
  --accent: #6d4aff;
  --accent-soft: #eee9ff;
  --border: #e3e1e8;
  font-family: "Segoe UI", system-ui, sans-serif;
}
* { box-sizing: border-box; }
body { margin: 0; min-height: 100vh; background: var(--page); color: var(--text); }
main { width: min(760px, calc(100% - 48px)); margin: 0 auto; padding: 12vh 0 64px; }
.hero { margin-bottom: 48px; }
.brand-mark { display: block; width: 36px; height: 36px; margin-bottom: 20px; border-radius: 12px; background: var(--accent); box-shadow: inset -9px -9px 0 var(--accent-soft); }
.eyebrow { margin: 0 0 10px; color: var(--accent); font-size: 13px; font-weight: 650; letter-spacing: .04em; }
h1 { margin: 0; font-size: clamp(32px, 6vw, 54px); line-height: 1.08; letter-spacing: -.035em; }
.description { max-width: 570px; margin: 18px 0 0; color: var(--muted); font-size: 17px; line-height: 1.6; }
.omnibox-hint { margin: 26px 0 0; color: var(--muted); font-size: 13px; }
h2 { margin: 0 0 16px; font-size: 15px; letter-spacing: .01em; }
.shortcut-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(168px, 1fr)); gap: 12px; margin: 0; padding: 0; list-style: none; }
.shortcut { display: flex; align-items: center; gap: 12px; min-height: 64px; padding: 12px; border: 1px solid var(--border); border-radius: 14px; background: var(--surface); color: var(--text); text-decoration: none; }
.shortcut:hover, .shortcut:focus-visible { border-color: var(--accent); outline: 2px solid transparent; box-shadow: 0 4px 16px rgb(50 35 110 / 12%); }
.shortcut-mark { display: grid; flex: 0 0 36px; height: 36px; place-items: center; border-radius: 11px; background: var(--accent-soft); color: var(--accent); font-weight: 700; }
.empty-state { margin: 0; padding: 22px; border: 1px dashed var(--border); border-radius: 14px; color: var(--muted); text-align: center; }
@media (prefers-color-scheme: dark) {
  :root { --page: #17161a; --surface: #232126; --text: #f1eff5; --muted: #b4b0bb; --accent: #b7a5ff; --accent-soft: #493d75; --border: #3b3840; }
}
@media (max-width: 520px) {
  main { width: min(100% - 32px, 760px); padding-top: 64px; }
}
)css";
}

}  // namespace crayon::browser_new_tab
