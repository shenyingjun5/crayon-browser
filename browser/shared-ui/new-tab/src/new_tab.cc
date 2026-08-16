#include "crayon/new_tab/new_tab.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <utility>

namespace crayon::browser::new_tab {
namespace {

constexpr std::string_view kHtmlPrefix =
    R"HTML(<!doctype html><html lang=")HTML";
constexpr std::string_view kHtmlHead = R"HTML("><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>)HTML";
constexpr std::string_view kHtmlStyle = R"HTML(</title><style>
:root{color-scheme:light dark;font-family:system-ui,-apple-system,"Segoe UI",sans-serif}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:#f7f8fa;color:#202124}
main{width:min(720px,calc(100% - 48px));margin:0 auto;padding:18vh 0 48px}
h1{font-size:28px;text-align:center;margin:0 0 28px}.search{width:100%;height:48px;border:1px solid #c9ccd1;border-radius:24px;background:#fff;color:#202124;padding:0 20px;font:inherit;box-shadow:0 2px 8px #00000014}
h2{font-size:16px;margin:32px 0 12px}.shortcuts{display:grid;grid-template-columns:repeat(auto-fill,minmax(132px,1fr));gap:12px}.shortcut{display:block;min-height:72px;border-radius:12px;padding:14px;background:#fff;color:inherit;text-decoration:none;overflow-wrap:anywhere}.shortcut:focus-visible,.search:focus-visible{outline:3px solid #1967d2;outline-offset:2px}.cast{margin-top:28px;border:0;border-radius:18px;padding:9px 16px}.private{text-align:center;max-width:520px;margin:30px auto 0}.private p{line-height:1.6;color:#5f6368}
@media(prefers-color-scheme:dark){body{background:#202124;color:#e8eaed}.search,.shortcut{background:#303134;color:#e8eaed;border-color:#5f6368}.private p{color:#bdc1c6}}
</style></head><body><main><h1>)HTML";

bool HasInvalidTextByte(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char byte) {
    return byte < 0x20 || byte == 0x7f;
  });
}

bool IsValidText(std::string_view value, std::size_t maximum_bytes) {
  return !value.empty() && value.size() <= maximum_bytes &&
         value.front() != ' ' && value.back() != ' ' &&
         !HasInvalidTextByte(value);
}

bool IsValidLanguageTag(std::string_view value) {
  return !value.empty() && value.size() <= 16 &&
         std::all_of(value.begin(), value.end(), [](unsigned char byte) {
           return (byte >= 'a' && byte <= 'z') ||
                  (byte >= 'A' && byte <= 'Z') ||
                  (byte >= '0' && byte <= '9') || byte == '-';
         });
}

bool IsValidHost(std::string_view host) {
  constexpr std::size_t kMaximumHostBytes = 253;
  constexpr std::size_t kMaximumHostLabelBytes = 63;
  if (host.empty() || host.size() > kMaximumHostBytes) {
    return false;
  }

  std::size_t label_start = 0;
  while (label_start < host.size()) {
    const std::size_t label_end = host.find('.', label_start);
    const std::string_view label =
        host.substr(label_start, label_end == std::string_view::npos
                                     ? std::string_view::npos
                                     : label_end - label_start);
    if (label.empty() || label.size() > kMaximumHostLabelBytes ||
        label.front() == '-' || label.back() == '-' ||
        !std::all_of(label.begin(), label.end(), [](unsigned char byte) {
          return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                 (byte >= '0' && byte <= '9') || byte == '-';
        })) {
      return false;
    }
    if (label_end == std::string_view::npos) {
      return true;
    }
    label_start = label_end + 1;
  }
  return false;
}

bool IsValidShortcutUrl(std::string_view url) {
  if (url.empty() || url.size() > kMaximumShortcutUrlBytes ||
      HasInvalidTextByte(url) ||
      url.find_first_of("\\<>?#") != std::string_view::npos) {
    return false;
  }

  std::size_t authority_start = 0;
  if (url.compare(0, 7, "http://") == 0) {
    authority_start = 7;
  } else if (url.compare(0, 8, "https://") == 0) {
    authority_start = 8;
  } else {
    return false;
  }

  const std::size_t path_start = url.find('/', authority_start);
  const std::string_view authority =
      path_start == std::string_view::npos
          ? url.substr(authority_start)
          : url.substr(authority_start, path_start - authority_start);
  return authority.find('@') == std::string_view::npos &&
         authority.find(':') == std::string_view::npos &&
         IsValidHost(authority);
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

bool ValidateStrings(const NewTabStrings& strings) {
  const std::array<std::string_view, 6> values = {
      strings.page_title,          strings.search_placeholder,
      strings.shortcuts_heading,   strings.private_heading,
      strings.private_description, strings.cast_label};
  return IsValidLanguageTag(strings.language_tag) &&
         std::all_of(values.begin(), values.end(), [](std::string_view value) {
           return IsValidText(value, kMaximumLocalizedStringBytes);
         });
}

bool AppendBounded(std::string& output, std::string_view value) {
  if (output.size() > kMaximumRenderedPageBytes ||
      value.size() > kMaximumRenderedPageBytes - output.size()) {
    return false;
  }
  output.append(value);
  return true;
}

std::optional<std::string> RenderPage(const NewTabModel& model,
                                      const NewTabStrings& strings) {
  std::string html;
  html.reserve(4096);
  const std::string language_tag = EscapeHtml(strings.language_tag);
  const std::string page_title = EscapeHtml(strings.page_title);
  const std::string search_placeholder = EscapeHtml(strings.search_placeholder);
  if (!AppendBounded(html, kHtmlPrefix) || !AppendBounded(html, language_tag) ||
      !AppendBounded(html, kHtmlHead) || !AppendBounded(html, page_title) ||
      !AppendBounded(html, kHtmlStyle) || !AppendBounded(html, page_title) ||
      !AppendBounded(html,
                     "</h1><input class=\"search\" type=\"text\" disabled "
                     "aria-label=\"") ||
      !AppendBounded(html, search_placeholder) ||
      !AppendBounded(html, "\" placeholder=\"") ||
      !AppendBounded(html, search_placeholder) || !AppendBounded(html, "\">")) {
    return std::nullopt;
  }

  if (model.profile_mode == ProfileMode::kPrivate) {
    if (!AppendBounded(html, "<section class=\"private\"><h2>") ||
        !AppendBounded(html, EscapeHtml(strings.private_heading)) ||
        !AppendBounded(html, "</h2><p>") ||
        !AppendBounded(html, EscapeHtml(strings.private_description)) ||
        !AppendBounded(html, "</p></section>")) {
      return std::nullopt;
    }
  } else if (model.show_shortcuts) {
    if (!AppendBounded(html, "<section><h2>") ||
        !AppendBounded(html, EscapeHtml(strings.shortcuts_heading)) ||
        !AppendBounded(html, "</h2><div class=\"shortcuts\">")) {
      return std::nullopt;
    }
    for (const auto& shortcut : model.shortcuts) {
      if (!AppendBounded(html,
                         "<a class=\"shortcut\" rel=\"noreferrer "
                         "noopener\" href=\"") ||
          !AppendBounded(html, EscapeHtml(shortcut.url)) ||
          !AppendBounded(html, "\">") ||
          !AppendBounded(html, EscapeHtml(shortcut.title)) ||
          !AppendBounded(html, "</a>")) {
        return std::nullopt;
      }
    }
    if (!AppendBounded(html, "</div></section>")) {
      return std::nullopt;
    }
  }

  if (model.show_cast_entry &&
      (!AppendBounded(html,
                      "<button class=\"cast\" type=\"button\" disabled>") ||
       !AppendBounded(html, EscapeHtml(strings.cast_label)) ||
       !AppendBounded(html, "</button>"))) {
    return std::nullopt;
  }
  if (!AppendBounded(html, "</main></body></html>")) {
    return std::nullopt;
  }
  return html;
}

}  // namespace

NewTabModel BuildNewTabModel(
    ProfileMode profile_mode,
    const std::vector<ShortcutCandidate>& shortcut_candidates) {
  NewTabModel model;
  model.profile_mode = profile_mode;
  model.show_cast_entry = true;
  if (profile_mode == ProfileMode::kPrivate) {
    return model;
  }

  std::unordered_set<std::string> accepted_urls;
  model.shortcuts.reserve(
      std::min(shortcut_candidates.size(), kMaximumPinnedShortcuts));
  for (const auto& candidate : shortcut_candidates) {
    if (model.shortcuts.size() >= kMaximumPinnedShortcuts) {
      break;
    }
    if (!IsValidText(candidate.title, kMaximumShortcutTitleBytes) ||
        !IsValidShortcutUrl(candidate.url) ||
        !accepted_urls.insert(candidate.url).second) {
      continue;
    }
    model.shortcuts.push_back({candidate.title, candidate.url});
  }
  model.show_shortcuts = !model.shortcuts.empty();
  return model;
}

NewTabRequestKind ValidateNewTabRequest(std::string_view method,
                                        std::string_view url) noexcept {
  if (url != kNewTabUrl) {
    return NewTabRequestKind::kReject;
  }
  if (method == "GET") {
    return NewTabRequestKind::kGet;
  }
  if (method == "HEAD") {
    return NewTabRequestKind::kHead;
  }
  return NewTabRequestKind::kReject;
}

std::optional<NewTabResource> BuildNewTabResource(
    NewTabRequestKind request_kind, const NewTabModel& model,
    const NewTabStrings& strings) {
  if (request_kind == NewTabRequestKind::kReject || !ValidateStrings(strings)) {
    return std::nullopt;
  }
  std::optional<std::string> body = RenderPage(model, strings);
  if (!body.has_value()) {
    return std::nullopt;
  }
  if (request_kind == NewTabRequestKind::kHead) {
    body->clear();
  }
  return NewTabResource{
      "text/html", "utf-8", "no-store",
      "default-src 'none'; style-src 'unsafe-inline'; img-src data:; "
      "base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
      std::move(*body)};
}

}  // namespace crayon::browser::new_tab
