#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_SEARCH_ENGINES_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_SEARCH_ENGINES_H_

// PLT-SHELL-24M2FIX-C8: the product's search-engine catalogue.
//
// The omnibox provider set itself has no default engine by design (see
// browser_omnibox_provider/search_provider.h: an empty set means nothing can be
// submitted to a remote service). Which engines the product offers, and which
// one is the default, is a product decision owned here so the toolbar, the
// selector menu and any future platform shell cannot drift apart.
//
// Every template is an ordinary https endpoint with exactly one
// {searchTerms} placeholder, so it passes ValidateProvider unchanged.

#include <array>
#include <cstddef>

#include "crayon/browser_omnibox_provider/search_provider.h"

namespace crayon::browser::cef_shell::window {

enum class SearchEngine { kBaidu = 0, kGoogle };

// Product decision (2026-09-23): Baidu is the default engine; Google is the
// alternative offered in the selector. Both are always offered, in this order.
inline constexpr std::array<SearchEngine, 2> kSearchEngineOrder = {
    SearchEngine::kBaidu, SearchEngine::kGoogle};

inline constexpr SearchEngine kDefaultSearchEngine = SearchEngine::kBaidu;

// Locale key for the user-facing engine name; the string itself lives in the
// locale catalogue, not in code.
inline constexpr const char *SearchEngineLabelKey(SearchEngine engine) {
  switch (engine) {
  case SearchEngine::kGoogle:
    return "search.engine.google";
  case SearchEngine::kBaidu:
    break;
  }
  return "search.engine.baidu";
}

inline browser_omnibox_provider::SearchProvider SearchProviderFor(
    SearchEngine engine) {
  switch (engine) {
  case SearchEngine::kGoogle:
    return {"Google", "https://www.google.com/search?q={searchTerms}"};
  case SearchEngine::kBaidu:
    break;
  }
  return {"Baidu", "https://www.baidu.com/s?wd={searchTerms}"};
}

// The provider set a fresh omnibox starts with: exactly the default engine, so
// input that is not a URL produces that engine's result page instead of the
// "no search engine configured" notice.
inline browser_omnibox_provider::SearchProviderSet DefaultSearchProviders() {
  browser_omnibox_provider::SearchProviderSet providers;
  static_cast<void>(providers.Add(SearchProviderFor(kDefaultSearchEngine)));
  return providers;
}

} // namespace crayon::browser::cef_shell::window

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_SEARCH_ENGINES_H_
