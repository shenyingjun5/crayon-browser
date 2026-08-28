#include "crayon/browser_mdv/mdv_transform.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace crayon::browser_mdv {
namespace {

struct LineRange {
  std::size_t start = 0;
  std::size_t end = 0;
};

struct ParsedLine {
  std::string body;
  std::string ending;
};

bool NextUtf8(std::string_view text, std::size_t at, std::size_t* bytes,
              std::size_t* utf16_units) {
  if (at >= text.size()) return false;
  const unsigned char lead = static_cast<unsigned char>(text[at]);
  std::size_t length = 0;
  std::uint32_t code_point = 0;
  if (lead < 0x80) {
    length = 1;
    code_point = lead;
  } else if ((lead & 0xE0) == 0xC0) {
    length = 2;
    code_point = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    length = 3;
    code_point = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    length = 4;
    code_point = lead & 0x07;
  } else {
    return false;
  }
  if (at + length > text.size()) return false;
  for (std::size_t i = 1; i < length; ++i) {
    const unsigned char next = static_cast<unsigned char>(text[at + i]);
    if ((next & 0xC0) != 0x80) return false;
    code_point = (code_point << 6) | (next & 0x3F);
  }
  const std::uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
  if (code_point < minimum[length] || code_point > 0x10FFFF ||
      (code_point >= 0xD800 && code_point <= 0xDFFF)) {
    return false;
  }
  *bytes = length;
  *utf16_units = code_point > 0xFFFF ? 2 : 1;
  return true;
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

LineRange SelectedLines(std::string_view text, std::size_t start,
                        std::size_t end) {
  const std::size_t before =
      start == 0 ? std::string_view::npos : text.rfind('\n', start - 1);
  const std::size_t line_start = before == std::string_view::npos ? 0 : before + 1;
  std::size_t probe = end;
  if (end > start && end <= text.size() && text[end - 1] == '\n') {
    --probe;
  }
  const std::size_t newline = text.find('\n', probe);
  return {line_start, newline == std::string_view::npos ? text.size() : newline};
}

std::vector<ParsedLine> SplitLines(std::string_view block) {
  std::vector<ParsedLine> lines;
  std::size_t cursor = 0;
  while (cursor <= block.size()) {
    const std::size_t newline = block.find('\n', cursor);
    const std::size_t end = newline == std::string_view::npos ? block.size() : newline;
    std::string body(block.substr(cursor, end - cursor));
    std::string ending;
    if (!body.empty() && body.back() == '\r') {
      body.pop_back();
      ending = "\r";
    }
    if (newline != std::string_view::npos) {
      ending += "\n";
    }
    lines.push_back({std::move(body), std::move(ending)});
    if (newline == std::string_view::npos) {
      break;
    }
    cursor = newline + 1;
  }
  return lines;
}

std::size_t IndentEnd(std::string_view line) {
  std::size_t at = 0;
  while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
    ++at;
  }
  return at;
}

std::size_t HeadingPrefix(std::string_view content) {
  std::size_t hashes = 0;
  while (hashes < content.size() && hashes < 6 && content[hashes] == '#') {
    ++hashes;
  }
  return hashes > 0 && hashes < content.size() && content[hashes] == ' '
             ? hashes + 1
             : 0;
}

std::size_t ListPrefix(std::string_view content) {
  if (content.size() >= 6 && content[0] == '-' && content[1] == ' ' &&
      content[2] == '[' &&
      (content[3] == ' ' || content[3] == 'x' || content[3] == 'X') &&
      content[4] == ']' && content[5] == ' ') {
    return 6;
  }
  if (content.size() >= 2 &&
      (content[0] == '-' || content[0] == '*' || content[0] == '+') &&
      content[1] == ' ') {
    return 2;
  }
  std::size_t digits = 0;
  while (digits < content.size() &&
         std::isdigit(static_cast<unsigned char>(content[digits]))) {
    ++digits;
  }
  return digits > 0 && digits + 1 < content.size() && content[digits] == '.' &&
                 content[digits + 1] == ' '
             ? digits + 2
             : 0;
}

bool HasStructureMarker(std::string_view content) {
  return ListPrefix(content) != 0 ||
         (content.size() >= 2 && content[0] == '>' && content[1] == ' ');
}

MdvTextTransform Replace(std::size_t start, std::size_t end,
                         std::string replacement, std::size_t selection_start,
                         std::size_t selection_end) {
  return {true, start, end, std::move(replacement), selection_start,
          selection_end};
}

MdvTextTransform Wrap(std::string_view text, std::size_t start,
                      std::size_t end, std::string_view before,
                      std::string_view after) {
  if (start >= before.size() && end + after.size() <= text.size() &&
      text.substr(start - before.size(), before.size()) == before &&
      text.substr(end, after.size()) == after) {
    return Replace(start - before.size(), end + after.size(),
                   std::string(text.substr(start, end - start)), 0, end - start);
  }
  const std::string_view selected = text.substr(start, end - start);
  if (selected.size() >= before.size() + after.size() &&
      selected.substr(0, before.size()) == before &&
      selected.substr(selected.size() - after.size()) == after) {
    std::string inner(selected.substr(
        before.size(), selected.size() - before.size() - after.size()));
    return Replace(start, end, std::move(inner), 0,
                   selected.size() - before.size() - after.size());
  }
  std::string replacement(before);
  replacement.append(selected);
  replacement.append(after);
  return Replace(start, end, std::move(replacement), before.size(),
                 before.size() + selected.size());
}

enum class PrefixMode { kHeading, kList, kQuote };

MdvTextTransform PrefixLines(std::string_view text, std::size_t start,
                             std::size_t end, std::string_view prefix,
                             PrefixMode mode) {
  const LineRange range = SelectedLines(text, start, end);
  auto lines = SplitLines(text.substr(range.start, range.end - range.start));
  bool all_requested = true;
  for (const auto& line : lines) {
    const std::size_t indent = IndentEnd(line.body);
    const std::string_view content(line.body.data() + indent,
                                   line.body.size() - indent);
    if (!content.empty() && content.substr(0, prefix.size()) != prefix) {
      all_requested = false;
    }
  }

  std::string replacement;
  for (const auto& line : lines) {
    const std::size_t indent = IndentEnd(line.body);
    const std::string_view indentation(line.body.data(), indent);
    std::string_view content(line.body.data() + indent, line.body.size() - indent);
    replacement.append(indentation);
    if (!content.empty()) {
      std::size_t old_prefix = 0;
      if (mode == PrefixMode::kHeading) {
        old_prefix = HeadingPrefix(content);
      } else if (mode == PrefixMode::kList) {
        old_prefix = ListPrefix(content);
      } else if (content.size() >= 2 && content[0] == '>' && content[1] == ' ') {
        old_prefix = 2;
      }
      content.remove_prefix(old_prefix);
      if (!all_requested) {
        replacement.append(prefix);
      }
    }
    replacement.append(content);
    replacement.append(line.ending);
  }
  const std::size_t selection_end = replacement.size();
  return Replace(range.start, range.end, std::move(replacement), 0,
                 selection_end);
}

MdvTextTransform ChangeIndent(std::string_view text, std::size_t start,
                              std::size_t end, bool increase) {
  const LineRange range = SelectedLines(text, start, end);
  auto lines = SplitLines(text.substr(range.start, range.end - range.start));
  for (const auto& line : lines) {
    const std::size_t indent = IndentEnd(line.body);
    const std::string_view content(line.body.data() + indent,
                                   line.body.size() - indent);
    if (!content.empty() && !HasStructureMarker(content)) {
      return {};
    }
  }
  std::string replacement;
  for (const auto& line : lines) {
    if (line.body.empty()) {
      replacement.append(line.ending);
      continue;
    }
    if (increase) {
      replacement.append("  ");
      replacement.append(line.body);
    } else if (line.body[0] == '\t') {
      replacement.append(line.body.substr(1));
    } else {
      std::size_t remove = 0;
      while (remove < line.body.size() && remove < 2 && line.body[remove] == ' ') {
        ++remove;
      }
      if (remove == 0) {
        return {};
      }
      replacement.append(line.body.substr(remove));
    }
    replacement.append(line.ending);
  }
  const std::size_t selection_end = replacement.size();
  return Replace(range.start, range.end, std::move(replacement), 0,
                 selection_end);
}

std::vector<std::string_view> TableCells(std::string_view line) {
  line = Trim(line);
  // Escaped pipes and inline-code pipes require a full GFM row parser. The
  // toolbar must not guess a column and rewrite the wrong delimiter cell.
  if (line.size() < 3 || line.find('|') == std::string_view::npos ||
      line.find("\\|") != std::string_view::npos ||
      line.find('`') != std::string_view::npos) {
    return {};
  }
  if (line.front() == '|') line.remove_prefix(1);
  if (!line.empty() && line.back() == '|') line.remove_suffix(1);
  std::vector<std::string_view> cells;
  std::size_t cursor = 0;
  while (cursor <= line.size()) {
    const std::size_t pipe = line.find('|', cursor);
    const std::size_t cell_end = pipe == std::string_view::npos ? line.size() : pipe;
    cells.push_back(Trim(line.substr(cursor, cell_end - cursor)));
    if (pipe == std::string_view::npos) break;
    cursor = pipe + 1;
  }
  return cells;
}

bool IsSeparatorCell(std::string_view cell) {
  cell = Trim(cell);
  if (!cell.empty() && cell.front() == ':') cell.remove_prefix(1);
  if (!cell.empty() && cell.back() == ':') cell.remove_suffix(1);
  return cell.size() >= 3 &&
         std::all_of(cell.begin(), cell.end(), [](char c) { return c == '-'; });
}

std::size_t TableColumnAt(std::string_view line, std::size_t offset) {
  offset = std::min(offset, line.size());
  std::size_t pipes = 0;
  for (std::size_t i = 0; i < offset; ++i) {
    if (line[i] == '|' && (i == 0 || line[i - 1] != '\\')) ++pipes;
  }
  if (!line.empty() && line.front() == '|' && pipes > 0) --pipes;
  return pipes;
}

MdvTextTransform AlignTable(std::string_view text, std::size_t caret,
                            MdvToolbarAction action) {
  std::vector<LineRange> ranges;
  std::size_t cursor = 0;
  while (cursor <= text.size()) {
    const std::size_t newline = text.find('\n', cursor);
    ranges.push_back({cursor, newline == std::string_view::npos ? text.size() : newline});
    if (newline == std::string_view::npos) break;
    cursor = newline + 1;
  }
  std::size_t current = ranges.size();
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    if (caret >= ranges[i].start && caret <= ranges[i].end) {
      current = i;
      break;
    }
  }
  if (current == ranges.size()) return {};
  std::size_t separator = ranges.size();
  for (std::size_t i = 1; i < ranges.size(); ++i) {
    const auto cells = TableCells(text.substr(ranges[i].start,
                                              ranges[i].end - ranges[i].start));
    if (!cells.empty() &&
        std::all_of(cells.begin(), cells.end(), IsSeparatorCell) &&
        TableCells(text.substr(ranges[i - 1].start,
                               ranges[i - 1].end - ranges[i - 1].start)).size() ==
            cells.size()) {
      bool contains_current = current == i - 1 || current == i;
      if (current > i) {
        contains_current = true;
        for (std::size_t row = i + 1; row <= current; ++row) {
          if (TableCells(text.substr(ranges[row].start,
                                     ranges[row].end - ranges[row].start)).size() !=
              cells.size()) {
            contains_current = false;
            break;
          }
        }
      }
      if (contains_current) {
        separator = i;
        break;
      }
    }
  }
  if (separator == ranges.size()) return {};
  const auto separator_line = text.substr(ranges[separator].start,
                                          ranges[separator].end - ranges[separator].start);
  auto cells = TableCells(separator_line);
  const auto current_line = text.substr(ranges[current].start,
                                        ranges[current].end - ranges[current].start);
  const std::size_t column =
      TableColumnAt(current_line, caret - ranges[current].start);
  if (column >= cells.size()) return {};
  std::vector<std::string> rendered;
  rendered.reserve(cells.size());
  for (std::size_t i = 0; i < cells.size(); ++i) {
    std::size_t dash_count = 0;
    for (char c : cells[i]) {
      if (c == '-') ++dash_count;
    }
    dash_count = std::max<std::size_t>(3, dash_count);
    std::string value(dash_count, '-');
    if (i == column) {
      if (action == MdvToolbarAction::kAlignLeft ||
          action == MdvToolbarAction::kAlignCenter) {
        value.insert(value.begin(), ':');
      }
      if (action == MdvToolbarAction::kAlignRight ||
          action == MdvToolbarAction::kAlignCenter) {
        value.push_back(':');
      }
    } else {
      const std::string_view original = Trim(cells[i]);
      if (!original.empty() && original.front() == ':') value.insert(value.begin(), ':');
      if (!original.empty() && original.back() == ':') value.push_back(':');
    }
    rendered.push_back(std::move(value));
  }
  std::string replacement = "| ";
  for (std::size_t i = 0; i < rendered.size(); ++i) {
    if (i != 0) replacement += " | ";
    replacement += rendered[i];
  }
  replacement += " |";
  return Replace(ranges[separator].start, ranges[separator].end,
                 std::move(replacement), 0, 0);
}

MdvTextTransform Insert(std::size_t at, std::string text,
                        std::string_view placeholder) {
  const std::size_t found = placeholder.empty() ? text.size() : text.find(placeholder);
  const std::size_t selection = found == std::string::npos ? text.size() : found;
  const std::size_t selection_end =
      found == std::string::npos ? selection : selection + placeholder.size();
  return Replace(at, at, std::move(text), selection, selection_end);
}

}  // namespace

std::optional<std::size_t> Utf16OffsetToUtf8Byte(
    std::string_view text, std::size_t utf16_offset) {
  std::size_t byte = 0;
  std::size_t units = 0;
  while (byte < text.size() && units < utf16_offset) {
    std::size_t length = 0;
    std::size_t next_units = 0;
    if (!NextUtf8(text, byte, &length, &next_units) ||
        units + next_units > utf16_offset) {
      return std::nullopt;
    }
    byte += length;
    units += next_units;
  }
  return units == utf16_offset ? std::optional<std::size_t>(byte)
                               : std::nullopt;
}

std::optional<std::size_t> Utf8ByteOffsetToUtf16(std::string_view text,
                                                 std::size_t byte_offset) {
  if (byte_offset > text.size()) return std::nullopt;
  std::size_t byte = 0;
  std::size_t units = 0;
  while (byte < byte_offset) {
    std::size_t length = 0;
    std::size_t next_units = 0;
    if (!NextUtf8(text, byte, &length, &next_units) ||
        byte + length > byte_offset) {
      return std::nullopt;
    }
    byte += length;
    units += next_units;
  }
  return byte == byte_offset ? std::optional<std::size_t>(units)
                             : std::nullopt;
}

std::optional<MdvToolbarAction> ParseMdvToolbarAction(std::string_view id) {
  static constexpr std::pair<std::string_view, MdvToolbarAction> kActions[] = {
      {"h1", MdvToolbarAction::kHeading1}, {"h2", MdvToolbarAction::kHeading2},
      {"h3", MdvToolbarAction::kHeading3}, {"bold", MdvToolbarAction::kBold},
      {"italic", MdvToolbarAction::kItalic}, {"strike", MdvToolbarAction::kStrike},
      {"inline-code", MdvToolbarAction::kInlineCode},
      {"bullet-list", MdvToolbarAction::kBulletList},
      {"ordered-list", MdvToolbarAction::kOrderedList},
      {"task-list", MdvToolbarAction::kTaskList}, {"quote", MdvToolbarAction::kQuote},
      {"code-block", MdvToolbarAction::kCodeBlock}, {"table", MdvToolbarAction::kTable},
      {"link", MdvToolbarAction::kLink}, {"divider", MdvToolbarAction::kDivider},
      {"indent", MdvToolbarAction::kIndent}, {"outdent", MdvToolbarAction::kOutdent},
      {"align-default", MdvToolbarAction::kAlignDefault},
      {"align-left", MdvToolbarAction::kAlignLeft},
      {"align-center", MdvToolbarAction::kAlignCenter},
      {"align-right", MdvToolbarAction::kAlignRight},
  };
  for (const auto& entry : kActions) {
    if (entry.first == id) return entry.second;
  }
  return std::nullopt;
}

MdvTextTransform TransformMarkdownText(std::string_view text,
                                       std::size_t selection_start,
                                       std::size_t selection_end,
                                       MdvToolbarAction action) {
  if (selection_start > selection_end || selection_end > text.size()) return {};
  switch (action) {
    case MdvToolbarAction::kBold:
      return Wrap(text, selection_start, selection_end, "**", "**");
    case MdvToolbarAction::kItalic:
      return Wrap(text, selection_start, selection_end, "*", "*");
    case MdvToolbarAction::kStrike:
      return Wrap(text, selection_start, selection_end, "~~", "~~");
    case MdvToolbarAction::kInlineCode:
      return Wrap(text, selection_start, selection_end, "`", "`");
    case MdvToolbarAction::kHeading1:
      return PrefixLines(text, selection_start, selection_end, "# ", PrefixMode::kHeading);
    case MdvToolbarAction::kHeading2:
      return PrefixLines(text, selection_start, selection_end, "## ", PrefixMode::kHeading);
    case MdvToolbarAction::kHeading3:
      return PrefixLines(text, selection_start, selection_end, "### ", PrefixMode::kHeading);
    case MdvToolbarAction::kBulletList:
      return PrefixLines(text, selection_start, selection_end, "- ", PrefixMode::kList);
    case MdvToolbarAction::kOrderedList:
      return PrefixLines(text, selection_start, selection_end, "1. ", PrefixMode::kList);
    case MdvToolbarAction::kTaskList:
      return PrefixLines(text, selection_start, selection_end, "- [ ] ", PrefixMode::kList);
    case MdvToolbarAction::kQuote:
      return PrefixLines(text, selection_start, selection_end, "> ", PrefixMode::kQuote);
    case MdvToolbarAction::kCodeBlock:
      return Insert(selection_start, "\n```\n代码内容\n```\n", "代码内容");
    case MdvToolbarAction::kTable:
      return Insert(selection_start, "\n| 列一 | 列二 |\n| --- | --- |\n| 内容 | 内容 |\n", "列一");
    case MdvToolbarAction::kLink:
      return Insert(selection_start, "[链接文字](https://)", "链接文字");
    case MdvToolbarAction::kDivider:
      return Insert(selection_start, "\n---\n", {});
    case MdvToolbarAction::kIndent:
      return ChangeIndent(text, selection_start, selection_end, true);
    case MdvToolbarAction::kOutdent:
      return ChangeIndent(text, selection_start, selection_end, false);
    case MdvToolbarAction::kAlignDefault:
    case MdvToolbarAction::kAlignLeft:
    case MdvToolbarAction::kAlignCenter:
    case MdvToolbarAction::kAlignRight:
      return AlignTable(text, selection_start, action);
  }
  return {};
}

}  // namespace crayon::browser_mdv
