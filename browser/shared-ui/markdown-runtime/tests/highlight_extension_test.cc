// MRT-06 / MR-004: closed language, fallback, marker and asset contracts.
#include "crayon/browser_markdown_runtime/highlight_extension.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using crayon::browser_markdown_runtime::AssetCatalogBuildStatus;
using crayon::browser_markdown_runtime::BuildHighlightAssetCatalog;
using crayon::browser_markdown_runtime::HighlightFenceKind;
using crayon::browser_markdown_runtime::HighlightFenceSelection;
using crayon::browser_markdown_runtime::kHighlightAssetManifestId;
using crayon::browser_markdown_runtime::kHighlightExtensionId;
using crayon::browser_markdown_runtime::kHighlightExtensionVersion;
using crayon::browser_markdown_runtime::RenderHighlightDocument;
using crayon::browser_markdown_runtime::ResolveHighlightFence;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool LanguageResolutionIsExactAndClosed() {
  CHECK(ResolveHighlightFence("cpp").canonical_id == "cpp");
  CHECK(ResolveHighlightFence("c++").canonical_id == "cpp");
  CHECK(ResolveHighlightFence("c#").canonical_id == "csharp");
  CHECK(ResolveHighlightFence("py").canonical_id == "python");
  const auto javascript = ResolveHighlightFence("js");
  CHECK(javascript.kind == HighlightFenceKind::kGrammar);
  CHECK(javascript.canonical_id == "javascript");
  CHECK(javascript.load_order ==
        std::vector<std::string>({"css", "graphql", "xml", "javascript"}));
  CHECK(ResolveHighlightFence("plaintext").kind ==
        HighlightFenceKind::kPlaintext);
  CHECK(ResolveHighlightFence("CPP").kind == HighlightFenceKind::kUnsupported);
  CHECK(ResolveHighlightFence("cpp extra").kind ==
        HighlightFenceKind::kUnsupported);
  CHECK(HighlightFenceSelection().size() > 25);
  return true;
}

bool DocumentDecorationKeepsFallbacksAndSourceAsText() {
  const std::string input =
      "```cpp\nint main() { return 42; }\n```\n\n"
      "```c++\nstd::string value;\n```\n\n"
      "```py\nprint('<script>alert(1)</script>')\n```\n\n"
      "```plaintext\nplain <b>text</b>\n```\n\n"
      "```unknown\nunknown <img src=x onerror=x>\n```\n";
  const auto result = RenderHighlightDocument(input, 7, 11);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.facts_status ==
        crayon::browser_markdown::ExtensionFactsStatus::kComplete);
  CHECK(result.decorated_blocks == 3);
  CHECK(result.safe_html.find("data-mdv-highlight=\"cpp\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"python\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("class=\"language-cpp hljs\"") ==
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"plaintext\"") ==
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"unknown\"") ==
        std::string::npos);
  CHECK(result.safe_html.find("<script>alert") == std::string::npos);
  CHECK(result.safe_html.find("<img src=x") == std::string::npos);

  const auto ordinary = RenderHighlightDocument("# Title\n\ntext\n", 7, 12);
  CHECK(ordinary.decorated_blocks == 0);
  CHECK(ordinary.safe_html.find("data-mdv-highlight") == std::string::npos);
  return true;
}

bool WindowsViewerFixtureKeepsKnownFencesDecorated() {
  std::string input =
      "# MRT-06 Windows Reproduction\n\n"
      "```cpp\nint main() { return 0; }\n```\n\n"
      "```plaintext\nconst plaintext = true;\n```\n\n"
      "```not-a-language\nconst unknown = true;\n```\n\n"
      "```javascript\nconst hostile = \"<img src=x><script>x</script>\";\n```\n\n";
  for (int index = 0; index < 10; ++index) {
    input += "## Spacer\n\nViewport lazy verification spacer.\n\n";
  }
  input +=
      "```js\nconst lazyAlias = (value) => value?.trim();\n```\n\n"
      "```c#\npublic sealed class AliasSample {}\n```\n";

  const auto result = RenderHighlightDocument(input, 1, 2);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.facts_status ==
        crayon::browser_markdown::ExtensionFactsStatus::kComplete);
  CHECK(result.decorated_blocks == 4);
  CHECK(result.safe_html.find("data-mdv-highlight=\"cpp\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"javascript\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"csharp\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"plaintext\"") ==
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"not-a-language\"") ==
        std::string::npos);
  CHECK(result.safe_html.find("<img src=x>") == std::string::npos);
  CHECK(result.safe_html.find("<script>x</script>") == std::string::npos);
  return true;
}

bool AssetsAreEmbeddedInTheClosedCatalog() {
  const auto built = BuildHighlightAssetCatalog();
  CHECK(built.status == AssetCatalogBuildStatus::kReady);
  CHECK(built.catalog);
  CHECK(built.catalog->bundle_count() == 1);
  const auto bundle = built.catalog->FindCompatible(kHighlightAssetManifestId,
                                                    kHighlightExtensionId,
                                                    kHighlightExtensionVersion);
  CHECK(bundle);
  CHECK(bundle->entry_resource_id == "adapter");
  CHECK(bundle->resources.size() == 27);
  CHECK(built.catalog->total_bytes() > 123071);
  return true;
}

}  // namespace

int main() {
  if (!LanguageResolutionIsExactAndClosed() ||
      !DocumentDecorationKeepsFallbacksAndSourceAsText() ||
      !WindowsViewerFixtureKeepsKnownFencesDecorated() ||
      !AssetsAreEmbeddedInTheClosedCatalog()) {
    return EXIT_FAILURE;
  }
  std::cout << "highlight_extension_test passed\n";
  return EXIT_SUCCESS;
}
