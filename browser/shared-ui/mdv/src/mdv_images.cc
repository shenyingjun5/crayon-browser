#include "crayon/browser_mdv/mdv_images.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace crayon::browser_mdv {
namespace {

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

/// Decodes the basic entities the engine emits inside attributes.
std::string DecodeBasicEntities(const std::string& text) {
  std::string out = text;
  auto replace_all = [&out](const char* from, const char* to) {
    const std::size_t from_len = std::char_traits<char>::length(from);
    const std::size_t to_len = std::char_traits<char>::length(to);
    std::size_t pos = 0;
    while ((pos = out.find(from, pos)) != std::string::npos) {
      out.replace(pos, from_len, to);
      pos += to_len;
    }
  };
  replace_all("&amp;", "&");
  replace_all("&lt;", "<");
  replace_all("&gt;", ">");
  replace_all("&quot;", "\"");
  replace_all("&#39;", "'");
  return out;
}

/// Extracts `name="value"` from attribute text (first occurrence).
bool ExtractAttribute(const std::string& attributes, const char* name,
                      std::string* value) {
  const std::string key = std::string(name) + "=\"";
  const std::size_t start = attributes.find(key);
  if (start == std::string::npos) {
    return false;
  }
  const std::size_t value_start = start + key.size();
  const std::size_t value_end = attributes.find('"', value_start);
  if (value_end == std::string::npos) {
    return false;
  }
  *value = attributes.substr(value_start, value_end - value_start);
  return true;
}

/// Lexically normalizes a path: forward slashes, collapse "." and ".."
/// segments.  A ".." that would climb above the root is dropped (the
/// containment check below then refuses the result as not-inside).
std::string NormalizeLexical(const std::string& path) {
  std::string unified = path;
  std::replace(unified.begin(), unified.end(), '\\', '/');
  std::vector<std::string> segments;
  std::string current;
  for (const char c : unified) {
    if (c == '/') {
      if (current == "..") {
        if (!segments.empty()) {
          segments.pop_back();
        }
      } else if (!current.empty() && current != ".") {
        segments.push_back(current);
      }
      current.clear();
    } else {
      current += c;
    }
  }
  if (current == "..") {
    if (!segments.empty()) {
      segments.pop_back();
    }
  } else if (!current.empty() && current != ".") {
    segments.push_back(current);
  }
  const bool rooted = !unified.empty() && unified[0] == '/';
  std::string result = rooted ? "/" : "";
  for (std::size_t i = 0; i < segments.size(); ++i) {
    result += segments[i];
    if (i + 1 < segments.size()) {
      result += '/';
    }
  }
  return result;
}

bool IsAbsolutePath(const std::string& path) {
  return (!path.empty() && path[0] == '/') ||
         (path.size() >= 2 &&
          std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
          path[1] == ':');
}

std::string Placeholder(const std::string& alt, const std::string& src) {
  return "<span class=\"md-img-placeholder\">[图片] " + EscapeHtmlText(alt) +
         " (" + EscapeHtmlText(src) + ")</span>";
}

}  // namespace

bool HasWhitelistedImageExtension(const std::string& path_utf8) {
  const auto dot = path_utf8.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = path_utf8.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
         ext == "webp" || ext == "bmp" || ext == "svg";
}

std::string PreparePreviewHtml(const std::string& html,
                               const std::string& doc_dir_utf8,
                               const LocalImageProbe& probe,
                               std::vector<std::string>* local_images) {
  std::string out;
  std::size_t pos = 0;
  const std::string marker = "<img class=\"md-img\" src=\"mdv-img:";
  while (true) {
    const std::size_t img = html.find(marker, pos);
    if (img == std::string::npos) {
      out.append(html, pos, std::string::npos);
      break;
    }
    out.append(html, pos, img - pos);
    const std::size_t close = html.find('>', img);
    if (close == std::string::npos) {
      out.append(html, img, std::string::npos);
      break;
    }
    const std::string tag = html.substr(img, close - img + 1);
    std::string raw;
    std::string alt;
    static_cast<void>(ExtractAttribute(tag, "data-mdv-raw", &raw));
    static_cast<void>(ExtractAttribute(tag, "alt", &alt));
    raw = DecodeBasicEntities(raw);
    alt = DecodeBasicEntities(alt);
    pos = close + 1;

    if (raw.compare(0, 8, "https://") == 0) {
      // Cloud image: https only, direct load.
      out += "<img class=\"md-img\" src=\"" + EscapeHtmlText(raw) +
             "\" alt=\"" + EscapeHtmlText(alt) + "\">";
      continue;
    }
    if (raw.empty() ||
        (raw.find(':') != std::string::npos && !IsAbsolutePath(raw))) {
      // data:, javascript:, other schemes never load.
      out += Placeholder(alt, raw);
      continue;
    }
    if (doc_dir_utf8.empty()) {
      // No document directory (fixture): local references placeholder.
      out += Placeholder(alt, raw);
      continue;
    }
    const std::string doc_dir = NormalizeLexical(doc_dir_utf8);
    const std::string resolved =
        NormalizeLexical(IsAbsolutePath(raw) ? raw : doc_dir + "/" + raw);
    const bool inside = resolved.size() > doc_dir.size() &&
                        resolved.compare(0, doc_dir.size(), doc_dir) == 0 &&
                        resolved[doc_dir.size()] == '/';
    if (!inside || !HasWhitelistedImageExtension(resolved)) {
      out += Placeholder(alt, raw);
      continue;
    }
    std::uint64_t size = 0;
    if (!probe || !probe(resolved, &size) || size > kMaxLocalImageBytes) {
      out += Placeholder(alt, raw);
      continue;
    }
    const std::size_t index = local_images->size();
    local_images->push_back(resolved);
    out += "<img class=\"md-img\" src=\"/img/" + std::to_string(index) +
           "\" alt=\"" + EscapeHtmlText(alt) + "\">";
  }
  return out;
}

}  // namespace crayon::browser_mdv
