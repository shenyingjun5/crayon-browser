// MRT-08 / MR-005: KaTeX routing, placeholders, fallback and asset closure.
#include "crayon/browser_markdown_runtime/katex_extension.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_markdown/markdown_math_facts.h"
#include "crayon/browser_markdown_runtime/highlight_extension.h"

namespace {

using crayon::browser_markdown_runtime::AssetCatalogBuildStatus;
using crayon::browser_markdown_runtime::BuildKatexAssetCatalog;
using crayon::browser_markdown_runtime::IsKatexRuntimeResourceId;
using crayon::browser_markdown_runtime::KatexSourceStatus;
using crayon::browser_markdown_runtime::kKatexAssetManifestId;
using crayon::browser_markdown_runtime::kKatexBlockExtensionId;
using crayon::browser_markdown_runtime::kKatexExtensionVersion;
using crayon::browser_markdown_runtime::kKatexInlineExtensionId;
using crayon::browser_markdown_runtime::RenderP0MarkdownDocument;
using crayon::browser_markdown_runtime::ValidateKatexSource;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool SourcePolicyIsClosed() {
  CHECK(ValidateKatexSource("\\frac{x^2+1}{2}") == KatexSourceStatus::kAllowed);
  for (const std::string& denied :
       {"\\href{https://example.test}{x}", "\\url{file:///tmp/x}",
        "\\includegraphics{x}", "\\htmlFuture{x}", "\\gdef\\x{y}",
        "\\csname href\\endcsname{x}"}) {
    CHECK(ValidateKatexSource(denied) == KatexSourceStatus::kDeniedCommand);
  }
  CHECK(ValidateKatexSource("") == KatexSourceStatus::kInvalidSource);
  std::string commands;
  for (std::size_t index = 0; index <= crayon::browser_markdown::kMaxMathTokens;
       ++index) {
    commands += "\\alpha";
  }
  CHECK(ValidateKatexSource(commands) == KatexSourceStatus::kTokenBudget);
  return true;
}

bool InlineBlockAndHighlightCompose() {
  const std::string input =
      "Inline $E=mc^2$.\n\n$$\n\\frac{x^2+1}{2}\n$$\n\n"
      "```cpp\nint main() { return 0; }\n```\n";
  const auto result = RenderP0MarkdownDocument(input, 7, 11);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.math_placeholders == 2);
  CHECK(result.decorated_code_blocks == 1);
  CHECK(result.safe_html.find("data-mdv-math=\"inline\"") != std::string::npos);
  CHECK(result.safe_html.find("data-mdv-math=\"block\"") != std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"cpp\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("CRAYONMATHMARKER") == std::string::npos);
  CHECK(result.safe_html.find("class=\"md-math-input\" hidden>E=mc^2</span>") !=
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-math-source") == std::string::npos);
  return true;
}

bool RejectedAndExcludedMathRemainsSafeText() {
  const std::string input =
      "Rejected $\\href{https://example.test}{x}$.\n\n"
      "`$code$` and [link]($destination$)\n\n"
      "Text $\\text{&lt;img src=x onerror=alert(1)&gt;}$";
  const auto result = RenderP0MarkdownDocument(input, 2, 3);
  CHECK(result.math_placeholders == 1);
  CHECK(result.safe_html.find("class=\"md-math-input\" hidden>\\text{") !=
        std::string::npos);
  CHECK(result.safe_html.find("<img src=x") == std::string::npos);
  CHECK(result.safe_html.find("class=\"md-math-input\" hidden>\\href") ==
        std::string::npos);
  CHECK(result.safe_html.find("<code>$code$</code>") != std::string::npos);
  return true;
}

bool OrdinaryMarkdownKeepsHighlightOutput() {
  const std::string input = "# Title\n\n```js\nconst x = 1;\n```\n";
  const auto p0 = RenderP0MarkdownDocument(input, 1, 8);
  const auto highlight =
      crayon::browser_markdown_runtime::RenderHighlightDocument(input, 1, 8);
  CHECK(p0.math_placeholders == 0);
  CHECK(p0.safe_html == highlight.safe_html);
  CHECK(p0.decorated_code_blocks == highlight.decorated_blocks);
  return true;
}

bool AssetsAreOneExactSharedClosure() {
  const auto built = BuildKatexAssetCatalog();
  CHECK(built.status == AssetCatalogBuildStatus::kReady);
  CHECK(built.catalog && built.catalog->bundle_count() == 1);
  const auto inline_bundle = built.catalog->FindCompatible(
      kKatexAssetManifestId, kKatexInlineExtensionId, kKatexExtensionVersion);
  const auto block_bundle = built.catalog->FindCompatible(
      kKatexAssetManifestId, kKatexBlockExtensionId, kKatexExtensionVersion);
  CHECK(inline_bundle && block_bundle && inline_bundle == block_bundle);
  CHECK(inline_bundle->resources.size() == 23);
  CHECK(IsKatexRuntimeResourceId("adapter"));
  CHECK(IsKatexRuntimeResourceId("katex"));
  CHECK(IsKatexRuntimeResourceId("stylesheet"));
  CHECK(IsKatexRuntimeResourceId("fonts/KaTeX_Main-Regular.woff2"));
  CHECK(!IsKatexRuntimeResourceId("../LICENSE"));
  CHECK(!IsKatexRuntimeResourceId("fonts/unknown.woff2"));
  return true;
}

}  // namespace

int main() {
  if (!SourcePolicyIsClosed() || !InlineBlockAndHighlightCompose() ||
      !RejectedAndExcludedMathRemainsSafeText() ||
      !OrdinaryMarkdownKeepsHighlightOutput() ||
      !AssetsAreOneExactSharedClosure()) {
    return EXIT_FAILURE;
  }
  std::cout << "katex_extension_test passed\n";
  return EXIT_SUCCESS;
}
