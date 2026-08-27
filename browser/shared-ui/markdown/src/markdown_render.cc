#include "crayon/browser_markdown/markdown_render.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>

extern "C" {
#include "md4c-html.h"
}

namespace crayon::browser_markdown {
namespace {

// GFM tables + strikethrough + tasklists; permissive autolinks are
// deliberately excluded (bare URLs stay plain text per MDV-01 §6) and
// raw HTML is disabled in both block and inline form.
constexpr unsigned kParserFlags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                                  MD_FLAG_TASKLISTS | MD_FLAG_NOHTMLBLOCKS |
                                  MD_FLAG_NOHTMLSPANS;

void AppendOutput(const MD_CHAR* data, MD_SIZE size, void* user_data) {
  static_cast<std::string*>(user_data)->append(data, size);
}

// --- entity decoding for attribute inspection (md4c escapes &<>"') ---

void DecodeBasicEntities(std::string* value) {
  const std::pair<const char*, const char*> table[] = {{"&amp;", "&"},
                                                       {"&lt;", "<"},
                                                       {"&gt;", ">"},
                                                       {"&quot;", "\""},
                                                       {"&#39;", "'"}};
  for (auto& [from, to] : table) {
    std::string out;
    std::size_t pos = 0;
    while (pos < value->size()) {
      const std::size_t hit = value->find(from, pos);
      if (hit == std::string::npos) {
        out.append(*value, pos, std::string::npos);
        break;
      }
      out.append(*value, pos, hit - pos);
      out.append(to);
      pos = hit + std::char_traits<char>::length(from);
    }
    *value = out;
  }
}

std::string EscapeHtmlText(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

// --- tag scanning helpers ---

struct TagInfo {
  std::string name;  // lower-cased tag name
  bool closing = false;
  bool self_closing = false;
  std::string attributes;  // raw attribute text inside the tag
};

/// Parses the tag starting at `pos` (which must point at '<').
/// Returns false when the bytes do not form a well-formed tag.
bool ParseTag(const std::string& html, std::size_t pos, TagInfo* tag) {
  if (pos >= html.size() || html[pos] != '<') {
    return false;
  }
  std::size_t i = pos + 1;
  if (i < html.size() && html[i] == '/') {
    tag->closing = true;
    ++i;
  }
  std::size_t start = i;
  while (i < html.size() &&
         (std::isalnum(static_cast<unsigned char>(html[i])) != 0)) {
    ++i;
  }
  if (i == start) {
    return false;
  }
  tag->name.clear();
  for (std::size_t c = start; c < i; ++c) {
    tag->name.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(html[c]))));
  }
  const std::size_t close = html.find('>', i);
  if (close == std::string::npos) {
    return false;
  }
  std::string inner = html.substr(i, close - i);
  if (!inner.empty() && inner.back() == '/') {
    tag->self_closing = true;
    inner.pop_back();
  }
  tag->attributes = std::move(inner);
  return true;
}

/// Extracts the value of `attribute` inside raw attribute text; the
/// value stays entity-encoded as generated.
bool ExtractAttribute(const std::string& attributes,
                      const std::string& attribute, std::string* value) {
  const std::string needle = attribute + "=\"";
  std::size_t pos = 0;
  while (true) {
    pos = attributes.find(needle, pos);
    if (pos == std::string::npos) {
      return false;
    }
    // Attribute position must start a fresh attribute (whitespace before).
    if (pos != 0 &&
        (std::isspace(static_cast<unsigned char>(attributes[pos - 1])) == 0)) {
      ++pos;
      continue;
    }
    const std::size_t value_start = pos + needle.size();
    const std::size_t value_end = attributes.find('"', value_start);
    if (value_end == std::string::npos) {
      return false;
    }
    *value = attributes.substr(value_start, value_end - value_start);
    return true;
  }
}

bool IsAllowedScheme(std::string url) {
  DecodeBasicEntities(&url);
  // Skip leading whitespace; md4c does not normally emit any.
  while (!url.empty() &&
         std::isspace(static_cast<unsigned char>(url.front())) != 0) {
    url.erase(url.begin());
  }
  auto has_prefix = [&url](const char* prefix) {
    return url.size() >= std::char_traits<char>::length(prefix) &&
           url.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
  };
  return has_prefix("http://") || has_prefix("https://") ||
         has_prefix("mailto:");
}

/// Rewrites `<a href="...">…</a>` anchors: allowed schemes keep the
/// anchor, everything else degrades to its plain text.
std::string FilterAnchors(const std::string& html, bool* violation) {
  std::string out;
  std::size_t pos = 0;
  while (pos < html.size()) {
    const std::size_t anchor = html.find("<a ", pos);
    if (anchor == std::string::npos) {
      out.append(html, pos, std::string::npos);
      break;
    }
    out.append(html, pos, anchor - pos);
    TagInfo tag;
    if (!ParseTag(html, anchor, &tag) || tag.name != "a" || tag.closing) {
      *violation = true;
      return out;
    }
    const std::size_t close = html.find("</a>", anchor);
    if (close == std::string::npos) {
      *violation = true;
      return out;
    }
    std::string href;
    const bool has_href = ExtractAttribute(tag.attributes, "href", &href);
    if (has_href && !IsAllowedScheme(href)) {
      // Degrade to inner text; the anchor and href disappear entirely.
      // "<a" is 2 bytes and the attribute text already includes the
      // leading space before the first attribute.
      const std::size_t inner_start = anchor + 2 + tag.attributes.size() + 1;
      out.append(html.substr(inner_start, close - inner_start));
    } else {
      out.append(html, anchor, close + 4 - anchor);
    }
    pos = close + 4;
  }
  return out;
}

/// Replaces every `<img …>` with an intermediate marker carrying the
/// raw reference in `data-mdv-raw`; the Browser-process preview
/// pipeline (MDV-13) classifies each reference into a cloud URL, a
/// validated local opaque index, or the placeholder.  The marker never
/// reaches the page as-is.
std::string ReplaceImages(const std::string& html) {
  std::string out;
  std::size_t pos = 0;
  std::size_t image_index = 0;
  while (pos < html.size()) {
    const std::size_t img = html.find("<img", pos);
    if (img == std::string::npos) {
      out.append(html, pos, std::string::npos);
      break;
    }
    out.append(html, pos, img - pos);
    TagInfo tag;
    if (!ParseTag(html, img, &tag) || tag.name != "img") {
      // Not an image tag (e.g. <input...>); copy the single byte and
      // rescan from the next position.
      out.push_back(html[img]);
      pos = img + 1;
      continue;
    }
    std::string src;
    std::string alt;
    static_cast<void>(ExtractAttribute(tag.attributes, "src", &src));
    static_cast<void>(ExtractAttribute(tag.attributes, "alt", &alt));
    DecodeBasicEntities(&src);
    DecodeBasicEntities(&alt);
    out += "<img class=\"md-img\" src=\"mdv-img:";
    out += std::to_string(image_index++);
    out += "\" data-mdv-raw=\"";
    out += EscapeHtmlText(src);
    out += "\" alt=\"";
    out += EscapeHtmlText(alt);
    out += "\">";
    // Skip past the self-closing tag.
    const std::size_t close = html.find('>', img);
    pos = close == std::string::npos ? html.size() : close + 1;
  }
  return out;
}

const std::array<const char*, 28>& AllowedTags() {
  static const std::array<const char*, 28> tags = {
      "h1", "h2",     "h3",         "h4",    "h5",   "h6",    "p",
      "br", "hr",     "blockquote", "pre",   "code", "ul",    "ol",
      "li", "table",  "thead",      "tbody", "tr",   "th",    "td",
      "em", "strong", "del",        "a",     "span", "input", "img"};
  return tags;
}

const std::array<const char*, 10>& AllowedAttributes() {
  static const std::array<const char*, 10> attributes = {
      "href",    "title",    "align", "class",        "type",
      "checked", "disabled", "src",   "data-mdv-raw", "alt"};
  return attributes;
}

bool IsAllowedTag(const std::string& name) {
  for (const char* tag : AllowedTags()) {
    if (name == tag) {
      return true;
    }
  }
  return false;
}

/// Validates every attribute in raw attribute text against the
/// whitelist.
bool AttributesAllowed(const std::string& attributes) {
  std::size_t pos = 0;
  while (pos < attributes.size()) {
    while (pos < attributes.size() &&
           std::isspace(static_cast<unsigned char>(attributes[pos])) != 0) {
      ++pos;
    }
    if (pos >= attributes.size()) {
      return true;
    }
    std::string name;
    std::size_t next = pos;
    const std::size_t eq = attributes.find('=', pos);
    const std::size_t space = attributes.find_first_of(" \t\n", pos);
    const bool valueless =
        eq == std::string::npos || (space != std::string::npos && space < eq);
    if (valueless) {
      // Boolean attribute (e.g. `disabled`): allowed only when the
      // name itself is whitelisted.
      name = attributes.substr(
          pos, space == std::string::npos ? std::string::npos : space - pos);
      next = space == std::string::npos ? attributes.size() : space;
    } else {
      name = attributes.substr(pos, eq - pos);
    }
    bool allowed = false;
    for (const char* candidate : AllowedAttributes()) {
      if (name == candidate) {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      return false;
    }
    if (valueless) {
      pos = next;
      continue;
    }
    // Skip the quoted value.
    const std::size_t quote = attributes.find('"', eq);
    if (quote == std::string::npos) {
      return false;
    }
    const std::size_t value_end = attributes.find('"', quote + 1);
    if (value_end == std::string::npos) {
      return false;
    }
    pos = value_end + 1;
  }
  return true;
}

/// Final gate: every tag in the generated output must be whitelisted
/// with whitelisted attributes.  Text outside tags is skipped; any
/// '<' that does not start a whitelisted tag is a violation.
bool OutputWithinWhitelist(const std::string& html) {
  std::size_t pos = 0;
  while (pos < html.size()) {
    const std::size_t lt = html.find('<', pos);
    if (lt == std::string::npos) {
      return true;
    }
    TagInfo tag;
    if (!ParseTag(html, lt, &tag)) {
      return false;  // stray '<' would have been escaped by md4c
    }
    if (!IsAllowedTag(tag.name) || !AttributesAllowed(tag.attributes)) {
      return false;
    }
    // "</span>" = 2 + name + attrs + 1; "<span ...>" = 1 + name +
    // attrs + (self-closing '/') + 1.
    pos = lt + (tag.closing ? 2 : 1) + tag.name.size() + tag.attributes.size() +
          (tag.self_closing ? 1 : 0) + 1;
  }
  return true;
}

std::string NormalizeInput(const std::string& input) {
  std::string data = input;
  // BOM strip.
  if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
      static_cast<unsigned char>(data[1]) == 0xBB &&
      static_cast<unsigned char>(data[2]) == 0xBF) {
    data.erase(0, 3);
  }
  // CRLF / CR → LF.
  std::string out;
  out.reserve(data.size());
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] == '\r') {
      if (i + 1 < data.size() && data[i + 1] == '\n') {
        ++i;
      }
      out.push_back('\n');
    } else {
      out.push_back(data[i]);
    }
  }
  return out;
}

}  // namespace

bool IsValidUtf8(const std::string& data) {
  std::size_t i = 0;
  const auto byte = [&data](std::size_t index) {
    return static_cast<unsigned char>(data[index]);
  };
  while (i < data.size()) {
    const unsigned char lead = byte(i);
    std::size_t length = 0;
    std::uint32_t code = 0;
    if (lead <= 0x7F) {
      ++i;
      continue;
    }
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
      code = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
      code = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
      code = lead & 0x07;
    } else {
      return false;  // continuation byte as lead or 0xF8+
    }
    if (i + length > data.size()) {
      return false;
    }
    for (std::size_t c = 1; c < length; ++c) {
      if ((byte(i + c) & 0xC0) != 0x80) {
        return false;
      }
      code = (code << 6) | (byte(i + c) & 0x3F);
    }
    // Overlong and range checks.
    if ((length == 2 && code < 0x80) || (length == 3 && code < 0x800) ||
        (length == 4 && code < 0x10000) || code > 0x10'FFFF ||
        (code >= 0xD800 && code <= 0xDFFF)) {
      return false;
    }
    i += length;
  }
  return true;
}

std::string RenderMarkdownToSafeHtml(const std::string& input,
                                     RenderStatus* status) {
  auto finish = [&](RenderStatus code, std::string&& output) {
    if (status != nullptr) {
      *status = code;
    }
    return code == RenderStatus::kOk ? std::move(output) : std::string();
  };
  if (input.size() > kMaxInputBytes) {
    return finish(RenderStatus::kInputTooLarge, {});
  }
  const std::string normalized = NormalizeInput(input);
  if (!IsValidUtf8(normalized)) {
    return finish(RenderStatus::kInvalidUtf8, {});
  }

  std::string generated;
  const int parse_result =
      md_html(normalized.data(), static_cast<MD_SIZE>(normalized.size()),
              AppendOutput, &generated, kParserFlags, 0);
  if (parse_result != 0) {
    return finish(RenderStatus::kOutputPolicyViolation, {});
  }

  bool violation = false;
  std::string filtered = FilterAnchors(generated, &violation);
  filtered = ReplaceImages(filtered);
  if (violation || !OutputWithinWhitelist(filtered)) {
    return finish(RenderStatus::kOutputPolicyViolation, {});
  }
  return finish(RenderStatus::kOk, std::move(filtered));
}

}  // namespace crayon::browser_markdown
