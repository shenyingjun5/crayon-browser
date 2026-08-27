// MDV-02 contract tests (MD-002): deterministic golden rendering,
// injection matrix, link scheme allowlist, image placeholders,
// input bounds and normalization.
#include "crayon/browser_markdown/markdown_render.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using crayon::browser_markdown::IsValidUtf8;
using crayon::browser_markdown::kMaxInputBytes;
using crayon::browser_markdown::RenderMarkdownToSafeHtml;
using crayon::browser_markdown::RenderStatus;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

std::string Render(const std::string& input) {
  return RenderMarkdownToSafeHtml(input, nullptr);
}

bool GoldenBasics() {
  CHECK(Render("# Hello") == "<h1>Hello</h1>\n");
  CHECK(Render("## Sub **bold** _em_") ==
        "<h2>Sub <strong>bold</strong> <em>em</em></h2>\n");
  CHECK(Render("a ~~gone~~ b") == "<p>a <del>gone</del> b</p>\n");
  CHECK(Render("`code` here") == "<p><code>code</code> here</p>\n");
  CHECK(Render("- one\n- two") == "<ul>\n<li>one</li>\n<li>two</li>\n</ul>\n");
  CHECK(Render("1. first\n2. second") ==
        "<ol>\n<li>first</li>\n<li>second</li>\n</ol>\n");
  CHECK(Render("> quoted") == "<blockquote>\n<p>quoted</p>\n</blockquote>\n");
  CHECK(Render("---") == "<hr>\n");
  CHECK(Render("hard  \nbreak") == "<p>hard<br>\nbreak</p>\n");
  // GFM table with alignment.
  const std::string table = Render("| a | b |\n|---|:-:|\n| 1 | 2 |\n");
  CHECK(table.find("<table>") != std::string::npos);
  CHECK(table.find("<thead>") != std::string::npos);
  CHECK(table.find("align=\"center\"") != std::string::npos);
  CHECK(table.find("<td>1</td>") != std::string::npos);
  // Task list renders a disabled checkbox.
  const std::string task = Render("- [x] done\n- [ ] open\n");
  CHECK(task.find("type=\"checkbox\"") != std::string::npos);
  CHECK(task.find("disabled") != std::string::npos);
  // Fenced code block.
  const std::string code = Render("```cpp\nint x;\n```\n");
  CHECK(code.find("<pre><code") != std::string::npos &&
        code.find("int x;") != std::string::npos);
  return true;
}

bool GoldenLinksAndAutolinks() {
  CHECK(Render("[site](https://a.example/x \"t\")") ==
        "<p><a href=\"https://a.example/x\" title=\"t\">site</a></p>\n");
  CHECK(Render("<https://a.example/y>") ==
        "<p><a href=\"https://a.example/y\">https://a.example/y</a></p>\n");
  CHECK(Render("<mailto:a@b.example>") ==
        "<p><a href=\"mailto:a@b.example\">mailto:a@b.example</a></p>\n");
  // Bare URLs stay plain text (no permissive autolinks).
  const std::string bare = Render("see https://a.example/z now");
  CHECK(bare.find("<a ") == std::string::npos);
  CHECK(bare.find("https://a.example/z") != std::string::npos);
  return true;
}

bool InjectionMatrix() {
  // Raw HTML is fully escaped, never passed through.
  const std::string script = Render("<script>alert(1)</script>");
  CHECK(script.find("<script") == std::string::npos);
  CHECK(script.find("&lt;script&gt;alert(1)&lt;/script&gt;") !=
        std::string::npos);
  // Inline raw HTML with event handlers escapes the handlers away.
  const std::string event = Render("x <b onclick=\"evil\">y</b> z");
  CHECK(event.find("onclick") == std::string::npos ||
        event.find("<b ") == std::string::npos);
  CHECK(event.find("&lt;b onclick=&quot;evil&quot;&gt;") != std::string::npos);
  // javascript: links degrade to plain text.
  const std::string js = Render("[click](javascript:alert(1))");
  CHECK(js.find("<a ") == std::string::npos);
  CHECK(js.find("click") != std::string::npos);
  CHECK(js.find("javascript:") == std::string::npos);
  // file: and relative links degrade too.
  CHECK(Render("[f](file:///etc/passwd)").find("<a ") == std::string::npos);
  CHECK(Render("[r](relative.md)").find("<a ") == std::string::npos);
  // Images become intermediate markers: the raw reference rides in
  // data-mdv-raw for Browser-side classification (MDV-13); nothing is
  // fetched here and the marker never reaches the page unchanged.
  const std::string image = Render("![alt text](https://img.example/pic.png)");
  CHECK(image.find("<img class=\"md-img\" src=\"mdv-img:0\"") !=
        std::string::npos);
  CHECK(image.find("data-mdv-raw=\"https://img.example/pic.png\"") !=
        std::string::npos);
  CHECK(image.find("alt=\"alt text\"") != std::string::npos);
  // HTML comment escapes.
  const std::string comment = Render("<!-- hidden -->");
  CHECK(comment.find("<!--") == std::string::npos);
  // Autolink with disallowed scheme degrades.
  CHECK(Render("<ftp://a.example/x>").find("<a ") == std::string::npos);
  return true;
}

bool Determinism() {
  const std::string input =
      "# T\n\ntext **b** and [l](https://x.example)\n\n| a | b |\n|---|---|\n| "
      "1 | 2 |\n";
  const std::string first = Render(input);
  CHECK(!first.empty());
  for (int i = 0; i < 3; ++i) {
    CHECK(Render(input) == first);
  }
  return true;
}

bool InputNormalization() {
  // BOM stripped.
  std::string bom = "\xEF\xBB\xBF# Title";
  CHECK(Render(bom) == Render("# Title"));
  // CRLF and lone CR normalized.
  CHECK(Render("# T\r\nbody") == Render("# T\nbody"));
  CHECK(Render("# T\rbody") == Render("# T\nbody"));
  // Unicode passes through.
  const std::string unicode = Render("# 中文标题 **粗体**");
  CHECK(unicode.find("中文标题") != std::string::npos);
  return true;
}

bool InputBounds() {
  RenderStatus status = RenderStatus::kOk;
  static_cast<void>(
      RenderMarkdownToSafeHtml(std::string(kMaxInputBytes + 1, 'a'), &status));
  CHECK(status == RenderStatus::kInputTooLarge);
  static_cast<void>(
      RenderMarkdownToSafeHtml(std::string(kMaxInputBytes, 'a'), &status));
  CHECK(status == RenderStatus::kOk);
  // Invalid UTF-8: bare continuation, overlong, surrogate, truncated.
  for (const std::string& bad :
       {std::string("\x80"), std::string("\xC0\xAF"),
        std::string("\xED\xA0\x80"), std::string("\xE4\xB8")}) {
    static_cast<void>(RenderMarkdownToSafeHtml(bad, &status));
    CHECK(status == RenderStatus::kInvalidUtf8);
  }
  CHECK(IsValidUtf8("ok 中文 \xF0\x9F\x96\xA8"));
  return true;
}

}  // namespace

int main() {
  const bool ok = GoldenBasics() && GoldenLinksAndAutolinks() &&
                  InjectionMatrix() && Determinism() && InputNormalization() &&
                  InputBounds();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "markdown_render_test passed\n";
  return EXIT_SUCCESS;
}
