#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace crayon::browser_mdv {

enum class MdvToolbarAction {
  kHeading1,
  kHeading2,
  kHeading3,
  kBold,
  kItalic,
  kStrike,
  kInlineCode,
  kBulletList,
  kOrderedList,
  kTaskList,
  kQuote,
  kCodeBlock,
  kTable,
  kLink,
  kDivider,
  kIndent,
  kOutdent,
  kAlignDefault,
  kAlignLeft,
  kAlignCenter,
  kAlignRight,
};

/// One replace operation suitable for textarea.setRangeText(). All offsets are
/// UTF-8 byte offsets; selection offsets are relative to `replacement`.
struct MdvTextTransform {
  bool applied = false;
  std::size_t replace_start = 0;
  std::size_t replace_end = 0;
  std::string replacement;
  std::size_t selection_start = 0;
  std::size_t selection_end = 0;
};

std::optional<MdvToolbarAction> ParseMdvToolbarAction(std::string_view id);

std::optional<std::size_t> Utf16OffsetToUtf8Byte(std::string_view text,
                                                 std::size_t utf16_offset);
std::optional<std::size_t> Utf8ByteOffsetToUtf16(std::string_view text,
                                                 std::size_t byte_offset);

/// Pure, deterministic Markdown edit transform. Invalid selections and
/// context-sensitive actions outside a supported structure fail closed.
MdvTextTransform TransformMarkdownText(std::string_view text,
                                       std::size_t selection_start,
                                       std::size_t selection_end,
                                       MdvToolbarAction action);

}  // namespace crayon::browser_mdv
