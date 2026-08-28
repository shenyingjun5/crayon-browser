#include "crayon/browser_mdv/mdv_transform.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using crayon::browser_mdv::MdvToolbarAction;
using crayon::browser_mdv::MdvTextTransform;
using crayon::browser_mdv::ParseMdvToolbarAction;
using crayon::browser_mdv::TransformMarkdownText;
using crayon::browser_mdv::Utf16OffsetToUtf8Byte;
using crayon::browser_mdv::Utf8ByteOffsetToUtf16;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

std::string Apply(std::string text, const MdvTextTransform& edit) {
  if (!edit.applied) return text;
  text.replace(edit.replace_start, edit.replace_end - edit.replace_start,
               edit.replacement);
  return text;
}

bool LinePrefixDoesNotDuplicateSuffix() {
  const std::string source = "alpha\nbeta\ngamma";
  const auto edit = TransformMarkdownText(source, 7, 7, MdvToolbarAction::kHeading1);
  CHECK(edit.applied);
  CHECK(edit.replace_start == 6);
  CHECK(edit.replace_end == 10);
  CHECK(Apply(source, edit) == "alpha\n# beta\ngamma");
  return true;
}

bool WrapToggleAndSelection() {
  const std::string source = "hello world";
  auto edit = TransformMarkdownText(source, 6, 11, MdvToolbarAction::kBold);
  CHECK(Apply(source, edit) == "hello **world**");
  CHECK(edit.selection_start == 2 && edit.selection_end == 7);
  const std::string wrapped = Apply(source, edit);
  edit = TransformMarkdownText(wrapped, 8, 13, MdvToolbarAction::kBold);
  CHECK(Apply(wrapped, edit) == source);
  const auto caret = TransformMarkdownText(source, 5, 5, MdvToolbarAction::kInlineCode);
  CHECK(Apply(source, caret) == "hello`` world");
  CHECK(caret.selection_start == 1 && caret.selection_end == 1);
  return true;
}

bool MultilinePrefixToggleAndCrLf() {
  const std::string source = "one\r\ntwo\r\nend";
  auto edit = TransformMarkdownText(source, 0, 8, MdvToolbarAction::kTaskList);
  const std::string listed = Apply(source, edit);
  CHECK(listed == "- [ ] one\r\n- [ ] two\r\nend");
  edit = TransformMarkdownText(listed, 0, 20, MdvToolbarAction::kTaskList);
  CHECK(Apply(listed, edit) == source);
  const auto heading =
      TransformMarkdownText("## old\nplain", 0, 6, MdvToolbarAction::kHeading3);
  CHECK(Apply("## old\nplain", heading) == "### old\nplain");
  return true;
}

bool SkeletonsSelectPlaceholders() {
  const std::string source = "x";
  const auto link = TransformMarkdownText(source, 1, 1, MdvToolbarAction::kLink);
  CHECK(Apply(source, link) == "x[链接文字](https://)");
  CHECK(link.replacement.substr(link.selection_start,
                                link.selection_end - link.selection_start) ==
        "链接文字");
  const auto code = TransformMarkdownText(source, 0, 0, MdvToolbarAction::kCodeBlock);
  CHECK(code.replacement.find("```\n代码内容\n```") != std::string::npos);
  return true;
}

bool StructuredIndentFailsClosed() {
  const std::string list = "- one\n  - two";
  auto edit = TransformMarkdownText(list, 0, list.size(), MdvToolbarAction::kIndent);
  CHECK(Apply(list, edit) == "  - one\n    - two");
  edit = TransformMarkdownText(Apply(list, edit), 0, edit.replacement.size(),
                               MdvToolbarAction::kOutdent);
  CHECK(Apply("  - one\n    - two", edit) == list);
  CHECK(!TransformMarkdownText("paragraph", 0, 0, MdvToolbarAction::kIndent).applied);
  CHECK(!TransformMarkdownText("- item\nparagraph", 0, 16,
                               MdvToolbarAction::kIndent)
             .applied);
  return true;
}

bool TableAlignmentIsContextBound() {
  const std::string table = "| A | B |\n| --- | ---: |\n| 1 | 2 |";
  auto edit = TransformMarkdownText(table, 3, 3, MdvToolbarAction::kAlignCenter);
  CHECK(edit.applied);
  CHECK(Apply(table, edit) == "| A | B |\n| :---: | ---: |\n| 1 | 2 |");
  const std::size_t second = table.find("B");
  edit = TransformMarkdownText(table, second, second, MdvToolbarAction::kAlignLeft);
  CHECK(Apply(table, edit) == "| A | B |\n| --- | :--- |\n| 1 | 2 |");
  CHECK(!TransformMarkdownText("A | B\nnot a table", 2, 2,
                               MdvToolbarAction::kAlignRight)
             .applied);
  CHECK(!TransformMarkdownText("| A\\|B | C |\n| --- | --- |", 4, 4,
                               MdvToolbarAction::kAlignRight)
             .applied);
  CHECK(!TransformMarkdownText("| `A|B` | C |\n| --- | --- |", 5, 5,
                               MdvToolbarAction::kAlignRight)
             .applied);
  return true;
}

bool ParseAndBoundaryMatrix() {
  CHECK(ParseMdvToolbarAction("bold") == MdvToolbarAction::kBold);
  CHECK(ParseMdvToolbarAction("align-right") == MdvToolbarAction::kAlignRight);
  CHECK(!ParseMdvToolbarAction("raw-html").has_value());
  CHECK(!TransformMarkdownText("abc", 3, 2, MdvToolbarAction::kBold).applied);
  CHECK(!TransformMarkdownText("abc", 0, 4, MdvToolbarAction::kBold).applied);
  const std::string unicode = "A中😀B";
  CHECK(Utf16OffsetToUtf8Byte(unicode, 0) == 0);
  CHECK(Utf16OffsetToUtf8Byte(unicode, 2) == 4);
  CHECK(Utf16OffsetToUtf8Byte(unicode, 4) == 8);
  CHECK(!Utf16OffsetToUtf8Byte(unicode, 3).has_value());
  CHECK(Utf8ByteOffsetToUtf16(unicode, 8) == 4);
  CHECK(!Utf8ByteOffsetToUtf16(unicode, 6).has_value());
  std::string text = "- item";
  for (int i = 0; i < 5'000; ++i) {
    const auto action = i % 2 == 0 ? MdvToolbarAction::kIndent
                                   : MdvToolbarAction::kOutdent;
    const auto edit = TransformMarkdownText(text, 0, text.size(), action);
    CHECK(edit.applied);
    text = Apply(std::move(text), edit);
  }
  CHECK(text == "- item");
  return true;
}

}  // namespace

int main() {
  const bool ok = LinePrefixDoesNotDuplicateSuffix() && WrapToggleAndSelection() &&
                  MultilinePrefixToggleAndCrLf() && SkeletonsSelectPlaceholders() &&
                  StructuredIndentFailsClosed() && TableAlignmentIsContextBound() &&
                  ParseAndBoundaryMatrix();
  if (!ok) return EXIT_FAILURE;
  std::cout << "mdv_transform_test passed\n";
  return EXIT_SUCCESS;
}
